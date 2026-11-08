#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

namespace KCDC::Shared::Game {
    /** Authored CAF length at playback scale one, from Animations.img. */
    struct AlchemyAnimationClip {
        std::string_view name;
        std::string_view path;
        float durationSeconds;
    };
    /** Authored mannequin mapping; looping clips are not finite actions. */
    struct AlchemyAnimationFragment {
        std::string_view fragment;
        std::string_view tags;
        std::string_view fragmentTags;
        std::string_view clip;
        std::uint32_t layer;
        bool loop;
    };
    /** Normalized native animation event position in its authored CAF. */
    struct AlchemyAnimationEvent {
        std::string_view clip;
        std::string_view name;
        float normalizedTime;
    };
    /** Object skeleton events driving native heating suppression and toggle. */
    struct AlchemyKettleMotion {
        bool onFire;
        std::string_view path;
        float durationSeconds;
        float movingNormalizedTime;
        float doneNormalizedTime;
    };
    extern const std::vector<AlchemyKettleMotion> g_AlchemyKettleMotions;
    const AlchemyKettleMotion *FindAlchemyKettleMotion(bool onFire);
    extern const std::vector<AlchemyAnimationEvent> g_AlchemyAnimationEvents;
    /** Returns null when absent or ambiguous; never picks an arbitrary marker. */
    const AlchemyAnimationEvent *FindAlchemyAnimationEvent(std::string_view clip, std::string_view name);
    extern const std::vector<AlchemyAnimationClip> g_AlchemyAnimationClips;
    extern const std::vector<AlchemyAnimationFragment> g_AlchemyAnimationFragments;
    const AlchemyAnimationClip *FindAlchemyAnimationClip(std::string_view name);
} // namespace KCDC::Shared::Game
