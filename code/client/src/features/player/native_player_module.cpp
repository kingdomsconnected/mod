// Winsock2 must precede the Windows headers included by MinHook.
#include <utils/safe_win32.h>

#include <MinHook.h>
#include <utils/hooking/hook_function.h>
#include <utils/hooking/hooking.h>
#include <logging/logger.h>

#include "core/application.h"
#include "features/player/player_inventory_service.h"
#include "common/module_support.h"
#include "native_player_module.h"
#include "sdk/cryengine/action/i_game_framework.h"

#include "sdk/cryengine/action/i_actor_system.h"
#include "sdk/patterns.h"
#include "sdk/wh/combatmodule/c_combat_lod_manager.h"
#include "sdk/wh/combatmodule/c_combat_scene.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <intrin.h>

namespace KCDC::Features::Player {
    thread_local bool NativePlayerModule::t_remoteExecution                                                = false;
    thread_local const SDK::wh::entitymodule::C_Actor *NativePlayerModule::t_destroyingRemoteFighter       = nullptr;
    thread_local SDK::wh::combatmodule::C_CombatCameraManager *NativePlayerModule::t_remoteCombatCamera    = nullptr;
    thread_local bool NativePlayerModule::t_combatAlignmentPass                                            = false;
    thread_local bool NativePlayerModule::t_remoteFighterAlignmentPass                                     = false;
    thread_local bool NativePlayerModule::t_localPlayerQuery                                           = false;
    SDK::wh::C_GlobalEnvironment *(__fastcall *NativePlayerModule::s_originalGetGlobalEnvironment)() = nullptr;

    NativePlayerModule &NativePlayerModule::Get() {
        static NativePlayerModule module;
        return module;
    }

    bool NativePlayerModule::IsRemote(const SDK::wh::entitymodule::C_Actor *actor) {
        return actor != nullptr && Get()._remoteBodies.Contains(BodyKey(actor));
    }

    bool NativePlayerModule::IsRemoteEntity(SDK::EntityId entityId) {
        return entityId != 0 && Get()._remoteEntities.Contains(entityId);
    }

    bool NativePlayerModule::RemoteKeySet::Contains(std::uint64_t key) const {
        if (_count.load(std::memory_order_relaxed) == 0) {
            return false;
        }
        const std::size_t start = Slot(key);
        for (std::size_t probe = 0; probe < kSize; ++probe) {
            const std::uint64_t held = _keys[(start + probe) & (kSize - 1)].load(std::memory_order_acquire);
            if (held == key) {
                return true;
            }
            if (held == 0) {
                return false;
            }
        }
        return false;
    }

    std::size_t NativePlayerModule::RemoteKeySet::Find(std::uint64_t key) const {
        // Zero is an empty slot and the retired marker a dead one; neither
        // is a key that can be found.
        if (key == 0 || key == kRetired) {
            return kSize;
        }
        const std::size_t start = Slot(key);
        for (std::size_t probe = 0; probe < kSize; ++probe) {
            const std::size_t index = (start + probe) & (kSize - 1);
            const std::uint64_t held = _keys[index].load(std::memory_order_acquire);
            if (held == key) {
                return index;
            }
            if (held == 0) {
                return kSize;
            }
        }
        return kSize;
    }

    void NativePlayerModule::RemoteKeySet::Store(std::uint64_t key, std::size_t channel, std::uint64_t value) {
        const std::size_t index = Find(key);
        if (index != kSize) {
            _values[index][channel].store(value, std::memory_order_release);
        }
    }

    std::uint64_t NativePlayerModule::RemoteKeySet::Take(std::uint64_t key, std::size_t channel) {
        if (_count.load(std::memory_order_relaxed) == 0) {
            return 0;
        }
        const std::size_t index = Find(key);
        return index != kSize ? _values[index][channel].exchange(0, std::memory_order_acq_rel) : 0;
    }

    std::uint64_t NativePlayerModule::RemoteKeySet::Read(std::uint64_t key, std::size_t channel) const {
        if (_count.load(std::memory_order_relaxed) == 0) {
            return 0;
        }
        const std::size_t index = Find(key);
        return index != kSize ? _values[index][channel].load(std::memory_order_acquire) : 0;
    }

    void NativePlayerModule::RemoteKeySet::Insert(std::uint64_t key) {
        if (key == 0 || key == kRetired) {
            return;
        }
        std::atomic<std::uint64_t> *target = nullptr;
        const std::size_t start            = Slot(key);
        for (std::size_t probe = 0; probe < kSize; ++probe) {
            std::atomic<std::uint64_t> &slot = _keys[(start + probe) & (kSize - 1)];
            const std::uint64_t held         = slot.load(std::memory_order_relaxed);
            if (held == key) {
                return;
            }
            if (held == kRetired && target == nullptr) {
                target = &slot;
                continue;
            }
            if (held == 0) {
                if (target == nullptr) {
                    target = &slot;
                }
                break;
            }
        }
        if (target != nullptr) {
            for (std::atomic<std::uint64_t> &value : _values[static_cast<std::size_t>(target - _keys.data())]) {
                value.store(0, std::memory_order_relaxed);
            }
            target->store(key, std::memory_order_release);
            _count.fetch_add(1, std::memory_order_relaxed);
        }
    }

    void NativePlayerModule::RemoteKeySet::Retire(std::uint64_t key) {
        const std::size_t start = Slot(key);
        for (std::size_t probe = 0; probe < kSize; ++probe) {
            std::atomic<std::uint64_t> &slot = _keys[(start + probe) & (kSize - 1)];
            const std::uint64_t held         = slot.load(std::memory_order_relaxed);
            if (held == key) {
                for (std::atomic<std::uint64_t> &value : _values[(start + probe) & (kSize - 1)]) {
                    value.store(0, std::memory_order_relaxed);
                }
                slot.store(kRetired, std::memory_order_release);
                _count.fetch_sub(1, std::memory_order_relaxed);
                return;
            }
            if (held == 0) {
                return;
            }
        }
    }

    SDK::ChannelId NativePlayerModule::AllocateChannelId() {
        SDK::IActorSystem *actors = SDK::IActorSystem::GetInstance();
        if (actors == nullptr) {
            return 0;
        }
        // Walks the remote range once from where the last allocation ended,
        // so ids are not reused while a released body's id could still be
        // read; a full lap with every id taken is exhaustion.
        const std::uint32_t rangeSize = 0x10000U - kRemoteChannelBase;
        for (std::uint32_t attempt = 0; attempt < rangeSize; ++attempt) {
            const SDK::ChannelId channelId = _nextChannelId;
            _nextChannelId                 = _nextChannelId == 0xFFFFU ? kRemoteChannelBase : static_cast<SDK::ChannelId>(_nextChannelId + 1);
            if (actors->GetActorByChannelId(channelId) == nullptr) {
                return channelId;
            }
        }
        return 0;
    }

    // --- The player's own lifecycle and update -------------------------------

    // Init's own registrations -- input action maps, the framework listener
    // the interactors tick from, the combat player -- are contained by the
    // hooks below; the scope covers the natives among them with no player.
    // The one place the channel is read: the game object is whole here, and
    // `CGameObjectSystem`'s creation hook set the channel before the queued
    // extension was even constructed. Registered first, so Init's own nested
    // natives already see a remote body. Not retired when Init fails: the
    // instantiator (0x1807068C4) then drops its shared_ptr, whose deleter
    // runs `Release` on this body, and that teardown must still see it as
    // remote.
    bool __fastcall NativePlayerModule::PlayerInit(SDK::wh::entitymodule::C_Player *player, SDK::IGameObject *gameObject) {
        const bool remote = gameObject != nullptr && gameObject->GetChannelId() >= kRemoteChannelBase;
        if (remote) {
            Get()._remoteBodies.Insert(BodyKey(player));
            Get()._remoteEntities.Insert(player->GetEntityId());
        }
        const RemoteScope scope(IsRemoteExecution() || remote);
        const bool initialized = Get()._originalPlayerInit(player, gameObject);
        // The extension's entity id is only certain once the native Init has
        // run; recorded again here so the by-entity set never holds a zero.
        if (remote) {
            Get()._remoteEntities.Insert(player->GetEntityId());
            // The body walks drunk, injured or armored as its condition says,
            // but at its owner's speeds: the owner's own body has no condition
            // controller, so no condition ever changes its stance.
            player->GetConditionController()->ClearConditionStances();
        }
        return initialized;
    }

    // Retired only after the native teardown, whose own hooks still have to
    // see a remote body. The native ends in `delete this`, so the body is
    // read before it and only its address is used after.
    void __fastcall NativePlayerModule::PlayerRelease(SDK::wh::entitymodule::C_Player *player) {
        const bool remote            = IsRemote(player);
        const SDK::EntityId entityId = remote ? player->GetEntityId() : 0;
        {
            const RemoteScope scope(IsRemoteExecution() || remote);
            Get()._originalPlayerRelease(player);
        }
        if (remote) {
            Get()._remoteBodies.Retire(BodyKey(player));
            Get()._remoteEntities.Retire(entityId);
            if (Get()._remoteReleased) {
                Get()._remoteReleased(entityId);
            }
        }
    }

    void __fastcall NativePlayerModule::PlayerUpdate(SDK::wh::entitymodule::C_Player *player, void *context, int slot) {
        const RemoteScope scope(IsRemote(player));
        Get()._originalPlayerUpdate(player, context, slot);
    }

    void __fastcall NativePlayerModule::PlayerPostUpdate(SDK::wh::entitymodule::C_Player *player, float frameTime) {
        const RemoteScope scope(IsRemote(player));
        Get()._originalPlayerPostUpdate(player, frameTime);
    }

    void __fastcall NativePlayerModule::PlayerProcessEvent(SDK::wh::entitymodule::C_Player *player, SDK::SEntityEvent *event) {
        const RemoteScope scope(IsRemote(player));
        Get()._originalPlayerProcessEvent(player, event);
    }

    // The player override adds only the process-wide "local player removed"
    // message after the complete human cleanup.
    void __fastcall NativePlayerModule::PlayerEntityRemoved(SDK::wh::entitymodule::C_Player *player) {
        if (!IsRemote(player)) {
            Get()._originalPlayerEntityRemoved(player);
            return;
        }
        const RemoteScope scope(true);
        hook::this_call<void>(SDK::gPatterns.C_Human__OnEntityRemoved, player);
    }

    void __fastcall NativePlayerModule::PlayerHandleDeath(SDK::wh::entitymodule::C_Player *player, bool dead) {
        const RemoteScope scope(IsRemote(player));
        Get()._originalPlayerHandleDeath(player, dead);
    }

    // The client-channel promotion: audio listener, player_who, module
    // message 40. Only this machine's player is ever promoted.
    void __fastcall NativePlayerModule::PlayerInitLocalPlayer(SDK::wh::entitymodule::C_Player *player) {
        if (!IsRemote(player)) {
            Get()._originalPlayerInitLocalPlayer(player);
        }
    }

    // The framework listener `C_Player::Init` registers ticks the player's
    // interactors, which aim from the local camera and install their action
    // callbacks into the shared controls service. A remote player's
    // interactions are replayed, never looked for.
    void __fastcall NativePlayerModule::PlayerOnPostUpdate(SDK::IGameFrameworkListener *listener, float frameTime) {
        if (!IsRemote(static_cast<SDK::wh::entitymodule::C_Player *>(listener))) {
            Get()._originalPlayerOnPostUpdate(listener, frameTime);
        }
    }

    // The player override adds local UI feedback through +0xCC8; remote
    // actions keep the shared actor handler.
    void __fastcall NativePlayerModule::PlayerNotifyActorActionRequest(SDK::wh::entitymodule::C_Player *player, const void *request) {
        if (!IsRemote(player)) {
            Get()._originalPlayerNotifyActorActionRequest(player, request);
            return;
        }
        const RemoteScope scope(true);
        hook::this_call<void>(SDK::gPatterns.C_Actor__NotifyActorActionRequest, player, request);
    }

    // The player override writes `player_in_crouch` and `player_stealth_skill`
    // on the global audio object and counts steps for a player-only routine
    // before running the human's own movement audio; a remote body runs only
    // the human's. The arguments are the override's own, forwarded as it
    // forwards them (0x18072E8A0): the third is a float in XMM2 and the
    // fourth a material name C_Human's reads as a string (0x180B72735) --
    // typed as integers they truncated that pointer and crashed.
    void __fastcall NativePlayerModule::PlayerUpdateMovementAudio(SDK::wh::entitymodule::C_Player *player, std::uintptr_t a2, float frameTime, const char *material, std::uint32_t a5) {
        if (!IsRemote(player)) {
            Get()._originalPlayerUpdateMovementAudio(player, a2, frameTime, material, a5);
            return;
        }
        hook::this_call<void>(SDK::gPatterns.C_Human__UpdateMovementAudio, player, a2, frameTime, material, a5);
    }

    // The player override answers the local cheat's god mode for any
    // player-controlled actor; C_Human's slot 39 (0x18066CD10) answers 0.
    int __fastcall NativePlayerModule::PlayerIsGod(SDK::wh::entitymodule::C_Player *player) {
        return IsRemote(player) ? 0 : Get()._originalPlayerIsGod(player);
    }

    // The player override opens the save prompt for Savior Schnapps and the
    // book UI for books; a remote body uses an item the way any actor does.
    std::uint32_t *__fastcall NativePlayerModule::PlayerUseItem(SDK::wh::entitymodule::C_Player *player, std::uint32_t *result, SDK::wh::entitymodule::C_Item *item, std::int32_t amount, std::int32_t context, bool checkFood, std::int32_t saveSlot) {
        if (!IsRemote(player)) {
            auto *framework = SDK::IGameFramework::GetInstance();
            if (KCDC::Core::gApplication != nullptr && framework != nullptr && player == framework->GetClientActor() && item != nullptr && static_cast<std::int32_t>(item->InvokeGetCategoryRaw()) == 5 && KCDC::Core::gApplication->GetPlayerInventoryService().HasAuthority()) {
                const KCDC::Features::Items::NativeFoodUse use {amount, context, checkFood, KCDC::Features::Items::NativeFoodUse::Source::Player, false, saveSlot};
                if (KCDC::Core::gApplication->GetLocalEvents().InterceptFoodUse(item, use)) {
                    // Nothing has been consumed yet. Bypass the player's failure
                    // signal and save side effects while preserving that result
                    // for synchronous callers. The inventory UI is held earlier.
                    *result = 128;
                    return result;
                }
            }
            return Get()._originalPlayerUseItem(player, result, item, amount, context, checkFood, saveSlot);
        }
        return hook::this_call<std::uint32_t *>(SDK::gPatterns.C_Actor__UseItem, player, result, item, amount, context, checkFood);
    }

    void __fastcall NativePlayerModule::PlayerItemEquipped(SDK::wh::entitymodule::I_EquipmentManagerListener *listener, void *inventoryContext, SDK::wh::framework::WUID itemId) {
        const RemoteScope scope(IsRemote(static_cast<SDK::wh::entitymodule::C_Player *>(listener)));
        Get()._originalPlayerItemEquipped(listener, inventoryContext, itemId);
    }

    void __fastcall NativePlayerModule::PlayerItemUnequipped(SDK::wh::entitymodule::I_EquipmentManagerListener *listener, void *inventoryContext, SDK::wh::framework::WUID itemId) {
        const RemoteScope scope(IsRemote(static_cast<SDK::wh::entitymodule::C_Player *>(listener)));
        Get()._originalPlayerItemUnequipped(listener, inventoryContext, itemId);
    }

    void __fastcall NativePlayerModule::PlayerRefreshHelmetActionHint(SDK::wh::entitymodule::C_Player *player) {
        if (!IsRemote(player)) {
            Get()._originalPlayerRefreshHelmetActionHint(player);
        }
    }

    // The faint sight in the dark every player has: `C_Player::Revive` ends
    // by pointing the player's own `C_AttachedLightHelper` (C_Player+0xBA8)
    // at the `Lights.Special.PlayerHeadLight` archetype and enabling it
    // whenever `wh_player_HeadLight` is set, which it is by default. That
    // archetype is a real deferred light -- 10 m radius, cold white, diffuse
    // multiplier 0.0001, no shadows, feeding volumetric fog -- carried by the
    // same helper a lit torch uses. A remote body revives through the same
    // native, so each one lit the dark it stood in and wore a halo in fog.
    // Only the player at this keyboard has eyes for a head light to serve.
    void __fastcall NativePlayerModule::PlayerRefreshHeadLight(SDK::wh::entitymodule::C_Player *player) {
        if (!IsRemote(player)) {
            Get()._originalPlayerRefreshHeadLight(player);
        }
    }

    void __fastcall NativePlayerModule::PlayerRegisterActions(SDK::wh::entitymodule::C_Player *player) {
        if (!IsRemote(player)) {
            Get()._originalPlayerRegisterActions(player);
        }
    }

    // Its one caller, `C_Player::Release` (0x180B51FEF), never loads RCX --
    // the receiver is whatever the previous call left there, and the native
    // ignores it. Decided by the release's scope alone; never read.
    void __fastcall NativePlayerModule::PlayerUnregisterActions(SDK::wh::entitymodule::C_Player *player) {
        if (!IsRemoteExecution()) {
            Get()._originalPlayerUnregisterActions(player);
        }
    }

    // --- The player's input ----------------------------------------------------

    void __fastcall NativePlayerModule::InputRegisterActions(SDK::wh::entitymodule::C_PlayerInput *input) {
        if (!IsRemote(input->GetPlayer())) {
            Get()._originalInputRegisterActions(input);
        }
    }

    // A remote body's input comes from replication, never from this keyboard.
    void __fastcall NativePlayerModule::InputOnAction(SDK::wh::entitymodule::C_PlayerInput *input, const SDK::CCryName &action, int activationMode, float value) {
        if (!IsRemote(input->GetPlayer())) {
            Get()._originalInputOnAction(input, action, activationMode, value);
        }
    }

    // Both roles run the native movement pass; a remote one under its scope,
    // since the rider input and combat controller listen from inside it.
    void __fastcall NativePlayerModule::InputPreUpdate(SDK::wh::entitymodule::C_PlayerInput *input) {
        const RemoteScope scope(IsRemote(input->GetPlayer()));
        Get()._originalInputPreUpdate(input);
    }

    void __fastcall NativePlayerModule::InputDestructor(SDK::wh::entitymodule::C_PlayerInput *input) {
        const RemoteScope scope(IsRemote(input->GetPlayer()));
        Get()._originalInputDestructor(input);
    }

    // --- Objects the player owns ------------------------------------------------

    void __fastcall NativePlayerModule::ChatFollowUpdate(SDK::wh::entitymodule::C_ChatFollowManager *chat) {
        if (!IsRemoteExecution() && !IsRemote(chat->GetPlayer())) {
            Get()._originalChatFollowUpdate(chat);
        }
    }

    // Publishes the HUD status `dog`, which a remote player -- who has no dog
    // here -- would clear on every spawn.
    void __fastcall NativePlayerModule::DogHudAvailability(SDK::wh::entitymodule::C_DogInteractionManager *manager) {
        if (!IsRemoteExecution() && !IsRemote(manager->GetPlayer())) {
            Get()._originalDogHudAvailability(manager);
        }
    }

    // Called only from the controller's constructor, inside `C_Player`'s,
    // before Init has registered the body: the spawner's scope decides.
    void __fastcall NativePlayerModule::MissileRegisterActions(SDK::wh::entitymodule::C_MissileWeaponPlayerController *controller) {
        if (!IsRemoteExecution()) {
            Get()._originalMissileRegisterActions(controller);
        }
    }

    // A copy's controller would bind this machine's action maps, so the
    // native never runs for it. Its tail -- queueing the next reload stage or
    // the draw as a reload stage finishes -- follows the owner's phase.
    void __fastcall NativePlayerModule::MissileRefreshActionMap(SDK::wh::entitymodule::C_MissileWeaponPlayerController *controller) {
        if (!IsRemoteExecution() && !IsRemote(controller->GetPlayer())) {
            Get()._originalMissileRefreshActionMap(controller);
        }
        else if (IsRemote(controller->GetPlayer()) && KCDC::Core::gApplication != nullptr) {
            KCDC::Core::gApplication->GetPlayerMissileService().ContinueRemoteReload(controller->GetPlayer());
        }
    }

    // The mounted hint bindings capture the rider and replace the shared
    // X-key dismount callback; only this machine's player owns that binding.
    void __fastcall NativePlayerModule::RiderRegisterMountedHints(SDK::wh::entitymodule::C_RiderPlayerControl *control) {
        if (!IsRemoteExecution() && !IsRemote(control->GetPlayer())) {
            Get()._originalRiderRegisterMountedHints(control);
        }
    }

    // `~C_RiderPlayerControl` (0x18100D1E4) calls this straight after
    // another call without reloading RCX, so the receiver is garbage there:
    // the native only uses it as a delegate's identity. Decided by the scope
    // alone -- a remote body's release always runs in one -- and the
    // receiver is never read.
    void __fastcall NativePlayerModule::RiderUnregisterMountedHints(SDK::wh::entitymodule::C_RiderPlayerControl *control) {
        if (!IsRemoteExecution()) {
            Get()._originalRiderUnregisterMountedHints(control);
        }
    }

    void __fastcall NativePlayerModule::RiderUpdateMountedHints(SDK::wh::entitymodule::C_RiderPlayerControl *control) {
        if (!IsRemoteExecution() && !IsRemote(control->GetPlayer())) {
            Get()._originalRiderUpdateMountedHints(control);
        }
    }

    // The rider's own locks, input listener and soul effects belong to every
    // rider; the script globals the callback ends in are the local player's.
    void __fastcall NativePlayerModule::RiderStateChanged(SDK::wh::entitymodule::C_RiderPlayerControl *control, std::uint8_t rideState) {
        const RemoteScope scope(IsRemote(control->GetPlayer()));
        Get()._originalRiderStateChanged(control, rideState);
    }

    // Aligns the rider input to the horse the body's ride reference names,
    // read unchecked on a get-on and a get-off. A remote body is put through
    // a get-off by replication, which can come once its horse has already
    // stopped resolving -- streamed out, or its ride reference cleared.
    void __fastcall NativePlayerModule::RiderInputStateChanged(SDK::wh::entitymodule::C_RiderPlayerInput *input, std::uint8_t rideState) {
        SDK::wh::entitymodule::C_Player *player = input->GetPlayer();
        if (IsRemote(player) && player->GetRiddenHorse() == nullptr) {
            return;
        }
        Get()._originalRiderInputStateChanged(input, rideState);
    }

    // Built from `C_Player`'s constructor, before Init has registered the
    // body: the spawner's scope is what says the body is remote this early.
    SDK::wh::combatmodule::C_CombatPlayer *__fastcall NativePlayerModule::CreateCombatPlayer(SDK::wh::combatmodule::C_CombatModule *module, SDK::wh::entitymodule::C_Actor *actor) {
        const bool remote                                  = IsRemoteExecution();
        SDK::wh::combatmodule::C_CombatPlayer *localPlayer = module->GetLocalPlayer();
        SDK::wh::combatmodule::C_CombatPlayer *player      = Get()._originalCreateCombatPlayer(module, actor);
        if (remote) {
            module->SetLocalPlayer(localPlayer);
        }
        return player;
    }

    bool __fastcall NativePlayerModule::CombatPlayerInit(SDK::wh::combatmodule::C_CombatPlayer *player) {
        const bool remote = IsRemoteExecution() || IsRemote(player->GetOwner());
        const RemoteScope scope(remote);
        if (!remote) {
            return Get()._originalCombatPlayerInit(player);
        }
        // The fighter's own scene, as the native reaches it (slot 201, +0x38).
        SDK::wh::combatmodule::C_CombatLodManager *lod     = player->GetScene()->GetLodManager();
        SDK::wh::combatmodule::C_CombatPlayer *localPlayer = lod->GetLocalPlayer();
        const bool initialized                             = Get()._originalCombatPlayerInit(player);
        lod->SetLocalPlayer(localPlayer);
        return initialized;
    }

    void __fastcall NativePlayerModule::CombatPlayerShutdown(SDK::wh::combatmodule::C_CombatPlayer *player) {
        const bool remote = IsRemoteExecution() || IsRemote(player->GetOwner());
        const RemoteScope scope(remote);
        if (!remote) {
            Get()._originalCombatPlayerShutdown(player);
            return;
        }
        SDK::wh::combatmodule::C_CombatLodManager *lod     = player->GetScene()->GetLodManager();
        SDK::wh::combatmodule::C_CombatPlayer *localPlayer = lod->GetLocalPlayer();
        Get()._originalCombatPlayerShutdown(player);
        lod->SetLocalPlayer(localPlayer);
    }

    // Broadcasts "the local player's fighter is gone" and clears the module's
    // local player for any player-controlled owner. A remote body is player
    // controlled by its channel, so the owner is named for the duration and
    // `C_Actor::IsPlayerControlled` answers false for it alone.
    void __fastcall NativePlayerModule::DestroyCombatActor(SDK::wh::combatmodule::C_CombatModule *module, SDK::wh::combatmodule::C_CombatActor **fighter) {
        SDK::wh::entitymodule::C_Actor *owner = fighter != nullptr && *fighter != nullptr ? (*fighter)->GetOwner() : nullptr;
        if (owner == nullptr || (!IsRemote(owner) && !IsRemoteExecution())) {
            Get()._originalDestroyCombatActor(module, fighter);
            return;
        }
        const SDK::wh::entitymodule::C_Actor *previous = t_destroyingRemoteFighter;
        t_destroyingRemoteFighter                      = owner;
        const RemoteScope scope(true);
        Get()._originalDestroyCombatActor(module, fighter);
        t_destroyingRemoteFighter = previous;
    }

    // Reached from the derived-stat recompute every soul runs, possibly off
    // the game thread. The native does nothing unless the soul carries the
    // player material flag; under it, it sets the body's own `mat_player`
    // switch and submits the global one this module contains. It reads the
    // body without a check (0x180BE22A1), so a soul with none is left alone.
    // Decided by entity id, never through the actor system's map.
    void __fastcall NativePlayerModule::SoulPlayerMaterialAudio(SDK::wh::rpgmodule::C_Soul *soul, std::uint32_t surfaceType) {
        if (!soul->HasPlayerMaterialAudio()) {
            Get()._originalSoulPlayerMaterialAudio(soul, surfaceType);
            return;
        }
        SDK::IEntity *entity = soul->GetActorEntityHandle(0);
        if (entity == nullptr) {
            return;
        }
        const RemoteScope scope(IsRemoteExecution() || IsRemoteEntity(entity->GetId()));
        Get()._originalSoulPlayerMaterialAudio(soul, surfaceType);
    }

    bool NativePlayerModule::IsRemoteSoul(SDK::wh::rpgmodule::C_Soul *soul) {
        if (soul == nullptr) {
            return false;
        }
        SDK::IEntity *entity = soul->GetActorEntityHandle(0);
        return entity != nullptr && IsRemoteEntity(entity->GetId());
    }

    bool NativePlayerModule::IsRemoteBuffOwner(SDK::wh::rpgmodule::C_BuffInstanceBase *buff) {
        return IsRemoteSoul(buff->GetOwner());
    }

    // The exhausted buff plays the `player_exhausted` effect and rolls the
    // extreme-exhaustion faint for any soul with the player flag, and a
    // remote body's soul keeps it. The owner's tiredness and faint already
    // arrive through its replicated state.
    void __fastcall NativePlayerModule::ExhaustionEffect(SDK::wh::rpgmodule::C_BuffInstanceBase *buff) {
        if (!IsRemoteBuffOwner(buff)) {
            Get()._originalExhaustionEffect(buff);
        }
    }

    // The starvation buff plays the full-screen `player_hunger` effect
    // through the body it ticks on, so a hungry remote body blurred this
    // machine's screen. Its handle stays 0, which the buff's stop path
    // already treats as nothing playing.
    void __fastcall NativePlayerModule::HungerEffect(SDK::wh::rpgmodule::C_BuffInstanceBase *buff) {
        if (!IsRemoteBuffOwner(buff)) {
            Get()._originalHungerEffect(buff);
        }
    }

    // Henry's own feedback sounds (hit grunts, breathing, push, bodyfalls,
    // the whistle and its tutorial) are authored for the player hearing
    // himself, so a remote body playing them sounded like this machine's
    // character. Stops still run so nothing started earlier is left on.
    void __fastcall NativePlayerModule::SetFeedbackSound(SDK::wh::entitymodule::C_Player *player, int sound, bool play, bool restart, const char *parameter) {
        if (play && IsRemote(player)) {
            return;
        }
        Get()._originalSetFeedbackSound(player, sound, play, restart, parameter);
    }

    // Reached only from the C_CombatEffects callback (0x180912468), which
    // plays the perfect block and master strike stingers on the body of a
    // player-controlled fighter whose action is one of the two. Both are
    // 2D events with a 65535 m culling distance, the player's own cue, so a
    // remote fighter's rang out here at full volume however far away it
    // fought. An NPC fighter plays neither; the callback's shouts stay.
    void __fastcall NativePlayerModule::ExecuteEntityAudioTrigger(void *unused, const std::uint32_t *triggerId, SDK::IEntity *entity) {
        if (!IsRemoteEntity(entity->GetId())) {
            Get()._originalExecuteEntityAudioTrigger(unused, triggerId, entity);
        }
    }

    // The soul's combat-danger pass writes two process-wide "player is in
    // combat danger" flags and raises PlayerIsInCombatDanger for every soul
    // with the player flag, which a remote body's soul keeps: a remote fight
    // put this machine in combat danger, and the two souls flipped the flags
    // against each other. The owner's combat state arrives replicated.
    void __fastcall NativePlayerModule::UpdateCombatDanger(void *context, SDK::wh::rpgmodule::C_Soul *soul, float frameTime) {
        if (!IsRemoteSoul(soul)) {
            Get()._originalUpdateCombatDanger(context, soul, frameTime);
        }
    }

    // A soul buff whose definition names an actor effect (drunkenness,
    // hangover, poison and the like) starts it on the body it attaches to,
    // and on a player body that effect is a full-screen one. A remote body's
    // buffs left this machine's screen blurred. The handle stays 0, which
    // the buff's detach already treats as nothing playing.
    void __fastcall NativePlayerModule::BuffStartActorEffect(SDK::wh::rpgmodule::C_BuffInstanceBase *buff, SDK::wh::entitymodule::C_Actor *actor) {
        if (!IsRemote(actor)) {
            Get()._originalBuffStartActorEffect(buff, actor);
        }
    }

    // Tutorials are this machine's player's; none is shown for what a remote
    // body does inside a remote scope.
    bool __fastcall NativePlayerModule::ShowTutorial(void *tutorials, const SDK::CryStringT<char> &name) {
        if (IsRemoteExecution()) {
            return false;
        }
        return Get()._originalShowTutorial(tutorials, name);
    }

    // Another player's body falls on its owner's machine and nowhere else.
    // The physics here reach it on their own -- a horse copy striking a wall
    // the owner's horse rode past drops a fall on its rider -- and the ragdoll
    // that follows replaces the rider action and ends the ride on this
    // machine alone, while the owner rides on.
    void __fastcall NativePlayerModule::ActorFall(SDK::wh::entitymodule::C_Actor *actor, const void *hitPos) {
        NativePlayerModule &module = Get();
        if (IsRemote(actor)) {
            // Only its owner's fall, replayed (ReplayFall).
            if (module._replayingFall) {
                module._originalActorFall(actor, hitPos);
            }
            return;
        }
        // This machine's player fell: every copy of its body falls too. Where
        // it struck goes from where the body stands now, for a copy to be
        // struck at the same point from where it stands when it falls. The
        // argument is the address of a by-value vector, never null.
        if (actor != nullptr && actor == KCDC::Common::LocalHumanActor()) {
            const SDK::Vec3 &hit       = *static_cast<const SDK::Vec3 *>(hitPos);
            const SDK::Vec3 at         = actor->GetEntity()->GetWorldPos();
            module._localFallHitOffset = {hit.x - at.x, hit.y - at.y, hit.z - at.z};
            ++module._localFalls;
        }
        module._originalActorFall(actor, hitPos);
    }

    // Another player's body dies animated. A player's own death stops every
    // layer of its character (`C_Player::HandleDeath`), unseen from the first
    // person; a copy, which dies with no blow to play a death fragment from,
    // then stands in its bind pose for the frames its ragdoll takes to
    // build, and falls from that pose. Kept animated, it falls from where it
    // stood. The function is folded into other classes' tables too; any
    // other body passes through.
    void __fastcall NativePlayerModule::StopAnimations(SDK::wh::entitymodule::C_Actor *actor) {
        if (!IsRemote(actor)) {
            Get()._originalStopAnimations(actor);
        }
    }

    void NativePlayerModule::ReplayFall(SDK::wh::entitymodule::C_Actor *actor, float x, float y, float z) {
        NativePlayerModule &module = Get();
        const float hitPos[3]      = {x, y, z};
        module._replayingFall      = true;
        ActorFall(actor, hitPos);
        module._replayingFall = false;
    }

    void NativePlayerModule::SetMovementCorrection(const SDK::wh::entitymodule::C_Actor *actor, float x, float y) {
        const std::uint64_t step = (static_cast<std::uint64_t>(std::bit_cast<std::uint32_t>(y)) << 32) | std::bit_cast<std::uint32_t>(x);
        Get()._remoteBodies.Store(BodyKey(actor), kValueStep, x == 0.0F && y == 0.0F ? 0 : step);
    }

    void NativePlayerModule::ForgetMount(const SDK::wh::entitymodule::C_Actor *mount) {
        Get()._remoteMounts.Retire(BodyKey(mount));
    }

    void NativePlayerModule::SetMountGait(const SDK::wh::entitymodule::C_Actor *mount, std::int32_t gaitStyleId) {
        RemoteKeySet &mounts = Get()._remoteMounts;
        if (!mounts.Contains(BodyKey(mount))) {
            mounts.Insert(BodyKey(mount));
        }
        mounts.Store(BodyKey(mount), kValueMountGait, static_cast<std::uint64_t>(static_cast<std::int64_t>(gaitStyleId) + 2));
    }

    void NativePlayerModule::SetMountLocomotion(const SDK::wh::entitymodule::C_Actor *mount, float pace, float speed) {
        RemoteKeySet &mounts = Get()._remoteMounts;
        if (!mounts.Contains(BodyKey(mount))) {
            mounts.Insert(BodyKey(mount));
        }
        mounts.Store(BodyKey(mount), kValueMountPace, kMountValueSet | std::bit_cast<std::uint32_t>(pace));
        mounts.Store(BodyKey(mount), kValueMountSpeed, kMountValueSet | std::bit_cast<std::uint32_t>(speed));
    }

    void NativePlayerModule::ClearMountLocomotion(const SDK::wh::entitymodule::C_Actor *mount) {
        RemoteKeySet &mounts = Get()._remoteMounts;
        if (!mounts.Contains(BodyKey(mount))) {
            return;
        }
        mounts.Store(BodyKey(mount), kValueMountPace, 0);
        mounts.Store(BodyKey(mount), kValueMountSpeed, 0);
    }

    bool NativePlayerModule::MountPace(const SDK::wh::entitymodule::C_Actor *mount, float &pace) {
        const std::uint64_t stored = mount != nullptr ? Get()._remoteMounts.Read(BodyKey(mount), kValueMountPace) : 0;
        if ((stored & kMountValueSet) == 0) {
            return false;
        }
        pace = std::bit_cast<float>(static_cast<std::uint32_t>(stored));
        return true;
    }

    bool NativePlayerModule::MountSpeed(const SDK::wh::entitymodule::C_Actor *mount, float &speed) {
        const std::uint64_t stored = mount != nullptr ? Get()._remoteMounts.Read(BodyKey(mount), kValueMountSpeed) : 0;
        if ((stored & kMountValueSet) == 0) {
            return false;
        }
        speed = std::bit_cast<float>(static_cast<std::uint32_t>(stored));
        return true;
    }

    bool NativePlayerModule::MountGait(const SDK::wh::entitymodule::C_Actor *mount, std::int32_t &gaitStyleId) {
        const std::uint64_t stored = mount != nullptr ? Get()._remoteMounts.Read(BodyKey(mount), kValueMountGait) : 0;
        if (stored == 0) {
            return false;
        }
        gaitStyleId = static_cast<std::int32_t>(static_cast<std::int64_t>(stored) - 2);
        return true;
    }

    void NativePlayerModule::SetCombatLook(const SDK::wh::entitymodule::C_Actor *actor, float yaw, float pitch) {
        // Never zero as stored: a zero word means "no look yet".
        const std::uint64_t look = (static_cast<std::uint64_t>(std::bit_cast<std::uint32_t>(pitch)) << 32) | std::bit_cast<std::uint32_t>(yaw);
        Get()._remoteBodies.Store(BodyKey(actor), kValueCombatLook, look != 0 ? look : 1);
    }

    void NativePlayerModule::SetLookHeld(SDK::EntityId entityId, bool held) {
        Get()._remoteEntities.Store(entityId, kValueLookHeld, held ? 1 : 0);
    }

    // The look context turns the head only while a looking clip asks for it
    // as well as the game (CProceduralContextLook::Update). A body holding
    // its owner's look has no such clip, and its context was logged with the
    // clip's request down at every update while it sat, whatever the body's
    // own update had written: so the request is raised here, where the
    // context reads it.
    void __fastcall NativePlayerModule::LookContextUpdate(SDK::CProceduralContextLook *context, float timePassed) {
        const SDK::IEntity *entity = context->GetEntity();
        if (entity != nullptr && Get()._remoteEntities.Read(entity->GetId(), kValueLookHeld) != 0) {
            context->SetClipRequestsLooking(true);
        }
        Get()._originalLookContextUpdate(context, timePassed);
    }

    bool NativePlayerModule::WasMovedByCombatInput(const SDK::wh::entitymodule::C_Actor *actor) {
        return actor != nullptr && Get()._remoteBodies.Read(BodyKey(actor), kValueCombatHandled) != 0;
    }

    // Every on-foot and airborne movement state hands its frame's request to
    // the animated character through here (`AddMovement`, vtable +0x158).
    // Position correction must not change C_Actor+0x4E8 or the desired
    // local location, which choose the move state and run/jog blend. Apply
    // it through `IAnimatedCharacter::ForceMovement`, which the player's
    // movement action calls every frame at 0x180949E59. `MergeEntityMovement`
    // (0x180839FF4) composes it after the control-method merge whatever the
    // horizontal method is, and it reaches physics in the same
    // `pe_action_move`, collisions and ground state intact. That includes a
    // swing's animation-driven movement: the owner's step toward its
    // opponent is its combat alignment, which a copy does not run (see
    // `CombatAlignmentProcessClipEvents`), so the correction is what carries
    // the copy along with it, on top of the clip's own root motion, instead
    // of the gap being closed in one lurch once the swing hands the body
    // back. Only for a Normal request -- any other type carries a jump, and
    // a changed move direction would fire it again. Translation only: the
    // body's rotation is the movement state's own request toward the player
    // rotation's base. Same object and same thread as the AddMovement just
    // made.
    void __fastcall NativePlayerModule::SubmitCharacterMoveRequest(SDK::wh::entitymodule::C_Actor *actor, void *frameParams, SDK::SCharacterMoveRequest *request) {
        const bool normal = request->type == SDK::eCMT_Normal;
        Get()._originalSubmitCharacterMoveRequest(actor, frameParams, request);
        if (!normal) {
            return;
        }
        const std::uint64_t step = Get()._remoteBodies.Take(BodyKey(actor), kValueStep);
        if (step == 0) {
            return;
        }
        SDK::IAnimatedCharacter *animated = actor->GetAnimatedCharacter();
        if (animated == nullptr) {
            return;
        }
        animated->ForceMovement(SDK::QuatT {{0.0F, 0.0F, 0.0F, 1.0F}, {std::bit_cast<float>(static_cast<std::uint32_t>(step)), std::bit_cast<float>(static_cast<std::uint32_t>(step >> 32)), 0.0F}});
    }

    // The combat input listener every player's PreUpdate runs (thunk
    // 0x18083019C; the fourth argument is PreUpdate's look delta, a pointer
    // -- `lea r9` at 0x18082F9DF). Locked on to a target, it seeds the
    // fighter's turn from "the player's" combat camera -- reached through
    // the module's one local fighter, so on a remote copy it is this
    // machine's camera -- and in its free mode writes that camera's yaw into
    // the body (0x1808314BC) and the player rotation's view and base
    // (0x1808314D4). Each fighter has a camera manager of its own
    // (C_CombatPlayer+0x458): a remote one is served that, holding where its
    // owner looks (SetCombatLook), so the listener turns the copy exactly as
    // the owner's own camera turns the owner. Never seeded from the copy's
    // own body: that loops the body's yaw back into its base, and the base
    // is what the ground movement turns the body toward.
    bool __fastcall NativePlayerModule::CombatFilterMovementRequest(SDK::wh::combatmodule::C_CombatPlayerInput *input, SDK::CMovementRequest *request, SDK::Vec3 *movement, SDK::Ang3 *lookDelta) {
        auto *fighter                                        = static_cast<SDK::wh::combatmodule::C_CombatPlayer *>(static_cast<const SDK::wh::combatmodule::C_CombatActorObject *>(input)->GetCombatActor());
        SDK::wh::entitymodule::C_Actor *owner                = fighter != nullptr ? fighter->GetOwner() : nullptr;
        SDK::wh::combatmodule::C_CombatCameraManager *camera = IsRemote(owner) ? fighter->GetCameraManager() : nullptr;
        if (camera == nullptr) {
            return Get()._originalCombatFilterMovementRequest(input, request, movement, lookDelta);
        }
        const std::uint64_t look = Get()._remoteBodies.Read(BodyKey(owner), kValueCombatLook);
        if (look != 0) {
            const float halfYaw   = std::bit_cast<float>(static_cast<std::uint32_t>(look)) * 0.5F;
            const float halfPitch = std::bit_cast<float>(static_cast<std::uint32_t>(look >> 32)) * 0.5F;
            // Yaw about Z, then pitch about the turned X: CryEngine's
            // Quat::CreateRotationZ(yaw) * Quat::CreateRotationX(pitch).
            const float cy = std::cos(halfYaw), sy = std::sin(halfYaw), cp = std::cos(halfPitch), sp = std::sin(halfPitch);
            camera->SetRotation(SDK::Quat {cy * sp, sy * sp, sy * cp, cy * cp});
        }
        // A remote body's movement is its owner's: the owner's move goes into
        // its request, and the correction carries it onto the owner's drawn
        // pose (`SubmitCharacterMoveRequest`). The listener's own movement is
        // not. Locked on, it moves the body around its opponent
        // (0x18118B8F4, 0x18118B9FC) and steers that by how the body really
        // moved since its last pass -- on a copy, the correction's own step,
        // which the listener then works against, so the copy swung around its
        // drawn pose. So it moves a scratch request, and a body it handles
        // takes the move as the listener's free mode hands it (0x1808314DF).
        // A dodge's movement is the dodge's own (0x182755C54) and goes into
        // the request as before. The listener's turn and the served camera
        // are unchanged.
        const bool dodging                                     = fighter->IsDodging();
        SDK::CMovementRequest scratch                          = *request;
        SDK::wh::combatmodule::C_CombatCameraManager *previous = t_remoteCombatCamera;
        t_remoteCombatCamera                                   = camera;
        const bool handled                                     = Get()._originalCombatFilterMovementRequest(input, dodging ? request : &scratch, movement, lookDelta);
        t_remoteCombatCamera                                   = previous;
        if (handled && !dodging) {
            request->AddDeltaMovement(*movement);
        }
        Get()._remoteBodies.Store(BodyKey(owner), kValueCombatHandled, handled && dodging ? 1 : 0);
        return handled;
    }

    SDK::wh::combatmodule::C_CombatCameraManager *__fastcall NativePlayerModule::GetLocalPlayerCameraManager(SDK::wh::combatmodule::C_CombatModule *module) {
        return t_remoteCombatCamera != nullptr ? t_remoteCombatCamera : Get()._originalGetLocalPlayerCameraManager(module);
    }

    // Combat alignment -- turning a fighter to its opponent, closing on
    // it, settling a paired move -- is a pair of solvers on the aligned
    // body's own alignment manager, and every one of them is installed from
    // inside this pass: the queued clip events of the attack, block and
    // paired fragments are drained here, and each handler installs its
    // solver (`SetPositionSolver`/`SetRotationSolver`) before it registers
    // it with this manager. A remote body's placement and facing are its
    // owner's, already aligned on the owner's machine and replicated.
    // On a copy the solvers would move it a second time toward this
    // machine's opponent position, or the world origin without an opponent.
    // Inside this pass a remote body refuses the solver, and the handler
    // registers nothing when it receives no id.
    // Outside it -- an interaction settling on a seat or a door -- the copy
    // aligns natively, to the same fixed target its owner did. This also
    // covers the alignment a local fighter's combined move puts on a remote
    // opponent: that body too is its owner's to move.
    //
    // And the other way round: a remote fighter's swing, block or paired
    // move installs a solver on its opponent too (ProcessCombinedAlignmentEvent,
    // which=1), and here that opponent is this machine's player. Run, it
    // turned the local player up to 30 degrees in a frame and slid it a few
    // centimetres around every remote swing -- the watcher's camera jolting.
    // The owner's machine does not align this player either (it is remote
    // there), so a remote fighter's pass installs nothing on anyone.
    void __fastcall NativePlayerModule::CombatAlignmentProcessClipEvents(SDK::wh::combatmodule::C_CombatAlignmentManager *manager) {
        SDK::wh::combatmodule::C_CombatActor *fighter = manager->GetCombatActor();
        const bool previous                           = t_combatAlignmentPass;
        const bool previousRemote                     = t_remoteFighterAlignmentPass;
        t_combatAlignmentPass                         = true;
        t_remoteFighterAlignmentPass                  = fighter != nullptr && IsRemote(fighter->GetOwner());
        Get()._originalCombatAlignmentProcessClipEvents(manager);
        t_combatAlignmentPass        = previous;
        t_remoteFighterAlignmentPass = previousRemote;
    }

    // The per-frame retarget of a fighter's own installed solvers toward
    // its opponent. A remote fighter has none to retarget.
    void __fastcall NativePlayerModule::CombatAlignmentUpdateSolverTargets(SDK::wh::combatmodule::C_CombatAlignmentManager *manager) {
        SDK::wh::combatmodule::C_CombatActor *fighter = manager->GetCombatActor();
        if (fighter != nullptr && IsRemote(fighter->GetOwner())) {
            return;
        }
        Get()._originalCombatAlignmentUpdateSolverTargets(manager);
    }

    std::uint32_t __fastcall NativePlayerModule::AlignmentSetPositionSolver(SDK::wh::entitymodule::I_AlignmentManager *alignment, const void *request) {
        if (t_combatAlignmentPass && (t_remoteFighterAlignmentPass || IsRemote(static_cast<SDK::wh::entitymodule::C_AlignmentManager *>(alignment)->GetOwner()))) {
            return 0;
        }
        return Get()._originalAlignmentSetPositionSolver(alignment, request);
    }

    std::uint32_t __fastcall NativePlayerModule::AlignmentSetRotationSolver(SDK::wh::entitymodule::I_AlignmentManager *alignment, const void *request) {
        if (t_combatAlignmentPass && (t_remoteFighterAlignmentPass || IsRemote(static_cast<SDK::wh::entitymodule::C_AlignmentManager *>(alignment)->GetOwner()))) {
            return 0;
        }
        return Get()._originalAlignmentSetRotationSolver(alignment, request);
    }

    // The combat camera clip a kick or a heavy hit carries
    // (C_CombatProceduralClipAnimateCamera) animates "the player's" camera
    // -- C_CombatModule::GetLocalPlayerCameraManager, this machine's -- on
    // OnEnter and lets it go on OnExit, both behind this one gate, which asks
    // C_Actor::IsPlayerControlled of the clip's body. A remote player's copy
    // is player controlled by its channel, so its kick shook the watcher's
    // camera. Inside the gate the copy is answered as what it is here: not
    // this machine's player (see the Combat module's IsPlayerControlled
    // detour). Enter and exit share the gate, so the camera's counter stays
    // balanced.
    bool __fastcall NativePlayerModule::CameraClipIsOwnerPlayerControlled(const void *clip) {
        const bool previous    = t_localPlayerQuery;
        t_localPlayerQuery = true;
        const bool answer      = Get()._originalCameraClipIsOwnerPlayerControlled(clip);
        t_localPlayerQuery = previous;
        return answer;
    }

    // Every Mannequin clip that drives a camera drives THE camera: the one
    // global C_CameraManager this machine renders. Each decides whether its
    // body is "the player" by C_Actor::IsPlayerControlled (slot 78) -- true
    // for a remote player's copy, whose channel says a player drives it --
    // and the bone camera's update and exit ask nothing at all. So a remote
    // player's free-guard kick (fragment FreeAttack, clip AnimateCamera),
    // swing or finisher took over the watcher's first-person camera, near
    // plane or bone camera, and AnimateCamera's exit even pushed the
    // watcher's view back into the copy. These clips touch nothing but the
    // camera, so a remote body plays its fragment without them, the way an
    // NPC's clip passes them by.
    bool NativePlayerModule::IsRemoteClip(const SDK::IProceduralClip *clip) {
        const SDK::IEntity *entity = clip->GetEntity();
        return entity != nullptr && IsRemoteEntity(entity->GetId());
    }

    void __fastcall NativePlayerModule::AnimateCameraClipEnter(SDK::IProceduralClip *clip, float blendTime, float duration, const void *params) {
        if (!IsRemoteClip(clip)) {
            Get()._originalAnimateCameraClipEnter(clip, blendTime, duration, params);
        }
    }

    void __fastcall NativePlayerModule::AnimateCameraClipExit(SDK::IProceduralClip *clip, float blendTime) {
        if (!IsRemoteClip(clip)) {
            Get()._originalAnimateCameraClipExit(clip, blendTime);
        }
    }

    void __fastcall NativePlayerModule::CameraNearClipEnter(SDK::IProceduralClip *clip, float blendTime, float duration, const void *params) {
        if (!IsRemoteClip(clip)) {
            Get()._originalCameraNearClipEnter(clip, blendTime, duration, params);
        }
    }

    void __fastcall NativePlayerModule::CameraNearClipExit(SDK::IProceduralClip *clip, float blendTime) {
        if (!IsRemoteClip(clip)) {
            Get()._originalCameraNearClipExit(clip, blendTime);
        }
    }

    void __fastcall NativePlayerModule::BoneCameraClipEnter(SDK::IProceduralClip *clip, float blendTime, float duration, const void *params) {
        if (!IsRemoteClip(clip)) {
            Get()._originalBoneCameraClipEnter(clip, blendTime, duration, params);
        }
    }

    void __fastcall NativePlayerModule::BoneCameraClipExit(SDK::IProceduralClip *clip, float blendTime) {
        if (!IsRemoteClip(clip)) {
            Get()._originalBoneCameraClipExit(clip, blendTime);
        }
    }

    void __fastcall NativePlayerModule::BoneCameraClipUpdate(SDK::IProceduralClip *clip, float timePassed) {
        if (!IsRemoteClip(clip)) {
            Get()._originalBoneCameraClipUpdate(clip, timePassed);
        }
    }

    // A mercy kill starts and ends the finisher camera on this machine's
    // combat camera (0x18277FBB8 through GetLocalPlayerCameraManager) and the
    // finisher HUD when its actor answers IsPlayerControlled. Both halves ask
    // inside the same scope, so a remote player's finisher touches neither
    // and the camera's counter stays balanced.
    bool __fastcall NativePlayerModule::MercyKillRequest(void *mercyKill, std::uint32_t target) {
        const bool previous = t_localPlayerQuery;
        t_localPlayerQuery  = true;
        const bool accepted = Get()._originalMercyKillRequest(mercyKill, target);
        t_localPlayerQuery  = previous;
        return accepted;
    }

    void __fastcall NativePlayerModule::MercyKillFinished(void *mercyKill) {
        const bool previous = t_localPlayerQuery;
        t_localPlayerQuery  = true;
        Get()._originalMercyKillFinished(mercyKill);
        t_localPlayerQuery = previous;
    }

    // --- Process-wide natives with no player argument ----------------------------

    // `C_Player::Revive` resets the camera manager, a HUD flag and save lock
    // 6 inline, behind a null test on this accessor (0x180BC684F); the rest
    // of Revive is the body's own. Answering null to that one call site, for
    // a remote body, skips exactly that block. The return address is
    // compared first, so every other caller pays one compare.
    SDK::wh::C_GlobalEnvironment *__fastcall NativePlayerModule::GetGlobalEnvironment() {
        if (reinterpret_cast<std::uintptr_t>(_ReturnAddress()) == SDK::gPatterns.C_Player__ReviveLocalGlobalsReturn && t_remoteExecution) {
            return nullptr;
        }
        return s_originalGetGlobalEnvironment();
    }

    void __fastcall NativePlayerModule::CameraUpdate(SDK::wh::game::C_CameraManager *camera) {
        if (!IsRemoteExecution()) {
            Get()._originalCameraUpdate(camera);
        }
    }

    void __fastcall NativePlayerModule::ScreenEffectsPostUpdate(void *effects) {
        if (!IsRemoteExecution()) {
            Get()._originalScreenEffectsPostUpdate(effects);
        }
    }

    void __fastcall NativePlayerModule::SetSaveLock(void *profile, std::uint8_t lock, bool enabled) {
        if (!IsRemoteExecution()) {
            Get()._originalSetSaveLock(profile, lock, enabled);
        }
    }

    void __fastcall NativePlayerModule::SetActionEnabled(SDK::wh::playermodule::I_ActionSets *sets, const SDK::CryStringT<char> &setName, const SDK::CryStringT<char> &actionName, bool enabled, bool includeDerived) {
        if (!IsRemoteExecution()) {
            Get()._originalSetActionEnabled(sets, setName, actionName, enabled, includeDerived);
        }
    }

    void __fastcall NativePlayerModule::ClearActionCallbacks(SDK::wh::playermodule::I_ActionSets *sets, const SDK::CryStringT<char> &setName, const SDK::CryStringT<char> &actionName, bool includeDerived) {
        if (!IsRemoteExecution()) {
            Get()._originalClearActionCallbacks(sets, setName, actionName, includeDerived);
        }
    }

    // Player.lua registers every Player with AI as AIOBJECT_PLAYER, and the
    // manager's player branch caches the one local player. A remote body is
    // registered as the game does it, and the cache handed back.
    bool __fastcall NativePlayerModule::RegisterNPC(SDK::wh::xgenaimodule::C_NPCManager *manager, SDK::IEntity *entity, std::uint32_t flags) {
        SDK::IActorSystem *actors = entity != nullptr ? SDK::IActorSystem::GetInstance() : nullptr;
        auto *actor               = actors != nullptr ? static_cast<SDK::wh::entitymodule::C_Actor *>(actors->GetActor(entity->GetId())) : nullptr;
        if (!IsRemoteExecution() && !IsRemote(actor)) {
            return Get()._originalRegisterNPC(manager, entity, flags);
        }
        // The player branch also rebinds the script `__player` variables and
        // the stealth watcher to the record it just made; the scope is what
        // tells those two detours the record is not this machine's player.
        const RemoteScope scope(true);
        const SDK::wh::xgenaimodule::C_NPCManager::LocalPlayerBinding localPlayer = manager->CaptureLocalPlayerBinding();
        const bool registered                                                     = Get()._originalRegisterNPC(manager, entity, flags);
        manager->RestoreLocalPlayerBinding(localPlayer);
        return registered;
    }

    void __fastcall NativePlayerModule::RebindScriptVariables(void *context) {
        if (!IsRemoteExecution()) {
            Get()._originalRebindScriptVariables(context);
        }
    }

    std::intptr_t __fastcall NativePlayerModule::BindStealthWatcher(SDK::wh::xgenaimodule::C_StealthWatcher *watcher) {
        return IsRemoteExecution() ? 0 : Get()._originalBindStealthWatcher(watcher);
    }

    // The player branch also points the one global C_PlayerStateHandler at
    // the record's actor and connects it to that actor's stance signals,
    // without dropping its connection to this machine's player. Only the
    // teardown of the cached local record ever unbinds it (TeardownRecord,
    // 0x180D472F8), so a remote body left it holding a freed actor and the
    // local player's next crouch called through it (KCDC-3P/3Q/4,
    // WHGame+0x40C935).
    void __fastcall NativePlayerModule::BindPlayerStateHandler(void *handler, SDK::wh::entitymodule::C_Actor *actor) {
        if (!IsRemoteExecution()) {
            Get()._originalBindPlayerStateHandler(handler, actor);
        }
    }

    // The player's trespass tracker (0x180875D94) keeps one label bitset for
    // the record GetLocalPlayerNPC names and pushes it into this machine's
    // C_Player::SetTrespassLevel, which drives the HUD "Trespassing" state.
    // RegisterNPC runs the area manager's first sweep (0x1803EDC90) while
    // the cache still names the remote record, so a remote body registered
    // inside a private area set this machine's trespass, and its later exit
    // never cleared it: the gate fails once the cache is restored.
    void __fastcall NativePlayerModule::TrespassAreaChanged(void *tracker, SDK::wh::xgenaimodule::C_NPC *npc, std::uint32_t areaLabel, bool entered) {
        if (!IsRemoteExecution()) {
            Get()._originalTrespassAreaChanged(tracker, npc, areaLabel, entered);
        }
    }

    // A remote player's eye must follow its own body. The movement
    // controller selects the camera using the game object's channel id and
    // the active local view (UpdateMovementState, 0x1806412AC), which a remote
    // body's channel passes: its eye, and so its aim origin
    // (UpdateLookAndAimPose) and next frame's weapon position, would be
    // this machine's camera. Its own eye is put back.
    //
    // Not while the controller is still being built: C_Actor::Init
    // (0x180AE86E0) constructs it through 0x180F2F688, whose last step runs
    // this update, and stores it at +0x180 only afterwards. GetLocalEyePos
    // reads the actor's animation state, which Init has not bound yet at
    // that point.
    void __fastcall NativePlayerModule::UpdateMovementState(SDK::wh::entitymodule::C_ActorMovementController *controller, SDK::SMovementState &state) {
        Get()._originalUpdateMovementState(controller, state);
        SDK::wh::entitymodule::C_Actor *actor = controller->GetActor();
        if (!IsRemote(actor) || actor->GetMovementController() != controller || actor->GetEntity() == nullptr) {
            return;
        }
        state.eyePosition = actor->GetEntity()->GetWorldTM().TransformPoint(actor->GetLocalEyePos());
    }

    // When a horse's rider changes, OnHorseRiderChanged (0x180A1C770)
    // registers its horse-collision handler with the physical world for a
    // player rider and removes it when a player gets off -- one global
    // client: CryPhysics' AddEventClient replaces an existing entry and
    // RemoveEventClient deletes it outright. A remote player getting off
    // would remove the handler from the local player's horse. When the
    // rider leaving is remote and the one arriving is not, the leaving
    // rider is queried as "this machine's player".
    void __fastcall NativePlayerModule::HorseRiderChanged(void *context, SDK::wh::entitymodule::I_HorseRiderSync *previous, SDK::wh::entitymodule::I_HorseRiderSync *next) {
        const bool previousRemote = previous != nullptr && IsRemote(previous->GetRiderRaw());
        const bool nextRemote     = next != nullptr && IsRemote(next->GetRiderRaw());
        if (!previousRemote || nextRemote) {
            Get()._originalHorseRiderChanged(context, previous, next);
            return;
        }
        const bool asking  = t_localPlayerQuery;
        t_localPlayerQuery = true;
        Get()._originalHorseRiderChanged(context, previous, next);
        t_localPlayerQuery = asking;
    }

    // A remote body is registered with the NPC manager as a player is, so
    // it gets an AI LOD agent like every record (C_NPCManager::
    // InitializeRecord, 0x180417DF0). When it leaves every LOD observer's
    // range the agent puts the body away (0x18040F894 -> 0x18040FCCC ->
    // RequestPutAway(true)), which on a C_Player shuts its combat
    // components down (C_CombatPlayer::ShutdownCombatComponents). Whether
    // another player's body is in the world is the server's streaming
    // decision, not this machine's AI budget: the request is refused, and
    // the answer is the state it keeps. Waking still goes through.
    bool __fastcall NativePlayerModule::RequestPutAway(SDK::wh::entitymodule::C_Actor *actor, bool putAway, bool immediate) {
        if (putAway && IsRemote(actor)) {
            return actor->IsPutAwayWanted();
        }
        return Get()._originalRequestPutAway(actor, putAway, immediate);
    }

    // A carry's phase change switches the carry_item and
    // carry_item_manipulation action filters (SetCarryInputMode,
    // 0x1810841EC) for a player-controlled carrier -- the filters of this
    // machine's one action-map manager. A remote body is player-controlled
    // by channel, so another player picking something up or putting it down
    // re-filtered the local player's keys. Its carry is its own business.
    void __fastcall NativePlayerModule::SetCarryInputMode(SDK::wh::entitymodule::C_ActorActionCarryItem *action, bool carrying) {
        if (IsRemote(action->GetActionActor()->GetActor())) {
            return;
        }
        Get()._originalSetCarryInputMode(action, carrying);
    }

    // A carry reaches this machine's one set of action sets whoever carries:
    // its update hides the put_item hint (C_ActionSets slot 5) and the end
    // of its pick-up installs the put_item callback (slot 9) -- neither
    // asks who the carrier is -- so another player's pick-up replaced the
    // local player's put-down and its exit cleared it. A remote carrier's
    // carry runs in a remote action scope.
    std::int32_t __fastcall NativePlayerModule::CarryItemUpdate(SDK::wh::entitymodule::C_ActorActionCarryItem *action, float deltaTime) {
        const RemoteActionScope scope(IsRemote(action->GetActionActor()->GetActor()));
        return Get()._originalCarryItemUpdate(action, deltaTime);
    }

    bool __fastcall NativePlayerModule::CarryItemCompletePickup(SDK::wh::entitymodule::C_ActorActionCarryItem *action) {
        const RemoteActionScope scope(IsRemote(action->GetActionActor()->GetActor()));
        return Get()._originalCarryItemCompletePickup(action);
    }

    // Every one of these switches a global action map -- "ledge" at a
    // vault's two ends (C_ActorStateMovement::Ledge, events 123 and 4),
    // "crouch" around a crouch, "no_input" around a paired animation's
    // slave side -- inside an IsPlayerControlled branch, which a remote body
    // takes by channel: another player vaulting a fence or crouching
    // re-mapped the local player's keys. The rest of each native is about
    // its own body and runs as it did; only the player branch is closed.
    std::int64_t __fastcall NativePlayerModule::BeginLedge(void *ledge, SDK::wh::entitymodule::C_Actor *actor, const void *event) {
        const RemoteActionScope scope(IsRemote(actor));
        return Get()._originalBeginLedge(ledge, actor, event);
    }

    std::int64_t __fastcall NativePlayerModule::EndLedge(void *ledge, SDK::wh::entitymodule::C_Actor *actor) {
        const RemoteActionScope scope(IsRemote(actor));
        return Get()._originalEndLedge(ledge, actor);
    }

    // No actor argument: the scope's IsPlayerControlled answer is the gate,
    // false for a remote body and unchanged for the local player's.
    void __fastcall NativePlayerModule::SetCrouchInputEnabled(void *action, bool enabled) {
        const bool asking  = t_localPlayerQuery;
        t_localPlayerQuery = true;
        Get()._originalSetCrouchInputEnabled(action, enabled);
        t_localPlayerQuery = asking;
    }

    bool __fastcall NativePlayerModule::AnimSlaveEnter(void *action, void *previousAction) {
        const bool asking  = t_localPlayerQuery;
        t_localPlayerQuery = true;
        const bool entered = Get()._originalAnimSlaveEnter(action, previousAction);
        t_localPlayerQuery = asking;
        return entered;
    }

    void __fastcall NativePlayerModule::AnimSlaveExit(void *action) {
        const bool asking  = t_localPlayerQuery;
        t_localPlayerQuery = true;
        Get()._originalAnimSlaveExit(action);
        t_localPlayerQuery = asking;
    }

    void __fastcall NativePlayerModule::SetActionCallback(SDK::wh::playermodule::I_ActionSets *sets, const SDK::CryStringT<char> &setName, const SDK::CryStringT<char> &actionName, void *callback, bool includeDerived) {
        if (!IsRemoteExecution()) {
            Get()._originalSetActionCallback(sets, setName, actionName, callback, includeDerived);
        }
    }

    void __fastcall NativePlayerModule::SubmitMaterialSwitch(void *context, std::uint32_t *switchId, std::uint32_t *stateId) {
        if (!IsRemoteExecution()) {
            Get()._originalSubmitMaterialSwitch(context, switchId, stateId);
        }
    }

    // `horse_mounting`, `horse_mounted` and `horse_dismounting` are the local
    // player's script globals. The last two are called without the rider in
    // RCX, so the ride-state callback's scope is what decides.
    void __fastcall NativePlayerModule::SetHorseMountingGlobal(void *control, bool active) {
        if (!IsRemoteExecution()) {
            Get()._originalSetHorseMountingGlobal(control, active);
        }
    }

    void __fastcall NativePlayerModule::SetHorseMountedGlobal(void *control, bool active) {
        if (!IsRemoteExecution()) {
            Get()._originalSetHorseMountedGlobal(control, active);
        }
    }

    void __fastcall NativePlayerModule::SetHorseDismountingGlobal(void *control, bool active) {
        if (!IsRemoteExecution()) {
            Get()._originalSetHorseDismountingGlobal(control, active);
        }
    }

    // Resolving the soul of any player-controlled actor -- and a remote body
    // is one, by its channel -- records it as the player's soul (0x18068A2C4
    // on a hit, 0x180AD7FBA on a new soul), and everything that means "the
    // player's soul" reads it back: the HUD's health and stamina bars first.
    // The actor spawn resolves it, and so does every later lookup through
    // this slot, so the local player's record is put back after each one
    // made for a remote body. The soul's own player flags stay as the
    // native sets them.
    SDK::wh::rpgmodule::I_Soul *__fastcall NativePlayerModule::GetOrCreateSoul(SDK::wh::rpgmodule::C_SoulList *souls, SDK::IEntity *entity) {
        if (!IsRemoteExecution() && !IsRemoteEntity(entity->GetId())) {
            return Get()._originalGetOrCreateSoul(souls, entity);
        }
        const SDK::wh::framework::WUID playerSoul = souls->GetPlayerSoulWuid();
        SDK::wh::rpgmodule::I_Soul *soul          = Get()._originalGetOrCreateSoul(souls, entity);
        souls->SetPlayerSoulWuid(playerSoul);
        return soul;
    }

    // C_Player::Update offers every player body to the battlement manager,
    // which starts the window and battlement lean at the first slot the body
    // stands at and faces. A remote player's body leans when its owner does
    // (Human::ApplyLean), never on its own: a lean started here would have
    // no way out, its exit being the player's key. 0 is the native's own
    // "no slot nearby".
    std::int32_t __fastcall NativePlayerModule::TryStartBattleArchery(void *manager, SDK::IEntity *actorEntity, std::int32_t startMode) {
        if (IsRemoteExecution()) {
            return 0;
        }
        return Get()._originalTryStartBattleArchery(manager, actorEntity, startMode);
    }

    // A shooting sub-state (Enter, Exit, its actor-model callback) settles
    // what the shot does to its body. A remote player's body takes the player
    // branch here, as on its owner's machine: movement restriction scope 15
    // only while crouched, or on a draw where the weapon's descriptor asks for
    // it (+225/+226). The AI branch would hold it for the whole action, reload
    // included, and stop the copy's legs while its owner walks. The player
    // branch also switches this machine's `no_horizontal_camera` filter,
    // which the scope keeps off for a remote body (ApplyFilterByReason).
    void __fastcall NativePlayerModule::UpdateShootingRestrictions(SDK::wh::entitymodule::C_ActorActionShootingBase *action) {
        const RemoteScope scope(IsRemoteExecution() || IsRemote(action->GetActionActor()->GetActor()));
        Get()._originalUpdateShootingRestrictions(action);
    }

    // This machine's action filters are its own player's input: a native
    // running for a remote body never switches one.
    void __fastcall NativePlayerModule::ApplyFilterByReason(SDK::IActionMapManager *manager, const char *name, bool enable, std::uint8_t reason, bool flag) {
        if (IsRemoteExecution()) {
            return;
        }
        Get()._originalApplyFilterByReason(manager, name, enable, reason, flag);
    }

    // Every body's combat controller holds this machine's `no_inventory`
    // filter (I, M, P, J, N) while its fighter is in a perfect block, a master
    // strike or one of a few other combat states (0x18061684C), and switches
    // it only when its own state at +0xF9 changes. Those changes come from the
    // fighter's update, from replayed actions and from a deferred queue, so a
    // remote body's switch on could land outside a remote scope and its
    // switch off inside one, where ApplyFilterByReason drops it: the filter
    // stayed held until the level was reloaded. The controller's owner decides
    // instead -- this machine's player switches it, a remote body never does.
    void __fastcall NativePlayerModule::SetInventoryBlocked(SDK::wh::combatmodule::C_CombatPlayerController *controller, bool blocked) {
        const RemoteScope scope(IsRemote(static_cast<SDK::wh::combatmodule::C_CombatActorObject *>(controller)->GetCombatActor()->GetOwner()));
        Get()._originalSetInventoryBlocked(controller, blocked);
    }

    // --- Installation ---------------------------------------------------------------

    bool NativePlayerModule::IsLocalPlayerSite(const void *returnAddress) {
        const auto &sites = Get()._localPlayerSites;
        return std::find(sites.begin(), sites.end(), reinterpret_cast<std::uintptr_t>(returnAddress)) != sites.end();
    }

    void NativePlayerModule::Install() {
        NativePlayerModule &module = Get();
        module._localPlayerSites   = {
            SDK::gPatterns.LocalPlayerSite__RefreshRiderMagnetismController,
            SDK::gPatterns.LocalPlayerSite__EndInteractiveAction,
            SDK::gPatterns.LocalPlayerSite__StartInteractiveActionByName,
            SDK::gPatterns.LocalPlayerSite__StartJumpAction,
            SDK::gPatterns.LocalPlayerSite__BeginCorpsePutDown,
            SDK::gPatterns.LocalPlayerSite__UnregisterDBAGroups,
            SDK::gPatterns.LocalPlayerSite__RiderMagnetismFollow,
            SDK::gPatterns.LocalPlayerSite__ScriptIsPlayer,
            SDK::gPatterns.LocalPlayerSite__CanStartChatWithSelf,
            SDK::gPatterns.LocalPlayerSite__CanStartChatWithCounterpart,
            SDK::gPatterns.LocalPlayerSite__CanStartChatWithParticipant,
            SDK::gPatterns.LocalPlayerSite__HorseScaredTutorial,
            SDK::gPatterns.LocalPlayerSite__UnconsciousWake,
            SDK::gPatterns.LocalPlayerSite__IsThirdPerson,
            SDK::gPatterns.LocalPlayerSite__SpeedFeedbackSound,
            SDK::gPatterns.LocalPlayerSite__AuxTorchLight,
            SDK::gPatterns.LocalPlayerSite__TorchLightArchetype,
            SDK::gPatterns.LocalPlayerSite__FirstPersonViewState,
            SDK::gPatterns.LocalPlayerSite__ShootingAimSway,
            SDK::gPatterns.LocalPlayerSite__StartInteractiveAction,
            SDK::gPatterns.LocalPlayerSite__CombatStealthInput,
            SDK::gPatterns.LocalPlayerSite__StoneThrowingStart,
            SDK::gPatterns.LocalPlayerSite__StoneThrowingInputPhase,
            SDK::gPatterns.LocalPlayerSite__StoneThrowingInteractionAction,
            SDK::gPatterns.LocalPlayerSite__StoneThrowingFinishInput,
            SDK::gPatterns.LocalPlayerSite__StoneThrowingFinishControl,
            SDK::gPatterns.LocalPlayerSite__MinigameBeginInteraction,
            SDK::gPatterns.LocalPlayerSite__MinigamePrepareInteraction,
            SDK::gPatterns.LocalPlayerSite__MinigameEndInteraction,
            SDK::gPatterns.LocalPlayerSite__MinigameReleasePlayerInput,
            SDK::gPatterns.LocalPlayerSite__MinigameSetPlayerInputEnabled,
            SDK::gPatterns.LocalPlayerSite__MinigameClearActionMap,
            SDK::gPatterns.LocalPlayerSite__MinigameSetActionMap,
            SDK::gPatterns.LocalPlayerSite__MinigameSetInteractionTarget,
            SDK::gPatterns.LocalPlayerSite__MinigameClearInteractionTarget,
            SDK::gPatterns.LocalPlayerSite__BattleArcheryStart,
            SDK::gPatterns.LocalPlayerSite__BattleArcheryControls,
            SDK::gPatterns.LocalPlayerSite__BattleArcheryFinish,
            SDK::gPatterns.LocalPlayerSite__MovementAudioSuffix,
            SDK::gPatterns.LocalPlayerSite__MaterialMovementAudio,
            SDK::gPatterns.LocalPlayerSite__BodyfallAudio,
            SDK::gPatterns.LocalPlayerSite__CombatTimewarp,
            SDK::gPatterns.LocalPlayerSite__CombatTimewarpFx,
            SDK::gPatterns.LocalPlayerSite__HorseMovementAudio,
            // Seen from outside, a remote body walks drunk, injured or armored
            // as an NPC does: its condition controller is set up in its Init.
            SDK::gPatterns.LocalPlayerSite__ActorConditionControllerInit,
        };
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_Player__Init), reinterpret_cast<LPVOID>(&PlayerInit), reinterpret_cast<LPVOID *>(&module._originalPlayerInit));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_Player__Release), reinterpret_cast<LPVOID>(&PlayerRelease), reinterpret_cast<LPVOID *>(&module._originalPlayerRelease));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_Player__Update), reinterpret_cast<LPVOID>(&PlayerUpdate), reinterpret_cast<LPVOID *>(&module._originalPlayerUpdate));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_Player__PostUpdate), reinterpret_cast<LPVOID>(&PlayerPostUpdate), reinterpret_cast<LPVOID *>(&module._originalPlayerPostUpdate));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.CProceduralContextLook__Update), reinterpret_cast<LPVOID>(&LookContextUpdate), reinterpret_cast<LPVOID *>(&module._originalLookContextUpdate));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_Player__ProcessEvent), reinterpret_cast<LPVOID>(&PlayerProcessEvent), reinterpret_cast<LPVOID *>(&module._originalPlayerProcessEvent));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_Player__OnEntityRemoved), reinterpret_cast<LPVOID>(&PlayerEntityRemoved), reinterpret_cast<LPVOID *>(&module._originalPlayerEntityRemoved));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_Player__HandleDeath), reinterpret_cast<LPVOID>(&PlayerHandleDeath), reinterpret_cast<LPVOID *>(&module._originalPlayerHandleDeath));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_Player__InitLocalPlayer), reinterpret_cast<LPVOID>(&PlayerInitLocalPlayer), reinterpret_cast<LPVOID *>(&module._originalPlayerInitLocalPlayer));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_Player__OnPostUpdate), reinterpret_cast<LPVOID>(&PlayerOnPostUpdate), reinterpret_cast<LPVOID *>(&module._originalPlayerOnPostUpdate));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_Player__NotifyActorActionRequest), reinterpret_cast<LPVOID>(&PlayerNotifyActorActionRequest), reinterpret_cast<LPVOID *>(&module._originalPlayerNotifyActorActionRequest));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_Player__UpdateMovementAudio), reinterpret_cast<LPVOID>(&PlayerUpdateMovementAudio), reinterpret_cast<LPVOID *>(&module._originalPlayerUpdateMovementAudio));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_Player__IsGod), reinterpret_cast<LPVOID>(&PlayerIsGod), reinterpret_cast<LPVOID *>(&module._originalPlayerIsGod));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_Player__UseItem), reinterpret_cast<LPVOID>(&PlayerUseItem), reinterpret_cast<LPVOID *>(&module._originalPlayerUseItem));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_Player__OnItemEquipped), reinterpret_cast<LPVOID>(&PlayerItemEquipped), reinterpret_cast<LPVOID *>(&module._originalPlayerItemEquipped));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_Player__OnItemUnequipped), reinterpret_cast<LPVOID>(&PlayerItemUnequipped), reinterpret_cast<LPVOID *>(&module._originalPlayerItemUnequipped));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_Player__RefreshHelmetActionHint), reinterpret_cast<LPVOID>(&PlayerRefreshHelmetActionHint), reinterpret_cast<LPVOID *>(&module._originalPlayerRefreshHelmetActionHint));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_Player__RefreshHeadLight), reinterpret_cast<LPVOID>(&PlayerRefreshHeadLight), reinterpret_cast<LPVOID *>(&module._originalPlayerRefreshHeadLight));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_Player__RegisterLocalInputActions), reinterpret_cast<LPVOID>(&PlayerRegisterActions), reinterpret_cast<LPVOID *>(&module._originalPlayerRegisterActions));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_Player__UnregisterLocalInputActions), reinterpret_cast<LPVOID>(&PlayerUnregisterActions), reinterpret_cast<LPVOID *>(&module._originalPlayerUnregisterActions));

        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_PlayerInput__RegisterLocalInputActions), reinterpret_cast<LPVOID>(&InputRegisterActions), reinterpret_cast<LPVOID *>(&module._originalInputRegisterActions));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_PlayerInput__OnAction), reinterpret_cast<LPVOID>(&InputOnAction), reinterpret_cast<LPVOID *>(&module._originalInputOnAction));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_PlayerInput__PreUpdate), reinterpret_cast<LPVOID>(&InputPreUpdate), reinterpret_cast<LPVOID *>(&module._originalInputPreUpdate));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_PlayerInput__Destructor), reinterpret_cast<LPVOID>(&InputDestructor), reinterpret_cast<LPVOID *>(&module._originalInputDestructor));

        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_ChatFollowManager__Update), reinterpret_cast<LPVOID>(&ChatFollowUpdate), reinterpret_cast<LPVOID *>(&module._originalChatFollowUpdate));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_DogInteractionManager__UpdateDogHudAvailability), reinterpret_cast<LPVOID>(&DogHudAvailability), reinterpret_cast<LPVOID *>(&module._originalDogHudAvailability));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_MissileWeaponPlayerController__RegisterInputActions), reinterpret_cast<LPVOID>(&MissileRegisterActions), reinterpret_cast<LPVOID *>(&module._originalMissileRegisterActions));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_MissileWeaponPlayerController__RefreshMissileActionMap), reinterpret_cast<LPVOID>(&MissileRefreshActionMap), reinterpret_cast<LPVOID *>(&module._originalMissileRefreshActionMap));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_RiderPlayerControl__RegisterMountedHintCallbacks), reinterpret_cast<LPVOID>(&RiderRegisterMountedHints), reinterpret_cast<LPVOID *>(&module._originalRiderRegisterMountedHints));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_RiderPlayerControl__UnregisterMountedHintCallbacks), reinterpret_cast<LPVOID>(&RiderUnregisterMountedHints), reinterpret_cast<LPVOID *>(&module._originalRiderUnregisterMountedHints));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_RiderPlayerControl__UpdateMountedHintBindings), reinterpret_cast<LPVOID>(&RiderUpdateMountedHints), reinterpret_cast<LPVOID *>(&module._originalRiderUpdateMountedHints));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_RiderPlayerControl__OnHorseRideStateChanged), reinterpret_cast<LPVOID>(&RiderStateChanged), reinterpret_cast<LPVOID *>(&module._originalRiderStateChanged));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_RiderPlayerInput__OnHorseRideStateChanged), reinterpret_cast<LPVOID>(&RiderInputStateChanged), reinterpret_cast<LPVOID *>(&module._originalRiderInputStateChanged));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_Actor__SubmitCharacterMoveRequest), reinterpret_cast<LPVOID>(&SubmitCharacterMoveRequest), reinterpret_cast<LPVOID *>(&module._originalSubmitCharacterMoveRequest));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_CombatPlayerInput__FilterMovementRequest), reinterpret_cast<LPVOID>(&CombatFilterMovementRequest), reinterpret_cast<LPVOID *>(&module._originalCombatFilterMovementRequest));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_CombatModule__GetLocalPlayerCameraManager), reinterpret_cast<LPVOID>(&GetLocalPlayerCameraManager), reinterpret_cast<LPVOID *>(&module._originalGetLocalPlayerCameraManager));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_CombatAlignmentManager__ProcessClipEvents), reinterpret_cast<LPVOID>(&CombatAlignmentProcessClipEvents), reinterpret_cast<LPVOID *>(&module._originalCombatAlignmentProcessClipEvents));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_CombatAlignmentManager__UpdateSolverTargets), reinterpret_cast<LPVOID>(&CombatAlignmentUpdateSolverTargets), reinterpret_cast<LPVOID *>(&module._originalCombatAlignmentUpdateSolverTargets));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_AlignmentManager__SetPositionSolver), reinterpret_cast<LPVOID>(&AlignmentSetPositionSolver), reinterpret_cast<LPVOID *>(&module._originalAlignmentSetPositionSolver));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_AlignmentManager__SetRotationSolver), reinterpret_cast<LPVOID>(&AlignmentSetRotationSolver), reinterpret_cast<LPVOID *>(&module._originalAlignmentSetRotationSolver));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_CombatProceduralClipAnimateCamera__IsOwnerPlayerControlled), reinterpret_cast<LPVOID>(&CameraClipIsOwnerPlayerControlled), reinterpret_cast<LPVOID *>(&module._originalCameraClipIsOwnerPlayerControlled));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_ProceduralClipAnimateCamera__OnEnter), reinterpret_cast<LPVOID>(&AnimateCameraClipEnter), reinterpret_cast<LPVOID *>(&module._originalAnimateCameraClipEnter));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_ProceduralClipAnimateCamera__OnExit), reinterpret_cast<LPVOID>(&AnimateCameraClipExit), reinterpret_cast<LPVOID *>(&module._originalAnimateCameraClipExit));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.CProceduralClipCameraNear__OnEnter), reinterpret_cast<LPVOID>(&CameraNearClipEnter), reinterpret_cast<LPVOID *>(&module._originalCameraNearClipEnter));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.CProceduralClipCameraNear__OnExit), reinterpret_cast<LPVOID>(&CameraNearClipExit), reinterpret_cast<LPVOID *>(&module._originalCameraNearClipExit));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_ProceduralClipCameraBoneAnimated__OnEnter), reinterpret_cast<LPVOID>(&BoneCameraClipEnter), reinterpret_cast<LPVOID *>(&module._originalBoneCameraClipEnter));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_ProceduralClipCameraBoneAnimated__OnExit), reinterpret_cast<LPVOID>(&BoneCameraClipExit), reinterpret_cast<LPVOID *>(&module._originalBoneCameraClipExit));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_ProceduralClipCameraBoneAnimated__Update), reinterpret_cast<LPVOID>(&BoneCameraClipUpdate), reinterpret_cast<LPVOID *>(&module._originalBoneCameraClipUpdate));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_CombatActorMercyKill__RequestMercyKill), reinterpret_cast<LPVOID>(&MercyKillRequest), reinterpret_cast<LPVOID *>(&module._originalMercyKillRequest));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_CombatActorMercyKill__OnMercyKillActionFinished), reinterpret_cast<LPVOID>(&MercyKillFinished), reinterpret_cast<LPVOID *>(&module._originalMercyKillFinished));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.CreateCombatPlayer), reinterpret_cast<LPVOID>(&CreateCombatPlayer), reinterpret_cast<LPVOID *>(&module._originalCreateCombatPlayer));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_CombatPlayer__Init), reinterpret_cast<LPVOID>(&CombatPlayerInit), reinterpret_cast<LPVOID *>(&module._originalCombatPlayerInit));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_CombatPlayer__ShutdownCombatComponents), reinterpret_cast<LPVOID>(&CombatPlayerShutdown), reinterpret_cast<LPVOID *>(&module._originalCombatPlayerShutdown));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_CombatModule__DestroyCombatActor), reinterpret_cast<LPVOID>(&DestroyCombatActor), reinterpret_cast<LPVOID *>(&module._originalDestroyCombatActor));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_ExhaustedBuffInstance__TriggerPlayerExhaustionEffect), reinterpret_cast<LPVOID>(&ExhaustionEffect), reinterpret_cast<LPVOID *>(&module._originalExhaustionEffect));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_StarvationBuffInstance__TriggerHungerEffect), reinterpret_cast<LPVOID>(&HungerEffect), reinterpret_cast<LPVOID *>(&module._originalHungerEffect));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_Player__SetFeedbackSound), reinterpret_cast<LPVOID>(&SetFeedbackSound), reinterpret_cast<LPVOID *>(&module._originalSetFeedbackSound));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.ExecuteEntityAudioTrigger), reinterpret_cast<LPVOID>(&ExecuteEntityAudioTrigger), reinterpret_cast<LPVOID *>(&module._originalExecuteEntityAudioTrigger));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_SoulContext__UpdateCombatDanger), reinterpret_cast<LPVOID>(&UpdateCombatDanger), reinterpret_cast<LPVOID *>(&module._originalUpdateCombatDanger));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_SoulBuffInstance__StartActorEffect), reinterpret_cast<LPVOID>(&BuffStartActorEffect), reinterpret_cast<LPVOID *>(&module._originalBuffStartActorEffect));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_Actor__Fall), reinterpret_cast<LPVOID>(&ActorFall), reinterpret_cast<LPVOID *>(&module._originalActorFall));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_Player__StopAnimations), reinterpret_cast<LPVOID>(&StopAnimations), reinterpret_cast<LPVOID *>(&module._originalStopAnimations));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_TutorialManager__ShowTutorial), reinterpret_cast<LPVOID>(&ShowTutorial), reinterpret_cast<LPVOID *>(&module._originalShowTutorial));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_Soul__UpdatePlayerMaterialAudio), reinterpret_cast<LPVOID>(&SoulPlayerMaterialAudio), reinterpret_cast<LPVOID *>(&module._originalSoulPlayerMaterialAudio));

        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.WHGlobalEnvironment__GetInstance), reinterpret_cast<LPVOID>(&GetGlobalEnvironment), reinterpret_cast<LPVOID *>(&s_originalGetGlobalEnvironment));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_CameraManager__Update), reinterpret_cast<LPVOID>(&CameraUpdate), reinterpret_cast<LPVOID *>(&module._originalCameraUpdate));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.CScreenEffects__PostUpdate), reinterpret_cast<LPVOID>(&ScreenEffectsPostUpdate), reinterpret_cast<LPVOID *>(&module._originalScreenEffectsPostUpdate));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_PlayerProfileWHManager__SetSaveLock), reinterpret_cast<LPVOID>(&SetSaveLock), reinterpret_cast<LPVOID *>(&module._originalSetSaveLock));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.I_ActionSets__SetActionEnabled), reinterpret_cast<LPVOID>(&SetActionEnabled), reinterpret_cast<LPVOID *>(&module._originalSetActionEnabled));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.I_ActionSets__ClearActionCallbacks), reinterpret_cast<LPVOID>(&ClearActionCallbacks), reinterpret_cast<LPVOID *>(&module._originalClearActionCallbacks));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_NPCManager__RegisterNPC), reinterpret_cast<LPVOID>(&RegisterNPC), reinterpret_cast<LPVOID *>(&module._originalRegisterNPC));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.RebindLocalPlayerScriptVariables), reinterpret_cast<LPVOID>(&RebindScriptVariables), reinterpret_cast<LPVOID *>(&module._originalRebindScriptVariables));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_StealthWatcher__BindLocalPlayer), reinterpret_cast<LPVOID>(&BindStealthWatcher), reinterpret_cast<LPVOID *>(&module._originalBindStealthWatcher));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_PlayerStateHandler__BindPlayer), reinterpret_cast<LPVOID>(&BindPlayerStateHandler), reinterpret_cast<LPVOID *>(&module._originalBindPlayerStateHandler));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.PlayerTrespass__OnAreaChanged), reinterpret_cast<LPVOID>(&TrespassAreaChanged), reinterpret_cast<LPVOID *>(&module._originalTrespassAreaChanged));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.SubmitPlayerMaterialAudioSwitch), reinterpret_cast<LPVOID>(&SubmitMaterialSwitch), reinterpret_cast<LPVOID *>(&module._originalSubmitMaterialSwitch));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_ActorMovementController__UpdateMovementState), reinterpret_cast<LPVOID>(&UpdateMovementState), reinterpret_cast<LPVOID *>(&module._originalUpdateMovementState));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.OnHorseRiderChanged), reinterpret_cast<LPVOID>(&HorseRiderChanged), reinterpret_cast<LPVOID *>(&module._originalHorseRiderChanged));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_Actor__RequestPutAway), reinterpret_cast<LPVOID>(&RequestPutAway), reinterpret_cast<LPVOID *>(&module._originalRequestPutAway));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_ActorActionCarryItem__SetCarryInputMode), reinterpret_cast<LPVOID>(&SetCarryInputMode), reinterpret_cast<LPVOID *>(&module._originalSetCarryInputMode));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_ActorActionCarryItem__Update), reinterpret_cast<LPVOID>(&CarryItemUpdate), reinterpret_cast<LPVOID *>(&module._originalCarryItemUpdate));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_ActorActionCarryItem__CompleteItemPickup), reinterpret_cast<LPVOID>(&CarryItemCompletePickup), reinterpret_cast<LPVOID *>(&module._originalCarryItemCompletePickup));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_ActorStateMovement__BeginLedge), reinterpret_cast<LPVOID>(&BeginLedge), reinterpret_cast<LPVOID *>(&module._originalBeginLedge));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_ActorStateMovement__EndLedge), reinterpret_cast<LPVOID>(&EndLedge), reinterpret_cast<LPVOID *>(&module._originalEndLedge));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_ActorActionCrouch__SetPlayerCrouchInputEnabled), reinterpret_cast<LPVOID>(&SetCrouchInputEnabled), reinterpret_cast<LPVOID *>(&module._originalSetCrouchInputEnabled));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.ActorActionAnimSlave__Enter), reinterpret_cast<LPVOID>(&AnimSlaveEnter), reinterpret_cast<LPVOID *>(&module._originalAnimSlaveEnter));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.ActorActionAnimSlave__Exit), reinterpret_cast<LPVOID>(&AnimSlaveExit), reinterpret_cast<LPVOID *>(&module._originalAnimSlaveExit));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.I_ActionSets__SetActionCallback), reinterpret_cast<LPVOID>(&SetActionCallback), reinterpret_cast<LPVOID *>(&module._originalSetActionCallback));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_RiderPlayerControl__SetHorseMountingGlobal), reinterpret_cast<LPVOID>(&SetHorseMountingGlobal), reinterpret_cast<LPVOID *>(&module._originalSetHorseMountingGlobal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_RiderPlayerControl__SetHorseMountedGlobal), reinterpret_cast<LPVOID>(&SetHorseMountedGlobal), reinterpret_cast<LPVOID *>(&module._originalSetHorseMountedGlobal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_RiderPlayerControl__SetHorseDismountingGlobal), reinterpret_cast<LPVOID>(&SetHorseDismountingGlobal), reinterpret_cast<LPVOID *>(&module._originalSetHorseDismountingGlobal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_SoulList__GetOrCreateSoul), reinterpret_cast<LPVOID>(&GetOrCreateSoul), reinterpret_cast<LPVOID *>(&module._originalGetOrCreateSoul));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_BattlementManager__TryStartBattleArchery), reinterpret_cast<LPVOID>(&TryStartBattleArchery), reinterpret_cast<LPVOID *>(&module._originalTryStartBattleArchery));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_ActorActionShootingBase__UpdateShootingRestrictions), reinterpret_cast<LPVOID>(&UpdateShootingRestrictions), reinterpret_cast<LPVOID *>(&module._originalUpdateShootingRestrictions));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.CActionMapManager__ApplyFilterByReason), reinterpret_cast<LPVOID>(&ApplyFilterByReason), reinterpret_cast<LPVOID *>(&module._originalApplyFilterByReason));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_CombatPlayerController__SetInventoryBlocked), reinterpret_cast<LPVOID>(&SetInventoryBlocked), reinterpret_cast<LPVOID *>(&module._originalSetInventoryBlocked));
    }
} // namespace KCDC::Features::Player

static InitFunction init(
    []() {
        KCDC::Features::Player::NativePlayerModule::Install();
    },
    "NativePlayer");
