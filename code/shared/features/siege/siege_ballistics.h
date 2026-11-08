#pragma once

#include "shared/game/siege_catalog.h"
#include "shared/pose_math.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>

/**
 * The flight of a siege projectile, which both peers compute and neither
 * simulates.
 *
 * A shot is a launch point, a velocity and the server clock it left at.
 * Everything after that is this arc: the server solves the launch for the
 * target, every client draws the stone along the same curve, and the server
 * checks an impact a client reports against it. There is no physics in the
 * flight at all -- a simulated stone would land somewhere different on every
 * machine.
 */
namespace KCDC::Shared::Game::Siege {
    /** The engine's own `p_gravity_z`, so a stone falls as everything else does. */
    inline constexpr float kGravity = 9.81F;

    /** The longest a stone is allowed in the air before a shot is given up on, in seconds. */
    inline constexpr float kMaxFlightSeconds = 30.0F;

    /** Where a stone launched with `velocity` from `origin` is `seconds` later. */
    inline glm::vec3 PositionAt(const glm::vec3 &origin, const glm::vec3 &velocity, float seconds) {
        return origin + velocity * seconds + glm::vec3(0.0F, 0.0F, -0.5F * kGravity * seconds * seconds);
    }

    /** Its velocity then. */
    inline glm::vec3 VelocityAt(const glm::vec3 &velocity, float seconds) {
        return velocity + glm::vec3(0.0F, 0.0F, -kGravity * seconds);
    }

    /**
     * The upright rotation that faces an engine standing at `from` towards
     * `to`. Forward is +Y, the engine's own: rotating it by `yaw` about Z gives
     * (-sin yaw, cos yaw).
     */
    inline glm::quat FacingRotation(const glm::vec3 &from, const glm::vec3 &to) {
        const glm::vec2 heading(to.x - from.x, to.y - from.y);
        if (glm::dot(heading, heading) < 1e-6F) {
            return glm::quat(1.0F, 0.0F, 0.0F, 0.0F);
        }
        return glm::angleAxis(std::atan2(-heading.x, heading.y), glm::vec3(0.0F, 0.0F, 1.0F));
    }

    /** Whether the engine's elevation is solved, raising it about `pivot`, rather than fixed by its arm. */
    inline bool LaysItsBarrel(const SiegeEngineAsset &asset) {
        return asset.muzzleSpeed > 0.0F;
    }

    /**
     * Where an engine lets its projectile go, in its own frame, laid at
     * `elevation` radians: a barrel raised about the back of its bed carries
     * its muzzle up with it. An engine with a fixed arm ignores it.
     */
    inline glm::vec3 LocalReleasePoint(const SiegeEngineAsset &asset, float elevation) {
        const glm::vec3 offset(asset.releaseOffset.x, asset.releaseOffset.y, asset.releaseOffset.z);
        if (!LaysItsBarrel(asset) || elevation == 0.0F) {
            return offset;
        }
        const glm::vec3 pivot(asset.pivot.x, asset.pivot.y, asset.pivot.z);
        return pivot + glm::angleAxis(elevation, glm::vec3(1.0F, 0.0F, 0.0F)) * (offset - pivot);
    }

    /** Where an engine lets its projectile go, in the world. */
    inline glm::vec3 ReleasePoint(const SiegeEngineAsset &asset, const glm::vec3 &position, const glm::quat &rotation, float elevation) {
        return position + rotation * LocalReleasePoint(asset, elevation);
    }

    /** The elevation a launch leaves at, in radians above the horizontal. */
    inline float ElevationOf(const glm::vec3 &velocity) {
        return std::atan2(velocity.z, glm::length(glm::vec2(velocity.x, velocity.y)));
    }

    /** Why a target cannot be hit. */
    enum class AimResult : std::uint8_t {
        Ok,
        TooClose,
        TooFar,
        /** In range on the ground, but too high above the launch point for the arc the engine throws. */
        Unreachable,
    };

    inline const char *AimResultReason(AimResult result) {
        switch (result) {
        case AimResult::Ok: return "";
        case AimResult::TooClose: return "the target is closer than the engine can throw";
        case AimResult::TooFar: return "the target is further than the engine can throw";
        case AimResult::Unreachable: return "the target is too high for the engine's arc";
        }
        return "";
    }

    /** A solved shot: where it leaves, how fast, and how long until it comes down on the target. */
    struct Launch {
        glm::vec3 origin {0.0F};
        glm::vec3 velocity {0.0F};
        float flightSeconds = 0.0F;
    };

    /**
     * Solves the shot from an engine at `position`, facing `rotation`, that
     * comes down on `target`.
     *
     * The engine throws in its own vertical plane, so the caller turns it to
     * face the target first; only the distance and the height are solved here.
     * A trebuchet's arm lets the stone go at one angle, so its speed is what
     * is solved for. A cannon fires at one speed, so its elevation is -- the
     * lower of the two arcs, as a gunner lays a gun -- and its muzzle rises
     * about the back of its bed with it.
     */
    inline AimResult SolveLaunch(const SiegeEngineAsset &asset, const glm::vec3 &position, const glm::quat &rotation, const glm::vec3 &target, Launch &launch) {
        const float ground = glm::length(glm::vec2(target.x - position.x, target.y - position.y));
        if (ground < asset.minRange) {
            return AimResult::TooClose;
        }
        if (ground > asset.maxRange) {
            return AimResult::TooFar;
        }

        // A laid barrel's muzzle rises with it, so the elevation it is laid
        // at moves the point it is solved from. A few rounds settle it: the
        // muzzle moves centimetres for degrees.
        float elevation = 0.0F;
        float speed     = 0.0F;
        glm::vec3 origin(0.0F);
        glm::vec2 forward(0.0F, 1.0F);
        float distance = 0.0F;
        for (int round = 0; round < (LaysItsBarrel(asset) ? 4 : 1); ++round) {
            origin                  = ReleasePoint(asset, position, rotation, elevation);
            const glm::vec2 heading = glm::vec2(target.x - origin.x, target.y - origin.y);
            distance                = glm::length(heading);
            const float rise        = target.z - origin.z;
            if (distance < 1.0F) {
                return AimResult::TooClose;
            }
            forward = heading / distance;

            if (!LaysItsBarrel(asset)) {
                // v^2 = g d^2 / (2 cos^2(a) (d tan(a) - h))
                elevation         = glm::radians(asset.launchElevationDeg);
                const float cosA  = std::cos(elevation);
                const float above = distance * std::tan(elevation) - rise;
                if (above <= 0.0F) {
                    return AimResult::Unreachable;
                }
                speed = std::sqrt(kGravity * distance * distance / (2.0F * cosA * cosA * above));
                continue;
            }
            // tan(a) = (v^2 - sqrt(v^4 - g (g d^2 + 2 h v^2))) / (g d), the lower arc
            speed                    = asset.muzzleSpeed;
            const float speedSquared = speed * speed;
            const float discriminant = speedSquared * speedSquared - kGravity * (kGravity * distance * distance + 2.0F * rise * speedSquared);
            if (discriminant < 0.0F) {
                return AimResult::Unreachable;
            }
            elevation = std::atan((speedSquared - std::sqrt(discriminant)) / (kGravity * distance));
        }

        const float horizontal = speed * std::cos(elevation);
        if (!(horizontal > 0.0F) || !std::isfinite(speed)) {
            return AimResult::Unreachable;
        }
        launch.origin        = origin;
        launch.velocity      = glm::vec3(forward.x * horizontal, forward.y * horizontal, speed * std::sin(elevation));
        launch.flightSeconds = distance / horizontal;
        return launch.flightSeconds <= kMaxFlightSeconds ? AimResult::Ok : AimResult::TooFar;
    }

    /**
     * Where an engine at `position` is laid for a look at `target`: the point
     * itself when it can hit it, else the nearest point along the ground the
     * same way that it can, at the height looked at -- a look at the far
     * hills lays it as far as it throws. Nothing for a point too high for its
     * arc. The server lays an operator's look with this, and the operator's
     * own client draws the beam from it before the server has answered.
     */
    inline std::optional<glm::vec3> ReachableAlong(const SiegeEngineAsset &asset, const glm::vec3 &position, const glm::vec3 &target) {
        Launch launch;
        const AimResult aim = SolveLaunch(asset, position, FacingRotation(position, target), target, launch);
        if (aim == AimResult::Ok) {
            return target;
        }
        if (aim == AimResult::Unreachable) {
            return std::nullopt;
        }

        const glm::vec2 heading(target.x - position.x, target.y - position.y);
        const float distance = glm::length(heading);
        if (distance < 1e-3F) {
            return std::nullopt;
        }
        // Walked in from the edge it fell past until it lands: past the edge
        // of the range, or past the longest flight.
        const bool tooClose = aim == AimResult::TooClose;
        const float first   = tooClose ? asset.minRange + 0.5F : asset.maxRange - 0.5F;
        for (float step = first; tooClose ? step <= asset.maxRange : step > asset.minRange; step += tooClose ? 10.0F : -10.0F) {
            const glm::vec2 along = glm::vec2(position) + heading / distance * step;
            const glm::vec3 point(along.x, along.y, target.z);
            if (SolveLaunch(asset, position, FacingRotation(position, point), point, launch) == AimResult::Ok) {
                return point;
            }
        }
        return std::nullopt;
    }

    /**
     * How close `point` comes to the arc, and when along it, in seconds after
     * launch. Sampled rather than solved: the closest point to a parabola is a
     * cubic, and a reported impact only needs to be near enough.
     */
    inline float DistanceToArc(const glm::vec3 &origin, const glm::vec3 &velocity, const glm::vec3 &point, float untilSeconds, float &atSeconds) {
        constexpr float kStep = 0.02F;
        float best            = std::numeric_limits<float>::max();
        atSeconds             = 0.0F;
        for (float seconds = 0.0F; seconds <= untilSeconds; seconds += kStep) {
            const glm::vec3 separation = PositionAt(origin, velocity, seconds) - point;
            const float distance       = glm::dot(separation, separation);
            if (distance < best) {
                best      = distance;
                atSeconds = seconds;
            }
        }
        return std::sqrt(best);
    }

    /** How much of an engine's damage reaches something `distance` from the impact: all of it at the point, a quarter at the edge, none past it. */
    inline float DamageFalloff(float distance, float radius) {
        if (!(radius > 0.0F) || distance > radius) {
            return 0.0F;
        }
        return 1.0F - 0.75F * std::clamp(distance / radius, 0.0F, 1.0F);
    }
} // namespace KCDC::Shared::Game::Siege
