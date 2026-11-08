#pragma once

#include "shared/streamed_assets/streamed_asset_policy.h"

#include <networking/replication/network_entity.h>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>

namespace KCDC::Shared::Game {
    /**
     * A static model held on one joint of the body for as long as an
     * animation plays -- the axe in a woodcutter's hand, the bucket at a well.
     */
    struct AnimationProp {
        static constexpr std::size_t kMaxModelLength = 192;
        static constexpr std::size_t kMaxJointLength = 64;

        /** `objects/...cgf`, what `I3DEngine::LoadStatObj` takes. Empty is no prop. */
        std::string model;

        /** The skeleton joint it hangs on; `righthandslot` and `lefthandslot` are the hands. */
        std::string joint;

        /** Offset from the joint, in metres and in the joint's own frame. */
        glm::vec3 position = glm::vec3(0.0F);
        glm::quat rotation = glm::identity<glm::quat>();

        bool IsSet() const {
            return !model.empty();
        }

        bool IsSane() const {
            if (model.empty()) {
                return joint.empty();
            }
            const bool finite = std::isfinite(position.x) && std::isfinite(position.y) && std::isfinite(position.z) && std::isfinite(rotation.x) && std::isfinite(rotation.y) && std::isfinite(rotation.z) && std::isfinite(rotation.w);
            return finite && model.size() <= kMaxModelLength && !joint.empty() && joint.size() <= kMaxJointLength && glm::length(position) <= 4.0F && std::fabs(glm::length(rotation) - 1.0F) < 0.01F && IsModelPath(model);
        }

        bool operator==(const AnimationProp &other) const = default;

        /** Only a `.cgf` under `objects/`, spelled with nothing that climbs out of it. */
        static bool IsModelPath(const std::string &path) {
            const auto endsWith = [&](const char *suffix) {
                const std::size_t length = std::char_traits<char>::length(suffix);
                return path.size() > length && path.compare(path.size() - length, length, suffix) == 0;
            };
            return path.rfind("objects/", 0) == 0 && endsWith(".cgf") && path.find("..") == std::string::npos && path.find('\\') == std::string::npos && path.find(':') == std::string::npos;
        }
    };

    /**
     * What a body is being made to play, as the server last said.
     *
     * Held state rather than an order, for the reason `NpcIntent` gives: a
     * client that streams the body in, joins late or takes over its
     * simulation reads this and plays what is current, where an order sent
     * once would have been missed. `revision` is what tells a new request
     * from the same one read again, so a one-shot is played once per request
     * and a repeat of the same fragment still restarts it.
     */
    struct AnimationPlayback {
        static constexpr std::size_t kMaxProps          = 2;
        static constexpr std::size_t kMaxFragmentLength = 96;
        static constexpr std::size_t kMaxTagsLength     = 192;

        /** Bumped by every play and every stop; 0 means nothing was ever asked. */
        std::uint32_t revision = 0;

        /** Whether anything is being played at all. A stop clears it and bumps `revision`. */
        bool active = false;

        /**
         * The Mannequin fragment, by name. Empty plays no fragment and leaves
         * the body to its own animation, which with a hand tag and a prop is
         * how a body is made to carry something while it walks.
         */
        std::string fragment;

        /**
         * `+`-separated tags, as `actor_anim_action` spells them. Each is
         * looked up in the fragment's own tag dictionary first and in the
         * body's global one after, the order `C_Player::PlayAnim` uses.
         */
        std::string tags;

        /** Play again whenever the fragment ends or is interrupted, until stopped. */
        bool loop = false;

        /** Hold the local player's body still under the animation. Only the owner's body moves itself. */
        bool lockMovement = false;

        /**
         * The request is over when its fragment is: its tags come off at the
         * fragment's completion point, in the same controller update, so the
         * body blends into whatever follows without them. A body that first
         * sees the request after it began applies none of it. This is how
         * `stopAnimation({ standUp: true })` plays the way out of a stance:
         * the stance tag has to hold through `StandUp` for the database to
         * pick the variant that fits, and must be gone the moment it ends.
         */
        bool endsWithFragment = false;

        /**
         * A clip the server streams, `animations/kcdc/<resource>/...caf`, played
         * through `KCDC::Shared::StreamedAssets::kStreamedClipFragment`; empty
         * for one of the game's own fragments. Each body picks the option of
         * that fragment its level's database holds the clip under.
         */
        std::string clip;

        AnimationProp props[kMaxProps];

        bool IsSane() const {
            if (fragment.size() > kMaxFragmentLength || tags.size() > kMaxTagsLength) {
                return false;
            }
            if (!clip.empty() && (fragment != KCDC::Shared::StreamedAssets::kStreamedClipFragment || clip.size() > Framework::Utils::StreamedAssets::kMaxPathLength || !KCDC::Shared::StreamedAssets::Policy::IsStreamedClipPath(clip))) {
                return false;
            }
            for (const AnimationProp &prop : props) {
                if (!prop.IsSane()) {
                    return false;
                }
            }
            return !active || !fragment.empty() || !tags.empty() || props[0].IsSet() || props[1].IsSet();
        }

        bool operator==(const AnimationPlayback &other) const = default;

        static std::uint32_t Advance(std::uint32_t revision) {
            return revision + 1 != 0 ? revision + 1 : 1;
        }

        /**
         * One body for every channel that carries it -- a player's relay, an
         * NPC's field and forced state, the owner's own RPC. The props are
         * always written, set or not: the delta channel matches fields by
         * call order, so the number of calls must never change.
         */
        void Serialize(Framework::Networking::Replication::FieldSerializer &fields) {
            fields.Field(revision);
            fields.Field(active);
            fields.Field(fragment);
            fields.Field(tags);
            fields.Field(loop);
            fields.Field(lockMovement);
            fields.Field(endsWithFragment);
            fields.Field(clip);
            for (AnimationProp &prop : props) {
                fields.Field(prop.model);
                fields.Field(prop.joint);
                fields.Field(prop.position.x);
                fields.Field(prop.position.y);
                fields.Field(prop.position.z);
                fields.Field(prop.rotation.x);
                fields.Field(prop.rotation.y);
                fields.Field(prop.rotation.z);
                fields.Field(prop.rotation.w);
            }
        }
    };

    /**
     * Why a request's animation stopped playing.
     *
     * The first three are what the body that plays it saw, reported by the
     * one client whose body counts -- the owner for a player, the simulator
     * for an NPC -- from the Mannequin edge that ended it. The last two are
     * the server's own doing and never travel.
     */
    enum class AnimationEndReason : std::uint8_t {
        Finished,    // a one-shot reached its end
        Interrupted, // it was playing and the game cut it short: a hit, a weapon drawn, a teleport
        Refused,     // it never started: the controller would not install it
        Stopped,     // `stopAnimation`
        Replaced,    // another `playAnimation`
        Count,
    };

    /** The reasons a client may report; the others are the server's. */
    inline bool IsReportableAnimationEnd(AnimationEndReason reason) {
        return reason == AnimationEndReason::Finished || reason == AnimationEndReason::Interrupted || reason == AnimationEndReason::Refused;
    }

    inline const char *AnimationEndReasonName(AnimationEndReason reason) {
        switch (reason) {
        case AnimationEndReason::Finished: return "finished";
        case AnimationEndReason::Interrupted: return "interrupted";
        case AnimationEndReason::Refused: return "refused";
        case AnimationEndReason::Stopped: return "stopped";
        case AnimationEndReason::Replaced: return "replaced";
        default: return "finished";
        }
    }
} // namespace KCDC::Shared::Game
