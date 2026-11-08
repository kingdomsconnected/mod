#include "horse_service.h"
#include "horse.h"

#include "features/player/player.h"
#include "common/module_support.h"
#include "common/replication/body_life.h"

#include "shared/features/horse/horse_action.h"
#include "shared/features/horse/horse_damage.h"
#include "shared/features/horse/horse_mount.h"
#include "shared/features/horse/horse_ride_order.h"
#include "shared/game/buff_catalog.h"

#include "sdk/cryengine/action/c_animated_character.h"
#include "sdk/cryengine/action/i_actor_system.h"
#include "sdk/cryengine/action/i_game_framework.h"
#include "sdk/patterns.h"
#include "sdk/wh/entitymodule/c_actor.h"
#include "sdk/wh/entitymodule/c_horse.h"

#include "core/application.h"

#include <core_modules.h>
#include <logging/logger.h>
#include <networking/network_peer.h>
#include <networking/replication/replication_manager.h>

#include <mafianet/get_time.h>
#include <utils/hooking/hooking.h>

#include <algorithm>
#include <cmath>
#include <iterator>

namespace KCDC::Features::Horse {
    namespace {
        constexpr const char *kLogger = "Horse";

        // How long a scripted mount waits for the horse and the body to be
        // ready, and how often it asks the engine again meanwhile.
        constexpr MafiaNet::Time kPendingMountTimeoutMs = 10000;
        constexpr MafiaNet::Time kMountRetryMs          = 250;

        // `unconscious`: the knockout a blow gives, through the hit's
        // `AddUnconsciousnessBuffEffect` (0x182D25AE4, GUID 0x185583A78).
        constexpr KCDC::Shared::RPC::BuffId kBlowKnockoutBuff = KCDC::Shared::Game::BuffIdFromGuid("f8d60fe4-e2c1-420a-946a-213e1cd09264");
    } // namespace

    // --- The tick ---------------------------------------------------------

    void HorseService::Update() {
        auto *replication = Framework::CoreModules::GetReplication();
        if (replication == nullptr) {
            return;
        }

        // Registered here rather than at startup, because this is the first
        // point at which there is certainly a peer to register with.
        if (!_rpcsInstalled) {
            _rpcsInstalled = true;
            InstallRPCs();
        }

        ReleaseHeldDeaths();
        ReleaseHeldPutAways();

        replication->ForEach<Horse>([](Horse *horse) {
            horse->Frame();
        });

        UpdateRiders();
        UpdateLocalMount();
        UpdatePendingMount();
        FlushReports();
    }

    void HorseService::UpdateRiders() {
        auto *replication = Framework::CoreModules::GetReplication();
        if (replication == nullptr) {
            return;
        }

        // Built from the horses, because the horse carries the rider: a body
        // with no entry here is one that should be on its feet.
        std::unordered_map<std::uint64_t, Horse *> seated;
        replication->ForEach<Horse>([&seated](Horse *horse) {
            if (horse->riderNetworkId != 0 && horse->GetEntityId() != 0) {
                seated.emplace(horse->riderNetworkId, horse);
            }
        });

        replication->ForEach<KCDC::Features::Player::Human>([&seated](KCDC::Features::Player::Human *human) {
            // Our own body is ridden by us, and the engine is already doing it.
            if (human->IsOwner()) {
                return;
            }

            const auto it = seated.find(human->GetNetworkID());
            // The get-on plays where the seat filled in view and was asked
            // for as one; a ride streamed in, or an instant seat, is taken
            // straight away.
            human->ApplyRide(it != seated.end() ? it->second->GetEntityId() : 0, it != seated.end() && it->second->WasMountWitnessed() && !it->second->seatedInstantly);
        });
    }

    // --- The local player's ride ------------------------------------------

    void HorseService::NotifyRideState(SDK::EntityId riderEntityId, SDK::EntityId horseEntityId, std::uint8_t rideState, std::uint8_t dismountSide) {
        // The hook sees every actor's ride, the copies this client seats
        // included. Read live rather than from a tick-cached id, because a
        // mount can beat the first tick.
        if (riderEntityId == 0 || riderEntityId != KCDC::Common::LocalActorEntityId()) {
            return;
        }

        // A quick start, a pull-up, a stop, a rear or a throw the horse makes
        // under this player, once per entry: every copy plays it rather than
        // deciding its own.
        const bool reacting = rideState >= SDK::wh::entitymodule::C_HorseRideContext::RIDE_STATE_SYNC_4 && rideState <= SDK::wh::entitymodule::C_HorseRideContext::RIDE_STATE_SYNC_8;
        if (reacting && rideState != _rideState) {
            ++_localMountReactions;
            _localMountReactionHistory = (_localMountReactionHistory << 4) | rideState;
        }

        _rideHorseEntityId = horseEntityId;
        _rideState         = rideState;
        // Only a get-off chooses a flank, and the dismount report that
        // carries it can go out after the ride has already ended; a new
        // ride is what clears it.
        if (rideState == SDK::wh::entitymodule::C_HorseRideContext::RIDE_STATE_DISMOUNTING) {
            _dismountSide = dismountSide;
        }
        else if (rideState == SDK::wh::entitymodule::C_HorseRideContext::RIDE_STATE_MOUNTING) {
            _dismountSide = 0;
        }
    }

    std::uint8_t HorseService::DismountSideOf(SDK::EntityId horseEntityId) const {
        const Horse *horse = KCDC::Common::FindReplica<Horse>([horseEntityId](Horse *candidate) {
            return candidate->GetEntityId() == horseEntityId;
        });
        return horse != nullptr ? horse->dismountSide : 0;
    }

    std::uint64_t HorseService::MountedNetworkId() const {
        // Seated by the deny-list; naming the states that count as seated
        // instead reported a dismount every time the horse did anything.
        // Dismounting is then taken back off, and only it, because nothing
        // but a dismount request and the tear-down sets it -- so everyone
        // else's get-off starts alongside this one rather than an animation
        // later, and a refused dismount settles back to riding and reports
        // the saddle again.
        const bool inSaddle = SDK::wh::entitymodule::C_HorseRideContext::IsInSaddle(_rideState) && _rideState != SDK::wh::entitymodule::C_HorseRideContext::RIDE_STATE_DISMOUNTING;
        if (!inSaddle || _rideHorseEntityId == 0) {
            return 0;
        }

        // Only replicated horses are ours to report -- riding one of the
        // game's own is not a mount anybody else can see.
        const SDK::EntityId ridden = _rideHorseEntityId;
        auto *horse                = KCDC::Common::FindReplica<Horse>([ridden](Horse *candidate) {
            return candidate->GetEntityId() == ridden;
        });
        return horse != nullptr ? horse->GetNetworkID() : 0;
    }

    void HorseService::UpdateLocalMount() {
        if (KCDC::Core::gApplication == nullptr) {
            return;
        }

        const std::uint64_t mounted = MountedNetworkId();

        // A horse the server would not give us: ask to get off every frame
        // until the engine does, and report nothing meanwhile -- it never had
        // us seated, so no dismount is owed.
        if (_deniedHorseId != 0) {
            if (mounted == _deniedHorseId) {
                auto *framework = SDK::IGameFramework::GetInstance();
                if (auto *rider = framework != nullptr ? static_cast<SDK::wh::entitymodule::C_Actor *>(framework->GetClientActor()) : nullptr) {
                    rider->StopRiding();
                }
                return;
            }
            _deniedHorseId = 0;
        }

        // What the engine says we are on; the edge against what the server was
        // last told, and the report that follows it, are below.
        PublishSaddleChange(mounted);
    }

    void HorseService::PublishSaddleChange(std::uint64_t horseNetworkId) {
        if (horseNetworkId == _reportedHorseId) {
            return;
        }

        if (_reportedHorseId != 0) {
            Framework::Logging::GetLogger(kLogger)->info("Local player left horse {}", _reportedHorseId);
            ReportHorseMount(_reportedHorseId, false);
        }
        if (horseNetworkId != 0) {
            Framework::Logging::GetLogger(kLogger)->info("Local player mounted horse {}", horseNetworkId);
            ReportHorseMount(horseNetworkId, true);
        }

        _reportedHorseId = horseNetworkId;
    }

    void HorseService::ReportHorseMount(std::uint64_t horseNetworkId, bool mounted) {
        auto *net = Framework::CoreModules::GetNetworkPeer();
        if (net == nullptr) {
            return;
        }

        Shared::RPC::HorseMount payload;
        payload.horseNetworkId = horseNetworkId;
        payload.mounted        = mounted;
        payload.instant        = mounted && horseNetworkId == _orderedMountHorseId && _orderedMountInstant;
        payload.dismountSide   = mounted ? 0 : _dismountSide;
        if (mounted) {
            _orderedMountHorseId = 0;
        }
        net->BroadcastRPC(payload);
    }

    void HorseService::ForgetReportedSaddle(std::uint64_t horseNetworkId) {
        if (horseNetworkId != 0 && horseNetworkId == _reportedHorseId) {
            _reportedHorseId = 0;
        }
    }

    void HorseService::Reset() {
        ForgetLocalMount();
        _reportedHorseId     = 0;
        _pendingMountHorseId = 0;
        _orderedMountHorseId = 0;
        _pendingHits.clear();
        _pendingDamage.clear();
        _pendingDeaths.clear();
        _heldDeaths.clear();
        _mountsInFlight.clear();
        _heldPutAways.clear();
        _releasedPutAways.clear();
        for (auto &slot : _horseSlots) {
            slot.entityId.store(0, std::memory_order_release);
        }
        _slotsFullReported = false;
    }

    // --- Scripted mounts --------------------------------------------------

    void HorseService::UpdatePendingMount() {
        if (_pendingMountHorseId == 0) {
            return;
        }

        const MafiaNet::Time now = MafiaNet::GetTime();
        if (MountedNetworkId() == _pendingMountHorseId || now >= _pendingMountDeadline) {
            if (now >= _pendingMountDeadline) {
                Framework::Logging::GetLogger(kLogger)->warn("Gave up climbing onto horse {}: it or the local body never became ready", _pendingMountHorseId);
            }
            _pendingMountHorseId = 0;
            return;
        }
        if (now < _nextMountAttempt) {
            return;
        }
        _nextMountAttempt = now + kMountRetryMs;

        auto *horse = KCDC::Common::FindReplicaById<Horse>(_pendingMountHorseId);
        auto *rider = KCDC::Common::LocalHumanActor();
        if (horse == nullptr || rider == nullptr || !rider->IsSettled()) {
            return;
        }
        horse->RefreshNativeBinding();
        auto *body = horse->GetHorse();
        if (body == nullptr || !body->IsSettled() || rider->GetRideState() != SDK::wh::entitymodule::C_HorseRideContext::RIDE_STATE_NONE) {
            return;
        }

        // The player's own mount (C_Player::MountHorse, slot 204): unforced it
        // is the get-on a player plays at a horse, forced the seat Lua's
        // ForceMount takes -- whichever the order asked for. The local player
        // is a real player body, so the rider expansion's own checks apply to
        // it the way they do in the game. The seat is reported from the ride
        // state like any other mount, saying which of the two it was.
        if (rider->MountHorse(body, _pendingMountInstant)) {
            _orderedMountHorseId = _pendingMountHorseId;
            _orderedMountInstant = _pendingMountInstant;
        }
    }

    // --- Ending a ride ----------------------------------------------------

    void HorseService::EndRide(SDK::wh::entitymodule::C_Actor *rider) {
        if (rider == nullptr || rider->GetRideState() == SDK::wh::entitymodule::C_HorseRideContext::RIDE_STATE_NONE) {
            return;
        }

        // The game's immediate get-off picks a clear flank when there is one,
        // and ends the ride inline when it does.
        rider->StopRiding(/*immediate=*/true);
        if (rider->GetRideState() == SDK::wh::entitymodule::C_HorseRideContext::RIDE_STATE_NONE) {
            return;
        }

        // Both flanks blocked: the state machine, forced into dismounting,
        // which nothing refuses and which still sets the rider down on the
        // side it last chose.
        if (auto *context = rider->GetRideContext()) {
            context->SetState(SDK::wh::entitymodule::C_HorseRideContext::RIDE_STATE_DISMOUNTING, true);
        }
    }

    bool HorseService::HoldDeathInSaddle(const SDK::wh::rpgmodule::S_SoulDeathRecord &record) {
        auto *body = record.soul != nullptr ? record.soul->GetActor(0) : nullptr;
        if (body == nullptr) {
            return false;
        }

        // The seat is the rider-sync property on the animal, which only
        // AttachToHorse and DetachFromHorse write: the same answer from
        // either side of it.
        SDK::wh::entitymodule::C_Actor *rider = nullptr;
        if (SeatOf(body->GetEntityId()) != 0) {
            rider = body;
        }
        else if (body->GetActorClass() == SDK::wh::entitymodule::C_Horse::ActorClassId) {
            rider = static_cast<SDK::wh::entitymodule::C_Horse *>(body)->GetRider();
        }
        if (rider == nullptr) {
            return false;
        }

        EndRide(rider);
        _heldDeaths.push_back({record, body->GetEntityId()});
        return true;
    }

    void HorseService::ReleaseHeldDeaths() {
        if (_heldDeaths.empty()) {
            return;
        }

        auto *actorSystem = SDK::IActorSystem::GetInstance();
        std::vector<HeldDeath> held;
        held.swap(_heldDeaths);
        for (auto &death : held) {
            // A body that left with its soul took the death with it.
            auto *body = actorSystem != nullptr ? static_cast<SDK::wh::entitymodule::C_Actor *>(actorSystem->GetActor(death.bodyEntityId)) : nullptr;
            if (body == nullptr || body->GetSoul() != death.record.soul) {
                continue;
            }

            // Through the engine's own entry, so the death is dispatched
            // exactly as the queue would have: a saddle still occupied is
            // held again, an empty one lets it through.
            hook::call<void>(SDK::gPatterns.C_Soul__DispatchSoulDeath, &death.record);
        }
    }

    bool HorseService::IsDeadHorse(SDK::EntityId horseEntityId) const {
        auto *horse = KCDC::Common::FindReplica<Horse>([horseEntityId](Horse *candidate) {
            return candidate->GetEntityId() == horseEntityId;
        });
        return horse != nullptr && !horse->alive;
    }

    bool HorseService::IsHorseCopy(SDK::EntityId bodyEntityId) const {
        auto *horse = KCDC::Common::FindReplica<Horse>([bodyEntityId](Horse *candidate) {
            return candidate->GetEntityId() == bodyEntityId;
        });
        return horse != nullptr && !horse->IsOwner();
    }

    bool HorseService::RefusesKnockout(const SDK::wh::rpgmodule::C_Soul *soul, const SDK::CryGUID &buff) const {
        if (KCDC::Common::FromCryGuid<Shared::RPC::BuffId>(buff) != kBlowKnockoutBuff) {
            return false;
        }
        SDK::wh::entitymodule::C_Actor *body = soul != nullptr ? soul->GetActor(0) : nullptr;
        return body != nullptr && IsHorseCopy(body->GetEntityId());
    }

    // --- Life -------------------------------------------------------------

    void HorseService::QueueHit(std::uint64_t horseNetworkId, float amount, std::uint8_t reason) {
        _pendingHits.push_back({horseNetworkId, 0, amount, reason});
    }

    void HorseService::QueueDamage(std::uint64_t horseNetworkId, std::uint64_t attackerNetworkId, float amount, std::uint8_t reason) {
        _pendingDamage.push_back({horseNetworkId, attackerNetworkId, amount, reason});
    }

    void HorseService::QueueDeath(std::uint64_t horseNetworkId, std::uint64_t killerNetworkId, std::uint8_t reason) {
        _pendingDeaths.push_back({horseNetworkId, killerNetworkId, 0.0F, reason});
    }

    void HorseService::ApplyForwardedHit(std::uint64_t horseNetworkId, std::uint64_t attackerNetworkId, float amount, std::uint8_t reason) {
        auto *horse = KCDC::Common::FindReplicaById<Horse>(horseNetworkId);
        if (horse == nullptr || !horse->IsOwner() || !horse->alive) {
            return;
        }
        horse->RefreshNativeBinding();
        KCDC::Common::Replication::BodyLife::Get().ApplyBlow(horse->GetHorse(), attackerNetworkId, amount, reason);
    }

    void HorseService::FlushReports() {
        auto *net = Framework::CoreModules::GetNetworkPeer();
        if (net == nullptr) {
            _pendingHits.clear();
            _pendingDamage.clear();
            _pendingDeaths.clear();
            return;
        }

        for (const PendingDamage &pending : _pendingHits) {
            Shared::RPC::HorseHit hit;
            hit.horseNetworkId = pending.horseNetworkId;
            hit.amount         = pending.amount;
            hit.reason         = pending.reason;
            if (hit.IsSane()) {
                net->BroadcastRPC(hit, MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
            }
        }
        for (const PendingDamage &pending : _pendingDamage) {
            Shared::RPC::HorseDamaged report;
            report.horseNetworkId    = pending.horseNetworkId;
            report.attackerNetworkId = pending.attackerNetworkId;
            report.amount            = pending.amount;
            report.reason            = pending.reason;
            if (report.IsSane()) {
                net->BroadcastRPC(report, MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
            }
        }
        // After the damage, on the same ordered channel: the killing blow's
        // report reaches the server ahead of the death it caused.
        for (const PendingDamage &pending : _pendingDeaths) {
            Shared::RPC::HorseDied report;
            report.horseNetworkId  = pending.horseNetworkId;
            report.killerNetworkId = pending.attackerNetworkId;
            report.reason          = pending.reason;
            if (report.IsSane()) {
                net->BroadcastRPC(report, MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
            }
        }
        _pendingHits.clear();
        _pendingDamage.clear();
        _pendingDeaths.clear();
    }

    void HorseService::ForgetLocalMount() {
        _deniedHorseId     = 0;
        _rideHorseEntityId = 0;
        _rideState         = SDK::wh::entitymodule::C_HorseRideContext::RIDE_STATE_NONE;
        ForgetSeats();
    }

    // --- The seat register ------------------------------------------------

    template <typename Predicate>
    void HorseService::EraseSeatsWhere(Predicate &&matches) {
        for (auto it = _seats.begin(); it != _seats.end();) {
            it = matches(it->first, it->second) ? _seats.erase(it) : std::next(it);
        }
    }

    void HorseService::NotifySeat(SDK::EntityId horseEntityId, SDK::EntityId riderEntityId) {
        if (horseEntityId == 0) {
            return;
        }

        // Whoever the animal was carrying is off it either way: a seat
        // replaces the previous occupant, and a release has none.
        EraseSeatsWhere([horseEntityId](SDK::EntityId, SDK::EntityId horse) {
            return horse == horseEntityId;
        });

        if (riderEntityId != 0) {
            _seats[riderEntityId] = horseEntityId;

            // The mount that was in flight landed, whichever one it was.
            _mountsInFlight.erase(riderEntityId);
            return;
        }

        // The saddle emptied: a put-away the engine asked for while it was
        // occupied can go ahead now.
        if (const auto it = std::find(_heldPutAways.begin(), _heldPutAways.end(), horseEntityId); it != _heldPutAways.end()) {
            _heldPutAways.erase(it);
            _releasedPutAways.push_back(horseEntityId);
        }
    }

    SDK::EntityId HorseService::SeatOf(SDK::EntityId riderEntityId) const {
        const auto it = riderEntityId != 0 ? _seats.find(riderEntityId) : _seats.end();
        return it != _seats.end() ? it->second : 0;
    }

    void HorseService::ForgetSeatsFor(SDK::EntityId entityId) {
        if (entityId == 0) {
            return;
        }

        EraseSeatsWhere([entityId](SDK::EntityId rider, SDK::EntityId horse) {
            return rider == entityId || horse == entityId;
        });

        for (auto it = _mountsInFlight.begin(); it != _mountsInFlight.end();) {
            it = it->first == entityId || it->second.horseEntityId == entityId ? _mountsInFlight.erase(it) : std::next(it);
        }
        std::erase(_heldPutAways, entityId);
        std::erase(_releasedPutAways, entityId);
    }

    void HorseService::ForgetSeats() {
        _seats.clear();
        _mountsInFlight.clear();
        _heldPutAways.clear();
        _releasedPutAways.clear();
    }

    // --- Mounts in flight -------------------------------------------------

    bool HorseService::IsBodyReadyToRide(SDK::wh::entitymodule::C_Actor *actor) {
        if (actor == nullptr || !actor->IsSettled()) {
            return false;
        }
        auto *entity            = actor->GetEntity();
        auto *animatedCharacter = actor->GetAnimatedCharacterRuntime();
        return entity != nullptr && entity->GetCharacter(0) != nullptr && animatedCharacter != nullptr && animatedCharacter->GetActionController() != nullptr;
    }

    bool HorseService::IsMountOutstanding(SDK::EntityId riderEntityId, SDK::EntityId horseEntityId) {
        const auto it = _mountsInFlight.find(riderEntityId);
        if (it == _mountsInFlight.end() || it->second.horseEntityId != horseEntityId) {
            return false;
        }
        if (it->second.waitingForBodies) {
            auto *actors = SDK::IActorSystem::GetInstance();
            auto *rider  = actors != nullptr ? static_cast<SDK::wh::entitymodule::C_Actor *>(actors->GetActor(riderEntityId)) : nullptr;
            auto *horse  = actors != nullptr ? static_cast<SDK::wh::entitymodule::C_Actor *>(actors->GetActor(horseEntityId)) : nullptr;
            if (IsBodyReadyToRide(rider) && IsBodyReadyToRide(horse)) {
                // Dummy players need not pass through SetPutAway(false) when
                // their controller becomes ready. Retry from the frame tick.
                _mountsInFlight.erase(it);
                Framework::Logging::GetLogger(kLogger)->info("Rider entity {} and horse entity {} are ready; retrying mount", riderEntityId, horseEntityId);
                return false;
            }
        }
        return true;
    }

    void HorseService::NoteMountQueued(SDK::EntityId riderEntityId, SDK::EntityId horseEntityId) {
        if (riderEntityId == 0 || horseEntityId == 0) {
            return;
        }
        _mountsInFlight[riderEntityId] = MountInFlight {horseEntityId, false};
    }

    void HorseService::ForgetMountInFlight(SDK::EntityId riderEntityId) {
        const auto it = _mountsInFlight.find(riderEntityId);
        if (it != _mountsInFlight.end() && !it->second.refused) {
            _mountsInFlight.erase(it);
        }
    }

    void HorseService::NotifyMountRefused(SDK::EntityId riderEntityId, SDK::EntityId horseEntityId, bool waitingForBodies) {
        // Only mounts this client queued are tracked; the local player's own
        // and the world's riders are the engine's business.
        const auto it = _mountsInFlight.find(riderEntityId);
        if (it == _mountsInFlight.end() || it->second.horseEntityId != horseEntityId || it->second.refused) {
            return;
        }
        it->second.refused          = true;
        it->second.waitingForBodies = waitingForBodies;
        Framework::Logging::GetLogger(kLogger)->info("Mount of entity {} onto horse entity {} was refused; waiting for {}", riderEntityId, horseEntityId, waitingForBodies ? "both characters and animation controllers" : "either body to wake");
    }

    bool HorseService::HoldPutAway(SDK::wh::entitymodule::C_Actor *actor) {
        if (actor->GetActorClass() != SDK::wh::entitymodule::C_Horse::ActorClassId || !static_cast<SDK::wh::entitymodule::C_Horse *>(actor)->HasRider()) {
            return false;
        }

        const SDK::EntityId horseEntityId = actor->GetEntityId();
        if (std::find(_heldPutAways.begin(), _heldPutAways.end(), horseEntityId) == _heldPutAways.end()) {
            _heldPutAways.push_back(horseEntityId);
            Framework::Logging::GetLogger(kLogger)->info("Holding horse entity {} awake: somebody is in its saddle", horseEntityId);
        }
        return true;
    }

    void HorseService::NotifyActorWoken(SDK::EntityId entityId) {
        // Only refusals are released. A mount still queued on a rider that was
        // asleep is entered by the queue now that it updates again.
        std::erase_if(_mountsInFlight, [entityId](const std::pair<const SDK::EntityId, MountInFlight> &entry) {
            return entry.second.refused && (entry.first == entityId || entry.second.horseEntityId == entityId);
        });
    }

    void HorseService::ReleaseHeldPutAways() {
        if (_releasedPutAways.empty()) {
            return;
        }

        auto *actorSystem = SDK::IActorSystem::GetInstance();
        std::vector<SDK::EntityId> released;
        released.swap(_releasedPutAways);
        for (const SDK::EntityId horseEntityId : released) {
            // Handed back only if the engine still wants it away. The held
            // request left the wanted bit set, and `RequestPutAway` does
            // nothing for a wish it already records, so it is withdrawn --
            // which matches the applied bit and cancels -- and made again,
            // which queues it on the engine's own manager. A saddle taken in
            // between holds it once more, through the same hook.
            auto *horse = actorSystem != nullptr ? static_cast<SDK::wh::entitymodule::C_Actor *>(actorSystem->GetActor(horseEntityId)) : nullptr;
            if (horse != nullptr && horse->IsPutAwayWanted() && !horse->IsPutAway()) {
                horse->RequestPutAway(false);
                horse->RequestPutAway(true);
            }
        }
    }

    const HorseService::HorseSlot *HorseService::FindSlot(SDK::EntityId horseEntityId) const {
        if (horseEntityId == 0) {
            return nullptr;
        }
        for (const auto &slot : _horseSlots) {
            if (slot.entityId.load(std::memory_order_acquire) == horseEntityId) {
                return &slot;
            }
        }
        return nullptr;
    }

    HorseService::HorseSlot *HorseService::FindSlot(SDK::EntityId horseEntityId) {
        return const_cast<HorseSlot *>(static_cast<const HorseService *>(this)->FindSlot(horseEntityId));
    }

    HorseService::HorseSlot *HorseService::ClaimSlot(SDK::EntityId horseEntityId) {
        HorseSlot *free = nullptr;
        for (auto &slot : _horseSlots) {
            const SDK::EntityId held = slot.entityId.load(std::memory_order_relaxed);
            if (held == horseEntityId) {
                return &slot;
            }
            if (held == 0 && free == nullptr) {
                free = &slot;
            }
        }
        if (free == nullptr && !_slotsFullReported) {
            _slotsFullReported = true;
            Framework::Logging::GetLogger(kLogger)->error("More than {} horses at their controller at once; horse entity {} is left to itself", kMaxHorseSlots, horseEntityId);
        }
        return free;
    }

    void HorseService::Drive(SDK::wh::entitymodule::C_Horse *horse, SDK::CMovementRequest &request) {
        SDK::IMovementController *movement = horse->GetMovementController();
        const SDK::EntityId entityId       = horse->GetEntityId();
        HorseSlot *slot                    = movement != nullptr && entityId != 0 ? ClaimSlot(entityId) : nullptr;
        if (slot == nullptr) {
            return;
        }

        // Enrolled before the request goes in, so the horse's own request of
        // this same frame is already refused.
        slot->entityId.store(entityId, std::memory_order_release);

        _drivingRequest.store(&request, std::memory_order_release);
        movement->RequestMovement(request);
        _drivingRequest.store(nullptr, std::memory_order_release);
    }

    void HorseService::ForgetHorse(SDK::EntityId horseEntityId) {
        if (HorseSlot *slot = FindSlot(horseEntityId)) {
            slot->entityId.store(0, std::memory_order_release);
            _slotsFullReported = false;
        }
    }

    bool HorseService::OnHorseMovementRequest(SDK::EntityId entityId, const SDK::CMovementRequest &request) const {
        return FindSlot(entityId) == nullptr || _drivingRequest.load(std::memory_order_acquire) == &request;
    }

    // --- Mount jumps ------------------------------------------------------

    void HorseService::NotifyMountJumped(SDK::wh::entitymodule::C_Horse *horse) {
        SDK::wh::entitymodule::C_Actor *rider = horse->GetRider();
        const SDK::EntityId localEntityId     = KCDC::Common::LocalActorEntityId();
        if (rider == nullptr || localEntityId == 0 || rider->GetEntityId() != localEntityId) {
            return;
        }
        ++_localMountJumps;
    }

    // --- Naming -----------------------------------------------------------

    const char *HorseService::ResolveName(std::uint64_t networkId) {
        auto *horse = KCDC::Common::FindReplicaById<Horse>(networkId);
        if (horse == nullptr || horse->name.empty()) {
            return nullptr;
        }
        return horse->name.c_str();
    }

    // --- The wire ---------------------------------------------------------

    void HorseService::InstallRPCs() {
        auto *net = Framework::CoreModules::GetNetworkPeer();
        if (net == nullptr) {
            return;
        }

        net->RegisterRPC<Shared::RPC::HorseAction>([](const Shared::RPC::HorseAction &msg, MafiaNet::Packet *) {
            auto *horse = KCDC::Common::FindReplicaById<Horse>(msg.horseNetworkId);
            if (horse == nullptr) {
                return;
            }

            // Reached from outside the tick, so the body this replica is bound
            // to is re-read rather than taken on the last frame's word.
            horse->RefreshNativeBinding();
            auto *body = horse->GetHorse();
            if (body == nullptr) {
                return;
            }

            switch (msg.type) {
            case Shared::RPC::HorseAction::RearAndThrowDown:
                body->RearAndThrowDown();
                Framework::Logging::GetLogger(kLogger)->info("Horse {} was told to rear", msg.horseNetworkId);
                break;

            case Shared::RPC::HorseAction::PullDownRider: {
                // Asked of the attacker's own body, which is what the engine's
                // binding takes -- it finds the horse from the victim itself.
                auto *attacker = KCDC::Common::FindReplicaById<KCDC::Features::Player::Human>(msg.attackerNetworkId);
                auto *puller   = attacker != nullptr ? attacker->ResolveBody() : nullptr;
                auto *rider    = horse->GetRider();
                if (puller != nullptr && rider != nullptr) {
                    puller->RequestPullDownRider(rider->GetEntityId());
                    Framework::Logging::GetLogger(kLogger)->info("Player {} pulled the rider off horse {}", msg.attackerNetworkId, msg.horseNetworkId);
                }
                break;
            }

            case Shared::RPC::HorseAction::Wake:
                // Only on the client running the horse, which the order was
                // sent to and may since have handed it on: the copies follow
                // its soul through `unconscious`.
                if (horse->IsOwner() && body->GetSoul() != nullptr) {
                    horse->Wake();
                    Framework::Logging::GetLogger(kLogger)->info("Horse {} was woken", msg.horseNetworkId);
                }
                break;

            default: break;
            }
        });

        net->RegisterRPC<Shared::RPC::HorseRideOrder>([](const Shared::RPC::HorseRideOrder &msg, MafiaNet::Packet *) {
            if (KCDC::Core::gApplication == nullptr || msg.horseNetworkId == 0) {
                return;
            }
            HorseService &service = KCDC::Core::gApplication->GetHorseService();

            if (msg.mount) {
                service._pendingMountHorseId  = msg.horseNetworkId;
                service._pendingMountInstant  = msg.instant;
                service._pendingMountDeadline = MafiaNet::GetTime() + kPendingMountTimeoutMs;
                service._nextMountAttempt     = 0;
                if (service._deniedHorseId == msg.horseNetworkId) {
                    service._deniedHorseId = 0;
                }
                Framework::Logging::GetLogger(kLogger)->info("The server put us on horse {}", msg.horseNetworkId);
                return;
            }

            // Forget the mount and let the frame loop walk us back off it.
            // Only that saddle: the order is a round trip old, and by now the
            // local player may have climbed onto a horse nobody has refused.
            service._deniedHorseId = msg.horseNetworkId;
            service.ForgetReportedSaddle(msg.horseNetworkId);
            if (service._pendingMountHorseId == msg.horseNetworkId) {
                service._pendingMountHorseId = 0;
            }
            Framework::Logging::GetLogger(kLogger)->info("The server took us off horse {}; getting off", msg.horseNetworkId);
        });

        // A blow another player's game dealt this horse, for this client to
        // apply because it is the one running it.
        net->RegisterRPC<Shared::RPC::HorseHit>([](const Shared::RPC::HorseHit &msg, MafiaNet::Packet *) {
            if (KCDC::Core::gApplication != nullptr && msg.IsSane()) {
                KCDC::Core::gApplication->GetHorseService().ApplyForwardedHit(msg.horseNetworkId, msg.attackerNetworkId, msg.amount, msg.reason);
            }
        });
    }
} // namespace KCDC::Features::Horse
