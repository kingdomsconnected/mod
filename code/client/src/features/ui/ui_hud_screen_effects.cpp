#include "ui_hud_screen_effects.h"

#include "sdk/cryengine/3dengine/i_3d_engine.h"
#include "sdk/wh/guimodule/c_ui_overlay.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace KCDC::Features::UI {
    namespace {
        struct ChannelTarget {
            /** The renderer's own name for the parameter. */
            const char *engineName;

            // What a composed value is clamped to before it is written.
            float min;
            float max;
        };

        // Indexed by ScreenChannel. The eight Global_User_* parameters are the
        // ones C_UIOverlay::ApplyUserColor (0x1808BDD5C) publishes; the
        // renderer folds them into the colour matrix
        // (CPostEffectsMgr::GetColorMatrix, 0x180664418) that colour grading
        // merges every frame. That matrix clamps brightness to [0, 100] and
        // contrast and saturation to [-1, 100]; the bounds here stop at what
        // still reads as a picture.
        constexpr std::array<ChannelTarget, kScreenChannelCount> kTargets {{
            {"Global_User_ColorC", -1.0F, 1.0F},
            {"Global_User_ColorM", -1.0F, 1.0F},
            {"Global_User_ColorY", -1.0F, 1.0F},
            {"Global_User_ColorK", -1.0F, 1.0F},
            {"Global_User_Brightness", 0.0F, 4.0F},
            {"Global_User_Contrast", 0.0F, 4.0F},
            {"Global_User_Saturation", 0.0F, 4.0F},
            {"Global_User_ColorHue", -1.0F, 1.0F},
            {"FilterBlurring_Amount", 0.0F, 1.0F},
            {"ImageGhosting_Amount", 0.0F, 1.0F},
            {"ScreenBlood_Amount", 0.0F, 1.0F},
        }};

        constexpr std::size_t kUserColorChannels = SDK::wh::guimodule::C_UIOverlay::kUserColorCount;

        /** Below this a composed value counts as unchanged, so a steady effect is written once. */
        constexpr float kWriteEpsilon = 1.0e-4F;

        bool IsUserColor(ScreenChannel channel) {
            return static_cast<std::size_t>(channel) < kUserColorChannels;
        }

        /**
         * A script's 0..1 strength as the value the pass responds to. Each
         * mapping is read off the pass and its shader in the shipped cache, so
         * 0 is off and the rest of the scale is visible:
         *
         * - Blur. `CBlurringPostEffect::Preprocess` (0x180CB76E8) skips the
         *   pass at 0.09 and below; `BlurInterpolationPS` then lerps the screen
         *   towards its blurred copy by the amount.
         * - Ghosting. `ImageGhostingPS` writes the previous frame back with
         *   alpha `saturate(1 - (1 - amount + frameTime) * 33 / 8)`, which is
         *   zero below about 0.75 at 60 fps.
         * - Screen blood. `ScreenBloodPS` thresholds its mask at
         *   `max(0.2, border.z * (1 - amount))`, `ScreenBlood_Border.z` being 2
         *   (`0x18074803C`): nothing shows below about 0.45, and it is full at
         *   0.9.
         */
        float ToEngine(ScreenChannel channel, float strength) {
            if (strength <= 0.0F) {
                return 0.0F;
            }
            switch (channel) {
            case ScreenChannel::Blur: return 0.1F + 0.9F * strength;
            case ScreenChannel::Ghosting: return 0.75F + 0.25F * strength;
            case ScreenChannel::ScreenBlood: return 0.45F + 0.45F * strength;
            default: return strength;
            }
        }

        std::uint64_t NowMs() {
            return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
        }
    } // namespace

    HudScreenEffects &HudScreenEffects::Get() {
        static HudScreenEffects instance;
        return instance;
    }

    std::uint32_t HudScreenEffects::Create(const std::string &resource, const ScreenEffectSpec &effect, const ScreenEffectStack::ParamValues &values, const ScreenEffectStack::Timing &timing) {
        ObserveNativeContext();
        const std::uint32_t handle = _stack.Create(resource, effect, values, timing, NowMs());
        if (handle != ScreenEffectStack::kNoHandle) {
            Apply();
        }
        return handle;
    }

    bool HudScreenEffects::Retune(const std::string &resource, std::uint32_t handle, const ScreenEffectStack::ParamValues &values, std::uint32_t fadeMs) {
        ObserveNativeContext();
        if (!_stack.Retune(resource, handle, values, fadeMs, NowMs())) {
            return false;
        }
        Apply();
        return true;
    }

    bool HudScreenEffects::Remove(const std::string &resource, std::uint32_t handle, bool ownFade, std::uint32_t fadeOutMs) {
        ObserveNativeContext();
        if (!_stack.Remove(resource, handle, ownFade, fadeOutMs, NowMs())) {
            return false;
        }
        Apply();
        return true;
    }

    std::size_t HudScreenEffects::RemoveAll(const std::string &resource) {
        ObserveNativeContext();
        const std::size_t removed = _stack.RemoveAll(resource);
        if (removed != 0) {
            Apply();
        }
        return removed;
    }

    bool HudScreenEffects::IsActive(std::uint32_t handle) {
        ObserveNativeContext();
        return _stack.IsActive(handle, NowMs());
    }

    const ScreenEffectSpec *HudScreenEffects::EffectOf(const std::string &resource, std::uint32_t handle) {
        ObserveNativeContext();
        return _stack.EffectOf(resource, handle);
    }

    void HudScreenEffects::InstallCleanup(Framework::Scripting::ResourceManager &manager) {
        if (_cleanupInstalled) {
            return;
        }

        manager.AddOnResourceStopped([](const std::string &resource) {
            HudScreenEffects::Get().RemoveAll(resource);
        });
        _cleanupInstalled = true;
    }

    void HudScreenEffects::OnUserColorApplied() {
        if (_applying) {
            return;
        }

        // The game has just written its own tint over whatever the
        // composition had put there. Nothing else changed, so this is the one
        // moment the composition has to be written again.
        bool owned = false;
        for (std::size_t c = 0; c < kUserColorChannels; ++c) {
            owned |= _channels[c].owned;
            _channels[c].written = false;
        }
        if (owned) {
            Apply();
        }
    }

    void HudScreenEffects::Update() {
        ObserveNativeContext();
        if (!_stack.Empty()) {
            Apply();
        }
    }

    void HudScreenEffects::Reset() {
        _stack.Clear();
        Apply();
    }

    void HudScreenEffects::NativeContextChanged() {
        ++_nativeContext;
    }

    void HudScreenEffects::ObserveNativeContext() {
        const std::uint64_t context = _nativeContext.load();
        if (context == _observedContext) {
            return;
        }
        _observedContext = context;
        _stack.Clear();
        Apply();
    }

    float HudScreenEffects::BaseOf(ScreenChannel channel) const {
        if (!IsUserColor(channel)) {
            return _channels[static_cast<std::size_t>(channel)].capturedBase;
        }

        // The game's tint as the game itself would publish it right now.
        SDK::wh::guimodule::C_UIOverlay *overlay = SDK::wh::guimodule::C_UIOverlay::GetInstance();
        if (overlay == nullptr) {
            return _channels[static_cast<std::size_t>(channel)].capturedBase;
        }
        if (overlay->AreOverlaysHidden()) {
            return ScreenEffectStack::NeutralOf(channel);
        }
        return overlay->GetUserColor(static_cast<SDK::wh::guimodule::C_UIOverlay::E_UserColor>(channel));
    }

    void HudScreenEffects::Apply() {
        _applying = true;

        SDK::I3DEngine *engine                                   = SDK::I3DEngine::GetInstance();
        const ScreenEffectStack::Composition composition         = _stack.Compose(NowMs());
        bool restoreUserColor                                    = false;

        for (std::size_t c = 0; c < kScreenChannelCount; ++c) {
            const ScreenChannel channel = static_cast<ScreenChannel>(c);
            const ChannelTarget &target = kTargets[c];
            ChannelState &state         = _channels[c];

            if (!composition.held[c]) {
                if (!state.owned) {
                    continue;
                }
                state.owned   = false;
                state.written = false;
                if (IsUserColor(channel) && SDK::wh::guimodule::C_UIOverlay::GetInstance() != nullptr) {
                    restoreUserColor = true;
                }
                else {
                    engine->SetPostEffectParam(target.engineName, state.capturedBase, true);
                }
                continue;
            }

            if (!state.owned) {
                // What stands there before the first effect. The tint channels
                // are read off the game's publisher instead, so for them this
                // only matters while the GUI is not built.
                float current = ScreenEffectStack::NeutralOf(channel);
                engine->GetPostEffectParam(target.engineName, current);
                state.capturedBase = current;
                state.owned        = true;
                state.written      = false;
            }

            float value = 0.0F;
            switch (ScreenEffectStack::FoldOf(channel)) {
            case ScreenFold::Multiply: value = BaseOf(channel) * composition.value[c]; break;
            case ScreenFold::Add: value = BaseOf(channel) + composition.value[c]; break;
            case ScreenFold::Strongest: value = std::max(BaseOf(channel), ToEngine(channel, composition.value[c])); break;
            }
            value = std::clamp(value, target.min, target.max);

            if (!state.written || std::fabs(value - state.lastWritten) > kWriteEpsilon) {
                engine->SetPostEffectParam(target.engineName, value, true);
                state.written     = true;
                state.lastWritten = value;
            }
        }

        // The game's own publisher puts its tint back -- or the neutral one
        // while its overlays are hidden -- exactly as it would have stood. It
        // re-enters OnUserColorApplied, which _applying turns away.
        if (restoreUserColor) {
            SDK::wh::guimodule::C_UIOverlay *overlay = SDK::wh::guimodule::C_UIOverlay::GetInstance();
            overlay->ApplyUserColor(!overlay->AreOverlaysHidden());
        }

        _applying = false;
    }
} // namespace KCDC::Features::UI
