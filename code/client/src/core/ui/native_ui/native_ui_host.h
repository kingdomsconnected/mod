#pragma once

#include "native_ui.h"
#include "native_ui_event.h"

#include "sdk/cryengine/scaleform/c_flash_ui_element.h"

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace KCDC::Core {
    /** Where a host's events are put down; the module drains it once a tick. */
    class NativeUIEventQueue {
      public:
        virtual ~NativeUIEventQueue()           = default;
        virtual void Queue(NativeUIEvent event) = 0;
    };

    /**
     * One native movie, its element registration and every page composed in it.
     *
     * A host is one `UIElement` and therefore one ActionScript global context:
     * pages inside it share imported class definitions, so two libraries that
     * declare the same class need two hosts.
     */
    class NativeUIHost final
        : public SDK::IUIElementEventListener
        , public SDK::IActionScriptFunction {
      public:
        enum class Kind {
            /** A generated KCDC host: the AS bridge and root services exist. */
            Composition,
            /** Any other movie, driven entirely from outside. */
            Plain,
        };

        NativeUIHost(NativeUIEventQueue &events, std::string elementName, Kind kind);
        ~NativeUIHost() override;
        NativeUIHost(const NativeUIHost &)            = delete;
        NativeUIHost &operator=(const NativeUIHost &) = delete;

        /** Registers the element from `descriptorPath` if the registry has no such name, then initializes its movie. */
        bool Open(const std::string &descriptorPath);
        void Close();
        /** Rebuilds the movie from cached assets; every page is closed first. */
        bool Reload();

        bool IsOpen() const {
            return _element != nullptr;
        }
        bool IsReady() const {
            return _ready && static_cast<bool>(_movie);
        }
        Kind GetKind() const {
            return _kind;
        }
        const std::string &GetElementName() const {
            return _elementName;
        }
        const std::string &GetStatus() const {
            return _status;
        }
        SDK::CFlashUIElement *GetElement() const {
            return _element;
        }
        /** Borrowed for the duration of one call; never retained by callers. */
        SDK::IFlashPlayer *GetMovie() const {
            return _movie.get();
        }

        /** Stage size, which is what page layout is expressed in. */
        bool GetStageSize(int &width, int &height) const;

        /**
         * How much wider a horizontal stage unit must be drawn to come out as
         * wide as a vertical one is tall; 1 when it already does.
         *
         * `CFlashUIElement::UpdateViewPort` gives a fullscreen scaling element
         * `eSM_ExactFit` and a viewport of the render size, which fits the
         * stage to it on each axis independently: on any window that is not
         * the stage's own aspect a circle comes out an ellipse and glyphs come
         * out condensed. The other scale modes letterbox, which is already
         * square. Apply it as the x scale of the one container a composition
         * hangs off, and measure again on each show, since the window can be
         * resized in between.
         */
        float GetSquareUnitCorrection() const;
        /** Viewport fraction to stage units, through the element's own mapping. */
        bool ScreenToStage(float screenX, float screenY, float &stageX, float &stageY) const;

        std::shared_ptr<KCDC::Core::NativeUI> CreatePage(const std::string &name, const char *parentPath = "_root");
        std::shared_ptr<KCDC::Core::NativeUI> FindPage(const std::string &name) const;
        bool ClosePage(const std::string &name);
        bool ClosePages();

        /** Routes the focus map's actions into one page's root handlers. Composition hosts only: a plain movie has no bridge to route through. */
        bool SelectInputPage(const std::string &name);
        void ClearInputPage();
        std::shared_ptr<KCDC::Core::NativeUI> GetInputPage() const {
            return _inputPage.lock();
        }
        std::string FindPageName(const KCDC::Core::NativeUI *page) const;

        /** Mouse, keyboard, cursor and exclusivity, as one decision. */
        void SetInputDelivery(bool enabled);
        bool DispatchAction(const char *action, int activationMode);

        void SetVisible(bool visible);
        bool IsVisible() const;
        void SetLayer(int layer);

        void OnInit(SDK::IUIElement *sender, SDK::IFlashPlayer *player) override;
        void OnUnload(SDK::IUIElement *sender) override;
        void OnUIEventEx(SDK::IUIElement *sender, const char *command, const SDK::SUIArguments &arguments, void *userData) override;
        void OnUIEvent(SDK::IUIElement *sender, const SDK::SUIEventDesc &event, const SDK::SUIArguments &arguments, SDK::SFlashVarValue *result) override;
        void Call(const SDK::IActionScriptFunction::Params &params, SDK::IActionScriptFunction::IReturnValue *returnValue) override;

        /** Copies one call's arguments out; native storage expires with the call. */
        static std::vector<NativeUIValue> ConvertArguments(const SDK::IActionScriptFunction::Params &params);
        static std::vector<NativeUIValue> ConvertEventArguments(const SDK::SUIArguments &arguments);

      private:
        friend class KCDC::Core::NativeUI;

        struct Callback {
            std::weak_ptr<KCDC::Core::NativeUI> owner;
            std::function<void(const SDK::IActionScriptFunction::Params &, SDK::IActionScriptFunction::IReturnValue *)> invoke;
        };

        std::uintptr_t RegisterCallback(KCDC::Core::NativeUI *owner, std::function<void(const SDK::IActionScriptFunction::Params &, SDK::IActionScriptFunction::IReturnValue *)> callback);
        void RemoveCallback(std::uintptr_t id);
        bool IsCurrentMovie(const SDK::IFlashPlayer *movie) const {
            return _movie.get() == movie;
        }

        /** Takes the movie and decides readiness; called only from callbacks. */
        void AcquireMovie();
        void ReleaseMovie();
        void QueueReady();

        NativeUIEventQueue &_events;
        std::string _elementName;
        Kind _kind                                         = Kind::Composition;
        SDK::CFlashUIElement *_element                     = nullptr;
        SDK::IUIElementEventListener *_commonEventListener = nullptr; // GUI-owned; identity for removal only.
        std::shared_ptr<SDK::IFlashPlayer> _movie;
        // Declared before the pages so it also outlives their destruction.
        std::unordered_map<std::uintptr_t, Callback> _callbacks;
        std::uintptr_t _nextCallbackId = 1;
        std::unordered_map<std::string, std::shared_ptr<KCDC::Core::NativeUI>> _pages;
        std::weak_ptr<KCDC::Core::NativeUI> _inputPage;
        bool _ready                = false;
        bool _closingPages         = false;
        bool _creatingPage         = false;
        bool _selectingInputPage   = false;
        bool _inputDeliveryEnabled = false;
        unsigned int _generation   = 0;
        std::string _status        = "Not opened";
    };
} // namespace KCDC::Core
