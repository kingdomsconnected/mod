#include <utils/safe_win32.h>

#include "features/player/player_duel_service.h"

#include "core/application.h"
#include "features/npc/npc.h"
#include "features/npc/npc_service.h"
#include "features/player/player.h"
#include "features/player/player_service.h"
#include "common/module_support.h"
#include "core/presentation_module.h"
#include "common/replication/body_life.h"
#include "sdk/cryengine/action/i_game_framework.h"
#include "sdk/cryengine/action/i_game_rules.h"
#include "sdk/cryengine/action/i_game_rules_system.h"
#include "sdk/cryengine/entitysystem/i_entity.h"
#include "sdk/wh/combatmodule/e_combat_slot_id.h"
#include "sdk/wh/rpgmodule/c_health_value_effect.h"
#include "sdk/wh/shared/c_recursive_srw_lock.h"

#include <core_modules.h>
#include <logging/logger.h>
#include <networking/network_peer.h>
#include <networking/replication/replication_manager.h>

#include <mafianet/get_time.h>

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <utility>

#include <MinHook.h>
#include <utils/hooking/hook_function.h>
#include <cstdint>
#include <intrin.h>

#include "features/player/player_missile_service.h"
#include "features/player/native_player_module.h"
#include "features/horse/horse_service.h"
#include "features/player/player_inventory_service.h"
#include "features/world/world_service.h"

#include "sdk/patterns.h"
#include "sdk/wh/combatmodule/c_combat_actor.h"
#include "sdk/wh/combatmodule/c_combat_actor_clinch.h"
#include "sdk/wh/combatmodule/c_combat_actor_collision_process.h"
#include "sdk/wh/combatmodule/c_combat_actor_director.h"
#include "sdk/wh/combatmodule/c_combat_actor_multi_trigger.h"
#include "sdk/wh/combatmodule/c_combat_actor_slot_planner.h"
#include "sdk/wh/combatmodule/c_combat_actor_time_cop.h"
#include "sdk/wh/combatmodule/c_combat_actor_timewarp.h"
#include "sdk/wh/combatmodule/c_combat_opponent_candidates.h"
#include "sdk/wh/combatmodule/c_combat_rpg.h"
#include "sdk/wh/combatmodule/s_combat_hit_record.h"
#include "sdk/wh/entitymodule/c_actor.h"
#include "sdk/wh/rpgmodule/c_combat_soul.h"
#include "sdk/wh/rpgmodule/s_combat_hit_data.h"
#include "sdk/wh/combatmodule/c_combat_actor_model.h"
#include "sdk/wh/combatmodule/c_combat_actor_object.h"
#include "sdk/wh/combatmodule/c_combat_player_controller.h"
#include "sdk/wh/entitymodule/c_actor_action_dead.h"
#include "sdk/wh/entitymodule/c_item_attachment_manager.h"
#include "sdk/wh/entitymodule/c_player.h"
#include "sdk/wh/entitymodule/c_player_material_effects.h"
#include "sdk/wh/rpgmodule/c_soul.h"
#include "sdk/wh/rpgmodule/s_soul_death_record.h"


namespace KCDC::Features::Player {
    thread_local const SDK::wh::entitymodule::C_Actor *PlayerDuelService::_offeredCandidate  = nullptr;
    thread_local SDK::wh::combatmodule::C_CombatActor *PlayerDuelService::_resolvingAttacker = nullptr;
    thread_local PlayerDuelService::BlowCapture *PlayerDuelService::_blowCapture             = nullptr;
    thread_local bool PlayerDuelService::_replaying                                          = false;
    thread_local bool PlayerDuelService::_givingWay                                          = false;
    thread_local bool PlayerDuelService::_blockSlotsOnly                                     = false;
    thread_local float PlayerDuelService::_localAttackSpeed                                  = 0.0F;
    thread_local const SDK::wh::combatmodule::C_CombatActor *PlayerDuelService::_postUpdatingFighter = nullptr;
    thread_local const SDK::wh::combatmodule::C_CombatActor *PlayerDuelService::_runningActionFighter = nullptr;

    namespace {
        constexpr const char *kLogger = "Duel";

        SDK::wh::entitymodule::C_Actor *LocalActor() {
            auto *framework = SDK::IGameFramework::GetInstance();
            return static_cast<SDK::wh::entitymodule::C_Actor *>(framework != nullptr ? framework->GetClientActor() : nullptr);
        }

        /** What the body already has, without building one. */
        SDK::wh::combatmodule::C_CombatActor *SettledFighter(SDK::wh::entitymodule::C_Actor *actor) {
            SDK::wh::combatmodule::C_CombatActor *fighter = actor != nullptr ? actor->GetCombatActor() : nullptr;
            return fighter != nullptr && fighter->IsInitialised() ? fighter : nullptr;
        }

        /** The combat side of a replicated body, built if it has none yet. */
        SDK::wh::combatmodule::C_CombatActor *BuiltFighter(SDK::wh::entitymodule::C_Actor *actor) {
            if (actor == nullptr) {
                return nullptr;
            }
            SDK::wh::combatmodule::C_CombatActor *fighter = actor->GetOrCreateCombatActor();
            return fighter != nullptr && fighter->IsInitialised() ? fighter : nullptr;
        }

        float DistanceSq(const glm::vec3 &a, const SDK::Vec3 &b) {
            const float dx = a.x - b.x;
            const float dy = a.y - b.y;
            const float dz = a.z - b.z;
            return (dx * dx) + (dy * dy) + (dz * dz);
        }

        /** Whether a body is down -- dead or out cold. */
        bool IsActorDown(SDK::wh::entitymodule::C_Actor *actor) {
            SDK::wh::rpgmodule::C_Soul *soul = actor != nullptr ? actor->GetSoul() : nullptr;
            return soul != nullptr && (soul->IsDead() || soul->IsUnconscious());
        }

        /**
         * Whether a fighter's body is in a saddle. A rider has no usable lock
         * (HasUsableLockedCombatTarget), and its swings and guards are all
         * unopposed: given an opponent, the controller's opponent-lock path
         * clears every attack and block request it makes.
         */
        bool IsRiding(const SDK::wh::combatmodule::C_CombatActor *fighter) {
            const SDK::wh::entitymodule::C_Actor *owner = fighter != nullptr ? fighter->GetOwner() : nullptr;
            return owner != nullptr && KCDC::Core::gApplication != nullptr && KCDC::Core::gApplication->GetHorseService().SeatOf(owner->GetEntityId()) != 0;
        }

        /** Below this length a direction has none to speak of. */
        constexpr float kNegligibleLength = 1e-2F;

        /** The body a network id names on this machine: the local player's own, or a copy; null when it is not here. */
        SDK::wh::entitymodule::C_Actor *BodyOf(std::uint64_t networkId, std::uint64_t localNetworkId) {
            if (networkId != 0 && networkId == localNetworkId) {
                return LocalActor();
            }
            Human *human = networkId != 0 ? KCDC::Common::FindReplicaById<Human>(networkId) : nullptr;
            if (human != nullptr) {
                return human->IsOwner() ? LocalActor() : human->ResolveActor();
            }
            auto *npc = KCDC::Common::FindReplicaById<KCDC::Features::Npc::Npc>(networkId);
            return npc != nullptr ? npc->ResolveActor() : nullptr;
        }
    } // namespace

    void PlayerDuelService::Install() {
        auto *net = Framework::CoreModules::GetNetworkPeer();
        if (net == nullptr) {
            Framework::Logging::GetLogger(kLogger)->error("Cannot register the combat ruling RPC: network peer is unavailable during client initialization");
            return;
        }
        net->RegisterRPC<Shared::RPC::NpcCombatReaction>([](const Shared::RPC::NpcCombatReaction &message, MafiaNet::Packet *packet) {
            if (!message.reaction.IsSane() || KCDC::Core::gApplication == nullptr) {
                return;
            }
            auto &service = KCDC::Core::gApplication->GetPlayerDuelService();
            auto reaction = message.reaction;
            auto *peer    = Framework::CoreModules::GetNetworkPeer();
            if (reaction.relayedAt != 0 && packet != nullptr && peer != nullptr && peer->GetPeer() != nullptr) {
                reaction.relayedAt -= peer->GetPeer()->GetClockDifferential(packet->guid);
            }
            std::scoped_lock lock(service._publicationMutex);
            if (service._incomingNpcReactions.size() < kMaxPendingNpcReactions) {
                service._incomingNpcReactions.push_back({reaction, MafiaNet::GetTime()});
            }
        });
        net->RegisterRPC<Shared::RPC::CombatClaimQuery>([](const Shared::RPC::CombatClaimQuery &query, MafiaNet::Packet *) {
            if (query.IsSane() && KCDC::Core::gApplication != nullptr) {
                KCDC::Core::gApplication->GetPlayerDuelService().AnswerClaim(query);
            }
        });
        net->RegisterRPC<Shared::RPC::CombatCancel>([](const Shared::RPC::CombatCancel &command, MafiaNet::Packet *packet) {
            if (!command.IsSane() || KCDC::Core::gApplication == nullptr) {
                return;
            }
            // Presentations are fenced on this machine's clock, so the cancel
            // is shifted onto it the same way theirs were.
            Cancellation cancellation {command, 0};
            auto *peer = Framework::CoreModules::GetNetworkPeer();
            if (packet == nullptr || peer == nullptr || peer->GetPeer() == nullptr) {
                return;
            }
            cancellation.localAt = command.cancelledAt - peer->GetPeer()->GetClockDifferential(packet->guid);
            auto &service        = KCDC::Core::gApplication->GetPlayerDuelService();
            std::scoped_lock lock(service._rulingMutex);
            if (service._incomingCancellations.size() < kMaxQueuedRulings) {
                service._incomingCancellations.push_back(cancellation);
            }
        });
        net->RegisterRPC<Shared::RPC::CombatOutcome>([](const Shared::RPC::CombatOutcome &outcome, MafiaNet::Packet *) {
            if (!outcome.IsSane()) {
                Framework::Logging::GetLogger(kLogger)->warn("Rejected a malformed combat ruling: sequence={}, attacker={}, victim={}", outcome.sequence, outcome.attackerNetworkId, outcome.victimNetworkId);
                return;
            }
            if (KCDC::Core::gApplication != nullptr) {
                KCDC::Core::gApplication->GetPlayerDuelService().QueueRuling(outcome);
            }
        });
    }

    // --- Who is fighting whom ----------------------------------------------

    void PlayerDuelService::Update() {
        if (Framework::CoreModules::GetReplication() == nullptr) {
            return;
        }
        const std::uint64_t now = MafiaNet::GetTime();
        DrainCancellations();
        PublishRoster(now);
        const Roster &published = Current();
        DropExpiredRipostes(now);
        SampleGuard(published.LocalFighter(), now);

        // A fighter that is no longer a replicated body here swings nothing
        // and is owed nothing.
        const auto isRemote = [&published](const SDK::wh::combatmodule::C_CombatActor *fighter) {
            return published.IsNetworked(fighter);
        };
        _swingGenerations.Retain(isRemote);
        _blockedSwings.Retain(isRemote);
        _hitSwings.Retain(isRemote);
        _forcedAttackSpeeds.Retain(isRemote);
        _perfectBlockRows.Retain([&published](const auto *fighter, const auto *opponent) {
            return published.CanPublishReaction(fighter, opponent);
        });
        std::erase_if(_swingOpponents, [&isRemote](const auto &entry) {
            return !isRemote(entry.first);
        });
        {
            std::scoped_lock lock(_publicationMutex);
            std::erase_if(_recentClaims, [now](const auto &entry) {
                return now - entry.second.at > kClaimedReactionWindowMs;
            });
        }

        DrainNpcReactions(now);
        FlushPublications();
        DrainRulings(now);
    }

    void PlayerDuelService::PublishRoster(std::uint64_t now) {
        auto *replication = Framework::CoreModules::GetReplication();

        // Filled into a slot no hook is reading and published whole.
        Roster &roster = _rosters.Next();
        roster.Clear();

        SDK::wh::entitymodule::C_Actor *localActor = LocalActor();
        SDK::IEntity *localEntity                  = localActor != nullptr ? localActor->GetEntity() : nullptr;

        // Asked for rather than created: C_Player's own constructor builds the
        // local body's combat side.
        Human *owner = KCDC::Features::Player::Human::FindLocal();
        roster.SetLocal(SettledFighter(localActor), owner != nullptr ? owner->GetNetworkID() : 0);

        SDK::wh::combatmodule::C_CombatActor *localFighter = roster.LocalFighter();
        const SDK::IEntity *localTarget                    = localFighter != nullptr ? localFighter->GetTargetEntity() : nullptr;
        const SDK::Vec3 localPosition                      = localEntity != nullptr ? localEntity->GetWorldPos() : SDK::Vec3 {0.0F, 0.0F, 0.0F};
        const bool localStanceUp                           = localFighter != nullptr && localFighter->IsFightingStanceUp();

        replication->ForEach<Human>([&](Human *human) {
            if (human->IsOwner()) {
                return;
            }
            SDK::wh::entitymodule::C_Actor *actor = human->ResolveActor();
            if (actor == nullptr) {
                return;
            }

            const bool alive     = human->soul.CanAct();
            const bool nearby    = localEntity != nullptr && DistanceSq(human->position, localPosition) < kCandidateRange * kCandidateRange;
            const bool candidate = alive && nearby && localStanceUp && IsTargetable(human->GetNetworkID());
            const bool squaredUp = human->combatTargetNetworkId != 0;

            // A fighter's thirty subsystems are only built for a body that
            // has a fight to be in: one the local picker may weigh, or one
            // whose owner has locked on to someone.
            SDK::wh::combatmodule::C_CombatActor *fighter = candidate || squaredUp ? BuiltFighter(actor) : SettledFighter(actor);
            if (fighter == nullptr) {
                return;
            }
            roster.AddRemote(fighter, human->GetNetworkID(), actor, candidate);
            if (alive && human->KeepsCombatStance()) {
                roster.HoldRemoteStance(fighter);
            }
            if (squaredUp) {
                roster.HoldRemoteLock(fighter);
            }
            if (localTarget != nullptr && actor->GetEntity() == localTarget) {
                roster.SetLocalTarget(human->GetNetworkID());
            }

            // The fight a swing either way began holds while both still stand
            // ready and in reach. A lock is not an exchange: the target pick
            // keeps the engaged body locked on, and a lock that renewed the
            // fight would hold it for as long as the two stood near.
            if (_engagement.Current(now) == human->GetNetworkID()) {
                if (alive && localStanceUp && DistanceSq(human->position, localPosition) < kEngageRange * kEngageRange) {
                    roster.SetEngaged(actor, human->GetNetworkID());
                }
                else {
                    _engagement.End();
                }
            }

            // The owner's lock, handed to the copy through the same call
            // the owner's picker made. The engine drops a lock it already
            // holds, and makes it the opponent at the copy's next rest.
            // Without one, the body the owner last swung at: a fight the engine
            // runs against its opponent alone carries no lock to replicate.
            const auto swungAt         = _swingOpponents.find(fighter);
            std::uint64_t facing       = squaredUp ? human->combatTargetNetworkId : (swungAt != _swingOpponents.end() ? swungAt->second : 0);
            std::erase_if(_cancelledTargets, [&](const auto &pair) {
                return pair.first == human->GetNetworkID() && pair.second != facing;
            });
            if (_cancelledTargets.contains({human->GetNetworkID(), facing})) {
                facing = 0;
            }
            SDK::IEntity *wanted       = nullptr;
            if (facing != 0 && facing == roster.LocalNetworkId()) {
                wanted = localEntity;
            }
            else if (facing != 0) {
                // Only a body with its action actor: the engine's lock binds the
                // target's shooting expansion through it unchecked, and a copy
                // still being built, or torn down, has none.
                SDK::wh::entitymodule::C_Actor *targetActor = BodyOf(facing, roster.LocalNetworkId());
                wanted                                      = targetActor != nullptr && targetActor->GetActionActor() != nullptr ? targetActor->GetEntity() : nullptr;
            }
            // A riding copy is not locked; see IsRiding.
            if (IsRiding(fighter)) {
                wanted = nullptr;
            }
            if (fighter->GetTargetEntity() != wanted) {
                // Under the fighter's combat-group lock, as the engine's own
                // external writers take it: the group may be ticking on a
                // worker while this runs.
                const SDK::wh::shared::C_ScopedRecursiveSRWLock lock(fighter->GetCombatGroupLock());
                fighter->SetTarget(wanted);
            }
        });

        replication->ForEach<KCDC::Features::Npc::Npc>([&](KCDC::Features::Npc::Npc *npc) {
            auto *actor = npc->ResolveActor();
            if (actor == nullptr) {
                return;
            }
            auto *fighter = BuiltFighter(actor);
            if (fighter == nullptr) {
                return;
            }
            roster.AddNpc(fighter, npc->GetNetworkID(), npc->IsSimulating(), npc->spawnEpoch, npc->stateEpoch, npc->intentRevision, npc->maxHealth);
            if (npc->alive && npc->combatStance && npc->combatStateRevision == npc->intentRevision) {
                roster.HoldRemoteStance(fighter);
            }
            if (localTarget != nullptr && actor->GetEntity() == localTarget) {
                roster.SetLocalTarget(npc->GetNetworkID());
            }
        });
        _rosters.Publish();
    }

    void PlayerDuelService::DropExpiredRipostes(std::uint64_t now) {
        std::erase_if(_raisedRipostes, [&](const RaisedRiposte &raised) {
            if (now < raised.dropAt) {
                return false;
            }
            SDK::wh::combatmodule::C_CombatActor *fighter    = FighterFor(Current(), raised.networkId, false);
            SDK::wh::combatmodule::C_CombatActorModel *model = fighter != nullptr ? fighter->GetModel() : nullptr;
            if (model != nullptr && model->IsRiposteRequested()) {
                const SDK::wh::shared::C_ScopedRecursiveSRWLock lock(fighter->GetCombatGroupLock());
                model->SetRiposteRequested(false);
            }
            return true;
        });
    }

    void PlayerDuelService::SampleGuard(const SDK::wh::combatmodule::C_CombatActor *localFighter, std::uint64_t now) {
        const SDK::wh::combatmodule::C_CombatActorModel *model = localFighter != nullptr ? localFighter->GetModel() : nullptr;
        const bool blocking                                    = model != nullptr && (static_cast<std::uint32_t>(model->GetState()) & SDK::wh::combatmodule::E_CombatActorStateId::ParryInPlace) != 0;
        std::scoped_lock history(_guardHistoryMutex);
        _guardHistory.NoteBlocking(now, blocking);
        _guardHistory.Trim(now);
    }

    void PlayerDuelService::Engage(std::uint64_t networkId) {
        _engagement.Note(networkId, MafiaNet::GetTime());
    }

    bool PlayerDuelService::KeepEngagedTarget(SDK::wh::combatmodule::C_CombatPlayerController *controller) const {
        SDK::wh::combatmodule::C_CombatActor *fighter = controller != nullptr ? controller->GetCombatActor() : nullptr;
        const Roster &roster                          = Current();
        SDK::wh::entitymodule::C_Actor *engaged       = roster.EngagedBody();
        SDK::IEntity *entity                          = engaged != nullptr ? engaged->GetEntity() : nullptr;
        if (!roster.IsLocal(fighter) || entity == nullptr) {
            return false;
        }
        // The engine's own pick runs for a rider; see IsRiding.
        if (IsRiding(fighter)) {
            return false;
        }
        // What the player's own lock would hold: the picker never offers a
        // replicated body, so left to itself it drops the lock every frame,
        // the opponent with it at the next rest, and with the opponent every
        // window either fighter's swing opens on the other.
        if (fighter->GetTargetEntity() != entity) {
            const SDK::wh::shared::C_ScopedRecursiveSRWLock lock(fighter->GetCombatGroupLock());
            fighter->SetTarget(entity);
        }
        return true;
    }

    bool PlayerDuelService::KeepOwnerLock(SDK::wh::combatmodule::C_CombatPlayerController *controller) const {
        // The copy's own pick never meets the body its owner locked on to
        // (OfferRemoteCandidates feeds the local picker alone). For a copy its
        // controller does not count as locked -- fists up, out of the free
        // guard, an opponent, not mounted (HasUsableLockedCombatTarget,
        // 0x1809D3514) -- UpdateAttackTarget (0x1809D411C) sets what it
        // found: nothing. In the free guard it dropped the roster's lock
        // every frame, and with it the opponent SetTarget promotes at rest,
        // before UpdateOpponentLock, which runs next, could take the copy
        // into its fight stance (0x1812EDDE0); only a replayed swing squaring
        // the copy up locked it.
        //
        // Skipped in that state alone: the owner's own lock, not the body it
        // last swung at, which the roster hands too and which outlives the
        // fight, with the fists up in the free guard. Out of it a locked copy
        // keeps its lock natively, and one its controller does not count as
        // locked -- fists down, or mounted -- must lose it as before: given an
        // opponent, the same UpdateOpponentLock asks it to lower its stance
        // every frame (0x1812EDEE9), and its combat mode ends with it, so a
        // copy getting on a horse never sat.
        SDK::wh::combatmodule::C_CombatActor *fighter = controller->GetCombatActor();
        return Current().HoldsRemoteLock(fighter) && fighter->GetTargetEntity() != nullptr && fighter->IsFightingStanceUp() && fighter->IsInFreeGuardStance();
    }

    bool PlayerDuelService::WasCancelled(std::uint64_t first, std::uint64_t second, std::uint64_t relayedAt) const {
        const auto found = _cancelledAt.find(std::minmax(first, second));
        return found != _cancelledAt.end() && relayedAt <= found->second.localAt;
    }

    bool PlayerDuelService::WasRuledCancelled(std::uint64_t first, std::uint64_t second, std::uint64_t ruledAt) const {
        const auto found = _cancelledAt.find(std::minmax(first, second));
        return found != _cancelledAt.end() && ruledAt <= found->second.command.cancelledAt;
    }

    void PlayerDuelService::DrainCancellations() {
        std::vector<Cancellation> cancellations;
        {
            std::scoped_lock lock(_rulingMutex);
            cancellations.swap(_incomingCancellations);
        }
        for (const auto &cancellation : cancellations) {
            CancelPair(cancellation);
        }
    }

    void PlayerDuelService::CancelPair(const Cancellation &cancellation) {
        const Shared::RPC::CombatCancel &command = cancellation.command;
        auto *first  = KCDC::Common::FindReplicaById<Human>(command.first);
        auto *second = KCDC::Common::FindReplicaById<Human>(command.second);
        if ((first != nullptr && (first->soul.incarnation != command.firstIncarnation || first->stateEpoch != command.firstEpoch)) || (second != nullptr && (second->soul.incarnation != command.secondIncarnation || second->stateEpoch != command.secondEpoch))) {
            return;
        }
        _cancelledAt[std::minmax(command.first, command.second)] = cancellation;
        const auto standDown                                     = [&](std::uint64_t id, std::uint64_t other) {
            auto *fighter                = FighterFor(Current(), id, false);
            auto *body                   = BodyOf(other, Current().LocalNetworkId());
            auto *entity                 = body != nullptr ? body->GetEntity() : nullptr;
            const bool localSwingMatches = id == Current().LocalNetworkId() && _localSwingTarget.load() == other;
            _cancelledTargets.emplace(id, other);
            if (id == Current().LocalNetworkId()) {
                if (_engagement.Current(MafiaNet::GetTime()) == other) {
                    _engagement.End();
                }
                if (localSwingMatches) {
                    _localSwingTarget.store(0);
                    _localSwingGeneration.store(0);
                }
            }
            if (fighter == nullptr || entity == nullptr) {
                return;
            }
            const SDK::wh::shared::C_ScopedRecursiveSRWLock lock(fighter->GetCombatGroupLock());
            auto *opponent             = fighter->GetOpponent();
            auto *target               = fighter->GetTargetEntity();
            const bool opponentMatches = opponent != nullptr && opponent->GetOwner() == body;
            const bool targetMatches   = target == entity;
            const auto swungAt         = _swingOpponents.find(fighter);
            const bool swingMatches    = swungAt != _swingOpponents.end() && swungAt->second == other;
            if (swingMatches) {
                _swingOpponents.erase(swungAt);
            }
            if (targetMatches) {
                fighter->SetTarget(nullptr);
            }
            if (opponentMatches) {
                fighter->SetOpponent(nullptr);
            }
            // An unrelated target, including an NPC, still owns this stance.
            if ((!targetMatches && target != nullptr) || (!opponentMatches && opponent != nullptr) || (!targetMatches && !opponentMatches && !swingMatches && !localSwingMatches)) {
                return;
            }
            if (id == Current().LocalNetworkId()) {
                KCDC::Core::gApplication->GetPlayerService().NotifyCombatAttackCanceled();
            }
            else if (auto *human = KCDC::Common::FindReplicaById<Human>(id)) {
                human->CancelRemoteCombatAttack();
            }
            fighter->SetAttackInputClass(-1, false);
            if (auto *model = fighter->GetModel()) {
                model->SetCombatRequest(SDK::wh::combatmodule::C_CombatActorModel::COMBAT_REQUEST_ATTACK, false);
                model->SetRiposteRequested(false);
            }
            fighter->RequestGuard(SDK::wh::combatmodule::C_CombatActor::GUARD_REQUEST_NONE);
            fighter->LeaveCombatModeIfActive();
            fighter->RequestFightingStance(false);
            _swingGenerations.Retain([fighter](const auto *entry) {
                return entry != fighter;
            });
            _blockedSwings.Retain([fighter](const auto *entry) {
                return entry != fighter;
            });
            _hitSwings.Retain([fighter](const auto *entry) {
                return entry != fighter;
            });
            _forcedAttackSpeeds.Retain([fighter](const auto *entry) {
                return entry != fighter;
            });
            _perfectBlockRows.Retain([fighter](const auto *entry, const auto *opponent) {
                return entry != fighter && opponent != fighter;
            });
            std::erase_if(_raisedRipostes, [id](const auto &entry) {
                return entry.networkId == id;
            });
        };
        standDown(command.first, command.second);
        standDown(command.second, command.first);
        const auto local = Current().LocalNetworkId();
        const auto other = local == command.first ? command.second : local == command.second ? command.first : 0;
        if (other != 0) {
            std::scoped_lock lock(_publicationMutex);
            std::erase_if(_publications, [other](const auto &entry) {
                return entry.opponentNetworkId == other;
            });
            std::erase_if(_reports, [other](const auto &entry) {
                return entry.attackerNetworkId == other || entry.victimNetworkId == other;
            });
            std::erase_if(_recentClaims, [other](const auto &entry) {
                return entry.first.first == other;
            });
        }
    }

    void PlayerDuelService::Reset() {
        _cancelledAt.clear();
        _cancelledTargets.clear();
        _untargetable.clear();
        _rosters.Reset([](Roster &roster) {
            roster.Clear();
        });
        _engagement.End();
        {
            std::scoped_lock lock(_publicationMutex);
            _publications.clear();
            _reports.clear();
            _recentClaims.clear();
            _comboStrikeQueued = false;
            _incomingNpcReactions.clear();
            _npcReactions.clear();
            _lastNpcPresentation.clear();
        }
        {
            std::scoped_lock lock(_rulingMutex);
            _incomingRulings.clear();
            _incomingCancellations.clear();
        }
        {
            std::scoped_lock history(_guardHistoryMutex);
            _guardHistory = CombatGuardHistory {};
        }
        _rulings.clear();
        _lastRuling = 0;
        _forcedAttackSpeeds.Clear();
        _swingGenerations.Clear();
        _blockedSwings.Clear();
        _hitSwings.Clear();
        _swingOpponents.clear();
        _raisedRipostes.clear();
        _localSwingGeneration.store(0);
        _localSwingTarget.store(0);
        _perfectBlockRows.Clear();
        _lastDeferral = "";
    }

    bool PlayerDuelService::IsRemoteFighter(const SDK::wh::combatmodule::C_CombatActor *fighter) const {
        return Current().IsRemote(fighter);
    }

    bool PlayerDuelService::IsLocalFighter(const SDK::wh::combatmodule::C_CombatActor *fighter) const {
        return Current().IsLocal(fighter);
    }

    bool PlayerDuelService::KeepRemoteStance(const SDK::wh::combatmodule::C_CombatActor *fighter) const {
        return fighter == _postUpdatingFighter && Current().HoldsRemoteStance(fighter);
    }

    bool PlayerDuelService::IsPlayerPair(const SDK::wh::combatmodule::C_CombatActor *fighter) const {
        return fighter != nullptr && Current().IsPlayerPair(fighter, fighter->GetOpponent());
    }

    std::uint64_t PlayerDuelService::LocalTargetNetworkId() const {
        return Current().LocalTargetNetworkId();
    }

    std::uint64_t PlayerDuelService::LocalOpponentNetworkId() const {
        const Roster &roster                                 = Current();
        const SDK::wh::combatmodule::C_CombatActor *fighter  = roster.LocalFighter();
        const SDK::wh::combatmodule::C_CombatActor *opponent = fighter != nullptr ? fighter->GetOpponent() : nullptr;
        if (opponent != nullptr) {
            // A fight with an NPC is the NPC's, whoever else stands near.
            return roster.IsRemote(opponent) ? roster.NetworkIdOf(opponent) : 0;
        }
        if (roster.LocalTargetNetworkId() != 0) {
            return roster.LocalTargetNetworkId();
        }
        if (fighter != nullptr && fighter->GetTargetEntity() != nullptr) {
            return 0;
        }

        // Neither: the swing was thrown on the move, at whoever stands in
        // front -- which is who the engine's contact will find.
        SDK::wh::entitymodule::C_Actor *localActor = LocalActor();
        SDK::IEntity *localEntity                  = localActor != nullptr ? localActor->GetEntity() : nullptr;
        auto *replication                          = Framework::CoreModules::GetReplication();
        if (localEntity == nullptr || replication == nullptr) {
            return 0;
        }
        const SDK::Vec3 origin  = localEntity->GetWorldPos();
        const SDK::Vec3 forward = localEntity->GetForwardDir();
        const float coneCosine  = std::cos(glm::radians(kSoftTargetConeDegrees));
        std::uint64_t best      = 0;
        float bestDistanceSq    = kSoftTargetRange * kSoftTargetRange;
        replication->ForEach<Human>([&](Human *human) {
            if (human->IsOwner() || !human->soul.CanAct() || human->ResolveActor() == nullptr) {
                return;
            }
            const float dx         = human->position.x - origin.x;
            const float dy         = human->position.y - origin.y;
            const float distanceSq = (dx * dx) + (dy * dy);
            const float flatLength = std::sqrt((forward.x * forward.x) + (forward.y * forward.y));
            if (distanceSq >= bestDistanceSq || distanceSq <= kNegligibleLength * kNegligibleLength || flatLength <= kNegligibleLength) {
                return;
            }
            const float facing = ((dx * forward.x) + (dy * forward.y)) / (std::sqrt(distanceSq) * flatLength);
            if (facing >= coneCosine) {
                best           = human->GetNetworkID();
                bestDistanceSq = distanceSq;
            }
        });
        return best;
    }

    void PlayerDuelService::SquareUp(SDK::wh::combatmodule::C_CombatActor *fighter, std::uint64_t opponentNetworkId) {
        if (fighter == nullptr || opponentNetworkId == 0 || !IsRemoteFighter(fighter)) {
            return;
        }
        _swingOpponents[fighter] = opponentNetworkId;
        _cancelledTargets.erase({Current().NetworkIdOf(fighter), opponentNetworkId});

        const Roster &roster                        = Current();
        SDK::wh::entitymodule::C_Actor *opponent    = BodyOf(opponentNetworkId, roster.LocalNetworkId());
        SDK::IEntity *opponentEntity                = opponent != nullptr ? opponent->GetEntity() : nullptr;
        SDK::wh::combatmodule::C_CombatActor *other = opponentNetworkId == roster.LocalNetworkId() ? roster.LocalFighter() : BuiltFighter(opponent);
        if (opponentNetworkId == roster.LocalNetworkId()) {
            Engage(roster.NetworkIdOf(fighter));
        }
        // Prewarm only a stance the victim's owner actually requested. Being
        // swung at must not put an unready owner's copy into combat mode.
        // Reactions that require a stance raise it through the director.
        if (other != nullptr && roster.HoldsRemoteStance(other) && !other->IsFightingStanceUp()) {
            const SDK::wh::shared::C_ScopedRecursiveSRWLock stance(other->GetCombatGroupLock());
            other->RequestFightingStance(true);
        }
        // A riding attacker swings unopposed, as its owner's engine did.
        if (opponentEntity == nullptr || other == nullptr || fighter->GetOpponent() == other || IsRiding(fighter)) {
            return;
        }

        // The lock the owner's engine would have taken, then the opponent at
        // once: the swing is about to start, and the contact it raises looks
        // for this body. Under the group lock, as the engine's own writers.
        const SDK::wh::shared::C_ScopedRecursiveSRWLock lock(fighter->GetCombatGroupLock());
        if (fighter->GetTargetEntity() != opponentEntity) {
            fighter->SetTarget(opponentEntity);
        }
        fighter->SetOpponent(opponentEntity);
    }

    bool PlayerDuelService::InvolvesRemotePlayer(const SDK::wh::combatmodule::C_CombatActor *fighter) const {
        return fighter != nullptr && Current().InvolvesRemote(fighter, fighter->GetOpponent());
    }

    SDK::wh::combatmodule::C_CombatActor *PlayerDuelService::FighterFor(const Roster &roster, std::uint64_t networkId, bool build) const {
        if (networkId == 0) {
            return nullptr;
        }
        if (networkId == roster.LocalNetworkId()) {
            return roster.LocalFighter();
        }
        SDK::wh::entitymodule::C_Actor *actor = BodyOf(networkId, roster.LocalNetworkId());
        return build ? BuiltFighter(actor) : SettledFighter(actor);
    }

    // --- The local player's opponent picker ---------------------------------

    PlayerDuelService::CandidateScope::CandidateScope(const SDK::wh::entitymodule::C_Actor *actor): _previous(_offeredCandidate) {
        _offeredCandidate = actor;
    }

    PlayerDuelService::CandidateScope::~CandidateScope() {
        _offeredCandidate = _previous;
    }

    bool PlayerDuelService::IsPickableRemoteBody(const SDK::wh::entitymodule::C_Actor *actor) const {
        return Current().IsPickable(actor);
    }

    void PlayerDuelService::SetTargetable(std::uint64_t networkId, bool targetable) {
        if (targetable) {
            _untargetable.erase(networkId);
        }
        else {
            _untargetable.insert(networkId);
        }
    }

    bool PlayerDuelService::IsTargetable(std::uint64_t networkId) const {
        return _untargetable.count(networkId) == 0;
    }

    void PlayerDuelService::OfferRemoteCandidates(SDK::wh::combatmodule::C_CombatOpponentCandidates *candidates) const {
        const Roster &roster = Current();
        if (candidates == nullptr || candidates->GetOwner() == nullptr || !roster.IsLocal(candidates->GetOwner())) {
            return;
        }

        // Replicated bodies are in no AI grid, so the grid query never meets
        // them. Each is put through the grid's own per-candidate callback
        // instead, which is every native test the picker applies to an NPC.
        for (const SDK::wh::entitymodule::C_Actor *body : roster.PickableBodies()) {
            auto *actor = const_cast<SDK::wh::entitymodule::C_Actor *>(body);
            if (!candidates->Contains(SettledFighter(actor))) {
                candidates->Offer(actor);
            }
        }
    }

    bool PlayerDuelService::IsHostileCandidate(const SDK::wh::combatmodule::C_CombatRPG *asker, const SDK::wh::combatmodule::C_CombatActor *candidate) const {
        if (asker == nullptr || candidate == nullptr) {
            return false;
        }
        const Roster &roster                                = Current();
        const SDK::wh::combatmodule::C_CombatActor *fighter = asker->GetCombatActor();
        if (roster.IsHostileCandidate(fighter, candidate, candidate->GetOwner())) {
            return true;
        }
        // A server's NPC fighting the local player: the game takes only a
        // shared skirmish for hostility (0x180473864), which the NPC's brain
        // would join and which never runs for it, so the picker would drop it
        // as a target every frame.
        return roster.IsLocal(fighter) && KCDC::Features::Npc::NpcService::IsCombatPair(fighter->GetOwner()->GetEntityId(), candidate->GetOwner()->GetEntityId());
    }

    // --- The engine's choreography ------------------------------------------

    bool PlayerDuelService::MayDispatchSlots(const SDK::wh::combatmodule::C_CombatActor *attacker) const {
        return Current().MayDispatchSlots(attacker, attacker != nullptr ? attacker->GetOpponent() : nullptr);
    }

    bool PlayerDuelService::MayWeighLethalVariant(const SDK::wh::combatmodule::C_CombatRPG *rpg) const {
        // A player's death is the referee's to rule on; a lethal variant
        // picked here would be a finisher played on a body nobody here gets to
        // kill.
        const SDK::wh::combatmodule::C_CombatActor *fighter = rpg != nullptr ? rpg->GetCombatActor() : nullptr;
        return Current().MayWeighLethal(fighter, fighter != nullptr ? fighter->GetOpponent() : nullptr);
    }

    bool PlayerDuelService::PostsBlocksOnly(const SDK::wh::combatmodule::C_CombatActor *attacker) const {
        return attacker != nullptr && Current().BlocksOnly(attacker->GetOpponent());
    }

    bool PlayerDuelService::BlocksShownSwing(const SDK::wh::combatmodule::C_CombatActor *fighter, const SDK::wh::combatmodule::I_CombatActorAction *action) const {
        if (_replaying || !SDK::wh::combatmodule::C_CombatActorActionBlock::IsInstance(action) || Current().IsAuthority(fighter)) {
            return false;
        }
        const SDK::wh::combatmodule::C_CombatActor *attacker = fighter->GetOpponent();
        const std::uint32_t swing                            = SwingOf(attacker);
        return swing != 0 && _blockedSwings.Of(attacker) >= swing;
    }

    PlayerDuelService::BlockSlotsScope::BlockSlotsScope(bool active): _previous(_blockSlotsOnly) {
        _blockSlotsOnly = active;
    }

    PlayerDuelService::BlockSlotsScope::~BlockSlotsScope() {
        _blockSlotsOnly = _previous;
    }

    bool PlayerDuelService::HidesSlot(std::uint32_t kind) {
        return _blockSlotsOnly && kind != SDK::wh::combatmodule::E_CombatSlotId::Block;
    }

    // --- Blows -------------------------------------------------------------

    bool PlayerDuelService::IsClaimedContact(const SDK::wh::combatmodule::C_CombatActor *attacker, const SDK::wh::combatmodule::C_CombatActor *victim) const {
        const Roster &roster = Current();
        return roster.IsLocal(attacker) && roster.IsRemote(victim) && roster.NpcIdentity(victim) == nullptr;
    }

    bool PlayerDuelService::IsBystanderContact(const SDK::wh::combatmodule::C_CombatActor *attacker, const SDK::wh::combatmodule::C_CombatActor *victim) const {
        const auto &roster = Current();
        return roster.ShouldDropContact(attacker, victim) || ((roster.NpcIdentity(attacker) != nullptr || roster.NpcIdentity(victim) != nullptr) && !roster.IsAuthority(victim));
    }

    bool PlayerDuelService::HitsShownBlow(const SDK::wh::combatmodule::C_CombatActor *fighter, const SDK::wh::combatmodule::I_CombatActorAction *action) const {
        const BlowCapture *capture = _blowCapture;
        if (_replaying || capture == nullptr || !capture->report.IsClaim() || fighter != capture->victim || !SDK::wh::combatmodule::C_CombatActorActionHit::IsInstance(action)) {
            return false;
        }
        const std::uint32_t swing = capture->report.attackGeneration;
        return swing != 0 && _hitSwings.Of(fighter) >= swing;
    }

    PlayerDuelService::ClaimScope::ClaimScope(PlayerDuelService &service, SDK::wh::combatmodule::C_CombatActor *attacker, SDK::wh::combatmodule::C_CombatActor *victim): _service(service), _previousCapture(_blowCapture) {
        const Roster &roster                        = service.Current();
        SDK::wh::entitymodule::C_Actor *victimActor = victim != nullptr ? victim->GetOwner() : nullptr;
        _capture.victim                             = victim;
        _capture.attacker                           = attacker;
        _capture.victimSoul                         = victimActor != nullptr ? victimActor->GetSoul() : nullptr;
        _capture.report.source                      = static_cast<std::uint8_t>(Shared::RPC::CombatHitSource::Melee);
        _capture.report.resolver                    = static_cast<std::uint8_t>(Shared::RPC::CombatHitResolver::Attacker);
        _capture.report.attackerNetworkId           = roster.LocalNetworkId();
        _capture.report.victimNetworkId             = roster.NetworkIdOf(victim);
        _capture.report.attackGeneration            = service._localSwingGeneration.load();
        _blowCapture                                = &_capture;
    }

    PlayerDuelService::ClaimScope::~ClaimScope() {
        _blowCapture = _previousCapture;
        if (_capture.report.CarriesBlow()) {
            _service.Report(_capture);
        }
    }

    PlayerDuelService::ResolutionScope::ResolutionScope(PlayerDuelService &service, SDK::wh::combatmodule::C_CombatActor *attacker, SDK::wh::combatmodule::C_CombatActor *victim): _service(service), _previous(_resolvingAttacker), _previousCapture(_blowCapture) {
        const Roster &roster = service.Current();
        _resolvingAttacker   = roster.ResolvingAttacker(attacker, victim);
        if (_resolvingAttacker == nullptr) {
            _blowCapture = nullptr;
            return;
        }

        SDK::wh::entitymodule::C_Actor *victimActor = victim != nullptr ? victim->GetOwner() : nullptr;
        _capture.victim                             = victim;
        _capture.attacker                           = _resolvingAttacker;
        _capture.victimSoul                         = victimActor != nullptr ? victimActor->GetSoul() : nullptr;
        _capture.report.source                      = static_cast<std::uint8_t>(Shared::RPC::CombatHitSource::Melee);
        _capture.report.attackerNetworkId           = roster.NetworkIdOf(_resolvingAttacker);
        _capture.report.attackGeneration            = service.SwingOf(_resolvingAttacker);
        if (const auto *fence = roster.NpcIdentity(victim)) {
            _capture.FenceNpcVictim(roster.NetworkIdOf(victim), fence->incarnation, fence->epoch, fence->revision, fence->maximumHealth);
        }
        if (const auto *fence = roster.NpcIdentity(_resolvingAttacker)) {
            _capture.FenceNpcAttacker(fence->incarnation, fence->revision);
        }
        _blowCapture = &_capture;
    }

    void PlayerDuelService::BlowCapture::FenceNpcVictim(std::uint64_t networkId, std::uint32_t incarnation, std::uint8_t epoch, std::uint32_t revision, float maximumHealth) {
        report.reporterNetworkId = networkId;
        report.incarnation       = incarnation;
        report.epoch             = epoch;
        report.reporterRevision  = revision;
        const float native       = victimSoul != nullptr ? victimSoul->GetStateMax(SDK::wh::rpgmodule::E_SoulState::Health) : 0.0F;
        healthScale              = native > 0.0F ? maximumHealth / native : 1.0F;
    }

    PlayerDuelService::ResolutionScope::~ResolutionScope() {
        _resolvingAttacker = _previous;
        _blowCapture       = _previousCapture;
        if (_capture.attacker == nullptr) {
            return;
        }

        // A contact can resolve into nothing. Its reactions still go out;
        // there is no blow to rule on.
        if (!_capture.report.CarriesBlow()) {
            _service.Publish(_capture.reactions);
            return;
        }
        _service.Report(_capture);
    }

    PlayerDuelService::MissileHitScope::MissileHitScope(PlayerDuelService &service, const SDK::wh::rpgmodule::C_Soul *victimSoul, std::uint64_t shooterNetworkId, std::uint64_t shotSequence): _service(service), _previousCapture(_blowCapture) {
        _capture.victimSoul               = victimSoul;
        _capture.report.source            = static_cast<std::uint8_t>(Shared::RPC::CombatHitSource::Missile);
        _capture.report.attackerNetworkId = shooterNetworkId;
        _capture.report.shotSequence      = shotSequence;
        if (auto *npc = KCDC::Common::FindReplicaById<KCDC::Features::Npc::Npc>(shooterNetworkId)) {
            _capture.FenceNpcAttacker(npc->spawnEpoch, npc->intentRevision);
        }
        if (const auto body = KCDC::Features::Npc::NpcService::FindCombatBodyOfSoul(victimSoul); body && body->simulating) {
            if (auto *npc = KCDC::Common::FindReplicaById<KCDC::Features::Npc::Npc>(body->networkId)) {
                _capture.FenceNpcVictim(npc->GetNetworkID(), npc->spawnEpoch, npc->stateEpoch, npc->intentRevision, npc->maxHealth);
            }
        }
        _blowCapture = &_capture;
    }

    PlayerDuelService::MissileHitScope::~MissileHitScope() {
        _blowCapture = _previousCapture;
        _service.Report(_capture);
    }

    void PlayerDuelService::OnCombatHitData(const SDK::wh::rpgmodule::S_CombatHitData *data) {
        BlowCapture *capture = _blowCapture;
        if (capture == nullptr || capture->hitData || capture->victimSoul == nullptr || data == nullptr || data->victim.soulWuid != capture->victimSoul->GetWuid()) {
            return;
        }
        Shared::RPC::CombatHitReport &report = capture->report;
        report.attackTypeId                  = data->attackTypeId;
        report.hitSubpart                    = static_cast<std::int16_t>(std::clamp(data->hitSubpart, -1, 0x7FFF));
        report.flags                         = static_cast<std::uint8_t>((data->blocked ? Shared::RPC::CombatHitFlags::kBlocked : 0) | (data->weakBlock ? Shared::RPC::CombatHitFlags::kWeakBlock : 0) | (data->perfectBlock ? Shared::RPC::CombatHitFlags::kPerfectBlock : 0)
                                                                         | (data->blockBroken ? Shared::RPC::CombatHitFlags::kBlockBroken : 0));

        // What the referee prices the blow from: the hit data, and the stamina
        // the victim met it with, before this blow spends any.
        Shared::RPC::CombatBlowInputs &inputs = report.inputs;
        inputs.damageTypes                    = static_cast<std::uint8_t>(data->damageTypes & Shared::RPC::CombatBlowInputs::kAllDamageTypes);
        inputs.staminaCoefficient             = std::isfinite(data->staminaCoefficient) ? std::clamp(data->staminaCoefficient, 0.0F, 1.0F) : 1.0F;
        inputs.energy                         = std::isfinite(data->energy) ? std::clamp(data->energy, 0.0F, 1.0F) : 1.0F;
        const float speed                     = std::sqrt(data->relativeVelocity[0] * data->relativeVelocity[0] + data->relativeVelocity[1] * data->relativeVelocity[1] + data->relativeVelocity[2] * data->relativeVelocity[2]);
        inputs.relativeSpeed                  = std::isfinite(speed) ? std::clamp(speed, 0.0F, Shared::RPC::CombatBlowInputs::kMaxRelativeSpeed) : 0.0F;
        inputs.attackCoefficient              = std::isfinite(data->attackCoefficient) ? std::clamp(data->attackCoefficient, 0.0F, Shared::RPC::CombatBlowInputs::kMaxAttackCoefficient) : 1.0F;
        inputs.angle                          = std::isfinite(data->angle) ? std::clamp(data->angle, -Shared::RPC::CombatBlowInputs::kMaxAngle, Shared::RPC::CombatBlowInputs::kMaxAngle) : 0.0F;
        inputs.mounted                        = data->mounted;
        inputs.counterAttack                  = data->counterAttack;
        // What `CalculateSwingAttack` reads off the attacker's soul as it
        // prices this very swing.
        SDK::wh::entitymodule::C_Actor *attackerActor  = capture->attacker != nullptr ? capture->attacker->GetOwner() : nullptr;
        const SDK::wh::rpgmodule::C_Soul *attackerSoul = attackerActor != nullptr ? attackerActor->GetSoul() : nullptr;
        inputs.attackCharged                           = attackerSoul != nullptr && attackerSoul->GetDerivedStatValue(SDK::wh::rpgmodule::E_SoulDerivedStat::AttackCharge) > 0.0F;
        const float stamina                            = capture->victimSoul->GetState(SDK::wh::rpgmodule::E_SoulState::Stamina);
        const float staminaMaximum                     = capture->victimSoul->GetStateMax(SDK::wh::rpgmodule::E_SoulState::Stamina);
        inputs.victimStamina                           = std::isfinite(stamina) ? std::clamp(stamina, 0.0F, Shared::RPC::CombatBlowInputs::kMaxStamina) : 0.0F;
        inputs.victimStaminaMaximum                    = std::isfinite(staminaMaximum) ? std::clamp(staminaMaximum, 0.0F, Shared::RPC::CombatBlowInputs::kMaxStamina) : 0.0F;
        report.hasInputs                               = report.Source() == Shared::RPC::CombatHitSource::Melee;
        capture->hitData                               = true;
    }

    bool PlayerDuelService::ScreenSoulWrite(const SDK::wh::rpgmodule::C_SoulStateStorage18070F3E8 *storage, SDK::wh::rpgmodule::E_SoulState state, float value, const SDK::wh::rpgmodule::C_SoulStateEffect *cause) {
        // A collision hit (`C_CombatSoul` slot 44, 0x1816C79F8) hurts both
        // bodies that ran into each other and names the other as the
        // attacker. A remote player's body here is moved by the wire rather
        // than by physics, so what it "runs into" is an artifact of the copy,
        // never a blow its owner dealt: none of it comes off the local body.
        if (const SDK::wh::rpgmodule::C_HealthValueEffect *damage = SDK::wh::rpgmodule::C_HealthValueEffect::FromCause(state, cause)) {
            SDK::wh::entitymodule::C_Actor *localActor = LocalActor();
            const bool localBody                       = storage != nullptr && localActor != nullptr && storage->GetSoul() == localActor->GetSoul();
            if (localBody && Shared::Game::ToDamageReason(damage->GetDeathReason()) == Shared::Game::DamageReason::Collision && Human::FindRemoteSoulWuid(damage->GetAttackerSoulWuid()) != nullptr) {
                return value < storage->GetState(state);
            }
        }

        BlowCapture *capture = _blowCapture;
        if (capture == nullptr || storage == nullptr || capture->victimSoul == nullptr || storage->GetSoul() != capture->victimSoul) {
            return false;
        }
        const float current = storage->GetState(state);
        if (!(value < current)) {
            return false;
        }

        Shared::RPC::CombatHitReport &report = capture->report;
        if (state == SDK::wh::rpgmodule::E_SoulState::Health) {
            report.healthLoss += current - std::max(value, 0.0F);
            report.engineLethal = report.engineLethal || value <= 0.0F;
            if (const SDK::wh::rpgmodule::C_HealthValueEffect *damage = SDK::wh::rpgmodule::C_HealthValueEffect::FromCause(state, cause)) {
                report.reason = static_cast<std::uint8_t>(Shared::Game::ToDamageReason(damage->GetDeathReason()));
                if (report.hitSubpart < 0) {
                    report.hitSubpart = static_cast<std::int16_t>(std::clamp(damage->GetHitSubpart(), -1, 0x7FFF));
                }
            }
            // A claim's copy keeps its owner's health through its own pin.
            return !report.IsClaim();
        }

        // Spent where it fell: the victim's next block is decided by it, and
        // that cannot wait for a ruling.
        if (state == SDK::wh::rpgmodule::E_SoulState::Stamina) {
            report.staminaLoss += current - value;
        }
        return false;
    }

    void PlayerDuelService::OnHitRecordQueued(const SDK::wh::combatmodule::S_CombatHitRecord *record) {
        BlowCapture *capture = _blowCapture;
        if (capture == nullptr || capture->report.hasImpact || capture->victim == nullptr || capture->attacker == nullptr || record == nullptr || !record->PlaysMaterialEffect()) {
            return;
        }
        SDK::wh::entitymodule::C_Actor *victimActor   = capture->victim->GetOwner();
        SDK::wh::entitymodule::C_Actor *attackerActor = capture->attacker->GetOwner();
        SDK::IEntity *victimEntity                    = victimActor != nullptr ? victimActor->GetEntity() : nullptr;
        SDK::IEntity *attackerEntity                  = attackerActor != nullptr ? attackerActor->GetEntity() : nullptr;
        if (victimEntity == nullptr || attackerEntity == nullptr || record->m_victimEntityId != victimEntity->GetId() || record->m_attackerEntityId != attackerEntity->GetId()) {
            return;
        }

        const SDK::Vec3 origin      = victimEntity->GetWorldPos();
        const bool jointIsAttackers = record->m_jointId != SDK::wh::combatmodule::S_CombatHitRecord::kNoPart && record->m_jointOwnerEntityId == attackerEntity->GetId();
        Shared::RPC::CombatImpact impact;
        impact.weaponKind      = static_cast<std::uint8_t>(record->m_weaponKind);
        impact.hitType         = static_cast<std::uint8_t>(record->m_hitType);
        impact.attackStyle     = static_cast<std::uint8_t>(record->m_attackStyle);
        impact.passed          = record->m_healthDamage != 0.0F;
        impact.sourceSurfaceId = static_cast<std::int16_t>(record->m_sourceSurfaceId);
        impact.targetSurfaceId = static_cast<std::int16_t>(record->m_targetSurfaceId);
        impact.bodyMaterialId  = static_cast<std::int16_t>(record->m_bodyMaterialId);
        impact.partId          = static_cast<std::int16_t>(record->m_partId);
        impact.attackerJointId = static_cast<std::int16_t>(jointIsAttackers ? record->m_jointId : SDK::wh::combatmodule::S_CombatHitRecord::kNoPart);
        impact.offset[0]       = record->m_position.x - origin.x;
        impact.offset[1]       = record->m_position.y - origin.y;
        impact.offset[2]       = record->m_position.z - origin.z;
        impact.normal[0]       = record->m_normal.x;
        impact.normal[1]       = record->m_normal.y;
        impact.normal[2]       = record->m_normal.z;
        impact.direction[0]    = record->m_direction.x;
        impact.direction[1]    = record->m_direction.y;
        impact.direction[2]    = record->m_direction.z;
        if (impact.IsSane()) {
            capture->report.hasImpact = true;
            capture->report.impact    = impact;
        }
    }

    void PlayerDuelService::OnActionRun(SDK::wh::combatmodule::C_CombatActor *fighter, SDK::wh::combatmodule::I_CombatActorAction *action) {
        if (_replaying || fighter == nullptr || action == nullptr) {
            return;
        }
        const Roster &roster = Current();
        // An attack RunAction let in replaces the one a combo's next strike
        // waits on, and the strike goes with it.
        if (roster.IsLocal(fighter) && SDK::wh::combatmodule::C_CombatActorActionAttack::IsInstance(action)) {
            std::scoped_lock lock(_publicationMutex);
            _comboStrikeQueued = false;
        }

        // Inside a claim: the reaction the engine ran on the copy of the
        // victim, and the recoil on the local body, are the claim's -- the
        // referee hands them to every other machine with its ruling.
        if (BlowCapture *capture = _blowCapture; capture != nullptr && capture->report.IsClaim()) {
            if (fighter == capture->victim && SDK::wh::combatmodule::C_CombatActorActionHit::IsInstance(action)) {
                const auto *hit                = static_cast<const SDK::wh::combatmodule::C_CombatActorActionHit *>(action);
                capture->report.hitRowIndex    = SDK::wh::combatmodule::C_CombatActionHitDatabase::GetInstance()->GetRowIndex(hit->GetRow());
                capture->report.hitRecordFlags = hit->GetRecordFlags();
                return;
            }
            if (fighter == capture->attacker && SDK::wh::combatmodule::C_CombatActorActionFailedAttack::IsInstance(action)) {
                const auto *failed             = static_cast<const SDK::wh::combatmodule::C_CombatActorActionFailedAttack *>(action);
                capture->report.recoilRowIndex = SDK::wh::combatmodule::C_CombatActionFailedAttackDatabase::GetInstance()->GetRowIndex(failed->GetRow());
                return;
            }
        }

        if (_blowCapture != nullptr && _resolvingAttacker != nullptr && fighter == _resolvingAttacker && SDK::wh::combatmodule::C_CombatActorActionFailedAttack::IsInstance(action)) {
            // The remote attacker's recoil, decided here against this body's
            // own guard. It is the referee's to hand on: the attacker's
            // machine runs it on the real body once the blow is ruled on.
            const auto *failed                  = static_cast<const SDK::wh::combatmodule::C_CombatActorActionFailedAttack *>(action);
            _blowCapture->report.recoilRowIndex = SDK::wh::combatmodule::C_CombatActionFailedAttackDatabase::GetInstance()->GetRowIndex(failed->GetRow());
            return;
        }

        Shared::RPC::CombatReaction reaction;
        if (roster.IsAuthority(fighter)) {
            const SDK::wh::combatmodule::C_CombatActor *opponent = fighter->GetOpponent();
            const bool opponentIsRemote                          = roster.IsNetworked(opponent);
            if (SDK::wh::combatmodule::C_CombatActorActionPerfectBlock::IsInstance(action)) {
                const auto *block = static_cast<const SDK::wh::combatmodule::C_CombatActorActionPerfectBlock *>(action);
                if (!opponentIsRemote) {
                    return;
                }
                reaction.kind              = Shared::RPC::CombatReaction::PerfectBlock;
                reaction.opponentNetworkId = roster.NetworkIdOf(opponent);
                reaction.rowIndex          = SDK::wh::combatmodule::C_CombatActionPerfectBlockDatabase::GetInstance()->GetRowIndex(block->GetPerfectBlockRow());
                reaction.pairedRowIndex    = SDK::wh::combatmodule::C_CombatActionAttackDatabase::GetInstance()->GetRowIndex(block->GetRiposteRow());
                reaction.variantFlags      = block->GetCandidateFlag() != 0 ? 1 : 0;
                _perfectBlockRows.Note(fighter, opponent, {reaction.rowIndex, reaction.pairedRowIndex, reaction.variantFlags});
            }
            else if (SDK::wh::combatmodule::C_CombatActorActionSyncPerfectBlock::IsInstance(action)) {
                const auto *block = static_cast<const SDK::wh::combatmodule::C_CombatActorActionSyncPerfectBlock *>(action);
                if (!opponentIsRemote) {
                    return;
                }
                reaction.kind              = Shared::RPC::CombatReaction::SyncPerfectBlock;
                reaction.opponentNetworkId = roster.NetworkIdOf(opponent);
                reaction.rowIndex          = SDK::wh::combatmodule::C_CombatActionPerfectBlockDatabase::GetInstance()->GetRowIndex(block->GetPerfectBlockRow());
                reaction.pairedRowIndex    = SDK::wh::combatmodule::C_CombatActionSyncPerfectBlockHitDatabase::GetInstance()->GetRowIndex(block->GetPartnerRow());
                reaction.variantFlags      = block->IsLethal() ? 1 : 0;
            }
            else if (SDK::wh::combatmodule::C_CombatActorActionBlock::IsInstance(action)) {
                // The block the trigger raised against the opponent's swing.
                const auto *block = static_cast<const SDK::wh::combatmodule::C_CombatActorActionBlock *>(action);
                if (!opponentIsRemote) {
                    return;
                }
                reaction.kind              = Shared::RPC::CombatReaction::Block;
                reaction.opponentNetworkId = roster.NetworkIdOf(opponent);
                reaction.rowIndex          = SDK::wh::combatmodule::C_CombatActionBlockDatabase::GetInstance()->GetRowIndex(block->GetRow());
                reaction.attackGeneration  = SwingOf(opponent);
            }
            else if (SDK::wh::combatmodule::C_CombatActorActionDodge::IsInstance(action)) {
                // The dodge the jump input asked for. An NPC's comes from its
                // own query, asked with other flags (0x18090C0D4), and is not
                // replayed.
                if (!opponentIsRemote || !roster.IsLocal(fighter)) {
                    return;
                }
                const SDK::wh::combatmodule::S_CombatActorActionDodgeParams &params = static_cast<const SDK::wh::combatmodule::C_CombatActorActionDodge *>(action)->GetParams();
                reaction.kind                                                       = Shared::RPC::CombatReaction::Dodge;
                reaction.opponentNetworkId                                          = roster.NetworkIdOf(opponent);
                reaction.rowIndex                                                   = SDK::wh::combatmodule::C_CombatActionDodgeDatabase::GetInstance()->GetRowIndex(params.m_pRow);
                reaction.direction[0]                                               = params.m_direction[0];
                reaction.direction[1]                                               = params.m_direction[1];
            }
            else if (SDK::wh::combatmodule::C_CombatActorActionHit::IsInstance(action)) {
                // Only a reaction to a remote player's blow resolved here is
                // this machine's to publish; the attacker is the one in the
                // scope, not whatever the model calls the opponent.
                if (_resolvingAttacker == nullptr) {
                    return;
                }
                const auto *hit            = static_cast<const SDK::wh::combatmodule::C_CombatActorActionHit *>(action);
                reaction.kind              = Shared::RPC::CombatReaction::Hit;
                reaction.opponentNetworkId = roster.NetworkIdOf(_resolvingAttacker);
                reaction.rowIndex          = SDK::wh::combatmodule::C_CombatActionHitDatabase::GetInstance()->GetRowIndex(hit->GetRow());
                reaction.variantFlags      = hit->GetRecordFlags();
            }
            else if (SDK::wh::combatmodule::C_CombatActorActionAttack::IsInstance(action) && !KCDC::Core::gApplication->GetPlayerService().IsCapturingLocalAttackCommit(fighter)) {
                // A swing the engine started without the attack input: the
                // riposte, a combo. Swings from the input already travel on
                // the CombatAttack stream. Its generation is taken when it is
                // sent, on the game thread, from the input's own count.
                const auto *attack = static_cast<const SDK::wh::combatmodule::C_CombatActorActionAttack *>(action);
                if (!opponentIsRemote) {
                    return;
                }
                const SDK::wh::combatmodule::S_CombatActionAttackDataDBData *row = attack->GetAttackData();
                reaction.kind                                                    = Shared::RPC::CombatReaction::Attack;
                reaction.opponentNetworkId                                       = roster.NetworkIdOf(opponent);
                reaction.rowIndex                                                = SDK::wh::combatmodule::C_CombatActionAttackDatabase::GetInstance()->GetRowIndex(row);
                reaction.candidateFlag0                                          = attack->GetCandidateFlag0();
                reaction.candidateFlag1                                          = attack->GetCandidateFlag1();
                reaction.contextFlag0                                            = attack->GetContextFlag0();
                reaction.contextFlag1                                            = attack->GetContextFlag1();
                reaction.stateMode                                               = attack->GetStateMode();
                reaction.attackSpeed                                             = TakeLocalAttackSpeed();
                reaction.windowMs                                                = row != nullptr ? Shared::RPC::CombatSwingWindowMs(row->GetTimeToHit(), row->GetTimeToWithdraw(), reaction.attackSpeed) : 0;
            }
            else {
                return;
            }
        }
        else {
            // A copy's own block, raised from the guard its owner holds
            // (`DispatchDueSlots_Hook`): that swing's block is shown here, and
            // the owner's, when it arrives, is not played again.
            if (SDK::wh::combatmodule::C_CombatActorActionBlock::IsInstance(action)) {
                _blockedSwings.Note(fighter->GetOpponent(), SwingOf(fighter->GetOpponent()));
            }
            return;
        }

        // A defence of the local body's, for the answer to a claim on it.
        if (roster.IsLocal(fighter) && (reaction.kind == Shared::RPC::CombatReaction::PerfectBlock || reaction.kind == Shared::RPC::CombatReaction::SyncPerfectBlock)) {
            std::scoped_lock history(_guardHistoryMutex);
            _guardHistory.NotePerfectBlock(MafiaNet::GetTime(), reaction.opponentNetworkId);
        }
        PublishActionReaction(fighter, reaction);
    }

    void PlayerDuelService::PublishActionReaction(const SDK::wh::combatmodule::C_CombatActor *fighter, Shared::RPC::CombatReaction reaction) {
        const Roster &roster = Current();
        if (const auto *fence = roster.NpcIdentity(fighter)) {
            reaction.actorNetworkId       = roster.NetworkIdOf(fighter);
            reaction.incarnation          = fence->incarnation;
            reaction.epoch                = fence->epoch;
            reaction.combatRevision       = fence->revision;
            reaction.presentationSequence = _nextNpcPresentation.fetch_add(1);
            if (reaction.kind == Shared::RPC::CombatReaction::Attack || reaction.kind == Shared::RPC::CombatReaction::SyncPerfectBlock || reaction.kind == Shared::RPC::CombatReaction::Riposte) {
                reaction.attackGeneration = _nextNpcSwing.fetch_add(1);
                if (reaction.kind != Shared::RPC::CombatReaction::Attack) {
                    reaction.windowMs = Shared::RPC::CombatReaction::kSyncStrikeWindowMs;
                }
                _swingGenerations.Note(fighter, reaction.attackGeneration);
            }
        }
        PublishResolved(reaction);
    }

    void PlayerDuelService::OnRiposteRequested(SDK::wh::combatmodule::C_CombatActor *fighter) {
        const Roster &roster                                 = Current();
        const SDK::wh::combatmodule::C_CombatActor *opponent = fighter != nullptr ? fighter->GetOpponent() : nullptr;
        const auto block                                     = _perfectBlockRows.Find(fighter, opponent);
        if (_replaying || !roster.CanPublishReaction(fighter, opponent) || block.row == Shared::RPC::CombatReaction::kInvalidRowIndex) {
            return;
        }
        // The whole perfect block goes with its counter: a receiver whose copy
        // is not playing it rebuilds it from these (`ApplyRemoteReaction`).
        Shared::RPC::CombatReaction reaction;
        reaction.kind              = Shared::RPC::CombatReaction::Riposte;
        reaction.opponentNetworkId = roster.NetworkIdOf(opponent);
        reaction.rowIndex          = block.row;
        reaction.pairedRowIndex    = block.riposteRow;
        reaction.variantFlags      = block.candidateFlags;
        PublishActionReaction(fighter, reaction);
    }

    void PlayerDuelService::PublishResolved(const Shared::RPC::CombatReaction &reaction) {
        // Inside a resolution, held so it goes out with the blow.
        if (_blowCapture != nullptr) {
            _blowCapture->reactions.push_back(reaction);
            return;
        }
        Publish(reaction);
    }

    void PlayerDuelService::Publish(const std::vector<Shared::RPC::CombatReaction> &reactions) {
        std::scoped_lock lock(_publicationMutex);
        _publications.insert(_publications.end(), reactions.begin(), reactions.end());
    }

    void PlayerDuelService::Publish(const Shared::RPC::CombatReaction &reaction) {
        // The engine may have run the action on a combat-group worker; the
        // stream it goes out on is the game thread's.
        std::scoped_lock lock(_publicationMutex);
        _publications.push_back(reaction);
    }

    void PlayerDuelService::AnswerClaim(const Shared::RPC::CombatClaimQuery &query) {
        Shared::RPC::CombatClaimAnswer answer;
        {
            std::scoped_lock history(_guardHistoryMutex);
            answer = _guardHistory.Judge(query.attackerNetworkId, query.attackGeneration);
        }
        answer.queryId = query.queryId;
        auto *net      = Framework::CoreModules::GetNetworkPeer();
        if (net != nullptr) {
            net->BroadcastRPC(answer, MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered, MafiaNet::UNASSIGNED_RAKNET_GUID, Framework::Networking::Channel::Presentation);
        }
    }

    void PlayerDuelService::Report(BlowCapture &capture) {
        capture.report.healthLoss *= capture.healthScale;
        const Shared::RPC::CombatHitReport &report = capture.report;
        std::scoped_lock lock(_publicationMutex);
        if (report.IsClaim()) {
            _recentClaims[{report.victimNetworkId, report.attackGeneration}] = RecentClaim {MafiaNet::GetTime(), report.recoilRowIndex != Shared::RPC::CombatHitReport::kNoRow, report.hitRowIndex};
        }
        for (Shared::RPC::CombatReaction &reaction : capture.reactions) {
            // A reaction to a blow names the blow's swing: the attacker's
            // machine tells from it whether it already played that blow.
            if (reaction.kind == Shared::RPC::CombatReaction::Hit) {
                reaction.attackGeneration = report.attackGeneration;
            }
            _publications.push_back(reaction);
        }
        _reports.push_back(report);
    }

    void PlayerDuelService::DrainNpcReactions(std::uint64_t now) {
        {
            std::scoped_lock lock(_publicationMutex);
            _npcReactions.insert(_npcReactions.end(), _incomingNpcReactions.begin(), _incomingNpcReactions.end());
            _incomingNpcReactions.clear();
        }
        std::erase_if(_lastNpcPresentation, [](const auto &entry) {
            return KCDC::Common::FindReplicaById<KCDC::Features::Npc::Npc>(entry.first) == nullptr;
        });
        // An NPC's actions replay in order: one that has to wait holds back
        // everything after it on the same body.
        std::unordered_set<std::uint64_t> deferred;
        std::erase_if(_npcReactions, [this, now, &deferred](const PendingNpcReaction &pending) {
            const auto &reaction = pending.reaction;
            auto *npc            = KCDC::Common::FindReplicaById<KCDC::Features::Npc::Npc>(reaction.actorNetworkId);
            if (now - pending.receivedAt > kNpcReactionLifetimeMs || npc == nullptr || npc->IsSimulating()) {
                return true;
            }
            if (deferred.contains(reaction.actorNetworkId)) {
                return false;
            }
            const auto defer = [&] {
                deferred.insert(reaction.actorNetworkId);
                return false;
            };
            // Ahead of this replica: its next life or order has not arrived yet.
            if (static_cast<std::int32_t>(reaction.incarnation - npc->spawnEpoch) > 0 || (npc->spawnEpoch == reaction.incarnation && static_cast<std::int32_t>(reaction.combatRevision - npc->intentRevision) > 0)) {
                return defer();
            }
            if (npc->spawnEpoch != reaction.incarnation || npc->intentRevision != reaction.combatRevision || npc->stateEpoch != reaction.epoch || !npc->alive) {
                return true;
            }
            NpcPresentation &last = _lastNpcPresentation[reaction.actorNetworkId];
            if (last.incarnation != reaction.incarnation || last.revision != reaction.combatRevision) {
                last = {reaction.incarnation, reaction.combatRevision, 0};
            }
            if (reaction.presentationSequence <= last.sequence) {
                return true;
            }
            if (npc->ResolveActor() == nullptr || (reaction.relayedAt != 0 && npc->CombatRenderTime() < reaction.relayedAt)) {
                return defer();
            }
            const auto result = ApplyRemoteReaction(reaction);
            if (result == ReactionResult::Deferred) {
                return defer();
            }
            last.sequence = reaction.presentationSequence;
            return true;
        });
    }

    void PlayerDuelService::FlushPublications() {
        {
            std::scoped_lock lock(_publicationMutex);
            _sendingPublications.swap(_publications);
            _sendingReports.swap(_reports);
        }
        PlayerService &players = KCDC::Core::gApplication->GetPlayerService();
        for (Shared::RPC::CombatReaction &reaction : _sendingPublications) {
            if (reaction.actorNetworkId != 0) {
                auto *npc = KCDC::Common::FindReplicaById<KCDC::Features::Npc::Npc>(reaction.actorNetworkId);
                // An action from an order or a life this client no longer
                // simulates is the next simulator's to tell, not this one's.
                if (npc != nullptr && npc->IsSimulating() && npc->spawnEpoch == reaction.incarnation && npc->stateEpoch == reaction.epoch && npc->intentRevision == reaction.combatRevision && reaction.IsSane()) {
                    Shared::RPC::NpcCombatReaction message {reaction};
                    Framework::CoreModules::GetNetworkPeer()->BroadcastRPC(message, MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered, MafiaNet::UNASSIGNED_RAKNET_GUID, Framework::Networking::Channel::Presentation);
                }
                continue;
            }
            if (reaction.rowIndex == Shared::RPC::CombatReaction::kInvalidRowIndex || (reaction.kind != Shared::RPC::CombatReaction::Attack && reaction.opponentNetworkId == 0)) {
                Framework::Logging::GetLogger(kLogger)->error("A {} the engine ran could not be named for the wire: row={}, opponent={}", reaction.KindName(), reaction.rowIndex, reaction.opponentNetworkId);
                continue;
            }
            if (reaction.kind == Shared::RPC::CombatReaction::Attack) {
                reaction.attackGeneration = players.TakeCombatAttackGeneration();
                NoteLocalSwing(reaction.attackGeneration);
            }
            // A master strike and a riposte strike back: a blow their blade
            // lands is named by a generation, as a swing's is.
            if (reaction.kind == Shared::RPC::CombatReaction::SyncPerfectBlock || reaction.kind == Shared::RPC::CombatReaction::Riposte) {
                reaction.attackGeneration = players.TakeCombatAttackGeneration();
                reaction.windowMs         = Shared::RPC::CombatReaction::kSyncStrikeWindowMs;
                NoteLocalSwing(reaction.attackGeneration);
            }
            if (!players.CompleteCombatReactionFence(reaction) || !KCDC::Core::PresentationModule::Get().QueueLocalCombatReaction(reaction)) {
                Framework::Logging::GetLogger(kLogger)->warn("Could not queue a local {}: opponent={}, row={}", reaction.KindName(), reaction.opponentNetworkId, reaction.rowIndex);
            }
        }

        _sendingPublications.clear();

        auto *net = Framework::CoreModules::GetNetworkPeer();
        for (Shared::RPC::CombatHitReport &report : _sendingReports) {
            if ((report.reporterNetworkId == 0 && !players.CompleteCombatHitReportFence(report)) || !report.IsSane() || net == nullptr) {
                // Withheld and unreportable: a blow from a swing this machine
                // never saw started, or a body between lives. It takes nothing.
                Framework::Logging::GetLogger(kLogger)->warn("Could not report a {} blow: attacker={}, generation={}, shot={}, health={:.2f}", report.Source() == Shared::RPC::CombatHitSource::Missile ? "missile" : "melee", report.attackerNetworkId, report.attackGeneration,
                    report.shotSequence, report.healthLoss);
                continue;
            }
            net->BroadcastRPC(report, MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered, MafiaNet::UNASSIGNED_RAKNET_GUID, Framework::Networking::Channel::Presentation);
        }
        _sendingReports.clear();
    }

    // --- The referee's rulings -------------------------------------------------

    void PlayerDuelService::QueueRuling(const Shared::RPC::CombatOutcome &outcome) {
        std::scoped_lock lock(_rulingMutex);
        if (_incomingRulings.size() < kMaxQueuedRulings) {
            _incomingRulings.push_back(outcome);
        }
    }

    void PlayerDuelService::DrainRulings(std::uint64_t now) {
        {
            std::scoped_lock lock(_rulingMutex);
            _drainingRulings.swap(_incomingRulings);
        }

        for (const Shared::RPC::CombatOutcome &outcome : _drainingRulings) {
            if (outcome.sequence <= _lastRuling) {
                continue;
            }
            _lastRuling = outcome.sequence;
            PendingRuling ruling;
            ruling.outcome    = outcome;
            ruling.receivedAt = now;
            _rulings.push_back(ruling);
        }
        _drainingRulings.clear();

        // In the order the referee made them.
        std::erase_if(_rulings, [this, now](PendingRuling &ruling) {
            return ApplyRuling(ruling, now);
        });
    }

    bool PlayerDuelService::ApplyRuling(PendingRuling &ruling, std::uint64_t now) {
        const Shared::RPC::CombatOutcome &outcome = ruling.outcome;
        if (WasRuledCancelled(outcome.attackerNetworkId, outcome.victimNetworkId, outcome.ruledAt)) {
            return true;
        }
        const std::uint64_t local                 = Current().LocalNetworkId();

        // The machine that resolved the blow ran its consequences on its own
        // bodies at contact; everyone plays what is left. The victim always
        // takes the health, the one thing nobody took at contact.
        const bool victimHere    = Current().IsAuthority(FighterFor(Current(), outcome.victimNetworkId, false));
        const bool attackerHere  = Current().IsAuthority(FighterFor(Current(), outcome.attackerNetworkId, false));
        const bool resolvedHere  = outcome.Resolver() == Shared::RPC::CombatHitResolver::Victim ? victimHere : attackerHere;
        const std::uint64_t wait = now - ruling.receivedAt;
        const bool ruledHit      = outcome.Resolver() == Shared::RPC::CombatHitResolver::Attacker && outcome.hitRowIndex != Shared::RPC::CombatHitReport::kNoRow;

        // A blow lands after the swing that dealt it is seen. The swing of an
        // attacker this machine only draws plays from its presentation stream,
        // which waits on the attacker's replicated combat state and drawn
        // pose; a ruling skips both -- a claim, ruled on the attacker's word,
        // reached its victim before the attacker's copy there had swung. So a
        // melee ruling waits for that copy to play the swing, which it hurries,
        // for at most `kRuledSwingWaitMs`.
        if (!attackerHere && outcome.source == static_cast<std::uint8_t>(Shared::RPC::CombatHitSource::Melee) && outcome.attackGeneration != 0 && wait < kRuledSwingWaitMs) {
            const SDK::wh::combatmodule::C_CombatActor *attacker = FighterFor(Current(), outcome.attackerNetworkId, false);
            const std::uint32_t swung                            = attacker != nullptr ? _swingGenerations.Of(attacker) : 0;
            if (swung == 0 || swung < outcome.attackGeneration) {
                KCDC::Core::PresentationModule::Get().HurryCombatAttack(outcome.attackerNetworkId, outcome.attackGeneration);
                return false;
            }
        }
        if (victimHere && !ruling.healthApplied) {
            ruling.healthApplied = true;
            TakeRuledHealth(outcome);
        }
        if (resolvedHere) {
            // A claim this machine resolved reacted on the copy at contact,
            // unless the copy was busy; the ruled hit, the reaction the
            // owner's body plays, shows the blow then, and over a reaction
            // still playing the engine's own rule decides (RunRuledHit). It is
            // the contact's own row: once that played, it is not played again.
            RecentClaim claim;
            const bool shownAtContact = FindRecentClaim(outcome.victimNetworkId, outcome.attackGeneration, now, claim) && claim.hitRowIndex == outcome.hitRowIndex;
            if (ruledHit && !victimHere && outcome.victimNetworkId != local && !shownAtContact) {
                RunRuledHit(outcome);
            }
            return true;
        }

        // What the local player's own claim of this blow already played
        // here: the copy's reaction and the impact, and maybe the recoil.
        RecentClaim claim;
        const bool claimedHere = attackerHere && FindRecentClaim(outcome.victimNetworkId, outcome.attackGeneration, now, claim);

        // The blow itself, to the victim's body here: the real one after a
        // claim, a copy everywhere else. A body that is down reacted to the
        // blow that put it there. However late it comes it is played: it is
        // all anyone but the resolver ever sees of the blow, and a claim
        // reaches its victim only after the server asked it (a round trip
        // and more), so no lateness bound can tell a lost blow from a slow
        // one.
        if (!ruling.impactPlayed) {
            ruling.impactPlayed = true;
            // A claim's ruled hit below replays the combat reaction itself;
            // the record then carries only the impact, or the body would
            // react to the one blow twice. A blow that may kill is the
            // exception: through its reaction flag the record is the engine's
            // own route for it, a reaction or, on a body that died, the dead
            // action, so the record reacts and the ruled hit is not run.
            if (outcome.hasImpact && !claimedHere && !IsActorDown(BodyOf(outcome.victimNetworkId, local))) {
                // A copy reacts to a claim through the ruled hit below, and
                // to a melee blow its victim resolved through the hit
                // reaction the victim's machine published
                // (`ApplyRemoteReaction`); a missile's resolution publishes
                // none (`MissileHitScope` names no resolving attacker), so
                // the record reacts for it. The record's reaction also stays
                // for a blow that may kill, the engine's own route to the
                // dead action.
                const bool reacts = outcome.mayKill || (!ruledHit && (outcome.victimNetworkId == local || outcome.source == static_cast<std::uint8_t>(Shared::RPC::CombatHitSource::Missile)));
                if (DeliverRuledHit(outcome, reacts)) {
                    ruling.recordReacted = reacts;
                }
                else {
                    Framework::Logging::GetLogger(kLogger)->warn("Dropped a ruled impact with no bodies to play it on: ruling={}, attacker={}, victim={}", outcome.sequence, outcome.attackerNetworkId, outcome.victimNetworkId);
                }
            }
        }

        // A claim's reaction lands on the victim's body here: the real one on
        // the victim's machine, a copy everywhere else.
        if (!ruling.hitPlayed && ruledHit && !ruling.recordReacted) {
            const ReactionResult hit = RunRuledHit(outcome);
            ruling.hitPlayed         = hit != ReactionResult::Deferred || wait >= kRecoilRetryBudgetMs;
        }
        else {
            ruling.hitPlayed = true;
        }

        // A recoil the local body already ran at its own claim of the blow is
        // not run again.
        if (!ruling.recoilPlayed && outcome.recoilRowIndex != Shared::RPC::CombatHitReport::kNoRow && !(claimedHere && claim.recoilPlayed)) {
            const ReactionResult result = RunRuledRecoil(outcome);
            if (result == ReactionResult::Deferred && wait >= kRecoilRetryBudgetMs) {
                Framework::Logging::GetLogger(kLogger)->warn("Expired a ruled recoil: ruling={}, attacker={}, victim={}, row={}, waitedMs={}", outcome.sequence, outcome.attackerNetworkId, outcome.victimNetworkId, outcome.recoilRowIndex, wait);
            }
            ruling.recoilPlayed = result != ReactionResult::Deferred || wait >= kRecoilRetryBudgetMs;
        }
        else {
            ruling.recoilPlayed = true;
        }
        return ruling.hitPlayed && ruling.recoilPlayed;
    }

    void PlayerDuelService::TakeRuledHealth(const Shared::RPC::CombatOutcome &outcome) {
        if (KCDC::Common::FindReplicaById<KCDC::Features::Npc::Npc>(outcome.victimNetworkId) != nullptr) {
            return;
        }
        Human *owner = KCDC::Features::Player::Human::FindLocal();
        // A ruling about another life of this body takes nothing from this one.
        if (owner == nullptr || owner->GetNetworkID() != outcome.victimNetworkId || owner->soul.incarnation != outcome.victimIncarnation || owner->stateEpoch != outcome.victimEpoch) {
            return;
        }
        SDK::wh::entitymodule::C_Actor *actor = LocalActor();
        SDK::wh::rpgmodule::C_Soul *soul      = actor != nullptr ? actor->GetSoul() : nullptr;
        if (soul == nullptr || soul->IsDead()) {
            return;
        }

        // The server raised this damage itself as it ruled.
        const KCDC::Core::LocalEvents::RuledDamageScope ruled(KCDC::Core::gApplication->GetLocalEvents());

        // After a claim nothing here spent the blow's stamina: it was the
        // attacker's engine that resolved it.
        if (outcome.Resolver() == Shared::RPC::CombatHitResolver::Attacker && outcome.staminaLoss > 0.0F) {
            const float stamina = soul->GetState(SDK::wh::rpgmodule::E_SoulState::Stamina);
            soul->SetState(SDK::wh::rpgmodule::E_SoulState::Stamina, std::max(stamina - outcome.staminaLoss, 0.0F));
        }

        if (!(outcome.healthLoss > 0.0F)) {
            return;
        }

        // A blow that cannot kill stops where the engine's own clamp would
        // have stopped it, at the edge.
        float amount       = outcome.healthLoss;
        const float health = soul->GetState(SDK::wh::rpgmodule::E_SoulState::Health);
        if (!outcome.mayKill && amount >= health) {
            amount = std::max(health - kHealthLeftByNonLethalBlow, 0.0F);
        }
        if (!(amount > 0.0F)) {
            return;
        }

        // Through the game's own damage primitive when it kills, so the death
        // is declared and reacted to the native way, and written exactly
        // otherwise: the blow was already resolved against this body.
        KCDC::Common::Replication::BodyLife::Get().ApplyBlow(actor, outcome.attackerNetworkId, amount, outcome.reason);
    }

    PlayerDuelService::ReactionResult PlayerDuelService::RunRuledRecoil(const Shared::RPC::CombatOutcome &outcome) {
        const auto *row = SDK::wh::combatmodule::C_CombatActionFailedAttackDatabase::GetInstance()->GetRow(outcome.recoilRowIndex);
        if (row == nullptr) {
            return ReactionResult::Obsolete;
        }

        const Roster &roster = Current();
        const bool local     = outcome.attackerNetworkId == roster.LocalNetworkId();
        if (!local) {
            if (BodyOf(outcome.attackerNetworkId, roster.LocalNetworkId()) == nullptr) {
                return ReactionResult::Obsolete;
            }
        }
        SDK::wh::combatmodule::C_CombatActor *fighter = FighterFor(roster, outcome.attackerNetworkId, true);
        SDK::wh::combatmodule::C_CombatActor *victim  = FighterFor(roster, outcome.victimNetworkId, true);
        if (fighter == nullptr || fighter->GetActionManager() == nullptr || victim == nullptr) {
            return ReactionResult::Deferred;
        }
        if (IsDown(fighter)) {
            return ReactionResult::Obsolete;
        }

        const SDK::wh::shared::C_ScopedRecursiveSRWLock lock(fighter->GetCombatGroupLock());

        // The recoil reads its owner's opponent. A copy that is already locked
        // on to the victim is given it now rather than at its next rest; the
        // local body keeps whatever its own picker chose.
        if (fighter->GetOpponent() != victim) {
            SDK::wh::entitymodule::C_Actor *victimActor = victim->GetOwner();
            SDK::IEntity *victimEntity                  = victimActor != nullptr ? victimActor->GetEntity() : nullptr;
            if (local || victimEntity == nullptr || fighter->GetTargetEntity() != victimEntity) {
                if (!local || fighter->GetOpponent() == nullptr) {
                    return ReactionResult::Deferred;
                }
            }
            else {
                fighter->SetOpponent(victimEntity);
                if (fighter->GetOpponent() != victim) {
                    return ReactionResult::Deferred;
                }
            }
        }

        auto *failed = SDK::wh::combatmodule::C_CombatActorActionFailedAttack::Allocate(fighter);
        if (failed == nullptr) {
            return ReactionResult::Deferred;
        }
        failed->Prepare(row);
        return RunReplay(fighter, failed) ? ReactionResult::Applied : ReactionResult::Deferred;
    }

    PlayerDuelService::ReactionResult PlayerDuelService::RunRuledHit(const Shared::RPC::CombatOutcome &outcome) {
        const auto *row = SDK::wh::combatmodule::C_CombatActionHitDatabase::GetInstance()->GetRow(outcome.hitRowIndex);
        if (row == nullptr) {
            return ReactionResult::Obsolete;
        }
        const Roster &roster = Current();
        if (outcome.victimNetworkId != roster.LocalNetworkId()) {
            if (BodyOf(outcome.victimNetworkId, roster.LocalNetworkId()) == nullptr) {
                return ReactionResult::Obsolete;
            }
        }
        SDK::wh::combatmodule::C_CombatActor *fighter  = FighterFor(roster, outcome.victimNetworkId, true);
        SDK::wh::combatmodule::C_CombatActor *attacker = FighterFor(roster, outcome.attackerNetworkId, true);
        if (fighter == nullptr || fighter->GetActionManager() == nullptr || attacker == nullptr) {
            return ReactionResult::Deferred;
        }
        if (IsDown(fighter)) {
            return ReactionResult::Obsolete;
        }

        const SDK::wh::shared::C_ScopedRecursiveSRWLock lock(fighter->GetCombatGroupLock());
        auto *hit = SDK::wh::combatmodule::C_CombatActorActionHit::Allocate(fighter);
        if (hit == nullptr) {
            return ReactionResult::Deferred;
        }
        hit->Prepare(row, attacker, outcome.hitRecordFlags);

        // Over a flinch still playing or a move paired with another body, the
        // engine's own rule decides, as it does for every blow: a running hit
        // holds its slot until its clip ends, and a paired move is not cut
        // while its partner plays on. A blow it refuses lands unseen, as it
        // would have offline.
        const SDK::wh::combatmodule::I_CombatActorAction *running = fighter->GetRunningAction(hit->GetSlot());
        const bool held                                           = running != nullptr && (SDK::wh::combatmodule::C_CombatActorActionHit::IsInstance(running) || SDK::wh::combatmodule::IsSyncAction(running));
        if (held) {
            return RunReplay(fighter, hit, false) ? ReactionResult::Applied : ReactionResult::Obsolete;
        }
        return RunReplay(fighter, hit) ? ReactionResult::Applied : ReactionResult::Deferred;
    }

    bool PlayerDuelService::FindRecentClaim(std::uint64_t victimNetworkId, std::uint32_t generation, std::uint64_t now, RecentClaim &claim) {
        std::scoped_lock lock(_publicationMutex);
        const auto found = _recentClaims.find({victimNetworkId, generation});
        if (found == _recentClaims.end() || now - found->second.at > kClaimedReactionWindowMs) {
            return false;
        }
        claim = found->second;
        return true;
    }

    bool PlayerDuelService::DeliverRuledHit(const Shared::RPC::CombatOutcome &outcome, bool runsReaction) const {
        const Roster &roster                          = Current();
        SDK::wh::entitymodule::C_Actor *victim        = BodyOf(outcome.victimNetworkId, roster.LocalNetworkId());
        SDK::wh::entitymodule::C_Actor *attacker      = BodyOf(outcome.attackerNetworkId, roster.LocalNetworkId());
        SDK::IEntity *victimEntity                    = victim != nullptr ? victim->GetEntity() : nullptr;
        SDK::IEntity *attackerEntity                  = attacker != nullptr ? attacker->GetEntity() : nullptr;
        // The effect row is named after the striking weapon's class
        // (`BuildHitMaterialEffectName` reads the record's weapon item), so the
        // attacker's combat side is built for a copy that struck without ever
        // squaring up here, as after a respawn or a teleport.
        SDK::wh::combatmodule::C_CombatActor *fighter = BuiltFighter(attacker);
        auto *framework                               = SDK::IGameFramework::GetInstance();
        SDK::IGameRulesSystem *rules                  = framework != nullptr ? framework->GetIGameRulesSystem() : nullptr;
        SDK::IGameRules *gameRules                    = rules != nullptr ? rules->GetCurrentGameRules() : nullptr;
        if (victimEntity == nullptr || attackerEntity == nullptr || fighter == nullptr || gameRules == nullptr) {
            return false;
        }

        // The record the victim's handler would have had here: this machine's
        // two bodies, and the weapon this machine's copy of the attacker
        // holds, so the effect row is named as it is everywhere else.
        const Shared::RPC::CombatImpact &impact         = outcome.impact;
        const SDK::Vec3 origin                          = victimEntity->GetWorldPos();
        SDK::wh::combatmodule::S_CombatHitRecord record = SDK::wh::combatmodule::S_CombatHitRecord::Make();
        record.m_attackerEntityId                       = attackerEntity->GetId();
        record.m_weaponItemId                           = fighter->GetModel()->GetStrikingWeaponId();
        record.m_weaponKind                             = impact.weaponKind;
        record.m_sourceSurfaceId                        = impact.sourceSurfaceId;
        record.m_hitType                                = impact.hitType;
        record.m_attackStyle                            = impact.attackStyle;
        record.m_victimEntityId                         = victimEntity->GetId();
        record.m_healthDamage                           = outcome.healthLoss > 0.0F ? outcome.healthLoss : (impact.passed ? 1.0F : 0.0F);
        record.m_staminaDamage                          = outcome.staminaLoss;
        record.m_bodyMaterialId                         = impact.bodyMaterialId;
        record.m_targetSurfaceId                        = impact.targetSurfaceId;
        record.m_partId                                 = impact.partId;
        if (impact.attackerJointId != SDK::wh::combatmodule::S_CombatHitRecord::kNoPart) {
            record.m_jointOwnerEntityId = attackerEntity->GetId();
            record.m_jointId            = impact.attackerJointId;
        }
        record.m_position  = SDK::Vec3 {origin.x + impact.offset[0], origin.y + impact.offset[1], origin.z + impact.offset[2]};
        record.m_normal    = SDK::Vec3 {impact.normal[0], impact.normal[1], impact.normal[2]};
        record.m_direction = SDK::Vec3 {impact.direction[0], impact.direction[1], impact.direction[2]};
        if (!runsReaction) {
            record.m_presentationFlags &= ~SDK::wh::combatmodule::S_CombatHitRecord::kRunsHitReaction;
        }
        gameRules->QueueHitRecord(&record);
        return true;
    }

    // --- Timing --------------------------------------------------------------

    float PlayerDuelService::FilterAttackSpeed(const SDK::wh::combatmodule::C_CombatRPG *rpg, float nativeSpeed) {
        const SDK::wh::combatmodule::C_CombatActor *fighter = rpg != nullptr ? rpg->GetCombatActor() : nullptr;
        if (float forced = 0.0F; _forcedAttackSpeeds.Take(fighter, forced)) {
            return forced;
        }
        if (Current().IsAuthority(fighter)) {
            _localAttackSpeed = nativeSpeed;
        }
        return nativeSpeed;
    }

    float PlayerDuelService::TakeLocalAttackSpeed() {
        return std::exchange(_localAttackSpeed, 0.0F);
    }

    void PlayerDuelService::ForceNextAttackSpeed(const SDK::wh::combatmodule::C_CombatActor *fighter, float attackSpeed) {
        _forcedAttackSpeeds.Force(fighter, attackSpeed);
    }

    void PlayerDuelService::ForgetForcedAttackSpeed(const SDK::wh::combatmodule::C_CombatActor *fighter) {
        _forcedAttackSpeeds.Forget(fighter);
    }

    void PlayerDuelService::HoldLocalSwing(SDK::wh::combatmodule::C_CombatActorTimeCop *timeCop) const {
        SDK::wh::combatmodule::C_CombatActor *fighter = timeCop != nullptr ? timeCop->GetCombatActor() : nullptr;
        if (!IsLocalFighter(fighter) || (!IsRemoteFighter(fighter->GetOpponent()) && _localSwingTarget.load() == 0)) {
            return;
        }

        // Held back by the round trip the defender's answer takes (see
        // SwingHoldSeconds), the blow lands here as the answer does, and the
        // engine's own delay keeps the clip and the contact countdown together.
        std::int32_t roundTripMs = 0;
        if (!KCDC::Common::ServerRoundTripMs(roundTripMs)) {
            return;
        }
        const float holdSeconds = SwingHoldSeconds(roundTripMs, kSwingHoldMarginSeconds);
        const SDK::wh::shared::C_ScopedRecursiveSRWLock lock(fighter->GetCombatGroupLock());
        timeCop->ApplySlotDelay(holdSeconds, -1.0F);
    }

    void PlayerDuelService::OnComboStrikeQueued(const SDK::wh::combatmodule::C_CombatActorActionAttack *running, const SDK::wh::combatmodule::I_CombatActorAction *next) {
        // The slot is shared with the sync attack, and the handler may build a
        // sync attack as the strike: a paired move is not a swing a copy can
        // replay on its own. One the running attack already has stays, as the
        // engine keeps it.
        if (!SDK::wh::combatmodule::C_CombatActorActionAttack::IsInstance(running) || !SDK::wh::combatmodule::C_CombatActorActionAttack::IsInstance(next) || running->HasNextAction() || !Current().IsLocal(running->GetCombatActor())) {
            return;
        }
        const auto *strike = static_cast<const SDK::wh::combatmodule::C_CombatActorActionAttack *>(next);
        Shared::RPC::CombatReaction reaction;
        reaction.kind           = Shared::RPC::CombatReaction::Attack;
        reaction.rowIndex       = SDK::wh::combatmodule::C_CombatActionAttackDatabase::GetInstance()->GetRowIndex(strike->GetAttackData());
        reaction.candidateFlag0 = strike->GetCandidateFlag0();
        reaction.candidateFlag1 = strike->GetCandidateFlag1();
        reaction.contextFlag0   = strike->GetContextFlag0();
        reaction.contextFlag1   = strike->GetContextFlag1();
        reaction.stateMode      = strike->GetStateMode();
        std::scoped_lock lock(_publicationMutex);
        _comboStrike       = reaction;
        _comboStrikeQueued = true;
    }

    void PlayerDuelService::PublishComboStrike(SDK::wh::combatmodule::C_CombatActorTimeCop *timeCop) {
        SDK::wh::combatmodule::C_CombatActor *fighter = timeCop->GetCombatActor();
        if (fighter == _runningActionFighter || !IsLocalFighter(fighter)) {
            return;
        }
        Shared::RPC::CombatReaction reaction;
        {
            std::scoped_lock lock(_publicationMutex);
            if (!_comboStrikeQueued) {
                return;
            }
            reaction           = _comboStrike;
            _comboStrikeQueued = false;
        }
        // Sent as the combo branch of OnActionRun sends a swing the engine
        // started without the attack input; FlushPublications gives it its
        // own generation, which a blow it lands is claimed under.
        const SDK::wh::combatmodule::C_CombatActor *opponent = fighter->GetOpponent();
        const Roster &roster                                 = Current();
        if (!roster.IsNetworked(opponent)) {
            return;
        }
        const SDK::wh::combatmodule::S_CombatActionAttackDataDBData *row = SDK::wh::combatmodule::C_CombatActionAttackDatabase::GetInstance()->GetRow(reaction.rowIndex);
        reaction.opponentNetworkId                                       = roster.NetworkIdOf(opponent);
        reaction.attackSpeed                                             = TakeLocalAttackSpeed();
        reaction.windowMs                                                = row != nullptr ? Shared::RPC::CombatSwingWindowMs(row->GetTimeToHit(), row->GetTimeToWithdraw(), reaction.attackSpeed) : 0;
        PublishActionReaction(fighter, reaction);
    }

    // --- Replay --------------------------------------------------------------

    void PlayerDuelService::NoteReplayedSwing(const SDK::wh::combatmodule::C_CombatActor *fighter, std::uint32_t generation, std::uint32_t windowMs) {
        _swingGenerations.Note(fighter, generation);
        const Roster &roster = Current();
        if (fighter != nullptr && roster.IsLocal(fighter->GetOpponent())) {
            std::scoped_lock history(_guardHistoryMutex);
            _guardHistory.NoteSwing(MafiaNet::GetTime(), roster.NetworkIdOf(fighter), generation, windowMs);
        }
    }

    std::uint32_t PlayerDuelService::SwingOf(const SDK::wh::combatmodule::C_CombatActor *attacker) const {
        return Current().IsLocal(attacker) ? _localSwingGeneration.load() : _swingGenerations.Of(attacker);
    }

    bool PlayerDuelService::IsDown(const SDK::wh::combatmodule::C_CombatActor *fighter) {
        return fighter != nullptr && IsActorDown(fighter->GetOwner());
    }

    bool PlayerDuelService::RunReplay(SDK::wh::combatmodule::C_CombatActor *fighter, SDK::wh::combatmodule::I_CombatActorActionPrivate *action, bool forced) {
        // The owner's engine already decided this: whatever the copy is busy
        // with in that slot gives way (`CanBeReplacedBy_Hook`), as a defence
        // decided here would not have to.
        _replaying        = true;
        _givingWay        = forced;
        const bool queued = fighter->RunAction(&action);
        _givingWay        = false;
        _replaying        = false;
        action->Release();
        return queued;
    }

    PlayerDuelService::ReactionResult PlayerDuelService::ApplyRemoteReaction(const Shared::RPC::CombatReaction &reaction) {
        _lastDeferral        = "the action queue refused it";
        const Roster &roster = Current();
        if (reaction.actorNetworkId == 0 || reaction.actorNetworkId == roster.LocalNetworkId()) {
            return ReactionResult::Obsolete;
        }

        // A body this machine does not hold -- streamed out, or never in --
        // has nothing to show the action on, now or later.
        auto *subject = BodyOf(reaction.actorNetworkId, roster.LocalNetworkId());
        if (subject == nullptr || roster.IsAuthority(SettledFighter(subject))) {
            return ReactionResult::Obsolete;
        }
        SDK::wh::combatmodule::C_CombatActor *fighter = FighterFor(roster, reaction.actorNetworkId, true);
        if (fighter == nullptr || fighter->GetActionManager() == nullptr) {
            _lastDeferral = "the copy has no fighter yet";
            return ReactionResult::Deferred;
        }
        // A body that is down plays nothing over its death or its knockout.
        if (IsDown(fighter)) {
            return ReactionResult::Obsolete;
        }
        SDK::wh::combatmodule::C_CombatActor *opponent = FighterFor(roster, reaction.opponentNetworkId, true);
        switch (reaction.RequiredOpponent()) {
        case Shared::RPC::CombatReaction::OpponentRequirement::CurrentOpponent:
            if (opponent == nullptr) {
                _lastDeferral = "the opponent has no fighter here";
                return ReactionResult::Deferred;
            }
            if (fighter->GetOpponent() != opponent) {
                // Promoted now rather than at the copy's next rest: the owner's
                // engine built this defence against that very fighter, so the
                // copy takes the lock it had, even before the lock itself has
                // replicated -- the first parry of a fight arrives with it.
                SDK::wh::entitymodule::C_Actor *opponentActor = opponent->GetOwner();
                SDK::IEntity *opponentEntity                  = opponentActor != nullptr ? opponentActor->GetEntity() : nullptr;
                if (opponentEntity == nullptr) {
                    _lastDeferral = "the opponent has no entity here";
                    return ReactionResult::Deferred;
                }
                const SDK::wh::shared::C_ScopedRecursiveSRWLock promote(fighter->GetCombatGroupLock());
                if (fighter->GetTargetEntity() != opponentEntity) {
                    fighter->SetTarget(opponentEntity);
                }
                fighter->SetOpponent(opponentEntity);
                if (fighter->GetOpponent() != opponent) {
                    _lastDeferral = "the copy would not take its opponent";
                    return ReactionResult::Deferred;
                }
            }
            break;
        case Shared::RPC::CombatReaction::OpponentRequirement::Present:
            if (opponent == nullptr) {
                _lastDeferral = "the attacker is not here";
                return ReactionResult::Deferred;
            }
            break;
        case Shared::RPC::CombatReaction::OpponentRequirement::None: break;
        }

        // Built and queued under the fighter's combat-group lock, as the
        // engine's own external writers do; a sync partner shares that group,
        // because the engine groups a fighter with its opponent.
        const SDK::wh::shared::C_ScopedRecursiveSRWLock lock(fighter->GetCombatGroupLock());

        // A defence needs the swing it meets. One whose swing is already over
        // here -- the attacker's own engine met the copy first and recoiled,
        // or a bystander's copy of the swing ended before the parry arrived --
        // has nothing left to parry on this machine.
        if (reaction.kind == Shared::RPC::CombatReaction::PerfectBlock || reaction.kind == Shared::RPC::CombatReaction::SyncPerfectBlock || reaction.kind == Shared::RPC::CombatReaction::Block) {
            const SDK::wh::combatmodule::C_CombatActorModel *opponentModel = opponent != nullptr ? opponent->GetModel() : nullptr;
            const std::uint32_t swinging                                   = SDK::wh::combatmodule::E_CombatActorStateId::ReadingToStrike | SDK::wh::combatmodule::E_CombatActorStateId::Striking;
            if (opponentModel != nullptr && (static_cast<std::uint32_t>(opponentModel->GetState()) & swinging) == 0) {
                return ReactionResult::Obsolete;
            }
        }

        switch (reaction.kind) {
        case Shared::RPC::CombatReaction::PerfectBlock: {
            const auto *row    = SDK::wh::combatmodule::C_CombatActionPerfectBlockDatabase::GetInstance()->GetRow(reaction.rowIndex);
            const auto *attack = SDK::wh::combatmodule::C_CombatActionAttackDatabase::GetInstance()->GetRow(reaction.pairedRowIndex);
            if (row == nullptr || attack == nullptr) {
                return ReactionResult::Obsolete;
            }
            auto *block = SDK::wh::combatmodule::C_CombatActorActionPerfectBlock::Allocate(fighter);
            if (block == nullptr) {
                _lastDeferral = "no perfect block could be allocated";
                return ReactionResult::Deferred;
            }
            block->Prepare(fighter->GetActionManager()->GetPerfectBlockFactory(), row, attack, reaction.variantFlags);
            return RunReplay(fighter, block) ? ReactionResult::Applied : ReactionResult::Deferred;
        }
        case Shared::RPC::CombatReaction::SyncPerfectBlock: {
            const auto *row     = SDK::wh::combatmodule::C_CombatActionPerfectBlockDatabase::GetInstance()->GetRow(reaction.rowIndex);
            const auto *partner = SDK::wh::combatmodule::C_CombatActionSyncPerfectBlockHitDatabase::GetInstance()->GetRow(reaction.pairedRowIndex);
            if (row == nullptr || partner == nullptr) {
                return ReactionResult::Obsolete;
            }
            auto *block = SDK::wh::combatmodule::C_CombatActorActionSyncPerfectBlock::Allocate(fighter);
            if (block == nullptr) {
                _lastDeferral = "no sync perfect block could be allocated";
                return ReactionResult::Deferred;
            }
            block->Prepare(fighter->GetActionManager()->GetPerfectBlockFactory(), row, partner, (reaction.variantFlags & 1) != 0);
            if (!RunReplay(fighter, block)) {
                _lastDeferral = "the copy refused the sync perfect block";
                return ReactionResult::Deferred;
            }
            if (reaction.attackGeneration != 0) {
                NoteReplayedSwing(fighter, reaction.attackGeneration, reaction.windowMs);
            }
            return ReactionResult::Applied;
        }
        case Shared::RPC::CombatReaction::Block: {
            // Shown already: the copy's own block met this swing here -- or a
            // later swing of the same attacker, and this one is over.
            if (reaction.attackGeneration != 0 && _blockedSwings.Of(opponent) >= reaction.attackGeneration) {
                return ReactionResult::Obsolete;
            }
            const auto *row = SDK::wh::combatmodule::C_CombatActionBlockDatabase::GetInstance()->GetRow(reaction.rowIndex);
            if (row == nullptr) {
                return ReactionResult::Obsolete;
            }
            auto *block = SDK::wh::combatmodule::C_CombatActorActionBlock::Allocate(fighter);
            if (block == nullptr) {
                _lastDeferral = "no block could be allocated";
                return ReactionResult::Deferred;
            }
            block->Prepare(row);
            if (!RunReplay(fighter, block)) {
                _lastDeferral = "the copy refused the block";
                return ReactionResult::Deferred;
            }
            _blockedSwings.Note(opponent, reaction.attackGeneration);
            return ReactionResult::Applied;
        }
        case Shared::RPC::CombatReaction::Dodge: {
            // The owner's dodge, asked of this body as the jump input asked it
            // of the owner's: the same direction about the same opponent. The
            // query works out the arc from where this body is drawn and tests
            // that end point's clearance here; while the dodge runs, the
            // body's own combat input hands it the movement request.
            SDK::wh::combatmodule::S_CombatActionDodgeQuery query;
            query.m_direction[0] = reaction.direction[0];
            query.m_direction[1] = reaction.direction[1];
            query.m_pOpponent    = opponent;
            query.m_flags        = SDK::wh::combatmodule::S_CombatActionDodgeQuery::kTestsClearance;
            void *factory        = fighter->GetActionManager()->GetDodgeFactory();
            SDK::wh::combatmodule::S_CombatActorActionDodgeParams params;
            // A copy drawn where its owner's dodge does not fit -- by a wall
            // its owner had not reached -- is asked again without the
            // clearance test, as the engine's own dodge check asks
            // (0x18090A7A0): the dodge still plays, and physics stops the body
            // at the wall. Nothing is left out of the owner's chain.
            if (!SDK::wh::combatmodule::C_CombatActorActionDodge::Query(factory, query, params)) {
                query.m_flags = 0;
                if (!SDK::wh::combatmodule::C_CombatActorActionDodge::Query(factory, query, params)) {
                    return ReactionResult::Obsolete;
                }
            }
            // With its owner's row, whichever the query found first here. The
            // builder copies the parameters as they are (0x181482FAC), and
            // nothing the dodge runs reads the row against the arc: Enter
            // plays the row's fragment (0x181648A28), the movement it steers
            // takes the direction and the distance (0x182755C54).
            if (const SDK::wh::combatmodule::S_CombatActionDodgeDBData *row = SDK::wh::combatmodule::C_CombatActionDodgeDatabase::GetInstance()->GetRow(reaction.rowIndex)) {
                params.m_pRow = row;
            }
            if (!RunReplay(fighter, SDK::wh::combatmodule::C_CombatActorActionDodge::Create(factory, params))) {
                _lastDeferral = "the copy refused the dodge";
                return ReactionResult::Deferred;
            }
            return ReactionResult::Applied;
        }
        case Shared::RPC::CombatReaction::Hit: {
            // The local player's own blow, which this machine's engine also
            // resolved on the copy at contact -- where the copy may have
            // taken it otherwise, as a block, or been too busy to react.
            RecentClaim claim;
            const bool claimedHere = reaction.opponentNetworkId == roster.LocalNetworkId() && FindRecentClaim(reaction.actorNetworkId, reaction.attackGeneration, MafiaNet::GetTime(), claim);

            // Shown already: the contact here reacted with the owner's very
            // reaction. A different one replaces it below.
            if (claimedHere && claim.hitRowIndex == reaction.rowIndex) {
                return ReactionResult::Obsolete;
            }

            // CreateHitAction's first gate -- the victim's slot 184 asked
            // about the attacker -- is not repeated: the sender's engine
            // passed it, or there would be no hit to replay.

            const auto *row = SDK::wh::combatmodule::C_CombatActionHitDatabase::GetInstance()->GetRow(reaction.rowIndex);
            if (row == nullptr) {
                return ReactionResult::Obsolete;
            }
            auto *hit = SDK::wh::combatmodule::C_CombatActorActionHit::Allocate(fighter);
            if (hit == nullptr) {
                _lastDeferral = "no hit reaction could be allocated";
                return ReactionResult::Deferred;
            }
            hit->Prepare(row, opponent, reaction.variantFlags);

            // The owner's body took the blow, so the copy shows it; over a
            // flinch or a paired move still playing the engine's own rule
            // decides, as it does for a ruled hit (RunRuledHit).
            const SDK::wh::combatmodule::I_CombatActorAction *running = claimedHere ? fighter->GetRunningAction(hit->GetSlot()) : nullptr;
            const bool overReaction                                   = running != nullptr && (SDK::wh::combatmodule::C_CombatActorActionHit::IsInstance(running) || SDK::wh::combatmodule::IsSyncAction(running));
            if (!RunReplay(fighter, hit, !overReaction)) {
                return overReaction ? ReactionResult::Obsolete : ReactionResult::Deferred;
            }
            // A contact the local player's swing makes after this is not
            // shown again (`HitsShownBlow`).
            if (reaction.opponentNetworkId == roster.LocalNetworkId()) {
                _hitSwings.Note(fighter, reaction.attackGeneration);
            }
            return ReactionResult::Applied;
        }
        case Shared::RPC::CombatReaction::Riposte: {
            // The counter of the perfect block the copy is replaying: raised
            // through the engine's own setter, so the action's listener turns
            // it into the strike-back (`OnRiposteRequestChanged`). Dropped again
            // once the strike has had its time, as the owner's game drops it.
            SDK::wh::combatmodule::C_CombatActorModel *model = fighter->GetModel();
            if (model == nullptr) {
                _lastDeferral = "the copy has no model";
                return ReactionResult::Deferred;
            }
            // That listener exists only while a perfect block runs: its Enter
            // connects it (0x180C521F4) and its Exit drops it (0x180C534A8). A
            // copy whose perfect block was dropped -- on the attacker's own
            // screen its blade met the copy first -- or has already ended took
            // the request and played nothing, while the strike-back's blow was
            // still ruled on the attacker. That perfect block is played first,
            // with the strike-back it carries, and the counter raised once it
            // runs.
            if (!SDK::wh::combatmodule::C_CombatActorActionPerfectBlock::IsInstance(fighter->GetRunningAction(SDK::wh::combatmodule::C_CombatActorDirector::kMainSlot))) {
                const auto *row    = SDK::wh::combatmodule::C_CombatActionPerfectBlockDatabase::GetInstance()->GetRow(reaction.rowIndex);
                const auto *attack = SDK::wh::combatmodule::C_CombatActionAttackDatabase::GetInstance()->GetRow(reaction.pairedRowIndex);
                if (row == nullptr || attack == nullptr) {
                    return ReactionResult::Obsolete;
                }
                auto *block = SDK::wh::combatmodule::C_CombatActorActionPerfectBlock::Allocate(fighter);
                if (block == nullptr) {
                    _lastDeferral = "no perfect block could be allocated for the riposte";
                    return ReactionResult::Deferred;
                }
                block->Prepare(fighter->GetActionManager()->GetPerfectBlockFactory(), row, attack, reaction.variantFlags);
                RunReplay(fighter, block);
                _lastDeferral = "the riposte waits for its perfect block to run";
                return ReactionResult::Deferred;
            }
            model->SetRiposteRequested(true);
            _raisedRipostes.push_back({reaction.actorNetworkId, MafiaNet::GetTime() + kRiposteRequestHoldMs});
            NoteReplayedSwing(fighter, reaction.attackGeneration, reaction.windowMs);
            return ReactionResult::Applied;
        }
        case Shared::RPC::CombatReaction::Attack: {
            SquareUp(fighter, reaction.opponentNetworkId);
            ForceNextAttackSpeed(fighter, reaction.attackSpeed);
            _replaying                                                        = true;
            const SDK::wh::combatmodule::C_CombatActor::AttackOutcome outcome = fighter->ReplayAcceptedAttack(reaction.rowIndex, reaction.candidateFlag0, reaction.candidateFlag1, reaction.contextFlag0, reaction.contextFlag1, reaction.stateMode);
            _replaying                                                        = false;
            if (outcome == SDK::wh::combatmodule::C_CombatActor::AttackOutcome::Swung) {
                NoteReplayedSwing(fighter, reaction.attackGeneration, reaction.windowMs);
                return ReactionResult::Applied;
            }
            ForgetForcedAttackSpeed(fighter);
            return outcome == SDK::wh::combatmodule::C_CombatActor::AttackOutcome::InvalidRow ? ReactionResult::Obsolete : ReactionResult::Deferred;
        }
        default: return ReactionResult::Obsolete;
        }
    }
} // namespace KCDC::Features::Player

namespace KCDC::Features::Player {
    namespace {
        using BeginTimewarp_t       = void(__fastcall *)(SDK::wh::combatmodule::C_CombatActorTimewarp *);
        using ClinchAlignment_t     = bool(__fastcall *)(const SDK::wh::combatmodule::C_CombatActorClinch *);
        using DispatchDueSlots_t    = void(__fastcall *)(void *, void *, SDK::wh::combatmodule::C_CombatActorSlotPlanner *);
        using OnGeometricHit_t      = void(__fastcall *)(SDK::wh::combatmodule::C_CombatActorCollisionProcess *, SDK::wh::combatmodule::S_CombatContact *);
        using OnAnimationHit_t      = void(__fastcall *)(SDK::wh::combatmodule::C_CombatActorCollisionProcess *, SDK::wh::combatmodule::CombatHitInfo *);
        using IsHostileTo_t         = bool(__fastcall *)(SDK::wh::combatmodule::C_CombatRPG *, const SDK::wh::combatmodule::C_CombatActor *);
        using UpdateAttackTarget_t  = void(__fastcall *)(SDK::wh::combatmodule::C_CombatPlayerController *, bool);
        using RequestRiposte_t      = void(__fastcall *)(SDK::wh::combatmodule::C_CombatActor *);
        using ShouldSkipCandidate_t = bool(__fastcall *)(SDK::wh::combatmodule::C_CombatOpponentCandidates *, SDK::wh::entitymodule::C_Actor *);
        using UpdateCandidates_t    = void(__fastcall *)(SDK::wh::combatmodule::C_CombatOpponentCandidates *, float);
        using BeginAttackTimeline_t = std::uint64_t(__fastcall *)(SDK::wh::combatmodule::C_CombatActorTimeCop *, const void *);
        using GetAttackSpeed_t      = float(__fastcall *)(SDK::wh::combatmodule::C_CombatRPG *);
        using WouldKillOpponent_t   = bool(__fastcall *)(SDK::wh::combatmodule::C_CombatRPG *, std::uint32_t, void *, std::int32_t);
        using QueueHitRecord_t      = void(__fastcall *)(void *, const SDK::wh::combatmodule::S_CombatHitRecord *);
        using FindSlotObject_t      = SDK::wh::combatmodule::C_CombatActorMultiTrigger *(__fastcall *)(SDK::wh::combatmodule::C_CombatActorDirector *, std::uint32_t);
        using AddTrigger_t          = void(__fastcall *)(SDK::wh::combatmodule::C_CombatActorMultiTrigger *, SDK::wh::combatmodule::C_CombatActor *, void *);
        using CanBeReplacedBy_t     = bool(__fastcall *)(const SDK::wh::combatmodule::I_CombatActorActionPrivate *, SDK::wh::combatmodule::I_CombatActorActionPrivate *const *);
        using QueueNextAttack_t     = void(__fastcall *)(SDK::wh::combatmodule::I_CombatActionHelperAttackOwner *, SDK::wh::combatmodule::I_CombatActorAction **);
        using ProcessCombatHit_t    = SDK::wh::rpgmodule::S_EventHandle182D291D0 *(__fastcall *)(SDK::wh::rpgmodule::C_CombatSoul *, SDK::wh::rpgmodule::S_EventHandle182D291D0 *, const SDK::wh::rpgmodule::S_CombatHitData *);

        BeginTimewarp_t g_beginTimewarpOriginal             = nullptr;
        ClinchAlignment_t g_clinchAlignmentOriginal         = nullptr;
        DispatchDueSlots_t g_dispatchDueSlotsOriginal       = nullptr;
        OnGeometricHit_t g_onGeometricHitOriginal           = nullptr;
        OnAnimationHit_t g_onAnimationHitOriginal           = nullptr;
        IsHostileTo_t g_isHostileToOriginal                 = nullptr;
        UpdateAttackTarget_t g_updateAttackTargetOriginal   = nullptr;
        RequestRiposte_t g_requestRiposteOriginal           = nullptr;
        ShouldSkipCandidate_t g_shouldSkipCandidateOriginal = nullptr;
        UpdateCandidates_t g_updateCandidatesOriginal       = nullptr;
        BeginAttackTimeline_t g_beginAttackTimelineOriginal = nullptr;
        GetAttackSpeed_t g_getAttackSpeedOriginal           = nullptr;
        WouldKillOpponent_t g_wouldKillOpponentOriginal     = nullptr;
        QueueHitRecord_t g_queueHitRecordOriginal           = nullptr;
        ProcessCombatHit_t g_processCombatHitOriginal       = nullptr;
        CanBeReplacedBy_t g_canBeReplacedByOriginal         = nullptr;
        QueueNextAttack_t g_queueNextAttackOriginal         = nullptr;
        FindSlotObject_t g_findSlotObjectOriginal           = nullptr;
        AddTrigger_t g_addTriggerForOpponentOriginal        = nullptr;

        PlayerDuelService *Duel() {
            return KCDC::Core::gApplication != nullptr ? &KCDC::Core::gApplication->GetPlayerDuelService() : nullptr;
        }

        bool __fastcall ClinchAlignment_Hook(const SDK::wh::combatmodule::C_CombatActorClinch *clinch) {
            PlayerDuelService *duel = Duel();
            if (duel != nullptr && duel->IsPlayerPair(clinch->GetCombatActor())) {
                return false;
            }
            return g_clinchAlignmentOriginal(clinch);
        }

        /**
         * The combat slow-motion, confined to fights with no replicated body in
         * them. Its gate is IsPlayerControlled, which every replicated body
         * answers true to, and a dilation is local: it would slow this client
         * while the other player's ran on.
         */
        void __fastcall BeginTimewarp_Hook(SDK::wh::combatmodule::C_CombatActorTimewarp *timewarp) {
            PlayerDuelService *duel = Duel();
            if (duel != nullptr && timewarp != nullptr && duel->InvolvesRemotePlayer(timewarp->GetCombatActor())) {
                return;
            }
            g_beginTimewarpOriginal(timewarp);
        }

        /**
         * The attacker's planner opening the defender's windows: block,
         * perfect block, sync perfect block, master strike, riposte. All of
         * them onto the local body, whose windows only its own input fires,
         * and which could never perfect-block a remote swing without them.
         * Onto a replicated body, the plain block alone: its block trigger
         * fires on the guard its owner holds, so the copy parries exactly when
         * the owner does, and the rest are the owner's machine's to decide.
         */
        void __fastcall DispatchDueSlots_Hook(void *entries, void *model, SDK::wh::combatmodule::C_CombatActorSlotPlanner *planner) {
            PlayerDuelService *duel                        = Duel();
            SDK::wh::combatmodule::C_CombatActor *attacker = planner != nullptr ? planner->GetCombatActor() : nullptr;
            if (duel == nullptr || attacker == nullptr || duel->MayDispatchSlots(attacker)) {
                g_dispatchDueSlotsOriginal(entries, model, planner);
                return;
            }
            // Anything else -- a copy's planner onto an NPC -- is the owner's
            // machine's to choreograph.
            if (!duel->PostsBlocksOnly(attacker)) {
                return;
            }
            const PlayerDuelService::BlockSlotsScope blocksOnly(true);
            g_dispatchDueSlotsOriginal(entries, model, planner);
        }

        /** A window looked up by kind; inside a dispatch onto a replicated body, all but the block are absent. */
        SDK::wh::combatmodule::C_CombatActorMultiTrigger *__fastcall FindSlotObject_Hook(SDK::wh::combatmodule::C_CombatActorDirector *director, std::uint32_t kind) {
            if (PlayerDuelService::HidesSlot(kind)) {
                return nullptr;
            }
            return g_findSlotObjectOriginal(director, kind);
        }

        /** A window opened: its own activation looks windows up again, unfiltered. */
        void __fastcall AddTriggerForOpponent_Hook(SDK::wh::combatmodule::C_CombatActorMultiTrigger *trigger, SDK::wh::combatmodule::C_CombatActor *attacker, void *window) {
            const PlayerDuelService::BlockSlotsScope suspended(false);
            g_addTriggerForOpponentOriginal(trigger, attacker, window);
        }

        /**
         * A melee contact, resolved natively either way. The local player's
         * blow on a replicated player's body is claimed for the referee; a
         * remote player's blow on the local body is reported to it, its health
         * withheld until the ruling; one between two remote players is
         * theirs, and dropped; anything else is the engine's alone.
         */
        void __fastcall OnGeometricHit_Hook(SDK::wh::combatmodule::C_CombatActorCollisionProcess *process, SDK::wh::combatmodule::S_CombatContact *contact) {
            PlayerDuelService *duel                      = Duel();
            SDK::wh::combatmodule::C_CombatActor *victim = contact != nullptr ? contact->GetVictim() : nullptr;
            if (duel == nullptr || process == nullptr) {
                g_onGeometricHitOriginal(process, contact);
                return;
            }
            if (duel->IsBystanderContact(process->GetCombatActor(), victim)) {
                return;
            }
            if (duel->IsClaimedContact(process->GetCombatActor(), victim)) {
                const PlayerDuelService::ClaimScope claim(*duel, process->GetCombatActor(), victim);
                g_onGeometricHitOriginal(process, contact);
                return;
            }
            const PlayerDuelService::ResolutionScope scope(*duel, process->GetCombatActor(), victim);
            g_onGeometricHitOriginal(process, contact);
        }

        void __fastcall OnAnimationHit_Hook(SDK::wh::combatmodule::C_CombatActorCollisionProcess *process, SDK::wh::combatmodule::CombatHitInfo *hit) {
            PlayerDuelService *duel                      = Duel();
            SDK::wh::combatmodule::C_CombatActor *victim = hit != nullptr ? hit->GetVictim() : nullptr;
            if (duel == nullptr || process == nullptr) {
                g_onAnimationHitOriginal(process, hit);
                return;
            }
            if (duel->IsBystanderContact(process->GetCombatActor(), victim)) {
                return;
            }
            if (duel->IsClaimedContact(process->GetCombatActor(), victim)) {
                const PlayerDuelService::ClaimScope claim(*duel, process->GetCombatActor(), victim);
                g_onAnimationHitOriginal(process, hit);
                return;
            }
            const PlayerDuelService::ResolutionScope scope(*duel, process->GetCombatActor(), victim);
            g_onAnimationHitOriginal(process, hit);
        }

        /** Publishes counters requested by authoritative fighters against replicated opponents. */
        void __fastcall RequestRiposte_Hook(SDK::wh::combatmodule::C_CombatActor *fighter) {
            SDK::wh::combatmodule::C_CombatActorModel *model = fighter != nullptr ? fighter->GetModel() : nullptr;
            const bool before                                = model != nullptr && model->IsRiposteRequested();
            g_requestRiposteOriginal(fighter);
            PlayerDuelService *duel = Duel();
            if (duel != nullptr && model != nullptr && !before && model->IsRiposteRequested()) {
                duel->OnRiposteRequested(fighter);
            }
        }

        /** A player body's target pick; inside a fight with another player, that player stays the local player's target, and a copy keeps its owner's lock. */
        void __fastcall UpdateAttackTarget_Hook(SDK::wh::combatmodule::C_CombatPlayerController *controller, bool allowExtendedRange) {
            PlayerDuelService *duel = Duel();
            if (duel != nullptr && (duel->KeepEngagedTarget(controller) || duel->KeepOwnerLock(controller))) {
                return;
            }
            g_updateAttackTargetOriginal(controller, allowExtendedRange);
        }

        /** Two players are never hostile by faction; a remote body the picker may weigh is. */
        bool __fastcall IsHostileTo_Hook(SDK::wh::combatmodule::C_CombatRPG *rpg, const SDK::wh::combatmodule::C_CombatActor *candidate) {
            PlayerDuelService *duel = Duel();
            if (duel != nullptr && duel->IsHostileCandidate(rpg, candidate)) {
                return true;
            }
            return g_isHostileToOriginal(rpg, candidate);
        }

        /**
         * The picker's refusal, run unchanged on a remote body except for its
         * first test: inside the scope, that body answers IsPlayerControlled
         * as an NPC would, and every other test -- dead, hidden, angle,
         * distance -- is still the engine's.
         */
        bool __fastcall ShouldSkipCandidate_Hook(SDK::wh::combatmodule::C_CombatOpponentCandidates *candidates, SDK::wh::entitymodule::C_Actor *actor) {
            PlayerDuelService *duel = Duel();
            if (duel == nullptr || !duel->IsPickableRemoteBody(actor)) {
                return g_shouldSkipCandidateOriginal(candidates, actor);
            }
            const PlayerDuelService::CandidateScope scope(actor);
            return g_shouldSkipCandidateOriginal(candidates, actor);
        }

        /** The picker's refresh, then every remote body it cannot find by itself. */
        void __fastcall UpdateCandidates_Hook(SDK::wh::combatmodule::C_CombatOpponentCandidates *candidates, float dt) {
            g_updateCandidatesOriginal(candidates, dt);
            if (PlayerDuelService *duel = Duel()) {
                duel->OfferRemoteCandidates(candidates);
            }
        }

        /** A swing's timeline, then the hold on the local player's swing at a remote one. */
        std::uint64_t __fastcall BeginAttackTimeline_Hook(SDK::wh::combatmodule::C_CombatActorTimeCop *timeCop, const void *parameters) {
            const std::uint64_t result = g_beginAttackTimelineOriginal(timeCop, parameters);
            if (PlayerDuelService *duel = Duel()) {
                duel->PublishComboStrike(timeCop);
                duel->HoldLocalSwing(timeCop);
            }
            return result;
        }

        /** A combo's next strike handed to a running attack; noted before the engine stores it. */
        void __fastcall QueueNextAttack_Hook(SDK::wh::combatmodule::I_CombatActionHelperAttackOwner *owner, SDK::wh::combatmodule::I_CombatActorAction **next) {
            if (PlayerDuelService *duel = Duel()) {
                duel->OnComboStrikeQueued(static_cast<SDK::wh::combatmodule::C_CombatActorActionAttack *>(owner), *next);
            }
            g_queueNextAttackOriginal(owner, next);
        }

        /** The timeline's time scale: the sender's for a replayed swing, and noted for the local one. */
        float __fastcall GetAttackSpeed_Hook(SDK::wh::combatmodule::C_CombatRPG *rpg) {
            const float speed       = g_getAttackSpeedOriginal(rpg);
            PlayerDuelService *duel = Duel();
            return duel != nullptr ? duel->FilterAttackSpeed(rpg, speed) : speed;
        }

        /** The lethal dry run, answered "no" wherever a player's life is at stake. */
        bool __fastcall WouldKillOpponent_Hook(SDK::wh::combatmodule::C_CombatRPG *rpg, std::uint32_t attackType, void *attackData, std::int32_t attackZone) {
            PlayerDuelService *duel = Duel();
            if (duel != nullptr && !duel->MayWeighLethalVariant(rpg)) {
                return false;
            }
            return g_wouldKillOpponentOriginal(rpg, attackType, attackData, attackZone);
        }

        /**
         * A contact's record, queued for its victim. Inside a resolution of a
         * remote player's blow, its impact goes out with what the contact
         * published: no other machine resolves the contact, so no other
         * machine has a record to play the impact from.
         *
         * Another player's body is knocked out on its owner's machine and
         * nowhere else. This machine's own verdict on the blow would have
         * the body play the knockout reaction -- a fall into a ragdoll --
         * while its owner stands; it takes the blow as a plain hit instead,
         * and goes down when its owner's consciousness does
         * (`Human::PresentRemoteState`). So does a copy of a horse another
         * client runs, which goes down when its simulator's horse does
         * (`HorseService::IsHorseCopy`).
         */
        void __fastcall QueueHitRecord_Hook(void *gameRules, const SDK::wh::combatmodule::S_CombatHitRecord *record) {
            if (PlayerDuelService *duel = Duel()) {
                duel->OnHitRecordQueued(record);
            }
            if (record->m_knocksOutVictim && (KCDC::Features::Player::NativePlayerModule::IsRemoteEntity(record->m_victimEntityId) || (KCDC::Core::gApplication != nullptr && KCDC::Core::gApplication->GetHorseService().IsHorseCopy(record->m_victimEntityId)))) {
                SDK::wh::combatmodule::S_CombatHitRecord hit = *record;
                hit.m_knocksOutVictim                        = false;
                g_queueHitRecordOriginal(gameRules, &hit);
                return;
            }
            g_queueHitRecordOriginal(gameRules, record);
        }

        /**
         * The RPG side of one melee blow, on the attacker's combat soul (the
         * only implementation of the slot is C_CombatSoul's). Inside a
         * resolution of a remote player's blow on the local body, the hit data
         * says how the body took it -- blocked, perfectly, broken -- which the
         * report to the referee carries. Dry runs pass through here too and
         * are outside any resolution.
         */
        SDK::wh::rpgmodule::S_EventHandle182D291D0 *__fastcall ProcessCombatHit_Hook(SDK::wh::rpgmodule::C_CombatSoul *soul, SDK::wh::rpgmodule::S_EventHandle182D291D0 *result, const SDK::wh::rpgmodule::S_CombatHitData *data) {
            PlayerDuelService *duel = Duel();
            if (duel != nullptr) {
                duel->OnCombatHitData(data);
            }
            return g_processCombatHitOriginal(soul, result, data);
        }

        /**
         * The replace test of the action running in a fighter's slot. A
         * replay of another machine's decision comes in over whatever the
         * copy is busy with: the owner's engine already settled it.
         */
        bool __fastcall CanBeReplacedBy_Hook(const SDK::wh::combatmodule::I_CombatActorActionPrivate *running, SDK::wh::combatmodule::I_CombatActorActionPrivate *const *incoming) {
            if (PlayerDuelService::IsGivingWay()) {
                return true;
            }
            return g_canBeReplacedByOriginal(running, incoming);
        }
    } // namespace
} // namespace KCDC::Features::Player

static InitFunction initDuelHooks(
    []() {
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_CombatActorClinch__IsPairedAttackAlignmentValid), reinterpret_cast<LPVOID>(&KCDC::Features::Player::ClinchAlignment_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Player::g_clinchAlignmentOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_CombatActorTimewarp__BeginTimewarp), reinterpret_cast<LPVOID>(&KCDC::Features::Player::BeginTimewarp_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Player::g_beginTimewarpOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_CombatActorSlotPlanner__DispatchDueSlots), reinterpret_cast<LPVOID>(&KCDC::Features::Player::DispatchDueSlots_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Player::g_dispatchDueSlotsOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_CombatActorCollisionProcess__OnGeometricHit), reinterpret_cast<LPVOID>(&KCDC::Features::Player::OnGeometricHit_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Player::g_onGeometricHitOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_CombatActorCollisionProcess__OnAnimationHit), reinterpret_cast<LPVOID>(&KCDC::Features::Player::OnAnimationHit_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Player::g_onAnimationHitOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_CombatActor__RequestRiposte), reinterpret_cast<LPVOID>(&KCDC::Features::Player::RequestRiposte_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Player::g_requestRiposteOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_CombatPlayerController__UpdateAttackTarget), reinterpret_cast<LPVOID>(&KCDC::Features::Player::UpdateAttackTarget_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Player::g_updateAttackTargetOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_CombatRPG__IsHostileTo), reinterpret_cast<LPVOID>(&KCDC::Features::Player::IsHostileTo_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Player::g_isHostileToOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.CombatOpponentCandidates__ShouldSkip), reinterpret_cast<LPVOID>(&KCDC::Features::Player::ShouldSkipCandidate_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Player::g_shouldSkipCandidateOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.CombatOpponentCandidates__Update), reinterpret_cast<LPVOID>(&KCDC::Features::Player::UpdateCandidates_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Player::g_updateCandidatesOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_CombatActorTimeCop__BeginAttackTimeline), reinterpret_cast<LPVOID>(&KCDC::Features::Player::BeginAttackTimeline_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Player::g_beginAttackTimelineOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_CombatRPG__GetAttackSpeedMultiplier), reinterpret_cast<LPVOID>(&KCDC::Features::Player::GetAttackSpeed_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Player::g_getAttackSpeedOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.CombatRPG__WouldKillOpponent), reinterpret_cast<LPVOID>(&KCDC::Features::Player::WouldKillOpponent_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Player::g_wouldKillOpponentOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.CGameRules__QueueHitRecord), reinterpret_cast<LPVOID>(&KCDC::Features::Player::QueueHitRecord_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Player::g_queueHitRecordOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_CombatActorDirector__FindSlotObject), reinterpret_cast<LPVOID>(&KCDC::Features::Player::FindSlotObject_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Player::g_findSlotObjectOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_CombatActorMultiTrigger__AddTriggerForOpponent), reinterpret_cast<LPVOID>(&KCDC::Features::Player::AddTriggerForOpponent_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Player::g_addTriggerForOpponentOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_CombatSoul__ProcessCombatHit), reinterpret_cast<LPVOID>(&KCDC::Features::Player::ProcessCombatHit_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Player::g_processCombatHitOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_CombatActorAction__CanBeReplacedBy), reinterpret_cast<LPVOID>(&KCDC::Features::Player::CanBeReplacedBy_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Player::g_canBeReplacedByOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_CombatActorActionAttack__QueueNextAttack), reinterpret_cast<LPVOID>(&KCDC::Features::Player::QueueNextAttack_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Player::g_queueNextAttackOriginal));
    },
    "Duel");

namespace KCDC::Features::Player {
    namespace {
        using RequestStance_t  = bool(__fastcall *)(SDK::wh::combatmodule::C_CombatActor *, const SDK::wh::combatmodule::C_CombatActor::FightingStanceRequest *);
        using GuardEdge_t      = void(__fastcall *)(SDK::wh::combatmodule::C_CombatPlayerController *);
        using SetCombatZone_t  = void(__fastcall *)(SDK::wh::combatmodule::C_CombatActor *, std::uint32_t, std::int32_t, bool);
        using AttackEdge_t     = void(__fastcall *)(SDK::wh::combatmodule::C_CombatPlayerController *, std::uint32_t);
        using AbortAttack_t    = void(__fastcall *)(SDK::wh::combatmodule::C_CombatPlayerController *);
        using RunAction_t      = bool(__fastcall *)(SDK::wh::combatmodule::C_CombatActor *, SDK::wh::combatmodule::I_CombatActorActionPrivate **);
        using LeaveCombat_t    = void(__fastcall *)(SDK::wh::combatmodule::C_CombatActor *);
        using PostUpdate_t     = void(__fastcall *)(SDK::wh::combatmodule::C_CombatActor *);
        using IsPlayerDriven_t = bool(__fastcall *)(const SDK::wh::entitymodule::C_Actor *);

        RequestStance_t g_requestStanceOriginal   = nullptr;
        GuardEdge_t g_beginGuardOriginal          = nullptr;
        GuardEdge_t g_endGuardOriginal            = nullptr;
        SetCombatZone_t g_setCombatZoneOriginal   = nullptr;
        AttackEdge_t g_beginAttackOriginal        = nullptr;
        AttackEdge_t g_performAttackOriginal      = nullptr;
        AbortAttack_t g_abortAttackOriginal       = nullptr;
        RunAction_t g_runActionOriginal           = nullptr;
        LeaveCombat_t g_leaveCombatOriginal       = nullptr;
        PostUpdate_t g_postUpdateOriginal         = nullptr;
        IsPlayerDriven_t g_isPlayerDrivenOriginal = nullptr;

        /** The entity the fighter belongs to, or 0 before its Init has run. */
        SDK::EntityId FighterEntityId(SDK::wh::combatmodule::C_CombatActor *combatActor) {
            if (combatActor == nullptr || !combatActor->IsInitialised()) {
                return 0;
            }

            auto *owner = combatActor->GetOwner();
            return owner != nullptr ? owner->GetEntityId() : 0;
        }

        /** The combat side of the body a player controller drives. */
        SDK::wh::combatmodule::C_CombatActor *ControllerFighter(SDK::wh::combatmodule::C_CombatPlayerController *controller) {
            return controller != nullptr ? static_cast<SDK::wh::combatmodule::C_CombatActorObject *>(controller)->GetCombatActor() : nullptr;
        }

        /**
         * Whether a controller is this machine's own player's. Every remote
         * player is a C_Player with a controller of its own, whose edges the
         * engine runs too and which describe nothing this machine pressed.
         */
        bool IsLocalController(SDK::wh::combatmodule::C_CombatPlayerController *controller) {
            const SDK::EntityId entityId = FighterEntityId(ControllerFighter(controller));
            return entityId != 0 && entityId == KCDC::Common::LocalActorEntityId();
        }

        /**
         * A body putting its fists up or taking them down -- the pose the
         * punch and the guard both come out of. Reported as the request rather
         * than the finished stance, so everyone else starts the same actor
         * action at the same moment instead of hearing about it once it lands.
         */
        bool __fastcall RequestFightingStance_Hook(SDK::wh::combatmodule::C_CombatActor *combatActor, const SDK::wh::combatmodule::C_CombatActor::FightingStanceRequest *request) {
            const bool accepted = g_requestStanceOriginal(combatActor, request);

            const SDK::EntityId entityId = FighterEntityId(combatActor);
            if (!accepted || entityId == 0 || request == nullptr || KCDC::Core::gApplication == nullptr) {
                return accepted;
            }

            KCDC::Core::gApplication->GetPlayerService().NotifyCombatStance(entityId, request->up);
            return accepted;
        }

        /**
         * Whatever takes a body's fists back down -- C_CombatActor::PostUpdate
         * ends combat mode as soon as the combat queue runs dry, which it can do
         * to a remote body for reasons its owner never had. Read before the
         * original, which tears the fighter's listeners down.
         */
        void __fastcall LeaveCombatMode_Hook(SDK::wh::combatmodule::C_CombatActor *combatActor) {
            if (KCDC::Core::gApplication != nullptr && KCDC::Core::gApplication->GetPlayerDuelService().KeepRemoteStance(combatActor)) {
                return;
            }
            const SDK::EntityId entityId = FighterEntityId(combatActor);

            g_leaveCombatOriginal(combatActor);

            if (entityId == 0 || KCDC::Core::gApplication == nullptr) {
                return;
            }

            KCDC::Core::gApplication->GetPlayerService().NotifyCombatStanceLost(entityId);
        }

        void __fastcall PostUpdate_Hook(SDK::wh::combatmodule::C_CombatActor *combatActor) {
            const PlayerDuelService::CombatPostUpdateScope scope(combatActor);
            g_postUpdateOriginal(combatActor);
        }

        /**
         * The block key's intent after the controller's own press or release:
         * the guard it asks for as the player (BeginGuard's
         * `RequestGuard(1, 0)`) with no release counting down. Not the
         * model's block request bit, which BeginGuard never sets for a
         * fighter with an opponent (0x1827C29E0): a press while locked on
         * read as no guard and never reached the copies. Taken at the edge,
         * not at `RequestGuard`: a release only drops the guard once
         * `Update`'s timer runs out, and receivers replay that delay
         * themselves through their own `EndGuard`. Automation guards are not
         * the player's and come out of each body's own combat logic.
         */
        void NotifyGuardEdge(SDK::wh::combatmodule::C_CombatPlayerController *controller) {
            if (!IsLocalController(controller)) {
                return;
            }
            auto *fighter = ControllerFighter(controller);
            auto *model   = fighter != nullptr ? fighter->GetModel() : nullptr;
            if (model == nullptr || KCDC::Core::gApplication == nullptr) {
                return;
            }
            const bool held = model->GetGuardRequest().GetSource(SDK::wh::combatmodule::C_CombatActor::GUARD_SOURCE_PLAYER) != SDK::wh::combatmodule::C_CombatActor::GUARD_REQUEST_NONE && !controller->IsGuardReleasePending();
            KCDC::Core::gApplication->GetPlayerService().NotifyCombatGuard(FighterEntityId(fighter), held ? SDK::wh::combatmodule::C_CombatActor::GUARD_REQUEST_GUARD : SDK::wh::combatmodule::C_CombatActor::GUARD_REQUEST_NONE);
        }

        void __fastcall BeginGuard_Hook(SDK::wh::combatmodule::C_CombatPlayerController *controller) {
            g_beginGuardOriginal(controller);
            NotifyGuardEdge(controller);
        }

        void __fastcall EndGuard_Hook(SDK::wh::combatmodule::C_CombatPlayerController *controller) {
            g_endGuardOriginal(controller);
            NotifyGuardEdge(controller);
        }

        /** Where a fighter is aiming on the combat star; called only when it moves. */
        void __fastcall SetCombatZone_Hook(SDK::wh::combatmodule::C_CombatActor *combatActor, std::uint32_t zoneId, std::int32_t reason, bool alsoSetPrevious) {
            g_setCombatZoneOriginal(combatActor, zoneId, reason, alsoSetPrevious);

            const SDK::EntityId entityId = FighterEntityId(combatActor);
            auto *model                  = combatActor != nullptr ? combatActor->GetModel() : nullptr;
            if (entityId == 0 || model == nullptr || KCDC::Core::gApplication == nullptr) {
                return;
            }

            KCDC::Core::gApplication->GetPlayerService().NotifyCombatZone(entityId, model->GetCombatZone());
        }

        /**
         * The press half of a swing. It publishes the attack request bit and
         * the input class, which is what moves the fighter into
         * reading-to-strike -- and the strike below is only found from there.
         */
        void __fastcall BeginAttack_Hook(SDK::wh::combatmodule::C_CombatPlayerController *controller, std::uint32_t inputClassId) {
            g_beginAttackOriginal(controller, inputClassId);
            if (!IsLocalController(controller)) {
                return;
            }

            auto *fighter       = ControllerFighter(controller);
            auto *model         = fighter != nullptr ? fighter->GetModel() : nullptr;
            const bool accepted = fighter != nullptr && fighter->IsInitialised() && model != nullptr && model->IsAttackInputActive() && model->GetAttackInputClass() == static_cast<std::int32_t>(inputClassId);
            if (!accepted) {
                return;
            }
            if (KCDC::Core::gApplication == nullptr) {
                return;
            }

            KCDC::Core::gApplication->GetPlayerService().NotifyCombatAttackBegan(static_cast<std::int32_t>(inputClassId), model->GetCombatZone());
        }

        /**
         * The committed swing. Taken here rather than at the attack factory,
         * which is also entered for combos and for every AI fighter. The zone
         * is read first: it is the one the factory is about to read, and the
         * aim can move again in the same frame.
         */
        void __fastcall PerformAttack_Hook(SDK::wh::combatmodule::C_CombatPlayerController *controller, std::uint32_t inputClassId) {
            auto *fighter                                    = ControllerFighter(controller);
            SDK::wh::combatmodule::C_CombatActorModel *model = fighter != nullptr ? fighter->GetModel() : nullptr;
            const std::int32_t combatZone                    = model != nullptr ? model->GetCombatZone() : -1;
            const KCDC::Features::Player::PlayerService::LocalAttackCommitCapture capture(fighter, combatZone);
            g_performAttackOriginal(controller, inputClassId);
        }

        /**
         * Every combat action the engine accepts onto a fighter's queue.
         *
         * Inside the local controller's release it is the committed swing,
         * captured with the exact row and the time scale its timeline was
         * given. Outside it, it is anything else the fight produced -- a
         * perfect block, a hit reaction, a recoil -- which the duel service
         * publishes when this machine is the one that decided it.
         */
        bool __fastcall RunAction_Hook(SDK::wh::combatmodule::C_CombatActor *combatActor, SDK::wh::combatmodule::I_CombatActorActionPrivate **action) {
            SDK::wh::combatmodule::I_CombatActorActionPrivate *submittedAction = action != nullptr ? *action : nullptr;
            if (KCDC::Core::gApplication == nullptr) {
                return g_runActionOriginal(combatActor, action);
            }

            PlayerService &players  = KCDC::Core::gApplication->GetPlayerService();
            PlayerDuelService &duel = KCDC::Core::gApplication->GetPlayerDuelService();

            // A fighter with an opponent can have the factory hand back a sync
            // attack, which shares none of the attack's layout.
            const bool capturesLocalCommit                                          = submittedAction != nullptr && players.IsCapturingLocalAttackCommit(combatActor) && SDK::wh::combatmodule::C_CombatActorActionAttack::IsInstance(submittedAction);
            auto *submittedAttack                                                   = capturesLocalCommit ? static_cast<SDK::wh::combatmodule::C_CombatActorActionAttack *>(submittedAction) : nullptr;
            const SDK::wh::combatmodule::S_CombatActionAttackDataDBData *attackData = submittedAttack != nullptr ? submittedAttack->GetAttackData() : nullptr;
            const std::uint8_t candidateFlag0                                       = submittedAttack != nullptr ? submittedAttack->GetCandidateFlag0() : 0;
            const std::uint8_t candidateFlag1                                       = submittedAttack != nullptr ? submittedAttack->GetCandidateFlag1() : 0;
            const std::uint8_t contextFlag0                                         = submittedAttack != nullptr ? submittedAttack->GetContextFlag0() : 0;
            const std::uint8_t contextFlag1                                         = submittedAttack != nullptr ? submittedAttack->GetContextFlag1() : 0;
            const std::uint32_t stateMode                                           = submittedAttack != nullptr ? submittedAttack->GetStateMode() : 0;
            const std::int32_t combatZone                                           = capturesLocalCommit ? players.CapturedLocalAttackCombatZone() : -1;

            // A copy's own block of a swing its owner's replayed block already
            // showed here is refused, as the queue refuses what it will not
            // take: the block would start over.
            if (duel.BlocksShownSwing(combatActor, submittedAction)) {
                return false;
            }
            // So is the reaction a claimed contact raises on a copy that already
            // played its owner's reaction to that swing.
            if (duel.HitsShownBlow(combatActor, submittedAction)) {
                return false;
            }

            // Whatever time scale is noted from here on is this action's own.
            duel.TakeLocalAttackSpeed();
            const PlayerDuelService::RunActionScope running(combatActor);
            const bool accepted = g_runActionOriginal(combatActor, action);
            if (!accepted) {
                return accepted;
            }

            if (capturesLocalCommit) {
                players.NotifyCombatAttackCommitted(attackData, combatZone, candidateFlag0, candidateFlag1, contextFlag0, contextFlag1, stateMode, duel.TakeLocalAttackSpeed());
            }
            duel.OnActionRun(combatActor, submittedAction);
            return accepted;
        }

        /** Terminal edge for an accepted Press that produced no queued action. */
        void __fastcall AbortAttack_Hook(SDK::wh::combatmodule::C_CombatPlayerController *controller) {
            g_abortAttackOriginal(controller);
            if (KCDC::Core::gApplication != nullptr && IsLocalController(controller)) {
                KCDC::Core::gApplication->GetPlayerService().NotifyCombatAttackCanceled();
            }
        }

        /**
         * KCD2's "driven by a player rather than by AI" test, answered as the
         * engine wrote it for every body but one reproducing a remote player's
         * swing. The attack factory's unopposed selection refuses anything
         * this says no to.
         */
        bool __fastcall IsPlayerControlled_Hook(const SDK::wh::entitymodule::C_Actor *actor) {
            // A remote player's fighter being destroyed is not the local
            // player's; see NativePlayerModule::DestroyCombatActor.
            if (KCDC::Features::Player::NativePlayerModule::IsDestroyingRemoteFighter(actor)) {
                return false;
            }
            // The opponent picker weighing a remote body as it would an NPC:
            // its first refusal test is this one, and only that test is
            // answered differently.
            if (PlayerDuelService::IsOfferingRemoteCandidate(actor)) {
                return false;
            }
            // A combat camera clip asking whether its body is this machine's
            // player; see NativePlayerModule::CameraClipIsOwnerPlayerControlled.
            // A native asking whether its body is this machine's player -- the
            // combat camera clip, a mercy kill; see NativePlayerModule.
            if (KCDC::Features::Player::NativePlayerModule::IsAskingForLocalPlayer() && KCDC::Features::Player::NativePlayerModule::IsRemote(actor)) {
                return false;
            }
            // A caller that means "this machine's player" by it; see
            // NativePlayerModule::IsLocalPlayerSite.
            if (KCDC::Features::Player::NativePlayerModule::IsRemote(actor) && KCDC::Features::Player::NativePlayerModule::IsLocalPlayerSite(_ReturnAddress())) {
                return false;
            }
            return g_isPlayerDrivenOriginal(actor);
        }
    } // namespace

} // namespace KCDC::Features::Player

static InitFunction initCombatHooks(
    []() {
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_CombatActor__RequestFightingStance), reinterpret_cast<LPVOID>(&KCDC::Features::Player::RequestFightingStance_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Player::g_requestStanceOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_CombatActor__LeaveCombatMode), reinterpret_cast<LPVOID>(&KCDC::Features::Player::LeaveCombatMode_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Player::g_leaveCombatOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_CombatActor__PostUpdate), reinterpret_cast<LPVOID>(&KCDC::Features::Player::PostUpdate_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Player::g_postUpdateOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_CombatPlayerController__BeginGuard), reinterpret_cast<LPVOID>(&KCDC::Features::Player::BeginGuard_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Player::g_beginGuardOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_CombatPlayerController__EndGuard), reinterpret_cast<LPVOID>(&KCDC::Features::Player::EndGuard_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Player::g_endGuardOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_CombatActor__SetCombatZone), reinterpret_cast<LPVOID>(&KCDC::Features::Player::SetCombatZone_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Player::g_setCombatZoneOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_CombatPlayerController__BeginAttack), reinterpret_cast<LPVOID>(&KCDC::Features::Player::BeginAttack_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Player::g_beginAttackOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_CombatPlayerController__PerformAttack), reinterpret_cast<LPVOID>(&KCDC::Features::Player::PerformAttack_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Player::g_performAttackOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_CombatPlayerController__AbortAttack), reinterpret_cast<LPVOID>(&KCDC::Features::Player::AbortAttack_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Player::g_abortAttackOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_CombatActor__RunAction), reinterpret_cast<LPVOID>(&KCDC::Features::Player::RunAction_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Player::g_runActionOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_Actor__IsPlayerControlled), reinterpret_cast<LPVOID>(&KCDC::Features::Player::IsPlayerControlled_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Player::g_isPlayerDrivenOriginal));
    },
    "Combat");

namespace KCDC::Features::Player {
    namespace {
        using SoulDeathFn                     = void(__fastcall *)(SDK::wh::rpgmodule::S_SoulDeathRecord *);
        using RaiseGameOverFn                 = void(__fastcall *)(void *, std::uint32_t);
        using DeadEnterFn                     = bool(__fastcall *)(SDK::wh::entitymodule::C_ActorActionDead *, void *);
        using PlayerReviveFn                  = void(__fastcall *)(SDK::wh::entitymodule::C_Player *, std::uint8_t);
        using PlayDeathEffectFn               = void(__fastcall *)(SDK::wh::entitymodule::C_PlayerMaterialEffects *);
        using DropHandItemsFn                 = void(__fastcall *)(SDK::wh::entitymodule::C_Human *);
        SoulDeathFn originalSoulDeath         = nullptr;
        RaiseGameOverFn originalGameOver      = nullptr;
        DeadEnterFn originalEnter             = nullptr;
        PlayerReviveFn originalRevive         = nullptr;
        PlayDeathEffectFn originalDeathEffect = nullptr;
        DropHandItemsFn originalDropHandItems = nullptr;

        void __fastcall DropHandItems(SDK::wh::entitymodule::C_Human *human) {
            auto *app  = KCDC::Core::gApplication.get();
            auto *soul = human->GetSoul();
            if (app != nullptr && KCDC::Features::World::WorldService::IsSessionActive() && soul != nullptr && soul->IsDead()) {
                const bool local = human == KCDC::Common::LocalHumanActor() && app->GetPlayerInventoryService().HasAuthority();
                if (local || Human::FindRemoteSoul(soul) != nullptr) {
                    // Death retains the server's equipment. Put weapons back
                    // on the body before the native hand drop can transfer
                    // them into an unreplicated world item. Other carried
                    // objects still follow the original drop below.
                    auto *manager = human->GetItemAttachmentManager();
                    if (manager != nullptr) {
                        manager->InvokeHolsterWeapons();
                        manager->InvokeHolsterTorch();
                    }
                }
            }
            originalDropHandItems(human);
        }

        // The engine declares a death here, for every soul in the world, and
        // only afterwards calls HandleDeath and considers a game-over. The
        // killer's actor id rides in the record and the wire does not carry
        // one.
        //
        // A body in a saddle is held until its ride has ended the way a
        // living one does, then handed back here by the horse service.
        // Otherwise the death is reported before the original runs, so the
        // game-over the original may raise finds it already taken over.
        void __fastcall SoulDeathDispatched(SDK::wh::rpgmodule::S_SoulDeathRecord *record) {
            if (KCDC::Core::gApplication != nullptr) {
                if (KCDC::Core::gApplication->GetHorseService().HoldDeathInSaddle(*record)) {
                    return;
                }
                KCDC::Common::Replication::BodyLife::Get().NotifySoulDied(record->soul);
                KCDC::Core::gApplication->GetPlayerService().NotifySoulDied(record->soul);
            }
            originalSoulDeath(record);
        }

        void __fastcall RaiseGameOver(void *sequence, std::uint32_t reason) {
            if (KCDC::Features::Player::NativePlayerModule::IsRemoteExecution()) {
                return;
            }
            if (KCDC::Core::gApplication != nullptr && KCDC::Core::gApplication->GetPlayerService().NotifyGameOverRaised()) {
                return;
            }
            originalGameOver(sequence, reason);
        }

        bool __fastcall DeadEnter(SDK::wh::entitymodule::C_ActorActionDead *action, void *previous) {
            const bool entered = originalEnter(action, previous);
            if (!entered || KCDC::Core::gApplication == nullptr) {
                return entered;
            }
            KCDC::Common::Replication::BodyLife::Get().NotifyDeadActionEntered(action->GetActor());
            return entered;
        }

        // The singleplayer player_fx/player_death effect plays
        // special_player_death. Its FMOD event loops and silences all world
        // audio until stopped, normally by the level load after game-over.
        void __fastcall PlayDeathEffect(SDK::wh::entitymodule::C_PlayerMaterialEffects *effects) {
            if (KCDC::Features::World::WorldService::IsSessionActive()) {
                return;
            }
            originalDeathEffect(effects);
        }

        void __fastcall PlayerRevive(SDK::wh::entitymodule::C_Player *player, std::uint8_t reason) {
            const KCDC::Features::Player::NativePlayerModule::RemoteScope scope(KCDC::Features::Player::NativePlayerModule::IsRemote(player));
            originalRevive(player, reason);
            // Revive resets the animated character, which zeroes the inertia
            // Physicalize gave it, and nothing hands it back: the body would
            // jump straight up and move without inertia from here on.
            player->RestoreAnimatedCharacterInertia();
            if (KCDC::Core::gApplication != nullptr) {
                KCDC::Core::gApplication->GetPlayerService().NotifyRevived(player);
            }
        }
    } // namespace
} // namespace KCDC::Features::Player

static InitFunction initDeathHooks(
    []() {
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_Soul__DispatchSoulDeath), reinterpret_cast<LPVOID>(&KCDC::Features::Player::SoulDeathDispatched), reinterpret_cast<LPVOID *>(&KCDC::Features::Player::originalSoulDeath));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.RaiseGameOver), reinterpret_cast<LPVOID>(&KCDC::Features::Player::RaiseGameOver), reinterpret_cast<LPVOID *>(&KCDC::Features::Player::originalGameOver));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_ActorActionDead__Enter), reinterpret_cast<LPVOID>(&KCDC::Features::Player::DeadEnter), reinterpret_cast<LPVOID *>(&KCDC::Features::Player::originalEnter));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_Player__Revive), reinterpret_cast<LPVOID>(&KCDC::Features::Player::PlayerRevive), reinterpret_cast<LPVOID *>(&KCDC::Features::Player::originalRevive));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_PlayerMaterialEffects__PlayDeathEffect), reinterpret_cast<LPVOID>(&KCDC::Features::Player::PlayDeathEffect), reinterpret_cast<LPVOID *>(&KCDC::Features::Player::originalDeathEffect));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_Human__DropHandItems), reinterpret_cast<LPVOID>(&KCDC::Features::Player::DropHandItems), reinterpret_cast<LPVOID *>(&KCDC::Features::Player::originalDropHandItems));
    },
    "Death");
