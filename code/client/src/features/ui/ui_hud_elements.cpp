#include <utils/safe_win32.h>

#include "ui_hud_elements.h"

#include "sdk/patterns.h"
#include "sdk/wh/guimodule/c_ui_hud_mask.h"

#include <MinHook.h>
#include <utils/hooking/hook_function.h>

namespace KCDC::Features::UI {
    HudElements &HudElements::Get() {
        static HudElements instance;
        return instance;
    }

    void HudElements::Install() {
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.CFlashUIElement__Render), reinterpret_cast<LPVOID>(&RenderHook), reinterpret_cast<LPVOID *>(&Get()._renderOriginal));
    }

    void __fastcall HudElements::RenderHook(SDK::CFlashUIElement *element) {
        HudElements &owner = Get();
        owner.BeforeRender(element);
        owner._renderOriginal(element);
    }

    void HudElements::BeforeRender(SDK::CFlashUIElement *element) {
        if ((_hiddenMask.load() | _restoreMask.load()) == 0 || element->GetInstanceID() != 0 || std::string_view(element->GetName()) != "hud") {
            return;
        }

        // Take restoration work before reading suppression. A release racing
        // this draw queues another restoration for the following draw, rather
        // than being cleared by a hide already in progress.
        const std::uint32_t restore                 = _restoreMask.exchange(0);
        const std::uint32_t hidden                  = _hiddenMask.load();
        SDK::wh::guimodule::C_UIHudMask *nativeMask = SDK::wh::guimodule::C_UIHudMask::GetInstance();
        std::uint32_t retry                         = 0;

        // Render/RenderLockless share 0x181006468. The movie has advanced by
        // this point, but its render has not been submitted. Resolve fresh
        // wrappers so neither a reload nor timeline replacement leaves us
        // writing a detached display object. Release through the native heap.
        for (std::size_t index = 0; index < std::size(SDK::wh::framework::E_HudElements::kNames); ++index) {
            const std::uint32_t bit = std::uint32_t {1} << index;
            if (((hidden | restore) & bit) == 0) {
                continue;
            }
            const bool suppressed = (hidden & bit) != 0;
            if (!suppressed && (nativeMask == nullptr || nativeMask->GetElement() != element)) {
                retry |= bit;
                continue;
            }
            SDK::SFlashDisplayInfo display;
            display.SetVisible(!suppressed && nativeMask->IsElementVisible(static_cast<SDK::wh::framework::E_HudElements::Type>(index)));
            SDK::FlashVariableObjectRef clip;
            if (!element->GetLoadedMovieClip(SDK::wh::framework::E_HudElements::kNames[index].data(), clip) || !clip->SetDisplayInfo(display)) {
                retry |= restore & bit;
            }
        }
        _restoreMask.fetch_or(retry);
    }

    void HudElements::PublishHiddenMask() {
        std::uint32_t hidden = 0;
        for (const auto &[element, resource] : _owners) {
            hidden |= std::uint32_t {1} << static_cast<std::uint8_t>(element);
        }
        const std::uint32_t previous = _hiddenMask.exchange(hidden);
        _restoreMask.fetch_or(previous & ~hidden);
    }

    bool HudElements::SetVisible(const std::string &resource, SDK::wh::framework::E_HudElements::Type element, bool visible) {
        if (visible) {
            _owners.erase(element);
        }
        else {
            _owners[element] = resource;
        }
        PublishHiddenMask();
        return true;
    }

    bool HudElements::IsVisible(SDK::wh::framework::E_HudElements::Type element) const {
        return (_hiddenMask.load() & (std::uint32_t {1} << static_cast<std::uint8_t>(element))) == 0;
    }

    void HudElements::Release(const std::string &resource) {
        for (auto entry = _owners.begin(); entry != _owners.end();) {
            if (entry->second != resource) {
                ++entry;
                continue;
            }
            entry = _owners.erase(entry);
        }
        PublishHiddenMask();
        if (_voiceIndicatorOwner == resource) {
            _voiceIndicatorOwner.reset();
        }
    }

    void HudElements::Reset() {
        _owners.clear();
        PublishHiddenMask();
        _monologueEnabled      = false;
        _storyCutscenesEnabled = false;
        _voiceIndicatorOwner.reset();
    }

    void HudElements::InstallCleanup(Framework::Scripting::ResourceManager &manager) {
        if (_cleanupInstalled) {
            return;
        }
        manager.AddOnResourceStopped([](const std::string &resource) {
            HudElements::Get().Release(resource);
        });
        _cleanupInstalled = true;
    }
} // namespace KCDC::Features::UI

static InitFunction init([]() { KCDC::Features::UI::HudElements::Install(); }, "UI");
