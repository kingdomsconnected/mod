#include "native_ui_host.h"

#include "sdk/cryengine/scaleform/i_flash_ui.h"
#include "sdk/wh/c_wh_global_environment.h"
#include "sdk/wh/guimodule/c_gui_module.h"

#include <algorithm>
#include <cstring>
#include <exception>
#include <logging/logger.h>

namespace KCDC::Core {
    namespace {
        /** Stored-type tags of a native UI argument record, from 0x1803C22E8. */
        constexpr int kStoredInt    = 0;
        constexpr int kStoredFloat  = 1;
        constexpr int kStoredUInt   = 2;
        constexpr int kStoredString = 4;
        constexpr int kStoredWide   = 5;
        constexpr int kStoredBool   = 6;

        /** The game's own converters hand back a CryString, which must not cross the module boundary. */
        std::string Narrow(const wchar_t *text) {
            std::string out;
            if (text == nullptr) {
                return out;
            }
            for (const wchar_t *cursor = text; *cursor != L'\0'; ++cursor) {
                std::uint32_t code = static_cast<std::uint16_t>(*cursor);
                if (code >= 0xD800 && code <= 0xDBFF) {
                    const std::uint32_t low = static_cast<std::uint16_t>(cursor[1]);
                    if (low >= 0xDC00 && low <= 0xDFFF) {
                        code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
                        ++cursor;
                    }
                    else {
                        code = 0xFFFD;
                    }
                }
                else if (code >= 0xDC00 && code <= 0xDFFF) {
                    code = 0xFFFD;
                }
                if (code < 0x80) {
                    out.push_back(static_cast<char>(code));
                }
                else if (code < 0x800) {
                    out.push_back(static_cast<char>(0xC0 | (code >> 6)));
                    out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
                }
                else if (code < 0x10000) {
                    out.push_back(static_cast<char>(0xE0 | (code >> 12)));
                    out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
                    out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
                }
                else {
                    out.push_back(static_cast<char>(0xF0 | (code >> 18)));
                    out.push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3F)));
                    out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
                    out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
                }
            }
            return out;
        }

        std::vector<NativeUIValue> ConvertRecords(const SDK::SUIArguments &arguments) {
            std::vector<NativeUIValue> values;
            const int count = arguments.GetArgCount();
            values.reserve(count > 0 ? static_cast<std::size_t>(count) : 0);
            for (int index = 0; index < count; ++index) {
                const SDK::SUIArguments::Argument *argument = arguments.GetArgument(index);
                if (argument == nullptr) {
                    values.emplace_back();
                    continue;
                }
                switch (argument->GetStoredType()) {
                case kStoredInt: {
                    int value = 0;
                    values.emplace_back(argument->ReadInt(value) ? NativeUIValue {static_cast<double>(value)} : NativeUIValue {});
                    break;
                }
                case kStoredFloat: {
                    float value = 0.0f;
                    values.emplace_back(argument->ReadFloat(value) ? NativeUIValue {static_cast<double>(value)} : NativeUIValue {});
                    break;
                }
                case kStoredUInt: {
                    unsigned int value = 0;
                    values.emplace_back(argument->ReadUInt(value) ? NativeUIValue {static_cast<double>(value)} : NativeUIValue {});
                    break;
                }
                case kStoredString: {
                    const char *value = nullptr;
                    values.emplace_back(argument->ReadString(value) && value != nullptr ? NativeUIValue {std::string(value)} : NativeUIValue {});
                    break;
                }
                case kStoredWide: {
                    const wchar_t *value = nullptr;
                    values.emplace_back(argument->ReadWideString(value) ? NativeUIValue {Narrow(value)} : NativeUIValue {});
                    break;
                }
                case kStoredBool: {
                    bool value = false;
                    values.emplace_back(argument->ReadBool(value) ? NativeUIValue {value} : NativeUIValue {});
                    break;
                }
                default: values.emplace_back(); break;
                }
            }
            return values;
        }

        constexpr char kReadyEvent[] = "NativeUIReady";

        /** Which native tag an ActionScript number arrives as is not the movie's choice. */
        bool EqualsToken(const SDK::SFlashVarValue &value, int token) {
            switch (value.type) {
            case SDK::EFlashVariableType::Int: return value.i == token;
            case SDK::EFlashVariableType::UInt: return token >= 0 && value.u == static_cast<unsigned int>(token);
            case SDK::EFlashVariableType::Double: return value.d == static_cast<double>(token);
            case SDK::EFlashVariableType::Float: return static_cast<double>(value.f) == static_cast<double>(token);
            default: return false;
            }
        }
    } // namespace

    NativeUIHost::NativeUIHost(NativeUIEventQueue &events, std::string elementName, Kind kind): _events(events), _elementName(std::move(elementName)), _kind(kind) {}

    NativeUIHost::~NativeUIHost() {
        Close();
        // Anything still holding a page keeps a valid object whose host is gone.
        for (const auto &page : _pages) {
            page.second->Detach();
        }
        _pages.clear();
    }

    bool NativeUIHost::Open(const std::string &descriptorPath) {
        if (_element != nullptr) {
            return true;
        }
        SDK::IFlashUI *ui = SDK::IFlashUI::GetInstance();
        if (ui == nullptr) {
            _status = "Native UI is not initialized";
            return false;
        }
        SDK::IUIElementEventListener *commonEventListener = nullptr;
        if (_kind == Kind::Composition) {
            SDK::wh::C_GlobalEnvironment *environment = SDK::wh::C_GlobalEnvironment::GetInstance();
            SDK::wh::guimodule::C_GUIModule *gui      = environment != nullptr ? environment->GetGUIModule() : nullptr;
            commonEventListener                       = gui != nullptr ? gui->GetCommonEventHandler() : nullptr;
            if (commonEventListener == nullptr) {
                _status = "Native common UI services are not initialized";
                return false;
            }
        }
        SDK::IUIElement *element = ui->GetUIElementByInstanceStr(_elementName.c_str());
        if (element == nullptr) {
            if (!ui->LoadElementsFromFile(descriptorPath.c_str())) {
                _status = "Native UI XML load failed: " + descriptorPath;
                return false;
            }
            element = ui->GetUIElementByInstanceStr(_elementName.c_str());
        }
        if (element == nullptr) {
            _status = "Native UI element was not registered: " + _elementName;
            return false;
        }
        element->AddRef();
        _element             = static_cast<SDK::CFlashUIElement *>(element);
        _commonEventListener = commonEventListener;
        _element->AddEventListener(this, "KCDCNativeUI");
        if (_commonEventListener != nullptr) {
            _element->AddEventListener(_commonEventListener, "CommonEventHandler");
        }
        if (!_element->Init(true)) {
            Close();
            _status = "Native UI movie initialization failed";
            return false;
        }
        _element->SetVisible(true);
        // Init raises no notification for a movie that was already loaded.
        AcquireMovie();
        return true;
    }

    void NativeUIHost::Close() {
        ClosePages();
        if (_element != nullptr) {
            _element->Unload(false);
            _element->RemoveEventListener(this);
            if (_commonEventListener != nullptr) {
                _element->RemoveEventListener(_commonEventListener);
            }
            _commonEventListener = nullptr;
            ReleaseMovie();
            _element->Release();
            _element = nullptr;
        }
        _status = "Closed";
    }

    bool NativeUIHost::Reload() {
        if (_element == nullptr) {
            _status = "Host is not open";
            return false;
        }
        ClosePages();
        ReleaseMovie();
        _element->Reload(false);
        AcquireMovie();
        return IsReady();
    }

    void NativeUIHost::AcquireMovie() {
        if (_element == nullptr || !_element->IsInit()) {
            return;
        }
        std::shared_ptr<SDK::IFlashPlayer> movie = _element->GetFlashPlayer();
        if (!movie) {
            _status = "Native UI movie is unloaded";
            return;
        }
        _movie = std::move(movie);
        if (_kind == Kind::Composition) {
            SDK::SFlashVarValue version {};
            const bool matched = _movie->GetVariable("_root.kcdcNativeCompositionVersion", version) && EqualsToken(version, 1);
            if (!matched) {
                // The bridge declares its version on the first frame it runs; its readiness event brings us back here.
                _status = "Waiting for the native composition bridge";
                return;
            }
            _status = "Native composition host ready";
        }
        else {
            _status = "Native movie ready";
        }
        QueueReady();
    }

    void NativeUIHost::QueueReady() {
        if (_ready) {
            return;
        }
        _ready = true;
        NativeUIEvent event;
        event.kind = NativeUIEventKind::Ready;
        event.host = _elementName;
        _events.Queue(std::move(event));
    }

    void NativeUIHost::ReleaseMovie() {
        _ready = false;
        _movie.reset();
    }

    bool NativeUIHost::GetStageSize(int &width, int &height) const {
        width  = 0;
        height = 0;
        if (!_movie) {
            return false;
        }
        width  = _movie->GetWidth();
        height = _movie->GetHeight();
        return width > 0 && height > 0;
    }

    float NativeUIHost::GetSquareUnitCorrection() const {
        if (!_movie || _movie->GetViewScaleMode() != SDK::IFlashPlayer::eSM_ExactFit) {
            return 1.0F;
        }

        int x             = 0;
        int y             = 0;
        int width         = 0;
        int height        = 0;
        float aspectRatio = 1.0F;
        _movie->GetViewport(x, y, width, height, aspectRatio);
        const int stageWidth  = _movie->GetWidth();
        const int stageHeight = _movie->GetHeight();
        if (width <= 0 || height <= 0 || stageWidth <= 0 || stageHeight <= 0) {
            return 1.0F;
        }

        const float correction = (static_cast<float>(height) * static_cast<float>(stageWidth)) / (static_cast<float>(width) * static_cast<float>(stageHeight));
        return std::clamp(correction, 0.5F, 2.0F);
    }

    bool NativeUIHost::ScreenToStage(float screenX, float screenY, float &stageX, float &stageY) const {
        stageX = 0.0f;
        stageY = 0.0f;
        if (_element == nullptr || !_movie) {
            return false;
        }
        _element->ScreenToFlash(screenX, screenY, stageX, stageY, false);
        return true;
    }

    std::shared_ptr<KCDC::Core::NativeUI> NativeUIHost::CreatePage(const std::string &name, const char *parentPath) {
        if (_closingPages || _creatingPage || !IsReady() || name.empty() || _pages.find(name) != _pages.end()) {
            return {};
        }
        std::shared_ptr<KCDC::Core::NativeUI> page = std::make_shared<KCDC::Core::NativeUI>(*this, _movie);
        // Registered before mounting, so an unload during construction finds it.
        _pages.emplace(name, page);
        _creatingPage      = true;
        const bool mounted = page->Mount(parentPath, name);
        _creatingPage      = false;
        if (!mounted || !page->AcceptsCallback(_movie.get())) {
            const auto found = _pages.find(name);
            if (found != _pages.end() && found->second == page) {
                _pages.erase(found);
            }
            return {};
        }
        return page;
    }

    std::shared_ptr<KCDC::Core::NativeUI> NativeUIHost::FindPage(const std::string &name) const {
        const auto found = _pages.find(name);
        return found != _pages.end() ? found->second : std::shared_ptr<KCDC::Core::NativeUI> {};
    }

    std::string NativeUIHost::FindPageName(const KCDC::Core::NativeUI *page) const {
        for (const auto &entry : _pages) {
            if (entry.second.get() == page) {
                return entry.first;
            }
        }
        return {};
    }

    bool NativeUIHost::ClosePages() {
        if (_closingPages) {
            return false;
        }
        _closingPages = true;
        ClearInputPage();
        bool success = true;
        for (const auto &page : _pages) {
            success = page.second->Unmount() && success;
        }
        _pages.clear();
        _closingPages = false;
        if (!success) {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->warn("Native UI page teardown reported an ActionScript call failure on host {}", _elementName);
        }
        return success;
    }

    bool NativeUIHost::ClosePage(const std::string &name) {
        if (_closingPages) {
            return false;
        }
        const auto found = _pages.find(name);
        if (found == _pages.end()) {
            return false;
        }
        const std::shared_ptr<KCDC::Core::NativeUI> page = found->second;
        _closingPages                                    = true;
        if (_inputPage.lock() == page) {
            ClearInputPage();
        }
        const bool success = page->Unmount();
        _pages.erase(name);
        if (!_movie) {
            _pages.clear();
        }
        _closingPages = false;
        return success;
    }

    bool NativeUIHost::SelectInputPage(const std::string &name) {
        if (_closingPages || _selectingInputPage || _kind != Kind::Composition || !_movie) {
            return false;
        }
        const auto found = _pages.find(name);
        if (found == _pages.end() || !found->second->AcceptsCallback(_movie.get())) {
            return false;
        }
        const std::shared_ptr<KCDC::Core::NativeUI> page = found->second;
        const std::shared_ptr<SDK::IFlashPlayer> movie   = _movie;
        SDK::FlashVariableObjectRef root;
        SDK::FlashVariableObjectRef target(page->Root()->Clone());
        if (!movie->GetVariableObject("_root", root.Receive()) || !root || !target) {
            return false;
        }
        const SDK::IFlashVariableObject *arguments[] = {target.Get()};
        _selectingInputPage                          = true;
        const bool selected                          = root->InvokeObjects("KCDC_SetInputPage", arguments, 1, nullptr);
        _selectingInputPage                          = false;
        if (!selected || _movie != movie || !page->AcceptsCallback(movie.get())) {
            return false;
        }
        _inputPage = page;
        return true;
    }

    void NativeUIHost::ClearInputPage() {
        if (_movie && _kind == Kind::Composition && !_inputPage.expired()) {
            const std::shared_ptr<SDK::IFlashPlayer> movie = _movie;
            const SDK::SFlashVarValue noPage {SDK::EFlashVariableType::Null, 0, {}};
            movie->Invoke("_root.KCDC_SetInputPage", &noPage, 1, nullptr);
        }
        _inputPage.reset();
    }

    void NativeUIHost::SetInputDelivery(bool enabled) {
        if (_inputDeliveryEnabled == enabled) {
            return;
        }
        Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("Native UI input delivery {} on host {}", enabled ? "enabled" : "disabled", _elementName);
        const bool cancel     = _inputDeliveryEnabled && !enabled;
        _inputDeliveryEnabled = enabled;
        if (cancel && _movie && _kind == Kind::Composition && _movie->IsAvailable("_root.KCDC_CancelInteraction")) {
            const std::shared_ptr<SDK::IFlashPlayer> movie = _movie;
            movie->Invoke("_root.KCDC_CancelInteraction", nullptr, 0, nullptr);
        }
        if (_element == nullptr) {
            return;
        }
        _element->SetFlag(SDK::IUIElement::eFUI_MOUSEEVENTS, enabled);
        _element->SetFlag(SDK::IUIElement::eFUI_KEYEVENTS, enabled);
        _element->SetFlag(SDK::IUIElement::eFUI_EVENTS_EXCLUSIVE, enabled);
        _element->SetFlag(SDK::IUIElement::eFUI_HARDWARECURSOR, enabled);
        // SetFlag (0x18056A7A8, from slot 30) reconciles the element's cursor
        // reference in both directions, so only the raise needs this. Both
        // paths gate the cursor on wh_ui_InputDevice reading 1: no pointer on
        // a gamepad.
        if (enabled) {
            _element->UpdateFlags();
        }
    }

    bool NativeUIHost::DispatchAction(const char *action, int activationMode) {
        if (action == nullptr || !_movie || _kind != Kind::Composition) {
            return false;
        }
        const SDK::SFlashVarValue arguments[]          = {SDK::SFlashVarValue::String(action), SDK::SFlashVarValue::Int(activationMode)};
        const std::shared_ptr<SDK::IFlashPlayer> movie = _movie;
        return movie->Invoke("_root.fc_setInput", arguments, 2, nullptr);
    }

    void NativeUIHost::SetVisible(bool visible) {
        if (_element != nullptr) {
            _element->SetVisible(visible);
        }
    }

    bool NativeUIHost::IsVisible() const {
        return _element != nullptr && _element->IsVisible();
    }

    void NativeUIHost::SetLayer(int layer) {
        if (_element != nullptr) {
            _element->SetLayer(layer);
        }
    }

    void NativeUIHost::OnInit(SDK::IUIElement *sender, SDK::IFlashPlayer *) {
        if (sender != _element) {
            return;
        }
        ++_generation;
        Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("Native UI movie {} initialized, generation {}", _elementName, _generation);
        AcquireMovie();
    }

    void NativeUIHost::OnUnload(SDK::IUIElement *sender) {
        if (sender != _element) {
            return;
        }
        Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("Native UI movie {} unloaded, generation {}", _elementName, _generation);
        for (const auto &page : _pages) {
            page.second->Invalidate();
        }
        _inputPage.reset();
        if (!_closingPages) {
            _pages.clear();
        }
        ReleaseMovie();
        NativeUIEvent event;
        event.kind = NativeUIEventKind::Unload;
        event.host = _elementName;
        _events.Queue(std::move(event));
    }

    void NativeUIHost::OnUIEvent(SDK::IUIElement *sender, const SDK::SUIEventDesc &event, const SDK::SUIArguments &arguments, SDK::SFlashVarValue *) {
        if (sender != _element) {
            return;
        }
        // Scripts see the display name, which is what an element's own documentation calls it.
        const char *name        = event.GetName();
        const char *displayName = event.GetDisplayName();
        if (name == nullptr && displayName == nullptr) {
            return;
        }
        if ((name != nullptr && std::strcmp(name, kReadyEvent) == 0) || (displayName != nullptr && std::strcmp(displayName, kReadyEvent) == 0)) {
            AcquireMovie();
        }
        NativeUIEvent queued;
        queued.kind      = NativeUIEventKind::Element;
        queued.host      = _elementName;
        queued.name      = displayName != nullptr ? displayName : name;
        queued.arguments = ConvertRecords(arguments);
        _events.Queue(std::move(queued));
    }

    void NativeUIHost::OnUIEventEx(SDK::IUIElement *sender, const char *command, const SDK::SUIArguments &, void *) {
        if (sender == _element) {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("Native UI undeclared callback {} on host {}", command != nullptr ? command : "", _elementName);
        }
    }

    std::uintptr_t NativeUIHost::RegisterCallback(KCDC::Core::NativeUI *owner, std::function<void(const SDK::IActionScriptFunction::Params &, SDK::IActionScriptFunction::IReturnValue *)> callback) {
        if (_closingPages || _nextCallbackId == 0) {
            return 0;
        }
        for (const auto &page : _pages) {
            if (page.second.get() == owner) {
                const std::uintptr_t id = _nextCallbackId++;
                _callbacks.emplace(id, Callback {page.second, std::move(callback)});
                return id;
            }
        }
        return 0;
    }

    void NativeUIHost::RemoveCallback(std::uintptr_t id) {
        _callbacks.erase(id);
    }

    std::vector<NativeUIValue> NativeUIHost::ConvertEventArguments(const SDK::SUIArguments &arguments) {
        return ConvertRecords(arguments);
    }

    std::vector<NativeUIValue> NativeUIHost::ConvertArguments(const SDK::IActionScriptFunction::Params &params) {
        std::vector<NativeUIValue> values;
        if (params.pArgs == nullptr) {
            return values;
        }
        values.reserve(params.numArgs);
        for (unsigned int index = 0; index < params.numArgs; ++index) {
            const SDK::IFlashVariableObject *argument = params.pArgs[index];
            if (argument == nullptr) {
                values.emplace_back();
                continue;
            }
            const SDK::SFlashVarValue value = argument->ToVarValue();
            switch (value.type) {
            case SDK::EFlashVariableType::Bool: values.emplace_back(value.b); break;
            case SDK::EFlashVariableType::Int: values.emplace_back(static_cast<double>(value.i)); break;
            case SDK::EFlashVariableType::UInt: values.emplace_back(static_cast<double>(value.u)); break;
            case SDK::EFlashVariableType::Double: values.emplace_back(value.d); break;
            case SDK::EFlashVariableType::Float: values.emplace_back(static_cast<double>(value.f)); break;
            case SDK::EFlashVariableType::ConstStrPtr: values.emplace_back(value.str != nullptr ? std::string(value.str) : std::string()); break;
            case SDK::EFlashVariableType::ConstWstrPtr: values.emplace_back(Narrow(value.wstr)); break;
            default: values.emplace_back(); break;
            }
        }
        return values;
    }

    void NativeUIHost::Call(const SDK::IActionScriptFunction::Params &params, SDK::IActionScriptFunction::IReturnValue *returnValue) {
        const auto found = _callbacks.find(reinterpret_cast<std::uintptr_t>(params.pUserData));
        if (found == _callbacks.end()) {
            return;
        }
        const std::shared_ptr<KCDC::Core::NativeUI> owner = found->second.owner.lock();
        if (!owner || !owner->AcceptsCallback(params.pFromPlayer)) {
            return;
        }
        // A callback may close its page and erase this registry entry. Keep
        // the callable and page alive independently until dispatch returns.
        const std::function<void(const SDK::IActionScriptFunction::Params &, SDK::IActionScriptFunction::IReturnValue *)> callback = found->second.invoke;
        try {
            callback(params, returnValue);
        }
        catch (const std::exception &error) {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->error("Native UI callback failed: {}", error.what());
        }
        catch (...) {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->error("Native UI callback failed with an unknown exception");
        }
    }
} // namespace KCDC::Core
