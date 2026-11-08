#pragma once

#include "core/feature_registry.h"

#include "features/ui/ui_hud_screen_effect_stack.h"

#include <scripting/resource/resource_manager.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>

namespace KCDC::Features::UI {
    /**
     * Screen effects requested by client resources, on this machine's picture
     * only.
     *
     * The effects themselves -- who asked for what, their fades, how they add
     * up -- are `ScreenEffectStack`'s. This puts the sum on screen through the
     * renderer's own post-effect parameters, the same `CParamFloat`s the game,
     * TrackView and FlowGraph write. Nothing here is replicated and nothing is
     * authority: a script that wants a player to be drunk keeps that state
     * itself and asks for the picture to match it.
     *
     * **The game's own tint.** The game grades the screen from the player's
     * condition through the eight `Global_User_*` parameters, published by
     * `C_UIOverlay::ApplyUserColor` -- it desaturates as health runs low. The
     * composition starts from that tint, read off the overlay page, rather
     * than replacing it, and the publisher is detoured so a re-published tint
     * gets the composition put straight back on top of it.
     *
     * **Writes.** A channel is written forced, and only when its value
     * changes: once for a steady effect, every frame of a fade. Nothing is
     * re-asserted every frame. When the last effect on a channel goes, the
     * game's tint is re-published by the game's own publisher, and the other
     * channels get back the value they held before the first effect.
     *
     * **Lifetime.** An effect belongs to the resource that created it and goes
     * when that resource stops, when the session ends, and when the level
     * changes under it -- a level change also resets the renderer's
     * parameters.
     *
     * Game thread only, except `NativeContextChanged`.
     */
    class HudScreenEffects final: public KCDC::Core::IFeatureService {
      public:
        static HudScreenEffects &Get();

        /** Installs the tint publisher's detour. Called once, at boot. */
        static void Install();

        /** Starts an effect for `resource`. `ScreenEffectStack::kNoHandle` when the budget is full. */
        std::uint32_t Create(const std::string &resource, const ScreenEffectSpec &effect, const ScreenEffectStack::ParamValues &values, const ScreenEffectStack::Timing &timing);

        /** Moves some of an effect's parameters over `fadeMs`. False for a handle `resource` does not own. */
        bool Retune(const std::string &resource, std::uint32_t handle, const ScreenEffectStack::ParamValues &values, std::uint32_t fadeMs);

        /** Fades an effect out, over its own fade-out when `ownFade` is set. False for a handle `resource` does not own. */
        bool Remove(const std::string &resource, std::uint32_t handle, bool ownFade, std::uint32_t fadeOutMs);

        /** Drops every effect `resource` owns at once. Returns how many. */
        std::size_t RemoveAll(const std::string &resource);

        /** Whether the handle names an effect still on screen, fading out included. */
        bool IsActive(std::uint32_t handle);

        /** Which effect a handle `resource` owns is, or null for one it does not own. */
        const ScreenEffectSpec *EffectOf(const std::string &resource, std::uint32_t handle);

        /** Drops a stopped resource's effects. The manager lives as long as the process, so this installs once. */
        void InstallCleanup(Framework::Scripting::ResourceManager &manager);

        /** After the game has published its own tint: puts the composition back on top of it. */
        void OnUserColorApplied();

        void Update() override;
        void Reset() override;

        /** Raised from the loading thread; the next game-thread entry drops every effect. */
        void NativeContextChanged() override;

      private:
        struct ChannelState {
            /** An effect is on this channel, or was, and the value under it has not been put back yet. */
            bool owned = false;

            /** `lastWritten` is what the renderer holds. Cleared when the game writes the channel itself. */
            bool written      = false;
            float lastWritten = 0.0F;

            /** What the channel held before the first effect, for the channels the game does not publish itself. */
            float capturedBase = 0.0F;
        };

        /** Drops every effect after a level change. */
        void ObserveNativeContext();

        /** Composes every effect and writes what changed. Restores what no effect holds any more. */
        void Apply();

        /** The value a channel holds when no effect is on it. */
        float BaseOf(ScreenChannel channel) const;

        ScreenEffectStack _stack;
        std::array<ChannelState, kScreenChannelCount> _channels {};

        /** Set while Apply runs: restoring the tint re-enters the detour. */
        bool _applying = false;

        bool _cleanupInstalled = false;

        // Incremented from the loading thread and consumed on the game thread.
        std::atomic<std::uint64_t> _nativeContext {1};
        std::uint64_t _observedContext = 1;
    };
} // namespace KCDC::Features::UI
