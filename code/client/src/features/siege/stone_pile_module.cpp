#include <utils/safe_win32.h>

#include "stone_pile_module.h"

#include "stone_pile.h"

#include "features/player/player.h"
#include "common/module_support.h"
#include "shared/features/siege/siege_ballistics.h"
#include "sdk/cryengine/physics/c_physical_world.h"
#include "sdk/cryengine/system/i_console.h"
#include "sdk/patterns.h"
#include "sdk/wh/entitymodule/c_body_part_data.h"
#include "sdk/wh/rpgmodule/c_combat_soul.h"
#include "sdk/wh/rpgmodule/c_soul.h"

#include <MinHook.h>
#include <core_modules.h>
#include <logging/logger.h>
#include <networking/network_peer.h>
#include <networking/replication/replication_manager.h>
#include <utils/hooking/hook_function.h>

#include <algorithm>

namespace KCDC::Features::Siege {
    namespace {
        constexpr const char *kLogger = "StonePile";

        /** A thrown stone that has met nothing in this long has left the world this machine has streamed in. */
        constexpr std::uint64_t kMaxFlightMs = 8000;

        /** Moving less than this between two frames, a stone has come to rest where it lies. */
        constexpr float kRestStep = 0.002F;

        /** A stone at rest this soon after it left the hands is still being let go. */
        constexpr std::uint64_t kRestGraceMs = 400;

        /**
         * The stone's launch. `SetStoneUsageState(2)` (0x180D7F28C) pushes the
         * stone with `(0, LaunchStrength, 0)` pitched `LaunchXAngle` degrees
         * about X and turned by the thrower's rotation. A positive angle
         * pitches it down, measured live: the game's 1600 at 85 degrees drives
         * a 300 kg stone almost straight down at 5.3 m/s, which over a parapet
         * sends it down the wall and on open ground onto the thrower's toes.
         * 40 degrees up at 11 m/s, from the hands, carries it about 12 m.
         */
        constexpr float kLaunchXAngleDeg = -40.0F;
        constexpr float kLaunchStrength  = 3300.0F;

        /** The soul of the player a network id names, the local one included; null for nobody. */
        SDK::wh::rpgmodule::I_Soul *SoulOfPlayer(std::uint64_t playerNetworkId) {
            auto *human = KCDC::Common::FindReplicaById<KCDC::Features::Player::Human>(playerNetworkId);
            auto *actor = human != nullptr ? human->ResolveBody() : nullptr;
            return actor != nullptr ? actor->GetSoul() : nullptr;
        }

        bool IsLocalActor(const SDK::wh::playermodule::C_StoneThrowing *minigame) {
            const SDK::wh::entitymodule::C_Actor *actor = minigame != nullptr ? minigame->GetActor() : nullptr;
            return actor != nullptr && actor->GetEntityId() == KCDC::Common::LocalActorEntityId();
        }
    } // namespace

    StonePileModule &StonePileModule::Get() {
        static StonePileModule instance;
        return instance;
    }

    void StonePileModule::Install() {
        StonePileModule &module = Get();
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_StoneThrowing__StartWithPile), reinterpret_cast<LPVOID>(&StartWithPile), reinterpret_cast<LPVOID *>(&module._originalStartWithPile));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_StoneThrowing__Finish), reinterpret_cast<LPVOID>(&Finish), reinterpret_cast<LPVOID *>(&module._originalFinish));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_StoneThrowing__RequestThrow), reinterpret_cast<LPVOID>(&RequestThrow), reinterpret_cast<LPVOID *>(&module._originalRequestThrow));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_StoneThrowingPile__SetStoneUsageState), reinterpret_cast<LPVOID>(&SetStoneUsageState), reinterpret_cast<LPVOID *>(&module._originalSetStoneUsageState));
    }

    // --- The game's stone throwing -----------------------------------------------

    std::uint8_t __fastcall StonePileModule::StartWithPile(SDK::wh::playermodule::C_StoneThrowing *minigame, SDK::EntityId pileEntityId) {
        StonePileModule &module    = Get();
        const std::uint8_t started = module._originalStartWithPile(minigame, pileEntityId);
        if (started != 0 || module._replaying || !IsLocalActor(minigame)) {
            return started;
        }
        if (StonePile *pile = module.FindByPileEntity(pileEntityId)) {
            pile->NoteGrab(KCDC::Common::NowMs());
            module.Send(pile->GetNetworkID(), Shared::RPC::StonePileAction::Grab);
        }
        return started;
    }

    void __fastcall StonePileModule::Finish(SDK::wh::playermodule::C_StoneThrowing *minigame) {
        StonePileModule &module = Get();
        // Read before the original clears the pile binding.
        const SDK::wh::playermodule::C_StoneThrowingPile *native = minigame != nullptr ? minigame->GetPile() : nullptr;
        StonePile *pile                                          = native != nullptr && IsLocalActor(minigame) ? module.FindByPileEntity(native->GetEntityId()) : nullptr;
        module._originalFinish(minigame);
        if (pile != nullptr) {
            module.Send(pile->GetNetworkID(), Shared::RPC::StonePileAction::Leave);
        }
    }

    void __fastcall StonePileModule::RequestThrow(SDK::wh::playermodule::C_StoneThrowing *minigame) {
        StonePileModule &module = Get();
        // Read before the original moves the minigame on.
        const SDK::wh::playermodule::C_StoneThrowingPile *native = minigame != nullptr ? minigame->GetPile() : nullptr;
        StonePile *pile                                          = native != nullptr && !module._replaying && IsLocalActor(minigame) ? module.FindByPileEntity(native->GetEntityId()) : nullptr;
        module._originalRequestThrow(minigame);
        if (pile != nullptr) {
            module.Send(pile->GetNetworkID(), Shared::RPC::StonePileAction::Throw);
        }
    }

    void __fastcall StonePileModule::SetStoneUsageState(SDK::wh::playermodule::C_StoneThrowingPile *pile, SDK::EntityId stoneEntityId, int state) {
        StonePileModule &module = Get();
        module._originalSetStoneUsageState(pile, stoneEntityId, state);
        // State 2 is the stone let go as a rigid body, along its user's facing.
        if (state != 2 || pile == nullptr || pile->GetUserEntityId() != KCDC::Common::LocalActorEntityId()) {
            return;
        }
        StonePile *owner = module.FindByPileEntity(pile->GetEntityId());
        SDK::IEntity *stone = KCDC::Common::ResolveEntity(stoneEntityId);
        if (owner == nullptr || stone == nullptr) {
            return;
        }
        module._flight = Flight {owner->GetNetworkID(), stoneEntityId, KCDC::Common::ToGlm(stone->GetWorldPos()), KCDC::Common::NowMs()};
    }

    // --- Bookkeeping -------------------------------------------------------------

    StonePile *StonePileModule::FindByPileEntity(SDK::EntityId pileEntityId) const {
        if (pileEntityId == 0) {
            return nullptr;
        }
        return KCDC::Common::FindReplica<StonePile>([pileEntityId](StonePile *pile) {
            return pile->GetPileEntityId() == pileEntityId;
        });
    }

    void StonePileModule::Send(std::uint64_t pileNetworkId, Shared::RPC::StonePileAction action) {
        Shared::RPC::StonePileUse use;
        use.pileNetworkId = pileNetworkId;
        use.action        = action;
        if (auto *net = Framework::CoreModules::GetNetworkPeer()) {
            net->BroadcastRPC(use, MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
        }
    }

    void StonePileModule::InstallRPCs() {
        auto *net = Framework::CoreModules::GetNetworkPeer();
        if (net == nullptr) {
            return;
        }
        // Client RPCs are dispatched from the game tick.
        net->RegisterRPC<Shared::RPC::StonePileUsed>([this](const Shared::RPC::StonePileUsed &used, MafiaNet::Packet *) {
            if (used.IsSane()) {
                OnUsed(used);
            }
        });
        net->RegisterRPC<Shared::RPC::StoneImpact>([this](const Shared::RPC::StoneImpact &impact, MafiaNet::Packet *) {
            if (impact.IsSane()) {
                OnImpact(impact);
            }
        });
        _rpcsInstalled = true;
    }

    // --- Everyone else's throws ------------------------------------------------

    void StonePileModule::OnUsed(const Shared::RPC::StonePileUsed &used) {
        StonePile *pile = KCDC::Common::FindReplicaById<StonePile>(used.pileNetworkId);
        auto *human     = KCDC::Common::FindReplicaById<KCDC::Features::Player::Human>(used.playerNetworkId);
        auto *actor     = human != nullptr ? human->ResolveBody() : nullptr;
        if (pile == nullptr || pile->GetPileEntityId() == 0 || actor == nullptr) {
            return;
        }
        SDK::wh::playermodule::C_StoneThrowing *minigame = SDK::wh::playermodule::C_StoneThrowing::Of(actor, used.action == Shared::RPC::StonePileAction::Grab);
        if (minigame == nullptr) {
            return;
        }
        if (used.action == Shared::RPC::StonePileAction::Leave) {
            if (minigame->IsActive()) {
                minigame->RequestExit();
            }
            return;
        }
        // Their throw key: the stone their body holds goes, along its facing.
        if (used.action == Shared::RPC::StonePileAction::Throw) {
            if (minigame->IsActive() && minigame->GetHeldStoneEntityId() != 0) {
                _replaying = true;
                minigame->RequestThrow();
                _replaying = false;
            }
            return;
        }

        // The game's own grab on their body: the stone stays in its hands
        // until their throw is relayed.
        _replaying                 = true;
        const std::uint8_t started = minigame->StartWithPile(pile->GetPileEntityId());
        _replaying                 = false;
        if (started == 0) {
            pile->NoteGrab(KCDC::Common::NowMs());
        }
        Framework::Logging::GetLogger(kLogger)->info("Player {} threw from pile {} here: {}", used.playerNetworkId, used.pileNetworkId, started == 0 ? "replayed" : started == 1 ? "the pile refused their body" : "no interaction");
    }

    void StonePileModule::OnImpact(const Shared::RPC::StoneImpact &impact) {
        SDK::IEntity *player             = KCDC::Common::LocalPlayer();
        SDK::wh::rpgmodule::C_Soul *soul = KCDC::Common::LocalSoul();
        if (player == nullptr || soul == nullptr || soul->IsDead() || impact.damage <= 0.0F) {
            return;
        }
        // The thrower's own stone leaves their hands forward and away; it is
        // never theirs to take.
        auto *attacker = KCDC::Common::FindReplicaById<KCDC::Features::Player::Human>(impact.attackerNetworkId);
        auto *body     = attacker != nullptr ? attacker->ResolveBody() : nullptr;
        if (body != nullptr && body->GetEntityId() == KCDC::Common::LocalActorEntityId()) {
            return;
        }
        const float distance = glm::distance(KCDC::Common::ToGlm(player->GetWorldPos()), impact.point);
        const float share    = Shared::Game::Siege::DamageFalloff(distance, impact.damageRadius);
        auto *combat         = share > 0.0F ? soul->GetCombatSoul() : nullptr;
        if (combat == nullptr) {
            return;
        }
        // The game's own damage, with the thrower as the attacker, so the
        // local damage hook reports it to the server like any blow.
        const SDK::wh::entitymodule::C_BodyPartData bodyPart;
        combat->TakeDamage(0.0F, impact.damage * share, SoulOfPlayer(impact.attackerNetworkId), false, bodyPart);
        Framework::Logging::GetLogger(kLogger)->info("A stone hurt the local player for {:.1f} at {:.1f} m", impact.damage * share, distance);
    }

    // --- The local player's stone ----------------------------------------------

    void StonePileModule::Report(const glm::vec3 &point) {
        Shared::RPC::StoneImpactReport report;
        report.pileNetworkId = _flight.pileNetworkId;
        report.point         = point;
        if (auto *net = Framework::CoreModules::GetNetworkPeer()) {
            net->BroadcastRPC(report, MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
        }
        Framework::Logging::GetLogger(kLogger)->info("Our stone from pile {} came down at {:.1f}, {:.1f}, {:.1f}", _flight.pileNetworkId, point.x, point.y, point.z);
        _flight = Flight {};
    }

    void StonePileModule::Follow(std::uint64_t now) {
        if (_flight.stoneEntityId == 0) {
            return;
        }
        SDK::IEntity *stone = KCDC::Common::ResolveEntity(_flight.stoneEntityId);
        if (stone == nullptr || now - _flight.launchedAt > kMaxFlightMs) {
            _flight = Flight {};
            return;
        }
        const glm::vec3 point = KCDC::Common::ToGlm(stone->GetWorldPos());
        const glm::vec3 step  = point - _flight.lastPoint;

        // The step since the last frame, traced against everything a body or
        // the world has, past the stone itself and the thrower it leaves.
        SDK::CPhysicalWorld *physics = SDK::CPhysicalWorld::GetInstance();
        if (physics != nullptr && glm::dot(step, step) > kRestStep * kRestStep) {
            SDK::IPhysicalEntity *skip[2] = {stone->GetPhysics(), nullptr};
            int skipCount                 = skip[0] != nullptr ? 1 : 0;
            if (SDK::IEntity *player = KCDC::Common::LocalPlayer(); player != nullptr && player->GetPhysics() != nullptr) {
                skip[skipCount++] = player->GetPhysics();
            }
            SDK::SRayHit hit {};
            if (physics->RayWorldIntersection(KCDC::Common::ToNative(_flight.lastPoint), KCDC::Common::ToNative(step), SDK::CPhysicalWorld::kAllObjectTypes, SDK::CPhysicalWorld::kPickRayFlags, &hit, 1, skip, skipCount) > 0) {
                Report(KCDC::Common::ToGlm(hit.point));
                return;
            }
        }
        // Come to rest without the trace catching the contact: where it lies.
        if (glm::dot(step, step) <= kRestStep * kRestStep && now - _flight.launchedAt > kRestGraceMs) {
            Report(point);
            return;
        }
        _flight.lastPoint = point;
    }

    // --- The tick --------------------------------------------------------------

    void StonePileModule::NativeContextChanged() {
        _nativeContextLost = true;
    }

    void StonePileModule::Reset() {
        _flight = Flight {};
    }

    bool StonePileModule::ApplyLaunch() {
        // Every client launches every replayed stone through these, so they
        // are the same on all of them.
        const bool angle    = SDK::IConsole::TrySetFloatCVar("wh_pl_StoneThrowingLaunchXAngle", kLaunchXAngleDeg);
        const bool strength = SDK::IConsole::TrySetFloatCVar("wh_pl_StoneThrowingLaunchStrength", kLaunchStrength);
        return angle && strength;
    }

    void StonePileModule::Update() {
        if (!_rpcsInstalled) {
            InstallRPCs();
        }
        if (!_launchApplied) {
            _launchApplied = ApplyLaunch();
        }
        if (_nativeContextLost.exchange(false)) {
            _flight = Flight {};
            if (auto *replication = Framework::CoreModules::GetReplication()) {
                replication->ForEach<StonePile>([](StonePile *pile) {
                    pile->Unbind();
                });
            }
        }
        const std::uint64_t now = KCDC::Common::NowMs();
        if (auto *replication = Framework::CoreModules::GetReplication()) {
            replication->ForEach<StonePile>([now](StonePile *pile) {
                pile->Frame(now);
            });
        }
        Follow(now);
    }
} // namespace KCDC::Features::Siege

static InitFunction init(
    []() {
        KCDC::Features::Siege::StonePileModule::Install();
    },
    "StonePile");
