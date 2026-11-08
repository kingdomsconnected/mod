#pragma once

#include "core/feature_registry.h"
#include "sdk/cryengine/scaleform/c_flash_ui_element.h"
#include "sdk/wh/framework/e_hud_elements.h"

#include <scripting/resource/resource_manager.h>

#include <atomic>
#include <map>
#include <optional>
#include <string>

namespace KCDC::Features::UI {
    /**
     * Resource-owned suppression of native HUD elements. Hidden clips are
     * suppressed before render submission, after the movie advances. A
     * missing movie defers application without losing the request.
     *
     * Every override belongs to the resource that made it last, and the
     * native HUD policy takes over when that resource stops. Restoration
     * queries the live native mask; it never forces an element on or restores
     * a stale snapshot of a gameplay rule.
     *
     * Requests and ownership are game-thread only. The render hook reads only
     * an atomic suppression mask, including during native loading rendering.
     */
    class HudElements final: public KCDC::Core::IFeatureService {
      public:
        static HudElements &Get();
        static void Install();

        /** False retains suppression; true releases it. Works before HUD creation. */
        bool SetVisible(const std::string &resource, SDK::wh::framework::E_HudElements::Type element, bool visible);

        /** Whether multiplayer permits the element. Native gameplay rules can still hide it. */
        bool IsVisible(SDK::wh::framework::E_HudElements::Type element) const;

        /** Releases this resource's suppression back to the current native policy. */
        void Release(const std::string &resource);

        /** Releases each resource's overrides as it stops. The manager lives as long as the process, so this installs once. */
        void InstallCleanup(Framework::Scripting::ResourceManager &manager);

        void Reset() override;

        /**
         * The local character's monologue (`ui_monologue_hooks.cpp`): whether
         * a script let the game say it, and whether a line of it is being
         * spoken right now. Game-thread only.
         */
        bool IsMonologueEnabled() const {
            return _monologueEnabled;
        }
        void SetMonologueEnabled(bool enabled) {
            _monologueEnabled = enabled;
        }
        void SetSpeakingMonologue(bool speaking) {
            _speakingMonologue = speaking;
        }
        bool IsMonologueSuppressed() const {
            return _speakingMonologue && !_monologueEnabled;
        }

        /**
         * Story cutscenes (`ui_cutscene_hooks.cpp`): whether a script let the
         * quest graph play them. Game-thread only.
         */
        bool IsStoryCutscenesEnabled() const {
            return _storyCutscenesEnabled;
        }
        void SetStoryCutscenesEnabled(bool enabled) {
            _storyCutscenesEnabled = enabled;
        }

        /**
         * The mod's own voice indicator -- the microphone low on the left
         * while the local player sends (`VoiceService::RenderTransmitIndicator`)
         * and the talking icon on a nametag (`NametagPass::Draw`). Hidden
         * while a resource holds it, which is released like an element when
         * that resource stops or the session ends. Game-thread only.
         */
        bool IsVoiceIndicatorVisible() const {
            return !_voiceIndicatorOwner.has_value();
        }
        void SetVoiceIndicatorVisible(const std::string &resource, bool visible) {
            if (visible) {
                _voiceIndicatorOwner.reset();
            }
            else {
                _voiceIndicatorOwner = resource;
            }
        }

      private:
        void PublishHiddenMask();
        void BeforeRender(SDK::CFlashUIElement *element);
        static void __fastcall RenderHook(SDK::CFlashUIElement *element);

        std::map<SDK::wh::framework::E_HudElements::Type, std::string> _owners;
        std::atomic<std::uint32_t> _hiddenMask {0};
        // Consumed only after a successful write to the current movie. A
        // resource may stop while the HUD is unloaded or the session ends.
        std::atomic<std::uint32_t> _restoreMask {0};
        void(__fastcall *_renderOriginal)(SDK::CFlashUIElement *) = nullptr;
        bool _cleanupInstalled                                    = false;
        bool _monologueEnabled                                    = false;
        bool _speakingMonologue                                   = false;
        bool _storyCutscenesEnabled                               = false;
        std::optional<std::string> _voiceIndicatorOwner;
    };
} // namespace KCDC::Features::UI
