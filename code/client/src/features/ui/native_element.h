#pragma once

#include "core/ui/native_ui/native_ui_event.h"

#include "sdk/cryengine/scaleform/c_flash_ui_element.h"
#include "sdk/cryengine/scaleform/flash_variable_object_ref.h"
#include "sdk/cryengine/scaleform/i_ui_element_event_listener.h"
#include "sdk/cryengine/scaleform/s_flash_display_info.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace KCDC::Features::UI {
    /**
     * A handle on one of the game's own UI elements.
     *
     * A declared function is reached the way the engine's own script bridge
     * reaches it: ask the element for the descriptor, walk its parent chain
     * into an ActionScript path, invoke that. The other native route,
     * `CallFunction`, wants a native `SUIArguments`, and an engine container
     * that allocates must not be built on the mod's heap.
     *
     * Handles are shared by key: the same name and instance id is the same
     * handle whoever asks.
     */
    class NativeElement final: public SDK::IUIElementEventListener {
      public:
        ~NativeElement() override;

        /** Opens, or returns, the handle for one element instance. */
        static NativeElement *Acquire(const std::string &name, int instanceId, std::string &error);
        /** Drops every handle and its event registration. */
        static void ReleaseAll();
        /** The key an event carries. */
        static std::string MakeKey(const std::string &name, int instanceId);

        const std::string &GetKey() const {
            return _key;
        }
        const std::string &GetName() const {
            return _name;
        }
        int GetInstanceId() const {
            return _instanceId;
        }

        /** Calls a function the element's XML declares. The element must be loaded. */
        bool Call(const std::string &function, const std::vector<SDK::SFlashVarValue> &arguments, SDK::SFlashVarValue &result, std::string &resultText) const;
        bool SetVariable(const std::string &name, const SDK::SFlashVarValue &value) const;
        bool GetVariable(const std::string &name, SDK::SFlashVarValue &value, std::string &text) const;
        bool SetArray(const std::string &name, const std::vector<SDK::SFlashVarValue> &values) const;
        /**
         * Writes an array the way the engine's own setter does: resolve the
         * array object already bound to the name and mutate it in place.
         *
         * `SetArray` builds a fresh array and rebinds the variable. For a movie
         * that reads the variable each time it is used, the two are equivalent.
         * For one that captured the array at init they are not -- rebinding is
         * then invisible, and the call succeeds while nothing changes on screen.
         *
         * `CFlashUIElement::SetArray` (`0x1807F6300`) resolves the existing
         * object, calls `SetArraySize`, then `SetElement` per value. This is
         * that, and it is the one to reach for when driving the game's own
         * elements rather than a movie of ours.
         */
        bool SetArrayInPlace(const std::string &name, const std::vector<SDK::SFlashVarValue> &values) const;
        bool GetArray(const std::string &name, std::vector<KCDC::Core::NativeUIValue> &values) const;
        /** Scale and alpha are percentages, as everywhere in the movie. */
        bool SetMovieClipDisplay(const std::string &name, const SDK::SFlashDisplayInfo &info) const;

        /** Reads a declared movie clip back; presence bits say what is set. */
        bool GetMovieClipDisplay(const std::string &name, SDK::SFlashDisplayInfo &info) const;
        /** One-based frame, or a frame label when `label` is set. */
        bool GotoMovieClipFrame(const std::string &name, unsigned int frame, const char *label, bool play) const;

        bool Show() const;
        bool Hide() const;
        bool RequestHide() const;
        bool IsVisible() const;
        /** Rebuilds the movie from cached assets, as UIAction's Reload does. */
        bool Reload() const;
        /** Drops the movie; the next Show loads it again. */
        bool Unload() const;

        /** Starts forwarding this element's declared events to the queue. */
        bool Listen();
        static NativeElement *Find(const std::string &key);

        void OnUIEvent(SDK::IUIElement *sender, const SDK::SUIEventDesc &event, const SDK::SUIArguments &arguments, SDK::SFlashVarValue *result) override;
        void OnUnload(SDK::IUIElement *sender) override;

      private:
        NativeElement(std::string key, std::string name, int instanceId, SDK::CFlashUIElement *element, bool owned);

        /** The ActionScript path a descriptor names, `_root` first. */
        static bool BuildPath(const SDK::SUIParameterDesc *descriptor, std::string &path);
        /** The variable object behind a declared movie clip, owned by the caller. */
        bool ResolveMovieClip(const std::string &name, SDK::FlashVariableObjectRef &clip) const;
        SDK::IFlashPlayer *Movie() const;

        std::string _key;
        std::string _name;
        int _instanceId                = 0;
        SDK::CFlashUIElement *_element = nullptr;
        /** True when the instance was created for us and owes a Release. */
        bool _owned     = false;
        bool _listening = false;

        static std::unordered_map<std::string, std::unique_ptr<NativeElement>> _handles;
    };
} // namespace KCDC::Features::UI
