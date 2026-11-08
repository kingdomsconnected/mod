#pragma once

#include "shared/features/player/player_entity.h"
#include "shared/features/siege/siege_wire.h"
#include "shared/game/siege_defence_catalog.h"

#include <networking/replication/network_entity.h>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace KCDC::Shared::Entities {
    /**
     * Where a siege ladder is.
     *
     * `Standing` leans on the wall and is climbed with the game's own
     * prompt. `Pushed` is a defender's halberd push: the ladder stands through
     * the swing until the blade meets it, then falls away from the wall,
     * throwing off whoever is on it. `Down` lies where it fell. `Raising` is an
     * attacker laying it back against the wall. `Broken` lies in pieces and
     * nothing raises it until it is repaired.
     */
    enum class SiegeLadderState : std::uint8_t { Standing, Pushed, Down, Raising, Broken };

    inline const char *SiegeLadderStateName(SiegeLadderState state) {
        switch (state) {
        case SiegeLadderState::Standing: return "standing";
        case SiegeLadderState::Pushed: return "pushed";
        case SiegeLadderState::Down: return "down";
        case SiegeLadderState::Raising: return "raising";
        case SiegeLadderState::Broken: return "broken";
        }
        return "standing";
    }

    /**
     * A ladder a script leaned against a wall.
     *
     * The ladder is the game's own `Ladder` entity on every client, so the
     * climb is the game's, and so is the step off over the palisade at its
     * top. Like a siege engine's cycle, its fall and its raise are never
     * streamed: `state` and the server clock `stepStartMs` it began on are
     * replicated, and each client tilts its ladder on the curve below.
     *
     * The frame is the battle prefab's: the foot is `position`, and the
     * ladder leans towards its own -Y. `rotation` is turned half about so that
     * a script's forward (+Y) is the way to the wall.
     */
    class SiegeLadderEntity: public Framework::Networking::Replication::NetworkEntity {
      public:
        static constexpr const char *kTypeName = "KCDC::SiegeLadder";

        static constexpr float kStreamRange = 250.0F;

        /**
         * The push, on the clock of `GuardHlbrdPushAway_Player`: the blade
         * meets the ladder this far into the swing, and the ladder lands --
         * `battleLadder.xml`'s `player_pushLadder` plays the crash 4500 ms in
         * -- here. The swing itself runs 5933 ms.
         */
        static constexpr std::uint32_t kPushContactMs = 2650;
        static constexpr std::uint32_t kPushLandMs    = 4500;
        static constexpr std::uint32_t kPushSwingMs   = 5933;

        /** `LayLadder`'s `layFromIdle`, the attacker's raise. */
        static constexpr std::uint32_t kRaiseMs = 5667;

        /** Lying, the ladder turns a quarter about its foot, top away from the wall: the battle prefab's drop slot. */
        static constexpr float kLyingTiltDeg = -90.0F;

        /**
         * How near the top of a standing ladder a defender reaches it to push,
         * and how near its foot an attacker reaches a fallen one to raise it,
         * in metres -- the server's word. The prompt is offered a little
         * closer.
         */
        static constexpr float kPushReach  = 2.5F;
        static constexpr float kRaiseReach = 2.0F;

        /** Index into `Shared::Game::g_SiegeLadderAssets`. Fixed for the ladder's life. */
        std::uint8_t assetIndex = 0;

        SiegeLadderState state = SiegeLadderState::Standing;

        /** Server clock the push or the raise began on; 0 while standing, down or broken. */
        std::uint64_t stepStartMs = 0;

        /** Whether players are offered the push and the raise. Climbing is the game's, offered whenever it stands. */
        bool usable = true;

        /** The player who last pushed it, credited with whoever falls; 0 for nobody. */
        std::uint64_t pusherNetworkId = 0;

        const Shared::Game::SiegeLadderAsset &Asset() const {
            return Shared::Game::g_SiegeLadderAssets[assetIndex];
        }

        /** Whether the game's climb may be offered: it stands, and nobody has started to push it. */
        bool IsClimbable() const {
            return state == SiegeLadderState::Standing;
        }

        /** The ladder's own frame in the world: the script's rotation turned half about, as the prefab stands. */
        glm::quat LadderRotation() const {
            return glm::normalize(rotation * glm::angleAxis(glm::pi<float>(), glm::vec3(0.0F, 0.0F, 1.0F)));
        }

        /** How far it leans while standing, in radians, positive towards the wall. */
        float LeanRadians() const {
            return glm::radians(Asset().leanDeg);
        }

        /** Where a ladder tilted `tilt` about its foot puts the point `along` metres up it. */
        glm::vec3 PointAlong(float tilt, float along) const {
            return position + LadderRotation() * glm::vec3(0.0F, -std::sin(tilt) * along, std::cos(tilt) * along);
        }

        /** Its top end, standing. */
        glm::vec3 Top() const {
            return PointAlong(LeanRadians(), Asset().height);
        }

        /** Where a defender stands to push it: the battle prefab's push smart object. */
        glm::vec3 PushFrom() const {
            const Shared::Game::SiegePoint &at = Asset().pushFrom;
            return position + LadderRotation() * glm::vec3(at.x, at.y, at.z);
        }

        /** The duration of the running step, 0 when settled. */
        std::uint32_t StepDurationMs() const {
            switch (state) {
            case SiegeLadderState::Pushed: return kPushLandMs;
            case SiegeLadderState::Raising: return kRaiseMs;
            default: return 0;
            }
        }

        /**
         * The tilt about the foot `elapsedMs` into the current step, in
         * radians: the lean while it stands, a rod's fall about its foot from
         * the blade's contact to the ground, lying flat, and an eased swing
         * back up for the raise.
         */
        float TiltAfter(std::int64_t elapsedMs) const {
            const float lean  = LeanRadians();
            const float lying = glm::radians(kLyingTiltDeg);
            switch (state) {
            case SiegeLadderState::Standing: return lean;
            case SiegeLadderState::Down:
            case SiegeLadderState::Broken: return lying;
            case SiegeLadderState::Pushed: {
                if (elapsedMs <= static_cast<std::int64_t>(kPushContactMs)) {
                    return lean;
                }
                const float u = std::clamp(static_cast<float>(elapsedMs - kPushContactMs) / static_cast<float>(kPushLandMs - kPushContactMs), 0.0F, 1.0F);
                return lean + (lying - lean) * FallShape(u);
            }
            case SiegeLadderState::Raising: {
                const float u = std::clamp(static_cast<float>(elapsedMs) / static_cast<float>(kRaiseMs), 0.0F, 1.0F);
                return lying + (lean - lying) * u * u * (3.0F - 2.0F * u);
            }
            }
            return lean;
        }

        /**
         * How far through its fall a pushed ladder is at `u` of the fall's
         * time, 0 to 1. A rod tipping about its foot leaves the wall slowly
         * and is fastest as it lands, since gravity's pull on it grows with
         * the angle; this is an ease-in of that shape, not its integral. The
         * fall's length is the game's own crash time, not a simulated one.
         */
        static float FallShape(float u) {
            u = std::clamp(u, 0.0F, 1.0F);
            return u * u * (0.35F + 0.65F * u);
        }

        void OnSerializeConstruction(Framework::Networking::Replication::FieldSerializer &fields) override {
            fields.Field(assetIndex);
            SerializeState(fields);
            if (assetIndex >= Shared::Game::g_SiegeLadderAssets.size()) {
                assetIndex = 0;
            }
        }

        void SerializeFields(Framework::Networking::Replication::FieldSerializer &fields) override {
            SerializeState(fields);
        }

        void SerializeTransform(Framework::Networking::Replication::FieldSerializer &fields) override {
            SiegeFields::Transform(fields, position, rotation);
        }

      private:
        void SerializeState(Framework::Networking::Replication::FieldSerializer &fields) {
            // Seeded from the current value: on the delta path an unchanged
            // field is not written back.
            std::uint8_t wireState = static_cast<std::uint8_t>(state);
            fields.Field(wireState);
            state = wireState <= static_cast<std::uint8_t>(SiegeLadderState::Broken) ? static_cast<SiegeLadderState>(wireState) : SiegeLadderState::Standing;
            fields.Field(stepStartMs);
            fields.Field(usable);
            fields.Field(pusherNetworkId);
        }
    };
} // namespace KCDC::Shared::Entities
