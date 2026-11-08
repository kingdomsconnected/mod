#pragma once

#include "shared/features/player/player_entity.h"
#include "shared/features/siege/siege_wire.h"
#include "shared/game/siege_catalog.h"

#include <networking/replication/network_entity.h>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace KCDC::Shared::Entities {
    /**
     * Where a siege engine is in its cycle.
     *
     * `Idle` is unloaded with the arm down. `load` runs `Drawing` -- the
     * trebuchet's arm winched down -- then `Loading`, the crew laying the
     * projectile, into `Ready`. `fire` runs `Firing`, which lets the projectile
     * go partway through and settles back to `Idle`. `Wrecked` is an engine
     * whose health ran out; nothing works it until it is repaired.
     */
    enum class SiegeState : std::uint8_t { Idle, Drawing, Loading, Ready, Firing, Wrecked };

    inline bool IsSiegeMoving(SiegeState state) {
        return state == SiegeState::Drawing || state == SiegeState::Loading || state == SiegeState::Firing;
    }

    inline const char *SiegeStateName(SiegeState state) {
        switch (state) {
        case SiegeState::Idle: return "idle";
        case SiegeState::Drawing: return "drawing";
        case SiegeState::Loading: return "loading";
        case SiegeState::Ready: return "ready";
        case SiegeState::Firing: return "firing";
        case SiegeState::Wrecked: return "wrecked";
        }
        return "idle";
    }

    /**
     * A trebuchet or a cannon a script put in the world.
     *
     * Like a gate, the motion is never streamed: `state` is the step the
     * engine is in and `stepStartMs` the server clock it started on, and every
     * client plays the step out locally. `speed` scales the whole cycle
     * so a gamemode can have a crew that reloads in seconds rather than the
     * two minutes the game's own animation takes.
     *
     * The projectile is not part of the entity. A shot is a one-off message
     * (`SiegeShot`), because a stone that nobody was near to see simply did
     * not happen to them.
     */
    class SiegeEngineEntity: public Framework::Networking::Replication::NetworkEntity {
      public:
        static constexpr const char *kTypeName = "KCDC::SiegeEngine";

        /** Visible from about as far as a gate, and as far as a stone can land from it. */
        static constexpr float kStreamRange = 400.0F;

        static constexpr float kMinSpeed = 0.1F;
        static constexpr float kMaxSpeed = 20.0F;

        /** `ammo` when the engine never runs out. */
        static constexpr std::int32_t kUnlimitedAmmo = -1;

        /** The most crew places an engine has; a trebuchet's two winches. */
        static constexpr std::size_t kCrewPlaces = 2;

        /** Index into `Shared::Game::g_SiegeEngineAssets`. Fixed for the engine's life. */
        std::uint8_t assetIndex = 0;

        SiegeState state = SiegeState::Idle;

        /**
         * Server clock at the start of the current step; 0 when settled, and
         * 0 too while a running step is held -- a trebuchet's winding with
         * fewer hands at the winches than it needs -- in which case
         * `stepProgress` says where it stopped.
         */
        std::uint64_t stepStartMs = 0;

        /** How far through the running step a held one stopped, 0 to 1. */
        float stepProgress = 0.0F;

        /** How fast the cycle runs; 1 is the game's own pace. */
        float speed = 1.0F;

        /**
         * Whether players beside the engine get the game's own action hint
         * for it -- Load while it is idle, Fire while it is loaded. Off by
         * default: who may work an engine is the gamemode's to decide.
         */
        bool usable = false;

        /**
         * Whether the operator sees the red beam along the shot's path to
         * where it would land. Off by default: laying an engine by eye is the
         * game; a gamemode that wants it easier turns this on.
         */
        bool aimGuide = false;

        /**
         * The point the engine is laid on, once it has been: `fire` and
         * `aim` set it, and so does its operator's look. Every client solves
         * the same launch from it with `Siege::SolveLaunch`, which is how a
         * cannon's barrel shows its elevation and how the operator's mark
         * shows where the shot comes down, without either being sent.
         */
        glm::vec3 aimTarget {0.0F};
        bool aimed = false;

        /** What the engine can take before it is wrecked; 0 for an engine nothing can harm. */
        float maxHealth = 0.0F;
        float health    = 0.0F;

        /** Shots left to load, or `kUnlimitedAmmo`. A load takes one. */
        std::int32_t ammo = kUnlimitedAmmo;

        /** The player who has taken the engine's place and works it, or 0. */
        std::uint64_t operatorNetworkId = 0;

        /** The players at each crew place -- a trebuchet's winches -- 0 for an empty one. */
        std::array<std::uint64_t, kCrewPlaces> crewNetworkIds {};

        /**
         * How many of the crew places must be manned for a trebuchet's arm to
         * be winched down; 0 winds it by itself, as the engine always did.
         * Never more than the engine has.
         */
        std::uint8_t crewRequired = 0;

        /**
         * The engine's latest shot, from the moment `fire` solved it: its id,
         * where and how fast it leaves, and the server clock it is let go at.
         * Every client that has the engine launches it on its own clock at
         * that moment, so the stone leaves the sling as the arm lets it go
         * rather than a round trip later, and a client that streams the
         * engine in while a stone is in the air still sees it fly. 0 for
         * none yet.
         */
        std::uint32_t shotId = 0;
        glm::vec3 shotOrigin {0.0F};
        glm::vec3 shotVelocity {0.0F};
        std::uint64_t shotLaunchMs = 0;

        const Shared::Game::SiegeEngineAsset &Asset() const {
            return Shared::Game::g_SiegeEngineAssets[assetIndex];
        }

        /** The catalog step a state plays. */
        static Shared::Game::SiegeStep StepOf(SiegeState state) {
            switch (state) {
            case SiegeState::Drawing: return Shared::Game::SiegeStep::Draw;
            case SiegeState::Loading:
            case SiegeState::Ready: return Shared::Game::SiegeStep::Load;
            case SiegeState::Firing: return Shared::Game::SiegeStep::Fire;
            case SiegeState::Idle: return Shared::Game::SiegeStep::Idle;
            }
            return Shared::Game::SiegeStep::Idle;
        }

        /** How long a step takes at this engine's speed, in milliseconds. */
        std::uint32_t StepDurationMs(Shared::Game::SiegeStep step) const {
            const std::uint32_t authored = Asset().durationsMs[static_cast<std::size_t>(step)];
            return static_cast<std::uint32_t>(static_cast<float>(authored) / std::clamp(speed, kMinSpeed, kMaxSpeed));
        }

        /** How long the step now running takes; 0 when settled. */
        std::uint32_t CurrentStepDurationMs() const {
            return IsSiegeMoving(state) ? StepDurationMs(StepOf(state)) : 0;
        }

        /** How far into the fire step the projectile is let go, at this engine's speed. */
        std::uint32_t ReleaseDelayMs() const {
            return static_cast<std::uint32_t>(static_cast<float>(Asset().releaseMs) / std::clamp(speed, kMinSpeed, kMaxSpeed));
        }

        /** How far through the running step the engine is `elapsedMs` after it started, 0 to 1; where a held step stopped. */
        float PhaseAfter(std::int64_t elapsedMs) const {
            const std::uint32_t duration = CurrentStepDurationMs();
            if (duration == 0) {
                return 1.0F;
            }
            if (stepStartMs == 0) {
                return std::clamp(stepProgress, 0.0F, 1.0F);
            }
            return std::clamp(static_cast<float>(elapsedMs) / static_cast<float>(duration), 0.0F, 1.0F);
        }

        /** Whether the engine can be harmed at all. */
        bool CanBeHarmed() const {
            return maxHealth > 0.0F;
        }

        /** How many crew places are manned. */
        std::size_t CrewCount() const {
            return static_cast<std::size_t>(std::count_if(crewNetworkIds.begin(), crewNetworkIds.end(), [](std::uint64_t id) {
                return id != 0;
            }));
        }

        /** How many crew places the engine has. */
        std::size_t CrewPlaces() const {
            return static_cast<std::size_t>(std::count_if(Asset().crew.begin(), Asset().crew.end(), [](const Shared::Game::SiegeCrewPlace &place) {
                return place.tags[0] != '\0';
            }));
        }

        /** Where crew place `place` stands, in the world. */
        glm::vec3 CrewPlacePosition(std::size_t place) const {
            const Shared::Game::SiegePoint &at = Asset().crew[place].at;
            return position + rotation * glm::vec3(at.x, at.y, at.z);
        }

        /** Which crew place a player holds, or -1. */
        int CrewPlaceOf(std::uint64_t playerNetworkId) const {
            for (std::size_t place = 0; place < kCrewPlaces; ++place) {
                if (playerNetworkId != 0 && crewNetworkIds[place] == playerNetworkId) {
                    return static_cast<int>(place);
                }
            }
            return -1;
        }

        void OnSerializeConstruction(Framework::Networking::Replication::FieldSerializer &fields) override {
            fields.Field(assetIndex);
            SerializeCycle(fields);
            // Checked where it arrives, so nothing downstream indexes the
            // catalog with a value it does not hold.
            if (assetIndex >= Shared::Game::g_SiegeEngineAssets.size()) {
                assetIndex = 0;
            }
        }

        void SerializeFields(Framework::Networking::Replication::FieldSerializer &fields) override {
            SerializeCycle(fields);
        }

        void SerializeTransform(Framework::Networking::Replication::FieldSerializer &fields) override {
            SiegeFields::Transform(fields, position, rotation);
        }

      private:
        static void Vector(Framework::Networking::Replication::FieldSerializer &fields, glm::vec3 &value) {
            fields.Field(value.x);
            fields.Field(value.y);
            fields.Field(value.z);
            if (!std::isfinite(value.x) || !std::isfinite(value.y) || !std::isfinite(value.z)) {
                value = glm::vec3(0.0F);
            }
        }

        void SerializeCycle(Framework::Networking::Replication::FieldSerializer &fields) {
            // Seeded from the current value: on the delta path an unchanged
            // field is not written back, so a bare local would read as Idle.
            std::uint8_t wireState = static_cast<std::uint8_t>(state);
            fields.Field(wireState);
            state = wireState <= static_cast<std::uint8_t>(SiegeState::Wrecked) ? static_cast<SiegeState>(wireState) : SiegeState::Idle;
            fields.Field(stepStartMs);
            fields.Field(stepProgress);
            SiegeFields::Finite(stepProgress, 0.0F);
            fields.Field(speed);
            SiegeFields::Finite(speed, 1.0F);
            speed = std::clamp(speed, kMinSpeed, kMaxSpeed);
            fields.Field(usable);
            fields.Field(aimGuide);

            fields.Field(aimed);
            Vector(fields, aimTarget);

            fields.Field(maxHealth);
            SiegeFields::Finite(maxHealth, 0.0F);
            maxHealth = std::max(maxHealth, 0.0F);
            fields.Field(health);
            SiegeFields::Finite(health, 0.0F);
            health = std::clamp(health, 0.0F, maxHealth);
            fields.Field(ammo);
            ammo = std::max(ammo, kUnlimitedAmmo);

            fields.Field(operatorNetworkId);
            for (std::uint64_t &crew : crewNetworkIds) {
                fields.Field(crew);
            }
            fields.Field(crewRequired);
            crewRequired = static_cast<std::uint8_t>(std::min<std::size_t>(crewRequired, kCrewPlaces));

            fields.Field(shotId);
            Vector(fields, shotOrigin);
            Vector(fields, shotVelocity);
            fields.Field(shotLaunchMs);
        }
    };
} // namespace KCDC::Shared::Entities
