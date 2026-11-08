#include "siege_service.h"

#include "core/server.h"
#include "core/server_clock.h"
#include "features/animation/animation_verbs.h"
#include "features/navigation/navigation_service.h"
#include "features/player/player_roster.h"
#include "features/siege/siege_scripting.h"

#include "shared/features/player/player_entity.h"
#include "shared/pose_math.h"

#include <core_modules.h>
#include <logging/logger.h>
#include <networking/network_peer.h>
#include <networking/replication/replication_manager.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace KCDC::Features::Siege {
    namespace {
        constexpr const char *kLogger = "Siege";

        /** How far around a target the navigation mesh's floor is looked for, across and up or down. */
        constexpr float kFloorSearchRadius = 8.0F;
        constexpr float kFloorSearchHeight = 80.0F;

        float GroundDistance(const glm::vec3 &a, const glm::vec3 &b) {
            return glm::length(glm::vec2(a.x - b.x, a.y - b.y));
        }

        /** The verb `siegeEngineUse` names an action by. */
        const char *UseVerb(Shared::RPC::SiegeUseAction action) {
            switch (action) {
            case Shared::RPC::SiegeUseAction::Load: return "load";
            case Shared::RPC::SiegeUseAction::Fire: return "fire";
            case Shared::RPC::SiegeUseAction::Operate: return "operate";
            case Shared::RPC::SiegeUseAction::Leave: return "leave";
            case Shared::RPC::SiegeUseAction::Winch: return "winch";
            }
            return "operate";
        }
    } // namespace

    SiegeService &SiegeService::Get() {
        static SiegeService instance;
        return instance;
    }

    void SiegeService::Install() {
        auto *net = Framework::CoreModules::GetNetworkPeer();
        if (net == nullptr) {
            return;
        }
        net->RegisterRPC<Shared::RPC::SiegeImpactReport>([this](const Shared::RPC::SiegeImpactReport &report, MafiaNet::Packet *packet) {
            OnImpactReport(report, packet);
        });
        net->RegisterRPC<Shared::RPC::SiegeUse>([this](const Shared::RPC::SiegeUse &use, MafiaNet::Packet *packet) {
            OnUse(use, packet);
        });
        net->RegisterRPC<Shared::RPC::SiegeAim>([this](const Shared::RPC::SiegeAim &aim, MafiaNet::Packet *packet) {
            OnAim(aim, packet);
        });
    }

    Shared::Entities::SiegeEngineEntity *SiegeService::Spawn(std::size_t assetIndex, const glm::vec3 &position, const glm::quat &rotation, MafiaNet::VirtualWorldId virtualWorld) {
        const Shared::Game::SiegeEngineAsset *asset = Shared::Game::SiegeEngineAssetAt(assetIndex);
        if (asset == nullptr || !Shared::Game::IsFinite(position) || !Shared::Game::IsUsableRotation(rotation)) {
            return nullptr;
        }

        auto *replication = Framework::CoreModules::GetReplication();
        auto *engine      = replication != nullptr ? replication->CreateEntity<Shared::Entities::SiegeEngineEntity>() : nullptr;
        if (engine == nullptr) {
            Framework::Logging::GetLogger(kLogger)->error("Could not create a siege engine; is '{}' registered?", Shared::Entities::SiegeEngineEntity::kTypeName);
            return nullptr;
        }

        // All of it before the entity replicates, so it rides the construction
        // snapshot rather than arriving a tick after the engine was built.
        engine->assetIndex      = static_cast<std::uint8_t>(assetIndex);
        engine->position        = position;
        engine->rotation        = glm::normalize(rotation);
        engine->streaming.range = Shared::Entities::SiegeEngineEntity::kStreamRange;
        engine->SetVirtualWorld(virtualWorld);
        TuningOf(*engine);

        Framework::Logging::GetLogger(kLogger)->info("Spawned {} {} in world {} at {:.2f}, {:.2f}, {:.2f}", asset->name, engine->GetNetworkID(), virtualWorld, position.x, position.y, position.z);
        Scripting::Events::SiegeEngineSpawned(engine->GetNetworkID());
        return engine;
    }

    void SiegeService::Destroy(Shared::Entities::SiegeEngineEntity *engine) {
        auto *replication = Framework::CoreModules::GetReplication();
        if (engine == nullptr || replication == nullptr) {
            return;
        }

        // While the entity is still here, so a handler can read it. A shot in
        // the air keeps flying: it no longer needs the engine that threw it.
        Scripting::Events::SiegeEngineDestroyed(engine->GetNetworkID());
        _tuning.erase(engine->GetNetworkID());
        _lastAimMs.erase(engine->GetNetworkID());
        _shots.erase(std::remove_if(_shots.begin(), _shots.end(), [engine](const Shot &shot) {
            return !shot.launched && shot.engineNetworkId == engine->GetNetworkID();
        }), _shots.end());
        replication->DestroyEntity(engine);
    }

    std::size_t SiegeService::DestroyAll(std::optional<MafiaNet::VirtualWorldId> virtualWorld) {
        const std::vector<Shared::Entities::SiegeEngineEntity *> doomed = Collect(virtualWorld);
        for (Shared::Entities::SiegeEngineEntity *engine : doomed) {
            Destroy(engine);
        }
        return doomed.size();
    }

    std::vector<Shared::Entities::SiegeEngineEntity *> SiegeService::Collect(std::optional<MafiaNet::VirtualWorldId> virtualWorld) {
        return CollectSiegeEntities<Shared::Entities::SiegeEngineEntity>(virtualWorld);
    }

    Shared::Entities::SiegeEngineEntity *SiegeService::Find(std::uint64_t networkId) {
        return FindSiegeEntity<Shared::Entities::SiegeEngineEntity>(networkId);
    }

    SiegeService::Tuning &SiegeService::TuningOf(Shared::Entities::SiegeEngineEntity &engine) {
        auto found = _tuning.find(engine.GetNetworkID());
        if (found == _tuning.end()) {
            Tuning tuning;
            tuning.damage       = engine.Asset().damage;
            tuning.damageRadius = engine.Asset().damageRadius;
            tuning.range        = engine.Asset().defaultRange;
            found               = _tuning.emplace(engine.GetNetworkID(), tuning).first;
        }
        return found->second;
    }

    float SiegeService::GetDamage(Shared::Entities::SiegeEngineEntity &engine) const {
        const auto found = _tuning.find(engine.GetNetworkID());
        return found != _tuning.end() ? found->second.damage : engine.Asset().damage;
    }

    void SiegeService::SetDamage(Shared::Entities::SiegeEngineEntity &engine, float damage) {
        if (std::isfinite(damage)) {
            TuningOf(engine).damage = std::clamp(damage, 0.0F, Shared::RPC::SiegeImpact::kMaxDamage);
        }
    }

    float SiegeService::GetDamageRadius(Shared::Entities::SiegeEngineEntity &engine) const {
        const auto found = _tuning.find(engine.GetNetworkID());
        return found != _tuning.end() ? found->second.damageRadius : engine.Asset().damageRadius;
    }

    void SiegeService::SetDamageRadius(Shared::Entities::SiegeEngineEntity &engine, float radius) {
        if (std::isfinite(radius)) {
            TuningOf(engine).damageRadius = std::clamp(radius, 0.0F, Shared::RPC::SiegeImpact::kMaxDamageRadius);
        }
    }

    float SiegeService::GetRange(Shared::Entities::SiegeEngineEntity &engine) const {
        const auto found = _tuning.find(engine.GetNetworkID());
        return found != _tuning.end() ? found->second.range : engine.Asset().defaultRange;
    }

    void SiegeService::SetRange(Shared::Entities::SiegeEngineEntity &engine, float range) {
        if (std::isfinite(range)) {
            TuningOf(engine).range = std::clamp(range, engine.Asset().minRange, engine.Asset().maxRange);
        }
    }

    void SiegeService::SetMaxHealth(Shared::Entities::SiegeEngineEntity &engine, float maxHealth) {
        if (!std::isfinite(maxHealth)) {
            return;
        }
        engine.maxHealth = std::max(0.0F, maxHealth);
        engine.health    = engine.maxHealth;
        if (engine.state == Shared::Entities::SiegeState::Wrecked) {
            Repair(engine);
        }
    }

    void SiegeService::SetHealth(Shared::Entities::SiegeEngineEntity &engine, float health) {
        if (!std::isfinite(health) || !engine.CanBeHarmed()) {
            return;
        }
        engine.health = std::clamp(health, 0.0F, engine.maxHealth);
        if (engine.health <= 0.0F && engine.state != Shared::Entities::SiegeState::Wrecked) {
            Wreck(engine, 0);
        }
        else if (engine.health > 0.0F && engine.state == Shared::Entities::SiegeState::Wrecked) {
            Repair(engine);
        }
    }

    void SiegeService::Damage(Shared::Entities::SiegeEngineEntity &engine, float amount, std::uint64_t attackerNetworkId) {
        if (!engine.CanBeHarmed() || engine.state == Shared::Entities::SiegeState::Wrecked || !(amount > 0.0F)) {
            return;
        }
        engine.health = std::max(0.0F, engine.health - amount);

        const std::uint64_t engineNetworkId = engine.GetNetworkID();
        Framework::Logging::GetLogger(kLogger)->info("{} {} took {:.1f}, {:.1f} of {:.1f} left", engine.Asset().name, engineNetworkId, amount, engine.health, engine.maxHealth);
        Scripting::Events::SiegeEngineDamaged(engineNetworkId, amount, attackerNetworkId);

        // A handler may have repaired, re-armed or destroyed it.
        Shared::Entities::SiegeEngineEntity *still = Find(engineNetworkId);
        if (still != nullptr && still->CanBeHarmed() && still->health <= 0.0F && still->state != Shared::Entities::SiegeState::Wrecked) {
            Wreck(*still, attackerNetworkId);
        }
    }

    void SiegeService::Wreck(Shared::Entities::SiegeEngineEntity &engine, std::uint64_t attackerNetworkId) {
        engine.state        = Shared::Entities::SiegeState::Wrecked;
        engine.stepStartMs  = 0;
        engine.stepProgress = 0.0F;
        engine.health       = 0.0F;
        engine.operatorNetworkId = 0;
        engine.crewNetworkIds.fill(0);

        // A stone still in the sling goes down with the arm.
        _shots.erase(std::remove_if(_shots.begin(), _shots.end(), [&engine](const Shot &shot) {
            return !shot.launched && shot.engineNetworkId == engine.GetNetworkID();
        }), _shots.end());

        Framework::Logging::GetLogger(kLogger)->info("{} {} is wrecked", engine.Asset().name, engine.GetNetworkID());
        Scripting::Events::SiegeEngineWrecked(engine.GetNetworkID(), attackerNetworkId);
    }

    void SiegeService::Repair(Shared::Entities::SiegeEngineEntity &engine) {
        engine.health = engine.maxHealth;
        if (engine.state == Shared::Entities::SiegeState::Wrecked) {
            engine.state        = Shared::Entities::SiegeState::Idle;
            engine.stepStartMs  = 0;
            engine.stepProgress = 0.0F;
            Framework::Logging::GetLogger(kLogger)->info("{} {} is repaired", engine.Asset().name, engine.GetNetworkID());
        }
    }

    void SiegeService::SetAmmo(Shared::Entities::SiegeEngineEntity &engine, std::int32_t ammo) {
        engine.ammo = ammo < 0 ? Shared::Entities::SiegeEngineEntity::kUnlimitedAmmo : ammo;
    }

    void SiegeService::SetCrewRequired(Shared::Entities::SiegeEngineEntity &engine, std::uint8_t required) {
        // Only an engine with crew places has anyone to wait for.
        engine.crewRequired = static_cast<std::uint8_t>(std::min<std::size_t>(required, engine.CrewPlaces()));
        HoldForCrew(engine, KCDC::ServerClock::NowMs());
    }

    void SiegeService::LeaveAllPlaces(std::uint64_t playerNetworkId) {
        if (playerNetworkId == 0) {
            return;
        }
        if (auto *replication = Framework::CoreModules::GetReplication()) {
            replication->ForEach<Shared::Entities::SiegeEngineEntity>([this, playerNetworkId](Shared::Entities::SiegeEngineEntity *engine) {
                LeavePlaces(*engine, playerNetworkId);
            });
        }
    }

    void SiegeService::LeavePlaces(Shared::Entities::SiegeEngineEntity &engine, std::uint64_t playerNetworkId) {
        if (engine.operatorNetworkId == playerNetworkId) {
            engine.operatorNetworkId = 0;
        }
        bool crewChanged = false;
        for (std::uint64_t &crew : engine.crewNetworkIds) {
            if (crew == playerNetworkId) {
                crew        = 0;
                crewChanged = true;
            }
        }
        if (crewChanged) {
            HoldForCrew(engine, KCDC::ServerClock::NowMs());
        }
    }

    void SiegeService::OnPlayerDisconnect(std::uint64_t playerNetworkId) {
        LeaveAllPlaces(playerNetworkId);
        // Nothing to stop: the body leaves with the player.
        _pulling.erase(playerNetworkId);
    }

    bool SiegeService::SetOperator(Shared::Entities::SiegeEngineEntity &engine, std::uint64_t playerNetworkId) {
        if (playerNetworkId == 0) {
            engine.operatorNetworkId = 0;
            return true;
        }
        if (Player::Players::Find(playerNetworkId) == nullptr) {
            return false;
        }
        LeaveAllPlaces(playerNetworkId);
        engine.operatorNetworkId = playerNetworkId;
        Framework::Logging::GetLogger(kLogger)->info("Player {} operates {} {}", playerNetworkId, engine.Asset().name, engine.GetNetworkID());
        return true;
    }

    void SiegeService::SetSpeed(Shared::Entities::SiegeEngineEntity &engine, float speed) {
        if (!std::isfinite(speed)) {
            return;
        }
        const float clamped = std::clamp(speed, Shared::Entities::SiegeEngineEntity::kMinSpeed, Shared::Entities::SiegeEngineEntity::kMaxSpeed);
        if (clamped == engine.speed) {
            return;
        }

        // Back-dated so a step running when the speed changes carries on from
        // where it has got to rather than jumping. A held step keeps its place
        // as a fraction, which the new speed reads the same.
        if (Shared::Entities::IsSiegeMoving(engine.state) && engine.stepStartMs != 0) {
            const std::uint64_t now    = KCDC::ServerClock::NowMs();
            const float phase          = engine.PhaseAfter(now > engine.stepStartMs ? static_cast<std::int64_t>(now - engine.stepStartMs) : 0);
            engine.speed               = clamped;
            const std::uint64_t behind = static_cast<std::uint64_t>(phase * static_cast<float>(engine.CurrentStepDurationMs()));
            engine.stepStartMs         = now - std::min(now, behind);

            // A shot not yet let go keeps to the clip, and so does every
            // client's copy of when it goes.
            for (Shot &shot : _shots) {
                if (!shot.launched && shot.engineNetworkId == engine.GetNetworkID()) {
                    shot.releaseAtMs    = engine.stepStartMs + engine.ReleaseDelayMs();
                    engine.shotLaunchMs = shot.releaseAtMs;
                }
            }
            return;
        }
        engine.speed = clamped;
    }

    SiegeResult SiegeService::Load(Shared::Entities::SiegeEngineEntity &engine) {
        SiegeResult result;
        if (engine.state == Shared::Entities::SiegeState::Wrecked) {
            result.reason = "is wrecked";
            return result;
        }
        if (engine.state == Shared::Entities::SiegeState::Ready) {
            result.reason = "is already loaded";
            return result;
        }
        if (engine.state != Shared::Entities::SiegeState::Idle) {
            result.reason = engine.state == Shared::Entities::SiegeState::Firing ? "is still firing" : "is already being loaded";
            return result;
        }
        if (engine.ammo == 0) {
            result.reason = "has nothing left to throw";
            return result;
        }

        if (engine.ammo > 0) {
            --engine.ammo;
        }
        const bool draws    = engine.StepDurationMs(Shared::Game::SiegeStep::Draw) > 0;
        engine.state        = draws ? Shared::Entities::SiegeState::Drawing : Shared::Entities::SiegeState::Loading;
        engine.stepStartMs  = KCDC::ServerClock::NowMs();
        engine.stepProgress = 0.0F;
        HoldForCrew(engine, engine.stepStartMs);

        result.accepted = true;
        result.seconds  = static_cast<float>(engine.StepDurationMs(Shared::Game::SiegeStep::Draw) + engine.StepDurationMs(Shared::Game::SiegeStep::Load)) / 1000.0F;
        Framework::Logging::GetLogger(kLogger)->info("{} {} is loading ({:.1f}s{})", engine.Asset().name, engine.GetNetworkID(), result.seconds, engine.stepStartMs == 0 ? ", waiting for its crew" : "");
        return result;
    }

    void SiegeService::HoldForCrew(Shared::Entities::SiegeEngineEntity &engine, std::uint64_t now) {
        if (engine.state != Shared::Entities::SiegeState::Drawing) {
            return;
        }
        const bool manned = engine.CrewCount() >= engine.crewRequired;
        if (!manned && engine.stepStartMs != 0) {
            // Stopped where it stands: the fraction survives a speed change.
            engine.stepProgress = engine.PhaseAfter(now > engine.stepStartMs ? static_cast<std::int64_t>(now - engine.stepStartMs) : 0);
            engine.stepStartMs  = 0;
        }
        else if (manned && engine.stepStartMs == 0) {
            const std::uint64_t behind = static_cast<std::uint64_t>(engine.stepProgress * static_cast<float>(engine.CurrentStepDurationMs()));
            engine.stepStartMs         = std::max<std::uint64_t>(1, now - std::min(now, behind));
        }
    }

    Shared::Game::Siege::AimResult SiegeService::CanHit(const Shared::Entities::SiegeEngineEntity &engine, const glm::vec3 &target) {
        Shared::Game::Siege::Launch launch;
        return Shared::Game::Siege::SolveLaunch(engine.Asset(), engine.position, Shared::Game::Siege::FacingRotation(engine.position, target), target, launch);
    }

    SiegeResult SiegeService::Aim(Shared::Entities::SiegeEngineEntity &engine, const glm::vec3 &target) {
        SiegeResult result;
        if (!Shared::Game::IsFinite(target)) {
            result.reason = "was given a target that is not a point";
            return result;
        }
        if (engine.state == Shared::Entities::SiegeState::Firing) {
            result.reason = "is firing";
            return result;
        }
        if (engine.state == Shared::Entities::SiegeState::Wrecked) {
            result.reason = "is wrecked";
            return result;
        }
        engine.rotation  = Shared::Game::Siege::FacingRotation(engine.position, target);
        engine.aimTarget = target;
        engine.aimed     = true;
        result.accepted  = true;
        return result;
    }

    SiegeResult SiegeService::Fire(Shared::Entities::SiegeEngineEntity &engine, const glm::vec3 &target, std::uint64_t attackerNetworkId) {
        SiegeResult result;
        if (!Shared::Game::IsFinite(target)) {
            result.reason = "was given a target that is not a point";
            return result;
        }
        if (engine.state != Shared::Entities::SiegeState::Ready) {
            result.reason = engine.state == Shared::Entities::SiegeState::Wrecked  ? "is wrecked"
                            : engine.state == Shared::Entities::SiegeState::Firing ? "is already firing"
                            : Shared::Entities::IsSiegeMoving(engine.state)        ? "is still loading"
                                                                                   : "is not loaded";
            Framework::Logging::GetLogger(kLogger)->info("{} {} refused to fire: {} ({})", engine.Asset().name, engine.GetNetworkID(), result.reason, Shared::Entities::SiegeStateName(engine.state));
            return result;
        }

        // An engine throws in its own vertical plane, so it is turned to face
        // the target before anything is solved: the release point moves with it.
        const glm::quat facing = Shared::Game::Siege::FacingRotation(engine.position, target);
        Shot shot;
        const Shared::Game::Siege::AimResult aim = Shared::Game::Siege::SolveLaunch(engine.Asset(), engine.position, facing, target, shot.launch);
        if (aim != Shared::Game::Siege::AimResult::Ok) {
            result.reason = std::string("cannot fire: ") + Shared::Game::Siege::AimResultReason(aim);
            Framework::Logging::GetLogger(kLogger)->info("{} {} refused to fire at {:.1f}, {:.1f}, {:.1f}: {}", engine.Asset().name, engine.GetNetworkID(), target.x, target.y, target.z, result.reason);
            return result;
        }

        const std::uint64_t now = KCDC::ServerClock::NowMs();
        engine.rotation         = facing;
        engine.aimTarget        = target;
        engine.aimed            = true;
        engine.state            = Shared::Entities::SiegeState::Firing;
        engine.stepStartMs      = now;

        shot.shotId            = _nextShotId++;
        shot.engineNetworkId   = engine.GetNetworkID();
        shot.attackerNetworkId = attackerNetworkId;
        shot.assetIndex        = engine.assetIndex;
        shot.virtualWorld      = engine.GetVirtualWorld();
        shot.target            = target;
        shot.tuning            = TuningOf(engine);
        shot.releaseAtMs       = now + engine.ReleaseDelayMs();
        if (_nextShotId == 0) {
            _nextShotId = 1;
        }
        _shots.push_back(shot);

        // On the engine from now, so every client that has it launches the
        // stone itself as the arm lets it go.
        engine.shotId       = shot.shotId;
        engine.shotOrigin   = shot.launch.origin;
        engine.shotVelocity = shot.launch.velocity;
        engine.shotLaunchMs = shot.releaseAtMs;

        result.accepted = true;
        result.seconds  = static_cast<float>(engine.ReleaseDelayMs()) / 1000.0F;
        Framework::Logging::GetLogger(kLogger)->info("{} {} fires shot {} at {:.1f}, {:.1f}, {:.1f}: {:.1f} m/s, {:.1f}s in the air", engine.Asset().name, engine.GetNetworkID(), shot.shotId, target.x, target.y, target.z, glm::length(shot.launch.velocity), shot.launch.flightSeconds);
        return result;
    }

    std::uint64_t SiegeService::ChooseReporter(const Shot &shot) {
        Shared::Entities::HumanEntity *attacker = Player::Players::Find(shot.attackerNetworkId);
        if (attacker != nullptr && attacker->transformReady && attacker->GetVirtualWorld() == shot.virtualWorld && GroundDistance(attacker->position, shot.target) <= kReporterRadius) {
            return attacker->GetNetworkID();
        }

        std::uint64_t nearest = 0;
        float nearestDistance = kReporterRadius;
        if (auto *replication = Framework::CoreModules::GetReplication()) {
            replication->ForEach<Shared::Entities::HumanEntity>([&](Shared::Entities::HumanEntity *player) {
                if (!player->transformReady || player->GetVirtualWorld() != shot.virtualWorld) {
                    return;
                }
                const float distance = GroundDistance(player->position, shot.target);
                if (distance <= nearestDistance) {
                    nearestDistance = distance;
                    nearest         = player->GetNetworkID();
                }
            });
        }
        return nearest;
    }

    void SiegeService::Launch(Shot &shot, std::uint64_t now) {
        shot.launched          = true;
        shot.reporterNetworkId = ChooseReporter(shot);
        shot.deadlineMs        = now + static_cast<std::uint64_t>(shot.launch.flightSeconds * 1000.0F) + kReportGraceMs;

        // Stamped with the clock the engine said, not this tick's: a client
        // that launched it from the engine's copy agrees on every frame of it.
        Shared::RPC::SiegeShot message;
        message.shotId          = shot.shotId;
        message.engineNetworkId = shot.engineNetworkId;
        message.assetIndex      = shot.assetIndex;
        message.origin          = shot.launch.origin;
        message.velocity        = shot.launch.velocity;
        message.launchMs        = shot.releaseAtMs;

        auto *replication = Framework::CoreModules::GetReplication();
        auto *net         = Framework::CoreModules::GetNetworkPeer();
        std::size_t told  = 0;
        if (replication != nullptr && net != nullptr) {
            replication->ForEach<Shared::Entities::HumanEntity>([&](Shared::Entities::HumanEntity *viewer) {
                if (viewer->GetVirtualWorld() != shot.virtualWorld || !viewer->transformReady) {
                    return;
                }
                const bool reporter = viewer->GetNetworkID() == shot.reporterNetworkId;
                if (!reporter && glm::distance(viewer->position, shot.launch.origin) > kShotRelayRadius && glm::distance(viewer->position, shot.target) > kShotRelayRadius) {
                    return;
                }
                message.report = reporter;
                net->SendRPC(message, MafiaNet::ToGuid(viewer->ownerGUID), MafiaNet::Priority::High, MafiaNet::Reliability::Reliable);
                ++told;
            });
        }

        Framework::Logging::GetLogger(kLogger)->info("Shot {} let go from {:.1f}, {:.1f}, {:.1f}; told {} player(s), reporter {}", shot.shotId, shot.launch.origin.x, shot.launch.origin.y, shot.launch.origin.z, told, shot.reporterNetworkId);
        Scripting::Events::SiegeFired(shot.engineNetworkId, shot.attackerNetworkId, shot.target);
    }

    void SiegeService::OnUse(const Shared::RPC::SiegeUse &use, MafiaNet::Packet *packet) {
        if (!use.IsSane()) {
            return;
        }
        Shared::Entities::HumanEntity *player       = packet != nullptr ? Player::Players::FromPacket(packet) : nullptr;
        Shared::Entities::SiegeEngineEntity *engine = Find(use.engineNetworkId);
        if (player == nullptr || engine == nullptr || player->GetVirtualWorld() != engine->GetVirtualWorld()) {
            return;
        }
        const std::uint64_t playerNetworkId = player->GetNetworkID();
        const std::uint64_t engineNetworkId = engine->GetNetworkID();

        // Stepping away is always allowed, wherever the player stands.
        if (use.action == Shared::RPC::SiegeUseAction::Leave) {
            LeavePlaces(*engine, playerNetworkId);
            Scripting::Events::SiegeEngineUse(playerNetworkId, engineNetworkId, UseVerb(use.action));
            return;
        }

        if (!engine->usable || engine->state == Shared::Entities::SiegeState::Wrecked || GroundDistance(player->position, engine->position) > kUseReach) {
            return;
        }

        // What was asked, against what the engine is now: a Fire pressed on
        // an engine that has since fired is dropped, not turned into a reload,
        // and only the one in its place works it.
        switch (use.action) {
        case Shared::RPC::SiegeUseAction::Operate:
            if (engine->operatorNetworkId != 0) {
                return;
            }
            break;
        case Shared::RPC::SiegeUseAction::Winch:
            if (engine->crewRequired == 0 || engine->CrewPlaceOf(playerNetworkId) >= 0 || engine->CrewCount() >= engine->CrewPlaces()) {
                return;
            }
            break;
        case Shared::RPC::SiegeUseAction::Load:
            if (engine->operatorNetworkId != playerNetworkId || engine->state != Shared::Entities::SiegeState::Idle) {
                return;
            }
            break;
        case Shared::RPC::SiegeUseAction::Fire:
            if (engine->operatorNetworkId != playerNetworkId || engine->state != Shared::Entities::SiegeState::Ready) {
                return;
            }
            break;
        case Shared::RPC::SiegeUseAction::Leave: return;
        }

        if (!Scripting::Events::SiegeEngineUse(playerNetworkId, engineNetworkId, UseVerb(use.action))) {
            return;
        }
        // A handler may have destroyed the engine, or worked it itself.
        engine = Find(engineNetworkId);
        if (engine == nullptr) {
            return;
        }

        switch (use.action) {
        case Shared::RPC::SiegeUseAction::Operate:
            if (engine->operatorNetworkId == 0) {
                SetOperator(*engine, playerNetworkId);
            }
            return;
        case Shared::RPC::SiegeUseAction::Winch: {
            if (engine->CrewPlaceOf(playerNetworkId) >= 0) {
                return;
            }
            // The free winch nearest the player.
            int best           = -1;
            float bestDistance = std::numeric_limits<float>::max();
            for (std::size_t place = 0; place < engine->CrewPlaces(); ++place) {
                if (engine->crewNetworkIds[place] != 0) {
                    continue;
                }
                const glm::vec3 at   = engine->CrewPlacePosition(place);
                const float distance = GroundDistance(player->position, at);
                if (distance < bestDistance) {
                    bestDistance = distance;
                    best         = static_cast<int>(place);
                }
            }
            if (best >= 0) {
                LeaveAllPlaces(playerNetworkId);
                engine->crewNetworkIds[static_cast<std::size_t>(best)] = playerNetworkId;
                HoldForCrew(*engine, KCDC::ServerClock::NowMs());
                Framework::Logging::GetLogger(kLogger)->info("Player {} takes winch {} of {} {}", playerNetworkId, best, engine->Asset().name, engineNetworkId);
            }
            return;
        }
        case Shared::RPC::SiegeUseAction::Load:
            if (engine->state == Shared::Entities::SiegeState::Idle) {
                Load(*engine);
            }
            return;
        case Shared::RPC::SiegeUseAction::Fire: {
            if (engine->state != Shared::Entities::SiegeState::Ready) {
                return;
            }
            // Where it is laid; before the operator has laid it, along their
            // facing at the engine's range, onto the navigation mesh's floor
            // there. The server has no terrain of its own, and the stone comes
            // down wherever the world stops it anyway.
            glm::vec3 target = engine->aimTarget;
            if (!engine->aimed) {
                const glm::vec3 facing = player->rotation * glm::vec3(0.0F, 1.0F, 0.0F);
                const glm::vec2 ahead  = glm::dot(glm::vec2(facing), glm::vec2(facing)) > 1e-6F ? glm::normalize(glm::vec2(facing)) : glm::vec2(0.0F, 1.0F);
                const float range      = GetRange(*engine);
                target                 = glm::vec3(engine->position.x + ahead.x * range, engine->position.y + ahead.y * range, engine->position.z);
                Navigation::NavigationService &navigation = Server::Get()->GetNavigation();
                if (const std::optional<float> floor = navigation.GetMesh().FloorAt(target, navigation.Options(Navigation::DoorPolicy::Any, kFloorSearchRadius, kFloorSearchHeight))) {
                    target.z = *floor;
                }
            }
            const SiegeResult result = Fire(*engine, target, playerNetworkId);
            if (!result.accepted) {
                Framework::Logging::GetLogger(kLogger)->info("Player {} could not fire {} {}: {}", playerNetworkId, engine->Asset().name, engineNetworkId, result.reason);
            }
            return;
        }
        case Shared::RPC::SiegeUseAction::Leave: return;
        }
    }

    void SiegeService::OnAim(const Shared::RPC::SiegeAim &aim, MafiaNet::Packet *packet) {
        if (!aim.IsSane()) {
            return;
        }
        Shared::Entities::HumanEntity *player       = packet != nullptr ? Player::Players::FromPacket(packet) : nullptr;
        Shared::Entities::SiegeEngineEntity *engine = Find(aim.engineNetworkId);
        if (player == nullptr || engine == nullptr || player->GetVirtualWorld() != engine->GetVirtualWorld() || engine->operatorNetworkId != player->GetNetworkID() || engine->state == Shared::Entities::SiegeState::Firing) {
            return;
        }
        // The client throttles its look; one that does not is not let turn the engine every packet.
        const std::uint64_t now  = KCDC::ServerClock::NowMs();
        std::uint64_t &lastAimMs = _lastAimMs[engine->GetNetworkID()];
        if (now - lastAimMs < kAimIntervalMs) {
            return;
        }
        lastAimMs = now;
        if (const std::optional<glm::vec3> target = Shared::Game::Siege::ReachableAlong(engine->Asset(), engine->position, aim.target)) {
            Aim(*engine, *target);
        }
    }

    void SiegeService::OnImpactReport(const Shared::RPC::SiegeImpactReport &report, MafiaNet::Packet *packet) {
        if (!report.IsSane()) {
            return;
        }
        Shared::Entities::HumanEntity *sender = packet != nullptr ? Player::Players::FromPacket(packet) : nullptr;
        const auto found                      = std::find_if(_shots.begin(), _shots.end(), [&report](const Shot &shot) {
            return shot.launched && shot.shotId == report.shotId;
        });
        if (sender == nullptr || found == _shots.end() || found->reporterNetworkId != sender->GetNetworkID()) {
            return;
        }

        // Near the arc solved here, and no later along it than the stone could
        // have got: a client may say where the stone stopped, never send it
        // somewhere it was not going.
        const float until = found->launch.flightSeconds * 1.5F + 2.0F;
        float along       = 0.0F;
        const float off   = Shared::Game::Siege::DistanceToArc(found->launch.origin, found->launch.velocity, report.point, until, along);
        if (off > kReportTolerance) {
            Framework::Logging::GetLogger(kLogger)->warn("Shot {}: refused an impact {:.1f} m off its arc from player {}", found->shotId, off, sender->GetNetworkID());
            return;
        }

        const Shot shot = *found;
        _shots.erase(found);
        Framework::Logging::GetLogger(kLogger)->info("Shot {} came down at {:.1f}, {:.1f}, {:.1f} after {:.2f}s, as player {} saw it", shot.shotId, report.point.x, report.point.y, report.point.z, along, sender->GetNetworkID());
        Land(shot, report.point);
    }

    bool SiegeService::HarmEngines(const Shot &shot, const glm::vec3 &point) {
        if (!(shot.tuning.damageRadius > 0.0F)) {
            return false;
        }
        // Measured first and applied after, by id: a handler of the damage
        // this raises may destroy another engine.
        std::vector<std::pair<std::uint64_t, float>> struck;
        bool reached = false;
        for (Shared::Entities::SiegeEngineEntity *engine : Collect(shot.virtualWorld)) {
            // A trebuchet's frame stands high over its base; the blast is
            // measured to the nearer of the two.
            const float distance = std::min(glm::distance(engine->position, point), glm::distance(engine->position + glm::vec3(0.0F, 0.0F, kFrameHeight), point));
            const float share    = Shared::Game::Siege::DamageFalloff(distance, shot.tuning.damageRadius);
            if (share > 0.0F) {
                reached = true;
                struck.emplace_back(engine->GetNetworkID(), shot.tuning.damage * share);
            }
        }
        for (const std::pair<std::uint64_t, float> &hit : struck) {
            if (Shared::Entities::SiegeEngineEntity *engine = Find(hit.first)) {
                Damage(*engine, hit.second, shot.attackerNetworkId);
            }
        }
        return reached;
    }

    void SiegeService::Land(const Shot &shot, const glm::vec3 &point) {
        Shared::RPC::SiegeImpact impact;
        impact.shotId            = shot.shotId;
        impact.attackerNetworkId = shot.attackerNetworkId;
        impact.assetIndex        = shot.assetIndex;
        impact.point             = point;
        impact.damage            = shot.tuning.damage;
        impact.damageRadius      = shot.tuning.damageRadius;

        // Before the message, so it says whether an engine took the blow; the
        // damage events this raises run first, as the hit itself did.
        impact.struckEngine = HarmEngines(shot, point);

        // Everybody the shot was sent to may still be drawing it, and anyone
        // near the impact has to see it and take their share of it.
        auto *replication = Framework::CoreModules::GetReplication();
        auto *net         = Framework::CoreModules::GetNetworkPeer();
        if (replication != nullptr && net != nullptr) {
            replication->ForEach<Shared::Entities::HumanEntity>([&](Shared::Entities::HumanEntity *viewer) {
                if (viewer->GetVirtualWorld() != shot.virtualWorld || !viewer->transformReady) {
                    return;
                }
                if (glm::distance(viewer->position, shot.launch.origin) > kShotRelayRadius && glm::distance(viewer->position, point) > kShotRelayRadius) {
                    return;
                }
                net->SendRPC(impact, MafiaNet::ToGuid(viewer->ownerGUID), MafiaNet::Priority::High, MafiaNet::Reliability::Reliable);
            });
        }

        // A player's share is taken by their own client, from the message above.
        HarmNpcsInBlast(shot.virtualWorld, point, shot.tuning.damage, shot.tuning.damageRadius, shot.attackerNetworkId);

        Scripting::Events::SiegeImpact(shot.engineNetworkId, shot.attackerNetworkId, point);
    }

    void SiegeService::CheckPlaces(Shared::Entities::SiegeEngineEntity &engine) {
        if (engine.operatorNetworkId != 0) {
            Shared::Entities::HumanEntity *player = Player::Players::Find(engine.operatorNetworkId);
            if (player == nullptr || player->GetVirtualWorld() != engine.GetVirtualWorld() || (player->transformReady && GroundDistance(player->position, engine.position) > kUseReach)) {
                Framework::Logging::GetLogger(kLogger)->info("Player {} left the place of {} {}", engine.operatorNetworkId, engine.Asset().name, engine.GetNetworkID());
                engine.operatorNetworkId = 0;
            }
        }
        bool crewChanged = false;
        for (std::size_t place = 0; place < Shared::Entities::SiegeEngineEntity::kCrewPlaces; ++place) {
            std::uint64_t &crew = engine.crewNetworkIds[place];
            if (crew == 0) {
                continue;
            }
            Shared::Entities::HumanEntity *player = Player::Players::Find(crew);
            const glm::vec3 at                    = engine.CrewPlacePosition(place);
            if (player == nullptr || player->GetVirtualWorld() != engine.GetVirtualWorld() || (player->transformReady && GroundDistance(player->position, at) > kCrewReach)) {
                Framework::Logging::GetLogger(kLogger)->info("Player {} let go of winch {} of {} {}", crew, place, engine.Asset().name, engine.GetNetworkID());
                crew        = 0;
                crewChanged = true;
            }
        }
        if (crewChanged) {
            HoldForCrew(engine, KCDC::ServerClock::NowMs());
        }
    }

    void SiegeService::StopPull(std::uint64_t playerNetworkId) {
        const auto found = _pulling.find(playerNetworkId);
        if (found == _pulling.end()) {
            return;
        }
        const char *fragment = found->second;
        _pulling.erase(found);
        // Only the pull itself: a script may have played something else since.
        Shared::Entities::HumanEntity *player = Player::Players::Find(playerNetworkId);
        if (player != nullptr && player->animation.active && player->animation.loop && player->animation.fragment == fragment) {
            Animation::Animations::Stop(player, false);
        }
    }

    void SiegeService::SyncCrewAnimations(Shared::Entities::SiegeEngineEntity &engine) {
        const Shared::Game::SiegeEngineAsset &asset = engine.Asset();
        // Winding, and not held for want of hands.
        const bool winding = engine.state == Shared::Entities::SiegeState::Drawing && engine.stepStartMs != 0;
        for (std::size_t place = 0; place < engine.CrewPlaces(); ++place) {
            const std::uint64_t crew = engine.crewNetworkIds[place];
            if (crew == 0) {
                continue;
            }
            const auto pulling = _pulling.find(crew);
            if (winding && pulling == _pulling.end()) {
                Shared::Entities::HumanEntity *player = Player::Players::Find(crew);
                if (player == nullptr) {
                    continue;
                }
                Shared::Game::AnimationPlayback pull;
                pull.fragment     = asset.crewFragment;
                pull.tags         = asset.crew[place].tags;
                pull.loop         = true;
                pull.lockMovement = true;
                if (Animation::Animations::Play(player, pull)) {
                    _pulling.emplace(crew, asset.crewFragment);
                }
            }
            else if (!winding && pulling != _pulling.end()) {
                StopPull(crew);
            }
        }
    }

    void SiegeService::Advance(Shared::Entities::SiegeEngineEntity &engine, std::uint64_t now) {
        // A held step has no clock to run out.
        if (!Shared::Entities::IsSiegeMoving(engine.state) || engine.stepStartMs == 0 || now < engine.stepStartMs || now - engine.stepStartMs < engine.CurrentStepDurationMs()) {
            return;
        }

        // The next step starts where the last one ended, not where this tick
        // noticed, so a load's two steps add up to exactly their clips.
        const std::uint64_t ended = engine.stepStartMs + engine.CurrentStepDurationMs();
        switch (engine.state) {
        case Shared::Entities::SiegeState::Drawing:
            engine.state       = Shared::Entities::SiegeState::Loading;
            engine.stepStartMs = ended;
            break;
        case Shared::Entities::SiegeState::Loading:
            engine.state       = Shared::Entities::SiegeState::Ready;
            engine.stepStartMs = 0;
            Framework::Logging::GetLogger(kLogger)->info("{} {} is loaded", engine.Asset().name, engine.GetNetworkID());
            // Last: a handler may fire the engine or destroy it, and either
            // leaves nothing here to settle.
            Scripting::Events::SiegeEngineReady(engine.GetNetworkID());
            return;
        case Shared::Entities::SiegeState::Firing:
            engine.state       = Shared::Entities::SiegeState::Idle;
            engine.stepStartMs = 0;
            break;
        default: break;
        }

        // A step shorter than a tick could have ended too; settle it as well.
        Advance(engine, now);
    }

    void SiegeService::Update() {
        const std::uint64_t now = KCDC::ServerClock::NowMs();
        const bool checkPlaces  = now >= _nextPlaceCheck;
        if (checkPlaces) {
            _nextPlaceCheck = now + kPlaceCheckMs;
        }

        // By id, and resolved again for each: an event raised while one
        // engine settles can have a script destroy another.
        _engines.clear();
        if (auto *replication = Framework::CoreModules::GetReplication()) {
            replication->ForEach<Shared::Entities::SiegeEngineEntity>([this, checkPlaces](Shared::Entities::SiegeEngineEntity *engine) {
                if (checkPlaces || Shared::Entities::IsSiegeMoving(engine->state)) {
                    _engines.push_back(engine->GetNetworkID());
                }
            });
        }
        for (const std::uint64_t networkId : _engines) {
            Shared::Entities::SiegeEngineEntity *engine = Find(networkId);
            if (engine == nullptr) {
                continue;
            }
            if (checkPlaces) {
                CheckPlaces(*engine);
            }
            Advance(*engine, now);
            if (checkPlaces) {
                if (Shared::Entities::SiegeEngineEntity *still = Find(networkId)) {
                    SyncCrewAnimations(*still);
                }
            }
        }

        // A crewman out of every place -- let go, wrecked, removed -- stops
        // pulling. Collected first: stopping erases from the map.
        if (checkPlaces && !_pulling.empty()) {
            _manned.clear();
            if (auto *replication = Framework::CoreModules::GetReplication()) {
                replication->ForEach<Shared::Entities::SiegeEngineEntity>([this](Shared::Entities::SiegeEngineEntity *engine) {
                    for (const std::uint64_t crew : engine->crewNetworkIds) {
                        if (crew != 0) {
                            _manned.insert(crew);
                        }
                    }
                });
            }
            std::vector<std::uint64_t> released;
            for (const auto &[playerNetworkId, fragment] : _pulling) {
                if (!_manned.contains(playerNetworkId)) {
                    released.push_back(playerNetworkId);
                }
            }
            for (const std::uint64_t playerNetworkId : released) {
                StopPull(playerNetworkId);
            }
        }

        // By index: a script answering `siegeFire` may fire again, which
        // appends to the list being walked.
        for (std::size_t index = 0; index < _shots.size(); ++index) {
            if (!_shots[index].launched && now >= _shots[index].releaseAtMs) {
                Launch(_shots[index], now);
            }
        }

        std::vector<Shot> overdue;
        _shots.erase(std::remove_if(_shots.begin(), _shots.end(), [&](const Shot &shot) {
            if (shot.launched && now >= shot.deadlineMs) {
                overdue.push_back(shot);
                return true;
            }
            return false;
        }), _shots.end());
        for (const Shot &shot : overdue) {
            Framework::Logging::GetLogger(kLogger)->info("Shot {} was not reported by player {}; it lands on its target", shot.shotId, shot.reporterNetworkId);
            Land(shot, shot.target);
        }
    }
} // namespace KCDC::Features::Siege
