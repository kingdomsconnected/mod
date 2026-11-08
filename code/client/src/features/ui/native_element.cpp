#include "native_element.h"

#include "core/ui/native_ui/native_ui_module.h"

#include "sdk/cryengine/scaleform/i_flash_ui.h"

#include <logging/logger.h>

namespace KCDC::Features::UI {
    namespace {
        constexpr int kMaxDescriptorDepth = 8;
        constexpr unsigned int kMaxArray  = 4096;

        bool CopyValue(const SDK::SFlashVarValue &value, std::string &text) {
            if (value.type != SDK::EFlashVariableType::ConstStrPtr) {
                return true;
            }
            if (value.str == nullptr) {
                return false;
            }
            text.assign(value.str);
            return true;
        }

        KCDC::Core::NativeUIValue Convert(const SDK::SFlashVarValue &value) {
            switch (value.type) {
            case SDK::EFlashVariableType::Bool: return value.b;
            case SDK::EFlashVariableType::Int: return static_cast<double>(value.i);
            case SDK::EFlashVariableType::UInt: return static_cast<double>(value.u);
            case SDK::EFlashVariableType::Double: return value.d;
            case SDK::EFlashVariableType::Float: return static_cast<double>(value.f);
            case SDK::EFlashVariableType::ConstStrPtr: return value.str != nullptr ? std::string(value.str) : std::string();
            default: return {};
            }
        }
    } // namespace

    std::unordered_map<std::string, std::unique_ptr<NativeElement>> NativeElement::_handles;

    NativeElement::NativeElement(std::string key, std::string name, int instanceId, SDK::CFlashUIElement *element, bool owned): _key(std::move(key)), _name(std::move(name)), _instanceId(instanceId), _element(element), _owned(owned) {}

    NativeElement::~NativeElement() {
        if (_element == nullptr) {
            return;
        }
        if (_listening) {
            _element->RemoveEventListener(this);
        }
        if (_owned) {
            _element->Release();
        }
        _element = nullptr;
    }

    std::string NativeElement::MakeKey(const std::string &name, int instanceId) {
        return name + "@" + std::to_string(instanceId);
    }

    NativeElement *NativeElement::Find(const std::string &key) {
        const auto found = _handles.find(key);
        return found != _handles.end() ? found->second.get() : nullptr;
    }

    NativeElement *NativeElement::Acquire(const std::string &name, int instanceId, std::string &error) {
        if (name.empty() || name.size() > 128 || instanceId < 0) {
            error = "a native element takes a name and a non-negative instance id";
            return nullptr;
        }
        const std::string key = MakeKey(name, instanceId);
        if (NativeElement *existing = Find(key); existing != nullptr) {
            return existing;
        }
        SDK::IFlashUI *ui = SDK::IFlashUI::GetInstance();
        if (ui == nullptr) {
            error = "native UI is not initialized";
            return nullptr;
        }
        SDK::IUIElement *base = ui->GetUIElementByInstanceStr(name.c_str());
        if (base == nullptr) {
            error = "no native element is registered as " + name;
            return nullptr;
        }
        SDK::IUIElement *element = base;
        bool owned               = false;
        if (instanceId != 0) {
            // Slot 4 hands back an owning reference, creating the instance
            // from the same XML when it does not exist yet.
            SDK::IUIElement *instance = nullptr;
            base->GetInstance(&instance, static_cast<unsigned int>(instanceId));
            if (instance == nullptr) {
                error = "native element " + name + " has no instance " + std::to_string(instanceId);
                return nullptr;
            }
            element = instance;
            owned   = true;
        }
        auto handle        = std::unique_ptr<NativeElement>(new NativeElement(key, name, instanceId, static_cast<SDK::CFlashUIElement *>(element), owned));
        NativeElement *raw = handle.get();
        _handles.emplace(key, std::move(handle));
        return raw;
    }

    void NativeElement::ReleaseAll() {
        _handles.clear();
    }

    SDK::IFlashPlayer *NativeElement::Movie() const {
        // The loaded movie, not the lazy accessor: asking for the player
        // initializes the element, and a read must not raise a game screen.
        return _element != nullptr ? _element->GetLoadedFlashPlayer() : nullptr;
    }

    bool NativeElement::BuildPath(const SDK::SUIParameterDesc *descriptor, std::string &path) {
        path.clear();
        if (descriptor == nullptr) {
            return false;
        }
        std::vector<const char *> names;
        for (const SDK::SUIParameterDesc *cursor = descriptor; cursor != nullptr && names.size() < kMaxDescriptorDepth; cursor = cursor->GetParent()) {
            const char *name = cursor->GetName();
            if (name == nullptr || name[0] == '\0') {
                return false;
            }
            names.push_back(name);
        }
        if (names.empty() || names.size() >= kMaxDescriptorDepth) {
            return false;
        }
        // The root descriptor is the last link and is spelled `_root`; one that lacks it still resolves there.
        if (std::string(names.back()) != "_root") {
            path = "_root";
        }
        for (auto name = names.rbegin(); name != names.rend(); ++name) {
            if (!path.empty()) {
                path.push_back('.');
            }
            path += *name;
        }
        return true;
    }

    bool NativeElement::Call(const std::string &function, const std::vector<SDK::SFlashVarValue> &arguments, SDK::SFlashVarValue &result, std::string &resultText) const {
        result = {};
        resultText.clear();
        SDK::IFlashPlayer *movie = Movie();
        if (_element == nullptr || movie == nullptr) {
            return false;
        }
        std::string path;
        if (!BuildPath(_element->GetFunctionDesc(function.c_str()), path)) {
            return false;
        }
        SDK::SFlashVarValue value {};
        if (!movie->Invoke(path.c_str(), arguments.empty() ? nullptr : arguments.data(), static_cast<unsigned int>(arguments.size()), &value)) {
            return false;
        }
        if (!CopyValue(value, resultText)) {
            return false;
        }
        if (value.type == SDK::EFlashVariableType::ConstStrPtr) {
            value.str = resultText.c_str();
        }
        result = value;
        return true;
    }

    bool NativeElement::SetVariable(const std::string &name, const SDK::SFlashVarValue &value) const {
        SDK::IFlashPlayer *movie = Movie();
        if (_element == nullptr || movie == nullptr) {
            return false;
        }
        std::string path;
        return BuildPath(_element->GetVariableDesc(name.c_str()), path) && movie->SetVariable(path.c_str(), value);
    }

    bool NativeElement::GetVariable(const std::string &name, SDK::SFlashVarValue &value, std::string &text) const {
        value = {};
        text.clear();
        SDK::IFlashPlayer *movie = Movie();
        if (_element == nullptr || movie == nullptr) {
            return false;
        }
        std::string path;
        SDK::SFlashVarValue current {};
        if (!BuildPath(_element->GetVariableDesc(name.c_str()), path) || !movie->GetVariable(path.c_str(), current) || !CopyValue(current, text)) {
            return false;
        }
        if (current.type == SDK::EFlashVariableType::ConstStrPtr) {
            current.str = text.c_str();
        }
        value = current;
        return true;
    }

    bool NativeElement::SetArray(const std::string &name, const std::vector<SDK::SFlashVarValue> &values) const {
        SDK::IFlashPlayer *movie = Movie();
        if (_element == nullptr || movie == nullptr || values.size() > kMaxArray) {
            return false;
        }
        std::string path;
        if (!BuildPath(_element->GetArrayDesc(name.c_str()), path)) {
            return false;
        }
        // A fresh array, as the engine's own setter does: the name may hold nothing yet.
        SDK::FlashVariableObjectRef array;
        if (!movie->CreateArray(array.Receive()) || !array) {
            return false;
        }
        for (unsigned int index = 0; index < values.size(); ++index) {
            if (!array->SetElement(index, values[index])) {
                return false;
            }
        }
        return movie->SetVariableObject(path.c_str(), array.Get());
    }

    bool NativeElement::SetArrayInPlace(const std::string &name, const std::vector<SDK::SFlashVarValue> &values) const {
        SDK::IFlashPlayer *movie = Movie();
        if (_element == nullptr || movie == nullptr || values.size() > kMaxArray) {
            return false;
        }
        std::string path;
        SDK::FlashVariableObjectRef array;
        if (!BuildPath(_element->GetArrayDesc(name.c_str()), path) || !movie->GetVariableObject(path.c_str(), array.Receive()) || !array || !array->IsArray()) {
            return false;
        }
        if (!array->SetArraySize(static_cast<unsigned int>(values.size()))) {
            return false;
        }
        for (unsigned int index = 0; index < values.size(); ++index) {
            if (!array->SetElement(index, values[index])) {
                return false;
            }
        }
        return true;
    }

    bool NativeElement::GetArray(const std::string &name, std::vector<KCDC::Core::NativeUIValue> &values) const {
        values.clear();
        SDK::IFlashPlayer *movie = Movie();
        if (_element == nullptr || movie == nullptr) {
            return false;
        }
        std::string path;
        SDK::FlashVariableObjectRef array;
        if (!BuildPath(_element->GetArrayDesc(name.c_str()), path) || !movie->GetVariableObject(path.c_str(), array.Receive()) || !array || !array->IsArray()) {
            return false;
        }
        const unsigned int size = array->GetArraySize();
        if (size > kMaxArray) {
            return false;
        }
        values.reserve(size);
        for (unsigned int index = 0; index < size; ++index) {
            SDK::SFlashVarValue value {};
            values.emplace_back(array->GetElement(index, value) ? Convert(value) : KCDC::Core::NativeUIValue {});
        }
        return true;
    }

    bool NativeElement::ResolveMovieClip(const std::string &name, SDK::FlashVariableObjectRef &clip) const {
        return _element != nullptr && _element->GetLoadedMovieClip(name.c_str(), clip);
    }

    bool NativeElement::SetMovieClipDisplay(const std::string &name, const SDK::SFlashDisplayInfo &info) const {
        SDK::FlashVariableObjectRef clip;
        return ResolveMovieClip(name, clip) && clip->SetDisplayInfo(info);
    }

    bool NativeElement::GetMovieClipDisplay(const std::string &name, SDK::SFlashDisplayInfo &info) const {
        info = {};
        SDK::FlashVariableObjectRef clip;
        SDK::SFlashDisplayInfo current;
        if (!ResolveMovieClip(name, clip) || !clip->GetDisplayInfo(current)) {
            return false;
        }
        info = current;
        return true;
    }

    bool NativeElement::GotoMovieClipFrame(const std::string &name, unsigned int frame, const char *label, bool play) const {
        SDK::FlashVariableObjectRef clip;
        if (!ResolveMovieClip(name, clip)) {
            return false;
        }
        if (label != nullptr) {
            return play ? clip->GotoAndPlayLabel(label) : clip->GotoAndStopLabel(label);
        }
        return frame != 0 && (play ? clip->GotoAndPlayFrame(frame) : clip->GotoAndStopFrame(frame));
    }

    bool NativeElement::Show() const {
        if (_element == nullptr) {
            return false;
        }
        _element->SetVisible(true);
        return true;
    }

    bool NativeElement::Hide() const {
        if (_element == nullptr) {
            return false;
        }
        _element->SetVisible(false);
        return true;
    }

    bool NativeElement::RequestHide() const {
        if (_element == nullptr) {
            return false;
        }
        _element->RequestHide();
        return true;
    }

    bool NativeElement::IsVisible() const {
        return _element != nullptr && _element->IsVisible();
    }

    bool NativeElement::Reload() const {
        if (_element == nullptr) {
            return false;
        }
        _element->Reload(false);
        return true;
    }

    bool NativeElement::Unload() const {
        if (_element == nullptr) {
            return false;
        }
        _element->Unload(false);
        return true;
    }

    bool NativeElement::Listen() {
        if (_element == nullptr) {
            return false;
        }
        if (!_listening) {
            _element->AddEventListener(this, "KCDCNativeElement");
            _listening = true;
        }
        return true;
    }

    void NativeElement::OnUIEvent(SDK::IUIElement *sender, const SDK::SUIEventDesc &event, const SDK::SUIArguments &arguments, SDK::SFlashVarValue *) {
        if (sender != _element) {
            return;
        }
        const char *displayName = event.GetDisplayName();
        const char *name        = event.GetName();
        if (displayName == nullptr && name == nullptr) {
            return;
        }
        KCDC::Core::NativeUIEvent queued;
        queued.kind      = KCDC::Core::NativeUIEventKind::Element;
        queued.host      = _key;
        queued.name      = displayName != nullptr ? displayName : name;
        queued.arguments = KCDC::Core::NativeUIHost::ConvertEventArguments(arguments);
        KCDC::Core::NativeUIModule::Get().Queue(std::move(queued));
    }

    void NativeElement::OnUnload(SDK::IUIElement *sender) {
        if (sender != _element) {
            return;
        }
        Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->debug("Native element {} unloaded", _key);
    }
} // namespace KCDC::Features::UI
