#pragma once

#include "shared/game/cart_blueprints.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace KCDC::Shared::Game {
    // How a player drives a cart.
    //
    // The game moves a cart only along a road the level lays for it, forward,
    // at an NPC caravan's pace (`UpdateCartFollowers`, 0x180F8DB48): it has no
    // steering, no reverse and no free driving -- the player only ever sits in
    // one. So a cart a player drives moves the way a wagon does, here, and the
    // game is handed the pose it would have computed from a road: the chassis
    // is posed, the wheels rolled and the horses stepped by its own
    // functions from that pose.
    //
    // A wagon is a lead point and an axle trailing it. The lead point is
    // where the team pulls from and the cart steers about: a wagon's front
    // axle, whose centre is the kingpin the body turns on, or a two-wheeler's
    // shafts. The trailing axle follows it at the wheelbase, as if on a rope,
    // which is what turns the body. Pure and shared: the driver steps it, the
    // tests pin it.

    /** The most a wagon's front axle swings under its bed, in radians: about fifty degrees, past which the pole would break the bed. */
    inline constexpr float kCartMaxLock = 0.87F;

    /** The most a two-wheeler's team walks off the line of its shafts, in radians. */
    inline constexpr float kCartMaxShaftLock = 0.5F;

    /**
     * How fast the reins swing the lead point round, in radians a second: as
     * fast as the cart rolls over this many metres a second, up to one -- a
     * wagon turns about a five metre circle at any pace, and not standing.
     */
    inline constexpr float kCartSteerSpeed = 5.0F;

    /**
     * How hard a cart gets under way, how hard it slows to stop or turn
     * round, and how hard a cart backing up pulls away forward again, in
     * metres a second per second.
     */
    inline constexpr float kCartAcceleration    = 1.6F;
    inline constexpr float kCartBraking         = 2.8F;
    inline constexpr float kCartReverseRecovery = 3.0F;

    /** Below this a cart stands. */
    inline constexpr float kCartStandSpeed = 0.05F;

    /** The fastest a cart may be reported going, either way, in metres a second: past any gait a team has. */
    inline constexpr float kCartMaxSpeed = 15.0F;

    /**
     * Where a cart's parts sit, read off its blueprint: what the motion needs
     * to know about the wagon, and what posing it needs to know about the
     * entities.
     */
    struct CartRig {
        /** A wagon whose body turns on its front axle; false for a cart built on one rigid frame. */
        bool articulated = false;

        int chassisIndex = -1; // the root part, which the game's cart extension hangs off
        int bodyIndex    = -1; // the part that turns on the front axle; -1 when not articulated

        /** The lead point, in the chassis's frame: the front axle's centre, or the shafts' harness. */
        glm::vec3 lead = glm::vec3(0.0F);

        /** The trailing axle's centre, in the chassis's frame, as the prefab stands. */
        glm::vec3 axle = glm::vec3(0.0F);

        /** The body's place under the chassis as the prefab stands, which it turns about `lead` from. */
        glm::vec3 bodyPosition = glm::vec3(0.0F);
        glm::quat bodyRotation = glm::quat(1.0F, 0.0F, 0.0F, 0.0F);

        /** How far the trailing axle runs behind the lead point. */
        float wheelbase = 2.5F;

        /** How far the chassis's origin sits above the ground under its wheels. */
        float height = 0.0F;

        /** Half the frame's length behind the trailing axle, for looking behind it. */
        float tail = 1.5F;

        /**
         * How far a two-wheeler is tipped up about its axle, in radians: the
         * prefab rests its shafts on the ground, and a horse between them
         * holds them up at its harness. 0 for a wagon.
         */
        float lift = 0.0F;

        /**
         * Where each horse stands, on the ground: for a wagon in the frame of
         * its front axle, at its pole; for a two-wheeler at the lead point,
         * between the shafts. As many as the cart is harnessed for.
         */
        std::array<glm::vec2, 2> horses {};
        std::size_t horseCount = 0;

        /** The clothing preset each horse wears for its harness, by GUID. */
        std::array<std::string_view, 2> harness {};
    };

    namespace Detail {
        inline glm::vec3 PartPosition(const CartPart &part) {
            return glm::vec3(part.posX, part.posY, part.posZ);
        }

        inline glm::quat PartRotation(const CartPart &part) {
            return glm::normalize(glm::quat(part.rotW, part.rotX, part.rotY, part.rotZ));
        }

        /** The centre of the wheels hung off `parent`, in its frame, or nothing when it has none. */
        inline std::optional<glm::vec3> WheelCentre(const CartBlueprint &blueprint, int parent) {
            glm::vec3 sum     = glm::vec3(0.0F);
            std::size_t found = 0;
            for (std::size_t index = 0; index < blueprint.partCount; ++index) {
                const CartPart &part = blueprint.parts[index];
                if (part.parent == parent && part.name.starts_with("wheel_")) {
                    sum += PartPosition(part);
                    ++found;
                }
            }
            if (found == 0) {
                return std::nullopt;
            }
            return sum / static_cast<float>(found);
        }
    } // namespace Detail

    /**
     * The rig of a blueprint, or nothing for one that cannot be driven.
     *
     * A wagon's chassis is its front axle (`frontPart`): the front wheels hang
     * off it, and so does the body (`backPart`), with the rear wheels under
     * that. A two-wheeler is one frame with its wheels under it, pulled from
     * its shafts by the horse between them: the lead point is that horse,
     * `kCartShaftReach` ahead of the axle, and the frame is tipped up
     * `kCartShaftLift` to its harness.
     */
    inline constexpr float kCartShaftReach = 2.018F;
    inline constexpr float kCartShaftLift  = 0.35F;

    /** A wagon's two horses at its pole, in its front axle's frame. */
    inline constexpr glm::vec2 kCartPoleHorses[2] = {glm::vec2(-0.72F, 3.55F), glm::vec2(0.78F, 3.55F)};

    /** The game's own harness presets: a wagon's pair at the pole (left, right), and the horse between a two-wheeler's shafts (`horse_cart_single`). */
    inline constexpr std::string_view kCartPoleHarness[2] = {"e6cd2a13-096d-4a0e-9b69-111954984ba2", "587eb437-1000-46cd-be8a-8bc9490528d6"};
    inline constexpr std::string_view kCartShaftHarness   = "f98d905d-c574-44a1-bd4b-03a7c85a2487";

    inline std::optional<CartRig> BuildCartRig(const CartBlueprint &blueprint) {
        CartRig rig;
        rig.chassisIndex = FindCartChassis(blueprint);
        if (rig.chassisIndex < 0) {
            return std::nullopt;
        }
        rig.height = blueprint.parts[rig.chassisIndex].posZ;

        for (std::size_t index = 0; index < blueprint.partCount; ++index) {
            const CartPart &part = blueprint.parts[index];
            if (part.parent == rig.chassisIndex && part.name == "backPart") {
                rig.bodyIndex = static_cast<int>(index);
            }
        }

        const std::optional<glm::vec3> chassisWheels = Detail::WheelCentre(blueprint, rig.chassisIndex);
        if (!chassisWheels) {
            return std::nullopt;
        }

        if (rig.bodyIndex >= 0) {
            const std::optional<glm::vec3> bodyWheels = Detail::WheelCentre(blueprint, rig.bodyIndex);
            if (!bodyWheels) {
                return std::nullopt;
            }
            const CartPart &body = blueprint.parts[rig.bodyIndex];
            rig.articulated      = true;
            rig.lead             = *chassisWheels;
            rig.bodyPosition     = Detail::PartPosition(body);
            rig.bodyRotation     = Detail::PartRotation(body);
            rig.axle             = rig.bodyPosition + rig.bodyRotation * *bodyWheels;
            rig.horses           = {kCartPoleHorses[0], kCartPoleHorses[1]};
            rig.horseCount       = 2;
            rig.harness          = {kCartPoleHarness[0], kCartPoleHarness[1]};
        }
        else {
            rig.axle       = *chassisWheels;
            rig.lead       = rig.axle + glm::vec3(0.0F, kCartShaftReach, 0.0F);
            rig.lift       = kCartShaftLift;
            rig.horses     = {glm::vec2(rig.lead), glm::vec2(0.0F)};
            rig.horseCount = 1;
            rig.harness    = {kCartShaftHarness, std::string_view()};
        }

        rig.wheelbase = glm::length(glm::vec2(rig.lead - rig.axle));
        if (rig.wheelbase < 0.5F) {
            return std::nullopt;
        }
        return rig;
    }

    /** The direction a yaw faces: CryEngine's forward is +Y, turned about Z. */
    inline glm::vec2 CartHeading(float yaw) {
        return glm::vec2(-std::sin(yaw), std::cos(yaw));
    }

    /** The yaw a direction faces. */
    inline float CartYawOf(const glm::vec2 &direction) {
        return std::atan2(-direction.x, direction.y);
    }

    /** `angle` brought into [-pi, pi]. */
    inline float CartWrapAngle(float angle) {
        constexpr float kPi = 3.14159265358979F;
        angle               = std::fmod(angle + kPi, 2.0F * kPi);
        return angle < 0.0F ? angle + kPi : angle - kPi;
    }

    /** Where a cart is and where it is going: its lead point on the ground, which way the lead and the body face, and how fast it rolls. */
    struct CartDrive {
        glm::vec3 lead = glm::vec3(0.0F);
        float leadYaw  = 0.0F;
        float bodyYaw  = 0.0F;
        float speed    = 0.0F; // along the lead's heading; negative backing up

        /** The trailing axle, on the ground. */
        glm::vec3 Axle(float wheelbase) const {
            const glm::vec2 back = glm::vec2(lead) - CartHeading(bodyYaw) * wheelbase;
            return glm::vec3(back, lead.z);
        }
    };

    /** How a driver wants the cart to go, as their keys say it. */
    struct CartControls {
        std::int8_t throttle = 0;     // 1 forward, -1 back, 0 neither
        float steer          = 0.0F;  // -1 full right to +1 full left
        bool fast            = false; // forward at the trot rather than the walk
    };

    /** How fast the team goes: forward on its own, forward urged on, and backing up. */
    struct CartGaits {
        float walk    = 3.6F; // the trot a team takes up unurged
        float trot    = 7.0F; // urged on, up to the canter
        float reverse = 1.5F;
    };

    /** The speed a driver's controls ask for. */
    inline float CartTargetSpeed(const CartControls &controls, const CartGaits &gaits) {
        if (controls.throttle > 0) {
            return controls.fast ? gaits.trot : std::min(gaits.walk, gaits.trot);
        }
        if (controls.throttle < 0) {
            return -gaits.reverse;
        }
        return 0.0F;
    }

    /**
     * One step of a cart under `controls`.
     *
     * The speed eases to what the controls ask, and stops dead when
     * `blocked`. The reins swing the lead point while the cart rolls -- the
     * way it rolls, so backing up with the reins left backs the wagon round
     * to the left -- never standing: a team does not turn a wagon on the
     * spot. The lead point rolls along its heading; the trailing axle follows
     * it at `wheelbase` along the line between them, which is what turns the
     * body.
     */
    inline CartDrive StepCart(const CartDrive &from, const CartControls &controls, const CartGaits &gaits, float wheelbase, bool articulated, bool blocked, float dt) {
        CartDrive to = from;

        const float target = blocked ? 0.0F : CartTargetSpeed(controls, gaits);
        if (blocked) {
            to.speed = 0.0F;
        }
        else if (target > to.speed) {
            to.speed = std::min(target, to.speed + (to.speed < 0.0F ? kCartReverseRecovery : kCartAcceleration) * dt);
        }
        else {
            to.speed = std::max(target, to.speed - (to.speed > 0.0F && target <= 0.0F ? kCartBraking : kCartAcceleration) * dt);
        }

        if (std::fabs(to.speed) > kCartStandSpeed) {
            const float rate = std::min(1.0F, std::fabs(to.speed) / kCartSteerSpeed) * (to.speed > 0.0F ? 1.0F : -1.0F);
            to.leadYaw += std::clamp(controls.steer, -1.0F, 1.0F) * rate * dt;
        }
        const float lock = articulated ? kCartMaxLock : kCartMaxShaftLock;
        to.leadYaw       = to.bodyYaw + std::clamp(CartWrapAngle(to.leadYaw - to.bodyYaw), -lock, lock);

        const glm::vec2 lead = glm::vec2(from.lead) + CartHeading(to.leadYaw) * (to.speed * dt);
        const glm::vec2 axle = glm::vec2(from.Axle(wheelbase));
        const glm::vec2 line = lead - axle;
        if (glm::length(line) > 1e-4F) {
            to.bodyYaw = CartYawOf(line);
        }
        to.lead = glm::vec3(lead, from.lead.z);
        return to;
    }

    /**
     * The clip a team plays at a pace, and how fast it plays it.
     *
     * A team is not walked by the game: each horse is put where its harness
     * is every frame and plays the gait the cart's pace calls for -- the clip,
     * the range of paces it covers, and the pace it was made at, which is
     * what the clip's rate is measured against. Backing up is the walk
     * backwards, made at 1.5 m/s.
     */
    enum class CartHorseGait : std::uint8_t {
        Idle,
        Walk,
        Trot,
        Canter,
        Gallop,
        WalkBack,
    };

    struct CartHorseGaitBand {
        CartHorseGait gait;
        float from;
        float to;
        float madeAt;
    };

    inline constexpr CartHorseGaitBand kCartHorseGaits[] = {
        {CartHorseGait::Walk, 0.4F, 2.6F, 1.8F},
        {CartHorseGait::Trot, 2.6F, 5.0F, 3.6F},
        {CartHorseGait::Canter, 5.0F, 8.0F, 6.5F},
        {CartHorseGait::Gallop, 8.0F, 1e9F, 9.5F},
    };

    inline constexpr float kCartHorseWalkBackMadeAt = 1.5F;

    /** Below this a team stands; a team already walking keeps on down to the second. */
    inline constexpr float kCartHorseStand = 0.4F;
    inline constexpr float kCartHorseKeep  = 0.3F;

    /**
     * How far a gait's clip is sped up or slowed down to follow the pace: past
     * this a stride reads as slow motion or a scurry, and the hooves slipping
     * at a band's edges looks the more natural of the two.
     */
    inline constexpr float kCartHorseMinRate = 0.7F;
    inline constexpr float kCartHorseMaxRate = 1.3F;

    /** How fast `gait` plays at `speed`: 1 at the pace it was made at, within the rates above. */
    inline float CartHorseGaitRate(CartHorseGait gait, float speed) {
        const float pace = std::fabs(speed);
        if (gait == CartHorseGait::WalkBack) {
            return std::clamp(pace / kCartHorseWalkBackMadeAt, kCartHorseMinRate, kCartHorseMaxRate);
        }
        for (const CartHorseGaitBand &band : kCartHorseGaits) {
            if (band.gait == gait) {
                return std::clamp(pace / band.madeAt, kCartHorseMinRate, kCartHorseMaxRate);
            }
        }
        return 1.0F;
    }

    /**
     * The gait for `speed`, given the one the team has: within `margin` m/s
     * of its edges a gait the team has is kept, so a pace hovering at an edge
     * does not change the clip every frame.
     */
    inline CartHorseGait ChooseCartHorseGait(float speed, CartHorseGait current, float margin) {
        const float pace = std::fabs(speed);
        if (speed < 0.0F && (pace >= kCartHorseStand || (current == CartHorseGait::WalkBack && pace >= kCartHorseKeep))) {
            return CartHorseGait::WalkBack;
        }
        for (const CartHorseGaitBand &band : kCartHorseGaits) {
            if (speed > 0.0F && band.gait == current && pace >= std::max(kCartHorseKeep, band.from - margin) && pace < band.to + margin) {
                return band.gait;
            }
        }
        if (speed <= 0.0F || pace < kCartHorseStand) {
            return CartHorseGait::Idle;
        }
        for (const CartHorseGaitBand &band : kCartHorseGaits) {
            if (pace < band.to) {
                return band.gait;
            }
        }
        return CartHorseGait::Gallop;
    }

    /** Whether a reported drive is one a cart could be in: finite, on the world, and no faster than any team goes. */
    inline bool IsSaneCartDrive(const CartDrive &drive) {
        constexpr float kWorldLimit = 16384.0F;
        return std::isfinite(drive.lead.x) && std::isfinite(drive.lead.y) && std::isfinite(drive.lead.z) && std::fabs(drive.lead.x) < kWorldLimit && std::fabs(drive.lead.y) < kWorldLimit && std::fabs(drive.lead.z) < kWorldLimit && std::isfinite(drive.leadYaw)
               && std::isfinite(drive.bodyYaw) && std::isfinite(drive.speed) && std::fabs(drive.speed) <= kCartMaxSpeed;
    }
} // namespace KCDC::Shared::Game
