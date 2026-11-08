#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace KCDC::Features::UI {
    /**
     * One renderer post-effect parameter the screen effects are composed
     * into. The first eight are the game's own tint, in the order
     * `SDK::wh::guimodule::C_UIOverlay::E_UserColor` keeps them.
     */
    enum class ScreenChannel : std::uint8_t {
        Cyan,
        Magenta,
        Yellow,
        Darkness,
        Brightness,
        Contrast,
        Saturation,
        Hue,
        Blur,
        Ghosting,
        ScreenBlood,
        Count,
    };

    inline constexpr std::size_t kScreenChannelCount = static_cast<std::size_t>(ScreenChannel::Count);

    /** How a channel folds the effects on it together. */
    enum class ScreenFold : std::uint8_t {
        /** Each effect scales it, starting from 1: brightness, contrast, saturation. */
        Multiply,
        /** Each effect adds to it, starting from 0: the colour cast and the hue shift. */
        Add,
        /** The strongest request is drawn, starting from 0: blur, ghosting, blood. */
        Strongest,
    };

    /** One parameter an effect takes, as scripts see it. */
    struct ScreenEffectParam {
        std::string_view name;
        ScreenChannel channel;
        float min;
        float max;

        /** The value at which the parameter changes nothing. */
        float neutral;

        /** What `create` uses when the parameter is not given. */
        float fallback;

        std::string_view description;
    };

    /** One effect a script can create. */
    struct ScreenEffectSpec {
        std::string_view name;
        std::string_view description;
        std::span<const ScreenEffectParam> params;
    };

    /**
     * Every screen effect resources have asked for, and what they add up to.
     *
     * Pure bookkeeping on a millisecond clock the caller supplies: no engine
     * call is made from here, which is what lets the composition rules be
     * tested on their own. `HudScreenEffects` owns the one instance and
     * takes the result to the renderer.
     *
     * Effects are folded in creation order -- also the order of their
     * handles -- and every fold is commutative, so the result does not depend
     * on which resource asked first. An effect is weighted by its fades, and
     * a weighted parameter is moved towards its neutral value rather than
     * switched off.
     */
    class ScreenEffectStack final {
      public:
        /** Never handed out, so it is the "no effect" answer everywhere. */
        static constexpr std::uint32_t kNoHandle = 0;

        /** The most parameters one effect takes. */
        static constexpr std::size_t kMaxParams = 8;

        /** How many effects are kept at once, across every resource. */
        static constexpr std::size_t kMaxEffects = 64;

        static constexpr std::uint32_t kMaxFadeMs     = 60000;
        static constexpr std::uint32_t kMaxDurationMs = 86400000;

        /** Values for an effect's parameters, in the order its spec lists them. An entry not `given` is left as it was. */
        struct ParamValues {
            std::array<float, kMaxParams> values {};
            std::array<bool, kMaxParams> given {};
        };

        /** How an effect comes and goes. A zero duration lasts until the effect is removed. */
        struct Timing {
            std::uint32_t fadeInMs   = 0;
            std::uint32_t fadeOutMs  = 0;
            std::uint32_t durationMs = 0;
        };

        /**
         * What every effect adds up to at one moment, before the game's own
         * values are folded in. A `Multiply` channel holds the product of its
         * factors, an `Add` channel their sum, a `Strongest` channel the
         * strongest weighted request on the 0..1 scale scripts use.
         */
        struct Composition {
            std::array<float, kScreenChannelCount> value {};

            /** At least one live effect has a parameter on the channel, faded out or not. */
            std::array<bool, kScreenChannelCount> held {};
        };

        /** Every effect a script can create, in a fixed order. */
        static std::span<const ScreenEffectSpec> Effects();

        /** The effect by its script name, or null. */
        static const ScreenEffectSpec *FindEffect(std::string_view name);

        static ScreenFold FoldOf(ScreenChannel channel);

        /** The value the fold starts from, which is also the value that changes nothing. */
        static float NeutralOf(ScreenChannel channel);

        /** Starts an effect for `resource`, clamping every value into its range. `kNoHandle` when the budget is full. */
        std::uint32_t Create(const std::string &resource, const ScreenEffectSpec &effect, const ParamValues &values, const Timing &timing, std::uint64_t nowMs);

        /** Moves the given parameters to new values over `fadeMs`, from wherever they are at `nowMs`. False for a handle `resource` does not own. */
        bool Retune(const std::string &resource, std::uint32_t handle, const ParamValues &values, std::uint32_t fadeMs, std::uint64_t nowMs);

        /**
         * Fades an effect out over `fadeOutMs`, or over the fade-out it was
         * created with when `ownFade` is set, and drops it at the end. An
         * effect already fading out carries on from where its weight is.
         */
        bool Remove(const std::string &resource, std::uint32_t handle, bool ownFade, std::uint32_t fadeOutMs, std::uint64_t nowMs);

        /** Drops every effect `resource` owns at once. Returns how many. */
        std::size_t RemoveAll(const std::string &resource);

        /** Drops every effect. */
        void Clear();

        /** Whether the handle names an effect still on screen at `nowMs`, fading out included. */
        bool IsActive(std::uint32_t handle, std::uint64_t nowMs) const;

        /** Which effect a handle `resource` owns is, or null for one it does not own. */
        const ScreenEffectSpec *EffectOf(const std::string &resource, std::uint32_t handle) const;

        bool Empty() const {
            return _effects.empty();
        }

        std::size_t Count() const {
            return _effects.size();
        }

        /** Drops the effects that have faded out by `nowMs`, then folds the rest. */
        Composition Compose(std::uint64_t nowMs);

      private:
        struct Effect {
            std::uint32_t handle = kNoHandle;
            std::string resource;
            const ScreenEffectSpec *spec = nullptr;

            // A retune moves every parameter from `from` to `to` over
            // `retuneMs`, starting at `retuneStartMs`.
            std::array<float, kMaxParams> from {};
            std::array<float, kMaxParams> to {};
            std::uint64_t retuneStartMs = 0;
            std::uint32_t retuneMs      = 0;

            std::uint64_t createdMs = 0;
            std::uint32_t fadeInMs  = 0;
            std::uint32_t fadeOutMs = 0;

            /** When the duration runs out and the fade-out starts, or 0 for none. */
            std::uint64_t endsAtMs = 0;

            bool leaving               = false;
            std::uint64_t leaveStartMs = 0;
            std::uint32_t leaveMs      = 0;
        };

        static float ParamAt(const Effect &effect, std::size_t index, std::uint64_t nowMs);
        static float FadeOutWeight(const Effect &effect, std::uint64_t nowMs);
        static float WeightAt(const Effect &effect, std::uint64_t nowMs);

        /** Starts the fade-out its duration asked for, timed from when the duration ended rather than from when this is noticed. */
        static void BeginScheduledLeave(Effect &effect, std::uint64_t nowMs);

        Effect *Find(std::uint32_t handle);
        const Effect *Find(std::uint32_t handle) const;

        std::vector<Effect> _effects;
        std::uint32_t _nextHandle = 1;
    };
} // namespace KCDC::Features::UI
