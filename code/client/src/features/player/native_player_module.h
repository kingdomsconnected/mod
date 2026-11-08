#pragma once

#include "sdk/cryengine/action/i_action_map_manager.h"
#include "sdk/cryengine/action/i_animated_character.h"
#include "sdk/cryengine/animation/i_procedural_clip.h"
#include "sdk/cryengine/action/i_game_framework_listener.h"
#include "sdk/cryengine/action/i_game_object.h"
#include "sdk/cryengine/action/i_procedural_context.h"
#include "sdk/cryengine/entitysystem/i_entity.h"
#include "sdk/cryengine/entitysystem/s_entity_event.h"
#include "sdk/cryengine/system/cry_name.h"
#include "sdk/types.h"
#include "sdk/wh/c_wh_global_environment.h"
#include "sdk/wh/combatmodule/c_combat_actor.h"
#include "sdk/wh/rpgmodule/c_buff_instance.h"
#include "sdk/wh/combatmodule/c_combat_alignment_manager.h"
#include "sdk/wh/combatmodule/c_combat_module.h"
#include "sdk/wh/combatmodule/c_combat_camera_manager.h"
#include "sdk/wh/combatmodule/c_combat_player.h"
#include "sdk/wh/combatmodule/c_combat_player_input.h"
#include "sdk/wh/entitymodule/c_actor.h"
#include "sdk/wh/entitymodule/c_actor_action_carry_item.h"
#include "sdk/wh/entitymodule/c_actor_action_shooting_base.h"
#include "sdk/wh/entitymodule/c_actor_movement_controller.h"
#include "sdk/wh/entitymodule/c_alignment_manager.h"
#include "sdk/wh/entitymodule/c_chat_follow_manager.h"
#include "sdk/wh/entitymodule/c_dog_interaction_manager.h"
#include "sdk/wh/entitymodule/c_horse.h"
#include "sdk/wh/entitymodule/c_item.h"
#include "sdk/wh/entitymodule/c_missile_weapon_player_controller.h"
#include "sdk/wh/entitymodule/c_player.h"
#include "sdk/wh/entitymodule/c_player_input.h"
#include "sdk/wh/entitymodule/c_rider_player_control.h"
#include "sdk/wh/entitymodule/i_alignment_manager.h"
#include "sdk/wh/entitymodule/i_equipment_manager_listener.h"
#include "sdk/wh/entitymodule/i_horse_rider_sync.h"
#include "sdk/wh/framework/wuid.h"
#include "sdk/wh/game/c_camera_manager.h"
#include "sdk/wh/playermodule/i_action_sets.h"
#include "sdk/wh/rpgmodule/c_soul.h"
#include "sdk/wh/rpgmodule/c_soul_list.h"
#include "sdk/wh/xgenaimodule/c_npc_manager.h"
#include "sdk/wh/xgenaimodule/c_stealth_watcher.h"

#include <function2/function2.hpp>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace KCDC::Features::Player {
    /**
     * Keeps every remote player's native `C_Player` from acting as this
     * machine's player.
     *
     * KCD2's `C_Player` assumes it is the one local player: its overrides and
     * its update reach the camera, HUD status, input action maps, save locks,
     * script globals, the NPC manager's player cache, the combat module's
     * local player and the global audio object. A remote player is a real
     * `C_Player` all the same, the way CryEngine's own multiplayer spawns one
     * per channel, so each of those reaches is contained here at the native
     * function that makes it.
     *
     * A body is remote when its game object was given a channel id at or
     * above `kRemoteChannelBase` -- the engine's own "who drives this actor"
     * field, set by the actor factory before `C_Player::Init`. That is read
     * once, at Init, while the object is whole, and the body is recorded in
     * lock-free sets by its address and its entity id; `Release` retires
     * both after the native teardown -- which the engine's deleter runs even
     * for a body whose Init failed -- before the address can be handed out
     * again. Asking never touches the body, which matters: `Release` calls
     * some of the player's hooks with a receiver it never loaded, and other
     * threads ask too.
     *
     * A hook whose native has the player at hand decides by it; the
     * thread-local remote scope is only for natives with no player argument,
     * reached from inside a remote player's own native work.
     */
    class NativePlayerModule final {
      public:
        static NativePlayerModule &Get();

        /** Creates every containment hook. Called once from the module's `InitFunction`. */
        static void Install();

        /**
         * The first channel id given to a remote player. CryNetwork numbers
         * real channels from 1, and KCD2's local player is on its own client
         * channel, so the upper half is free for bodies with no engine
         * channel behind them.
         */
        static constexpr SDK::ChannelId kRemoteChannelBase = 0x8000;

        /** Whether `actor` is a remote player's body. Any thread, never reads the body; null is not remote. */
        static bool IsRemote(const SDK::wh::entitymodule::C_Actor *actor);

        /** Whether `entityId` is a remote player's body. Any thread. */
        static bool IsRemoteEntity(SDK::EntityId entityId);

        /** Whether this thread is inside a remote player's native work. */
        static bool IsRemoteExecution() {
            return t_remoteExecution;
        }

        /**
         * Marks this thread as running a remote player's native work, for the
         * hooks whose native has no player argument. Nested scopes restore
         * the caller's answer.
         */
        class RemoteScope final {
          public:
            explicit RemoteScope(bool remote): _previous(t_remoteExecution) {
                t_remoteExecution = remote;
            }
            ~RemoteScope() {
                t_remoteExecution = _previous;
            }
            RemoteScope(const RemoteScope &)            = delete;
            RemoteScope &operator=(const RemoteScope &) = delete;

          private:
            bool _previous;
        };

        /**
         * Runs a native for a remote player's body as the engine runs it for
         * anybody else's: `C_Actor::IsPlayerControlled` answers false for a
         * remote body, so the native takes its non-player branch, and the
         * global action-set writes it makes regardless are dropped. A body
         * of this machine's player is untouched by either. Nested scopes
         * restore the caller's answers.
         */
        class RemoteActionScope final {
          public:
            explicit RemoteActionScope(bool remote): _remote(remote), _previousQuery(t_localPlayerQuery) {
                t_localPlayerQuery = true;
            }
            ~RemoteActionScope() {
                t_localPlayerQuery = _previousQuery;
            }
            RemoteActionScope(const RemoteActionScope &)            = delete;
            RemoteActionScope &operator=(const RemoteActionScope &) = delete;

          private:
            RemoteScope _remote;
            bool _previousQuery;
        };

        /** A channel id no actor is using, from the remote range, or 0 when it is exhausted. Game thread. */
        SDK::ChannelId AllocateChannelId();

        /**
         * Told the entity id of every remote player body as its native
         * `C_Player::Release` runs, so holders of the pointer drop it at the
         * one moment the engine frees it. Game thread.
         */
        void SetRemoteReleasedListener(fu2::function<void(SDK::EntityId) const> listener) {
            _remoteReleased = std::move(listener);
        }

        /**
         * Whether `actor` is the body whose remote fighter
         * `C_CombatModule::DestroyCombatActor` is destroying on this thread.
         * That native takes a player-controlled owner for the local player's
         * fighter going away; `C_Actor::IsPlayerControlled` answers false for
         * exactly this body while it runs.
         */
        static bool IsDestroyingRemoteFighter(const SDK::wh::entitymodule::C_Actor *actor) {
            return actor != nullptr && actor == t_destroyingRemoteFighter;
        }

        /**
         * This frame's horizontal position correction for a remote body, in
         * world metres.
         * Taken once by the body's next native movement submission and handed
         * to its animated character as forced movement (see
         * `SubmitCharacterMoveRequest`); a later call replaces an untaken
         * one. Zero withdraws it. Game thread.
         */
        static void SetMovementCorrection(const SDK::wh::entitymodule::C_Actor *actor, float x, float y);

        /** Stops stating `mount`'s gait; before its actor goes, or once no remote rider carries it. Game thread. */
        static void ForgetMount(const SDK::wh::entitymodule::C_Actor *mount);

        /**
         * The gait the owner's horse was commanded, for a mount a remote
         * player's body rides: what `SubmitHorseMoveCommand` states on the
         * copy in place of the one its rider copy's replayed keys build.
         * Enrols the mount on first use. Game thread.
         */
        static void SetMountGait(const SDK::wh::entitymodule::C_Actor *mount, std::int32_t gaitStyleId);

        /** The gait stored for `mount`, or false when it carries none. Any thread. */
        static bool MountGait(const SDK::wh::entitymodule::C_Actor *mount, std::int32_t &gaitStyleId);

        /**
         * The locomotion a copy of `mount` is held to: the pace its owner's
         * horse plays, which `ApplyRuntimePaceAndDelta` is made to put on
         * the copy's request in place of the one it would derive from the
         * copy's speed, and the runtime speed the copy was seeded, which
         * `UpdateHorseMovementSpeed` is made to leave as seeded rather than
         * accelerate toward the commanded gait. Enrols the mount on first
         * use. Game thread.
         */
        static void SetMountLocomotion(const SDK::wh::entitymodule::C_Actor *mount, float pace, float speed);

        /** Lets the copy's own runtime decide its pace and speed again, as during a held pose. Game thread. */
        static void ClearMountLocomotion(const SDK::wh::entitymodule::C_Actor *mount);

        /** The pace held for `mount`, or false when its runtime decides. Any thread. */
        static bool MountPace(const SDK::wh::entitymodule::C_Actor *mount, float &pace);

        /** The speed held for `mount`, or false when its runtime decides. Any thread. */
        static bool MountSpeed(const SDK::wh::entitymodule::C_Actor *mount, float &speed);

        /**
         * What a remote body's combat camera holds: the body's facing yaw and
         * its owner's look pitch, radians. The combat input listener seeds a
         * fighter's turn from that camera and in its free mode turns the body
         * to it outright; on the owner's machine the camera is the owner's
         * own, whose yaw the owner's body then has. Game thread.
         */
        static void SetCombatLook(const SDK::wh::entitymodule::C_Actor *actor, float yaw, float pitch);

        /**
         * Whether a remote body holds its owner's head look where none of its
         * own looking clips asks for it -- seated, or leaning out: its look
         * context is then told one does as it updates (LookContextUpdate).
         * Game thread.
         */
        static void SetLookHeld(SDK::EntityId entityId, bool held);

        /**
         * Whether a dodge steered a remote body's movement through the combat
         * input listener on its last input pass: the dodge moves the body by
         * its own direction, not by the axes. Any other move the listener
         * hands on is the axes as the body's request takes them.
         */
        static bool WasMovedByCombatInput(const SDK::wh::entitymodule::C_Actor *actor);

        /**
         * Whether `returnAddress` is one of the `C_Actor::IsPlayerControlled`
         * calls the engine makes meaning "is this THIS machine's player",
         * where a remote player's body must answer no.
         *
         * The engine had one player, so the test that a channel drives a body
         * doubles as "this is the player at the keyboard". Most of its ~330
         * callers are about how a player body simulates and are right for any
         * player, but these drive this machine alone: the save locks, the
         * player-control toggle of an interaction, tutorials, action sets,
         * the camera mode, Lua's IsPlayer, NPC chat, the player's torch and
         * feedback sounds. Asked per call site rather than per native,
         * because several of those natives also ask the question for the
         * body's own simulation (UpdateUnconsciousness, the shooting action).
         * IsClient (vtable +0x278) is a thunk to the same native, so its callers
         * arrive here too.
         */
        static bool IsLocalPlayerSite(const void *returnAddress);

        /**
         * Whether this thread is inside a native that asks
         * `C_Actor::IsPlayerControlled` meaning "is this THIS machine's
         * player" -- the combat camera clip's gate, a mercy kill's camera and
         * HUD -- where a remote player's body must answer no.
         */
        static bool IsAskingForLocalPlayer() {
            return t_localPlayerQuery;
        }

      private:
        static thread_local bool t_remoteExecution;
        static thread_local const SDK::wh::entitymodule::C_Actor *t_destroyingRemoteFighter;
        static thread_local SDK::wh::combatmodule::C_CombatCameraManager *t_remoteCombatCamera;
        static thread_local bool t_combatAlignmentPass;
        static thread_local bool t_remoteFighterAlignmentPass;
        static thread_local bool t_localPlayerQuery;

        SDK::ChannelId _nextChannelId = kRemoteChannelBase;
        fu2::function<void(SDK::EntityId) const> _remoteReleased;

        /**
         * A set of non-zero keys: open addressing, read lock-free from any
         * thread, written on the game thread only. A retired slot keeps later
         * entries of its probe chain reachable and is reused by the next
         * insertion that passes it.
         */
        class RemoteKeySet final {
          public:
            bool Contains(std::uint64_t key) const;
            void Insert(std::uint64_t key);
            void Retire(std::uint64_t key);

            // Values carried beside each key, zero on insertion: stored on
            // any thread, taken -- read and cleared -- or read on any.
            static constexpr std::size_t kValueCount = 4;
            void Store(std::uint64_t key, std::size_t channel, std::uint64_t value);
            std::uint64_t Take(std::uint64_t key, std::size_t channel);
            std::uint64_t Read(std::uint64_t key, std::size_t channel) const;

          private:
            static constexpr std::size_t kSize        = 1024;
            static constexpr std::uint64_t kRetired   = ~0ULL;
            static std::size_t Slot(std::uint64_t key) {
                return static_cast<std::size_t>((key * 0x9E3779B97F4A7C15ULL) >> 54) & (kSize - 1);
            }

            std::array<std::atomic<std::uint64_t>, kSize> _keys {};
            std::array<std::array<std::atomic<std::uint64_t>, kValueCount>, kSize> _values {};
            std::atomic<std::uint32_t> _count {0};

            // The slot holding `key`, or kSize.
            std::size_t Find(std::uint64_t key) const;
        };

        static std::uint64_t BodyKey(const SDK::wh::entitymodule::C_Actor *actor) {
            return reinterpret_cast<std::uintptr_t>(actor);
        }

        // By C_Actor address. The values: the pending position step (two
        // floats), the combat camera's facing (yaw and pitch floats), and
        // whether a dodge moved the body through the combat input listener on
        // its last pass.
        static constexpr std::size_t kValueStep          = 0;
        static constexpr std::size_t kValueCombatLook    = 1;
        static constexpr std::size_t kValueCombatHandled = 2;
        static constexpr std::size_t kValueMountGait     = 3; // the owner's gait style id + 2, so 0 is none
        RemoteKeySet _remoteBodies;
        RemoteKeySet _remoteEntities; // by EntityId, for natives that only have the entity; kValueLookHeld
        static constexpr std::size_t kValueLookHeld = 0;
        // By C_Actor address. The values: the pace and the runtime speed a
        // copy is held to, each the float's bits with bit 32 set so that
        // zero is none, and the owner's commanded gait.
        static constexpr std::size_t kValueMountPace  = 0;
        static constexpr std::size_t kValueMountSpeed = 1;
        static constexpr std::uint64_t kMountValueSet = 1ULL << 32;
        RemoteKeySet _remoteMounts;

        // Return addresses of the calls IsLocalPlayerSite answers for, from
        // the LocalPlayerSite__ patterns.
        std::array<std::uintptr_t, 45> _localPlayerSites {};

        // --- The player's own lifecycle and update ---------------------------

        static bool __fastcall PlayerInit(SDK::wh::entitymodule::C_Player *player, SDK::IGameObject *gameObject);
        static void __fastcall PlayerRelease(SDK::wh::entitymodule::C_Player *player);
        static void __fastcall PlayerUpdate(SDK::wh::entitymodule::C_Player *player, void *context, int slot);
        static void __fastcall PlayerPostUpdate(SDK::wh::entitymodule::C_Player *player, float frameTime);
        static void __fastcall PlayerProcessEvent(SDK::wh::entitymodule::C_Player *player, SDK::SEntityEvent *event);
        static void __fastcall PlayerEntityRemoved(SDK::wh::entitymodule::C_Player *player);
        static void __fastcall PlayerHandleDeath(SDK::wh::entitymodule::C_Player *player, bool dead);
        static void __fastcall PlayerInitLocalPlayer(SDK::wh::entitymodule::C_Player *player);
        static void __fastcall PlayerOnPostUpdate(SDK::IGameFrameworkListener *listener, float frameTime);
        static void __fastcall PlayerNotifyActorActionRequest(SDK::wh::entitymodule::C_Player *player, const void *request);
        static void __fastcall PlayerUpdateMovementAudio(SDK::wh::entitymodule::C_Player *player, std::uintptr_t a2, float frameTime, const char *material, std::uint32_t a5);
        static int __fastcall PlayerIsGod(SDK::wh::entitymodule::C_Player *player);
        static std::uint32_t *__fastcall PlayerUseItem(SDK::wh::entitymodule::C_Player *player, std::uint32_t *result, SDK::wh::entitymodule::C_Item *item, std::int32_t amount, std::int32_t context, bool checkFood, std::int32_t saveSlot);
        static void __fastcall PlayerItemEquipped(SDK::wh::entitymodule::I_EquipmentManagerListener *listener, void *inventoryContext, SDK::wh::framework::WUID itemId);
        static void __fastcall PlayerItemUnequipped(SDK::wh::entitymodule::I_EquipmentManagerListener *listener, void *inventoryContext, SDK::wh::framework::WUID itemId);
        static void __fastcall PlayerRefreshHelmetActionHint(SDK::wh::entitymodule::C_Player *player);
        static void __fastcall PlayerRefreshHeadLight(SDK::wh::entitymodule::C_Player *player);
        static void __fastcall PlayerRegisterActions(SDK::wh::entitymodule::C_Player *player);
        static void __fastcall PlayerUnregisterActions(SDK::wh::entitymodule::C_Player *player);
        static void __fastcall LookContextUpdate(SDK::CProceduralContextLook *context, float timePassed);

        bool(__fastcall *_originalPlayerInit)(SDK::wh::entitymodule::C_Player *, SDK::IGameObject *)                                                                                          = nullptr;
        void(__fastcall *_originalPlayerRelease)(SDK::wh::entitymodule::C_Player *)                                                                                                          = nullptr;
        void(__fastcall *_originalPlayerUpdate)(SDK::wh::entitymodule::C_Player *, void *, int)                                                                                              = nullptr;
        void(__fastcall *_originalPlayerPostUpdate)(SDK::wh::entitymodule::C_Player *, float)                                                                                                = nullptr;
        void(__fastcall *_originalLookContextUpdate)(SDK::CProceduralContextLook *, float)                                                                                                       = nullptr;
        void(__fastcall *_originalPlayerProcessEvent)(SDK::wh::entitymodule::C_Player *, SDK::SEntityEvent *)                                                                                = nullptr;
        void(__fastcall *_originalPlayerEntityRemoved)(SDK::wh::entitymodule::C_Player *)                                                                                                    = nullptr;
        void(__fastcall *_originalPlayerHandleDeath)(SDK::wh::entitymodule::C_Player *, bool)                                                                                                = nullptr;
        void(__fastcall *_originalPlayerInitLocalPlayer)(SDK::wh::entitymodule::C_Player *)                                                                                                  = nullptr;
        void(__fastcall *_originalPlayerOnPostUpdate)(SDK::IGameFrameworkListener *, float)                                                                                                  = nullptr;
        void(__fastcall *_originalPlayerNotifyActorActionRequest)(SDK::wh::entitymodule::C_Player *, const void *)                                                                           = nullptr;
        void(__fastcall *_originalPlayerUpdateMovementAudio)(SDK::wh::entitymodule::C_Player *, std::uintptr_t, float, const char *, std::uint32_t)                                         = nullptr;
        int(__fastcall *_originalPlayerIsGod)(SDK::wh::entitymodule::C_Player *)                                                                                                             = nullptr;
        std::uint32_t *(__fastcall *_originalPlayerUseItem)(SDK::wh::entitymodule::C_Player *, std::uint32_t *, SDK::wh::entitymodule::C_Item *, std::int32_t, std::int32_t, bool, std::int32_t) = nullptr;
        void(__fastcall *_originalPlayerItemEquipped)(SDK::wh::entitymodule::I_EquipmentManagerListener *, void *, SDK::wh::framework::WUID)                                                  = nullptr;
        void(__fastcall *_originalPlayerItemUnequipped)(SDK::wh::entitymodule::I_EquipmentManagerListener *, void *, SDK::wh::framework::WUID)                                                = nullptr;
        void(__fastcall *_originalPlayerRefreshHelmetActionHint)(SDK::wh::entitymodule::C_Player *)                                                                                          = nullptr;
        void(__fastcall *_originalPlayerRefreshHeadLight)(SDK::wh::entitymodule::C_Player *)                                                                                                 = nullptr;
        void(__fastcall *_originalPlayerRegisterActions)(SDK::wh::entitymodule::C_Player *)                                                                                                  = nullptr;
        void(__fastcall *_originalPlayerUnregisterActions)(SDK::wh::entitymodule::C_Player *)                                                                                                = nullptr;

        // --- The player's input --------------------------------------------------

        static void __fastcall InputRegisterActions(SDK::wh::entitymodule::C_PlayerInput *input);
        static void __fastcall InputOnAction(SDK::wh::entitymodule::C_PlayerInput *input, const SDK::CCryName &action, int activationMode, float value);
        static void __fastcall InputPreUpdate(SDK::wh::entitymodule::C_PlayerInput *input);
        static void __fastcall InputDestructor(SDK::wh::entitymodule::C_PlayerInput *input);

        void(__fastcall *_originalInputRegisterActions)(SDK::wh::entitymodule::C_PlayerInput *)                                         = nullptr;
        void(__fastcall *_originalInputOnAction)(SDK::wh::entitymodule::C_PlayerInput *, const SDK::CCryName &, int, float)             = nullptr;
        void(__fastcall *_originalInputPreUpdate)(SDK::wh::entitymodule::C_PlayerInput *)                                               = nullptr;
        void(__fastcall *_originalInputDestructor)(SDK::wh::entitymodule::C_PlayerInput *)                                              = nullptr;

        // --- Objects the player owns ----------------------------------------------

        static void __fastcall ChatFollowUpdate(SDK::wh::entitymodule::C_ChatFollowManager *chat);
        static void __fastcall DogHudAvailability(SDK::wh::entitymodule::C_DogInteractionManager *manager);
        static void __fastcall MissileRegisterActions(SDK::wh::entitymodule::C_MissileWeaponPlayerController *controller);
        static void __fastcall MissileRefreshActionMap(SDK::wh::entitymodule::C_MissileWeaponPlayerController *controller);
        static void __fastcall RiderRegisterMountedHints(SDK::wh::entitymodule::C_RiderPlayerControl *control);
        static void __fastcall RiderUnregisterMountedHints(SDK::wh::entitymodule::C_RiderPlayerControl *control);
        static void __fastcall RiderUpdateMountedHints(SDK::wh::entitymodule::C_RiderPlayerControl *control);
        static void __fastcall RiderStateChanged(SDK::wh::entitymodule::C_RiderPlayerControl *control, std::uint8_t rideState);
        static void __fastcall RiderInputStateChanged(SDK::wh::entitymodule::C_RiderPlayerInput *input, std::uint8_t rideState);
        static SDK::wh::combatmodule::C_CombatPlayer *__fastcall CreateCombatPlayer(SDK::wh::combatmodule::C_CombatModule *module, SDK::wh::entitymodule::C_Actor *actor);
        static bool __fastcall CombatPlayerInit(SDK::wh::combatmodule::C_CombatPlayer *player);
        static void __fastcall CombatPlayerShutdown(SDK::wh::combatmodule::C_CombatPlayer *player);
        static void __fastcall DestroyCombatActor(SDK::wh::combatmodule::C_CombatModule *module, SDK::wh::combatmodule::C_CombatActor **fighter);
        static void __fastcall SoulPlayerMaterialAudio(SDK::wh::rpgmodule::C_Soul *soul, std::uint32_t surfaceType);
        static void __fastcall ExhaustionEffect(SDK::wh::rpgmodule::C_BuffInstanceBase *buff);
        static void __fastcall HungerEffect(SDK::wh::rpgmodule::C_BuffInstanceBase *buff);
        static void __fastcall SetFeedbackSound(SDK::wh::entitymodule::C_Player *player, int sound, bool play, bool restart, const char *parameter);
        static void __fastcall ExecuteEntityAudioTrigger(void *unused, const std::uint32_t *triggerId, SDK::IEntity *entity);
        static bool IsRemoteBuffOwner(SDK::wh::rpgmodule::C_BuffInstanceBase *buff);
        static bool IsRemoteSoul(SDK::wh::rpgmodule::C_Soul *soul);
        static void __fastcall UpdateCombatDanger(void *context, SDK::wh::rpgmodule::C_Soul *soul, float frameTime);
        static void __fastcall BuffStartActorEffect(SDK::wh::rpgmodule::C_BuffInstanceBase *buff, SDK::wh::entitymodule::C_Actor *actor);
        static bool __fastcall ShowTutorial(void *tutorials, const SDK::CryStringT<char> &name);
        static void __fastcall ActorFall(SDK::wh::entitymodule::C_Actor *actor, const void *hitPos);
        static void __fastcall StopAnimations(SDK::wh::entitymodule::C_Actor *actor);

      public:
        /**
         * The falls this machine's player took (`C_Actor::Fall`), and where the
         * newest struck from where the body stood: what the local `Human`
         * publishes as `fallSequence`. Game thread.
         */
        static std::uint8_t LocalFalls() {
            return Get()._localFalls;
        }
        static const SDK::Vec3 &LocalFallHitOffset() {
            return Get()._localFallHitOffset;
        }

        /** Plays a fall its owner took on a remote player's body, struck at the world point given. Game thread. */
        static void ReplayFall(SDK::wh::entitymodule::C_Actor *actor, float x, float y, float z);

      private:
        static void __fastcall SubmitCharacterMoveRequest(SDK::wh::entitymodule::C_Actor *actor, void *frameParams, SDK::SCharacterMoveRequest *request);
        static bool __fastcall CombatFilterMovementRequest(SDK::wh::combatmodule::C_CombatPlayerInput *input, SDK::CMovementRequest *request, SDK::Vec3 *movement, SDK::Ang3 *lookDelta);
        static SDK::wh::combatmodule::C_CombatCameraManager *__fastcall GetLocalPlayerCameraManager(SDK::wh::combatmodule::C_CombatModule *module);
        static void __fastcall CombatAlignmentProcessClipEvents(SDK::wh::combatmodule::C_CombatAlignmentManager *manager);
        static void __fastcall CombatAlignmentUpdateSolverTargets(SDK::wh::combatmodule::C_CombatAlignmentManager *manager);
        static std::uint32_t __fastcall AlignmentSetPositionSolver(SDK::wh::entitymodule::I_AlignmentManager *alignment, const void *request);
        static std::uint32_t __fastcall AlignmentSetRotationSolver(SDK::wh::entitymodule::I_AlignmentManager *alignment, const void *request);
        static bool __fastcall CameraClipIsOwnerPlayerControlled(const void *clip);
        static bool IsRemoteClip(const SDK::IProceduralClip *clip);
        static void __fastcall AnimateCameraClipEnter(SDK::IProceduralClip *clip, float blendTime, float duration, const void *params);
        static void __fastcall AnimateCameraClipExit(SDK::IProceduralClip *clip, float blendTime);
        static void __fastcall CameraNearClipEnter(SDK::IProceduralClip *clip, float blendTime, float duration, const void *params);
        static void __fastcall CameraNearClipExit(SDK::IProceduralClip *clip, float blendTime);
        static void __fastcall BoneCameraClipEnter(SDK::IProceduralClip *clip, float blendTime, float duration, const void *params);
        static void __fastcall BoneCameraClipExit(SDK::IProceduralClip *clip, float blendTime);
        static void __fastcall BoneCameraClipUpdate(SDK::IProceduralClip *clip, float timePassed);
        static bool __fastcall MercyKillRequest(void *mercyKill, std::uint32_t target);
        static void __fastcall MercyKillFinished(void *mercyKill);

        void(__fastcall *_originalChatFollowUpdate)(SDK::wh::entitymodule::C_ChatFollowManager *)                                                                          = nullptr;
        void(__fastcall *_originalDogHudAvailability)(SDK::wh::entitymodule::C_DogInteractionManager *)                                                                    = nullptr;
        void(__fastcall *_originalMissileRegisterActions)(SDK::wh::entitymodule::C_MissileWeaponPlayerController *)                                                        = nullptr;
        void(__fastcall *_originalMissileRefreshActionMap)(SDK::wh::entitymodule::C_MissileWeaponPlayerController *)                                                       = nullptr;
        void(__fastcall *_originalRiderRegisterMountedHints)(SDK::wh::entitymodule::C_RiderPlayerControl *)                                                                = nullptr;
        void(__fastcall *_originalRiderUnregisterMountedHints)(SDK::wh::entitymodule::C_RiderPlayerControl *)                                                              = nullptr;
        void(__fastcall *_originalRiderUpdateMountedHints)(SDK::wh::entitymodule::C_RiderPlayerControl *)                                                                  = nullptr;
        void(__fastcall *_originalRiderStateChanged)(SDK::wh::entitymodule::C_RiderPlayerControl *, std::uint8_t)                                                          = nullptr;
        void(__fastcall *_originalRiderInputStateChanged)(SDK::wh::entitymodule::C_RiderPlayerInput *, std::uint8_t)                                                       = nullptr;
        SDK::wh::combatmodule::C_CombatPlayer *(__fastcall *_originalCreateCombatPlayer)(SDK::wh::combatmodule::C_CombatModule *, SDK::wh::entitymodule::C_Actor *)         = nullptr;
        bool(__fastcall *_originalCombatPlayerInit)(SDK::wh::combatmodule::C_CombatPlayer *)                                                                               = nullptr;
        void(__fastcall *_originalCombatPlayerShutdown)(SDK::wh::combatmodule::C_CombatPlayer *)                                                                           = nullptr;
        void(__fastcall *_originalDestroyCombatActor)(SDK::wh::combatmodule::C_CombatModule *, SDK::wh::combatmodule::C_CombatActor **)                                     = nullptr;
        void(__fastcall *_originalExhaustionEffect)(SDK::wh::rpgmodule::C_BuffInstanceBase *) = nullptr;
        void(__fastcall *_originalHungerEffect)(SDK::wh::rpgmodule::C_BuffInstanceBase *)     = nullptr;
        void(__fastcall *_originalSetFeedbackSound)(SDK::wh::entitymodule::C_Player *, int, bool, bool, const char *) = nullptr;
        void(__fastcall *_originalExecuteEntityAudioTrigger)(void *, const std::uint32_t *, SDK::IEntity *)                                                                  = nullptr;
        void(__fastcall *_originalUpdateCombatDanger)(void *, SDK::wh::rpgmodule::C_Soul *, float)                    = nullptr;
        void(__fastcall *_originalBuffStartActorEffect)(SDK::wh::rpgmodule::C_BuffInstanceBase *, SDK::wh::entitymodule::C_Actor *) = nullptr;
        bool(__fastcall *_originalShowTutorial)(void *, const SDK::CryStringT<char> &)                                = nullptr;
        void(__fastcall *_originalActorFall)(SDK::wh::entitymodule::C_Actor *, const void *)                                                                                 = nullptr;
        std::uint8_t _localFalls                                                                                                                                             = 0;
        SDK::Vec3 _localFallHitOffset                                                                                                                                        = {};
        bool _replayingFall                                                                                                                                                  = false;
        void(__fastcall *_originalStopAnimations)(SDK::wh::entitymodule::C_Actor *)                                                                                          = nullptr;
        void(__fastcall *_originalSoulPlayerMaterialAudio)(SDK::wh::rpgmodule::C_Soul *, std::uint32_t)                                                                    = nullptr;
        void(__fastcall *_originalSubmitCharacterMoveRequest)(SDK::wh::entitymodule::C_Actor *, void *, SDK::SCharacterMoveRequest *)                                       = nullptr;
        bool(__fastcall *_originalCombatFilterMovementRequest)(SDK::wh::combatmodule::C_CombatPlayerInput *, SDK::CMovementRequest *, SDK::Vec3 *, SDK::Ang3 *)              = nullptr;
        SDK::wh::combatmodule::C_CombatCameraManager *(__fastcall *_originalGetLocalPlayerCameraManager)(SDK::wh::combatmodule::C_CombatModule *)                          = nullptr;
        void(__fastcall *_originalCombatAlignmentProcessClipEvents)(SDK::wh::combatmodule::C_CombatAlignmentManager *)                                                     = nullptr;
        void(__fastcall *_originalCombatAlignmentUpdateSolverTargets)(SDK::wh::combatmodule::C_CombatAlignmentManager *)                                                   = nullptr;
        std::uint32_t(__fastcall *_originalAlignmentSetPositionSolver)(SDK::wh::entitymodule::I_AlignmentManager *, const void *)                                          = nullptr;
        std::uint32_t(__fastcall *_originalAlignmentSetRotationSolver)(SDK::wh::entitymodule::I_AlignmentManager *, const void *)                                          = nullptr;
        bool(__fastcall *_originalCameraClipIsOwnerPlayerControlled)(const void *)                                                                                          = nullptr;
        void(__fastcall *_originalAnimateCameraClipEnter)(SDK::IProceduralClip *, float, float, const void *) = nullptr;
        void(__fastcall *_originalAnimateCameraClipExit)(SDK::IProceduralClip *, float)                     = nullptr;
        void(__fastcall *_originalCameraNearClipEnter)(SDK::IProceduralClip *, float, float, const void *)  = nullptr;
        void(__fastcall *_originalCameraNearClipExit)(SDK::IProceduralClip *, float)                        = nullptr;
        void(__fastcall *_originalBoneCameraClipEnter)(SDK::IProceduralClip *, float, float, const void *)  = nullptr;
        void(__fastcall *_originalBoneCameraClipExit)(SDK::IProceduralClip *, float)                        = nullptr;
        void(__fastcall *_originalBoneCameraClipUpdate)(SDK::IProceduralClip *, float)                      = nullptr;
        bool(__fastcall *_originalMercyKillRequest)(void *, std::uint32_t)                                  = nullptr;
        void(__fastcall *_originalMercyKillFinished)(void *)                                                = nullptr;

        // --- Process-wide natives with no player argument ------------------------

        static SDK::wh::C_GlobalEnvironment *__fastcall GetGlobalEnvironment();
        static void __fastcall CameraUpdate(SDK::wh::game::C_CameraManager *camera);
        static void __fastcall ScreenEffectsPostUpdate(void *effects);
        static void __fastcall SetSaveLock(void *profile, std::uint8_t lock, bool enabled);
        static void __fastcall SetActionEnabled(SDK::wh::playermodule::I_ActionSets *sets, const SDK::CryStringT<char> &setName, const SDK::CryStringT<char> &actionName, bool enabled, bool includeDerived);
        static void __fastcall ClearActionCallbacks(SDK::wh::playermodule::I_ActionSets *sets, const SDK::CryStringT<char> &setName, const SDK::CryStringT<char> &actionName, bool includeDerived);
        static bool __fastcall RegisterNPC(SDK::wh::xgenaimodule::C_NPCManager *manager, SDK::IEntity *entity, std::uint32_t flags);
        static void __fastcall RebindScriptVariables(void *context);
        static std::intptr_t __fastcall BindStealthWatcher(SDK::wh::xgenaimodule::C_StealthWatcher *watcher);
        static void __fastcall BindPlayerStateHandler(void *handler, SDK::wh::entitymodule::C_Actor *actor);
        static void __fastcall TrespassAreaChanged(void *tracker, SDK::wh::xgenaimodule::C_NPC *npc, std::uint32_t areaLabel, bool entered);
        static void __fastcall SubmitMaterialSwitch(void *context, std::uint32_t *switchId, std::uint32_t *stateId);
        static void __fastcall UpdateMovementState(SDK::wh::entitymodule::C_ActorMovementController *controller, SDK::SMovementState &state);
        static void __fastcall HorseRiderChanged(void *context, SDK::wh::entitymodule::I_HorseRiderSync *previous, SDK::wh::entitymodule::I_HorseRiderSync *next);
        static bool __fastcall RequestPutAway(SDK::wh::entitymodule::C_Actor *actor, bool putAway, bool immediate);
        static void __fastcall SetCarryInputMode(SDK::wh::entitymodule::C_ActorActionCarryItem *action, bool carrying);
        static std::int32_t __fastcall CarryItemUpdate(SDK::wh::entitymodule::C_ActorActionCarryItem *action, float deltaTime);
        static bool __fastcall CarryItemCompletePickup(SDK::wh::entitymodule::C_ActorActionCarryItem *action);
        static std::int64_t __fastcall BeginLedge(void *ledge, SDK::wh::entitymodule::C_Actor *actor, const void *event);
        static std::int64_t __fastcall EndLedge(void *ledge, SDK::wh::entitymodule::C_Actor *actor);
        static void __fastcall SetCrouchInputEnabled(void *action, bool enabled);
        static bool __fastcall AnimSlaveEnter(void *action, void *previousAction);
        static void __fastcall AnimSlaveExit(void *action);
        static void __fastcall SetActionCallback(SDK::wh::playermodule::I_ActionSets *sets, const SDK::CryStringT<char> &setName, const SDK::CryStringT<char> &actionName, void *callback, bool includeDerived);
        static void __fastcall SetHorseMountingGlobal(void *control, bool active);
        static void __fastcall SetHorseMountedGlobal(void *control, bool active);
        static void __fastcall SetHorseDismountingGlobal(void *control, bool active);
        static SDK::wh::rpgmodule::I_Soul *__fastcall GetOrCreateSoul(SDK::wh::rpgmodule::C_SoulList *souls, SDK::IEntity *entity);
        static std::int32_t __fastcall TryStartBattleArchery(void *manager, SDK::IEntity *actorEntity, std::int32_t startMode);
        static void __fastcall UpdateShootingRestrictions(SDK::wh::entitymodule::C_ActorActionShootingBase *action);
        static void __fastcall ApplyFilterByReason(SDK::IActionMapManager *manager, const char *name, bool enable, std::uint8_t reason, bool flag);
        static void __fastcall SetInventoryBlocked(SDK::wh::combatmodule::C_CombatPlayerController *controller, bool blocked);

        // Read on every call to an accessor with thousands of call sites: a
        // plain static, so the hot path is a compare and a jump.
        static SDK::wh::C_GlobalEnvironment *(__fastcall *s_originalGetGlobalEnvironment)();

        void(__fastcall *_originalCameraUpdate)(SDK::wh::game::C_CameraManager *)                                                                                                                         = nullptr;
        void(__fastcall *_originalScreenEffectsPostUpdate)(void *)                                                                                                                                        = nullptr;
        void(__fastcall *_originalSetSaveLock)(void *, std::uint8_t, bool)                                                                                                                                = nullptr;
        void(__fastcall *_originalSetActionEnabled)(SDK::wh::playermodule::I_ActionSets *, const SDK::CryStringT<char> &, const SDK::CryStringT<char> &, bool, bool)                                      = nullptr;
        void(__fastcall *_originalClearActionCallbacks)(SDK::wh::playermodule::I_ActionSets *, const SDK::CryStringT<char> &, const SDK::CryStringT<char> &, bool)                                        = nullptr;
        bool(__fastcall *_originalRegisterNPC)(SDK::wh::xgenaimodule::C_NPCManager *, SDK::IEntity *, std::uint32_t)                                                                                      = nullptr;
        void(__fastcall *_originalRebindScriptVariables)(void *)                                                                                                                                          = nullptr;
        std::intptr_t(__fastcall *_originalBindStealthWatcher)(SDK::wh::xgenaimodule::C_StealthWatcher *)                                                                                                 = nullptr;
        void(__fastcall *_originalBindPlayerStateHandler)(void *, SDK::wh::entitymodule::C_Actor *) = nullptr;
        void(__fastcall *_originalTrespassAreaChanged)(void *, SDK::wh::xgenaimodule::C_NPC *, std::uint32_t, bool) = nullptr;
        void(__fastcall *_originalSubmitMaterialSwitch)(void *, std::uint32_t *, std::uint32_t *)                                                                                                         = nullptr;
        void(__fastcall *_originalUpdateMovementState)(SDK::wh::entitymodule::C_ActorMovementController *, SDK::SMovementState &) = nullptr;
        void(__fastcall *_originalHorseRiderChanged)(void *, SDK::wh::entitymodule::I_HorseRiderSync *, SDK::wh::entitymodule::I_HorseRiderSync *) = nullptr;
        bool(__fastcall *_originalRequestPutAway)(SDK::wh::entitymodule::C_Actor *, bool, bool) = nullptr;
        void(__fastcall *_originalSetCarryInputMode)(SDK::wh::entitymodule::C_ActorActionCarryItem *, bool) = nullptr;
        std::int32_t(__fastcall *_originalCarryItemUpdate)(SDK::wh::entitymodule::C_ActorActionCarryItem *, float) = nullptr;
        bool(__fastcall *_originalCarryItemCompletePickup)(SDK::wh::entitymodule::C_ActorActionCarryItem *) = nullptr;
        std::int64_t(__fastcall *_originalBeginLedge)(void *, SDK::wh::entitymodule::C_Actor *, const void *) = nullptr;
        std::int64_t(__fastcall *_originalEndLedge)(void *, SDK::wh::entitymodule::C_Actor *) = nullptr;
        void(__fastcall *_originalSetCrouchInputEnabled)(void *, bool) = nullptr;
        bool(__fastcall *_originalAnimSlaveEnter)(void *, void *) = nullptr;
        void(__fastcall *_originalAnimSlaveExit)(void *) = nullptr;
        void(__fastcall *_originalSetActionCallback)(SDK::wh::playermodule::I_ActionSets *, const SDK::CryStringT<char> &, const SDK::CryStringT<char> &, void *, bool) = nullptr;
        void(__fastcall *_originalSetHorseMountingGlobal)(void *, bool)                                                                                                                                   = nullptr;
        void(__fastcall *_originalSetHorseMountedGlobal)(void *, bool)                                                                                                                                    = nullptr;
        void(__fastcall *_originalSetHorseDismountingGlobal)(void *, bool)                                                                                                                                = nullptr;
        std::int32_t(__fastcall *_originalTryStartBattleArchery)(void *, SDK::IEntity *, std::int32_t)                                                                                                    = nullptr;
        void(__fastcall *_originalUpdateShootingRestrictions)(SDK::wh::entitymodule::C_ActorActionShootingBase *)                                                                                         = nullptr;
        void(__fastcall *_originalApplyFilterByReason)(SDK::IActionMapManager *, const char *, bool, std::uint8_t, bool)                                                                                  = nullptr;
        void(__fastcall *_originalSetInventoryBlocked)(SDK::wh::combatmodule::C_CombatPlayerController *, bool)                                                                                           = nullptr;
        SDK::wh::rpgmodule::I_Soul *(__fastcall *_originalGetOrCreateSoul)(SDK::wh::rpgmodule::C_SoulList *, SDK::IEntity *)                                                                              = nullptr;
    };
} // namespace KCDC::Features::Player
