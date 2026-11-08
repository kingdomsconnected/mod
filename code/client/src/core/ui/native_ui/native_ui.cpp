#include "native_ui.h"
#include "native_ui_host.h"

#include <cmath>

namespace KCDC::Core {
    NativeUI::~NativeUI() {
        Unmount();
    }

    bool NativeUI::Mount(const char *parentPath, const std::string &instanceName) {
        if (!_movie || _root || _closing || parentPath == nullptr || instanceName.empty()) {
            return false;
        }
        const std::shared_ptr<SDK::IFlashPlayer> movie = _movie;
        SDK::FlashVariableObjectRef parent;
        if (!movie->GetVariableObject(parentPath, parent.Receive()) || !parent || !parent->IsDisplayObject() || parent->HasMember(instanceName.c_str()) || _closing || _movie != movie) {
            return false;
        }
        SDK::FlashVariableObjectRef root;
        if (!parent->CreateEmptyMovieClip(root.Receive(), instanceName.c_str()) || !root) {
            return false;
        }
        if (_closing || _movie != movie) {
            // Construction can re-enter page close. Remove the clip only if
            // its movie is still loaded.
            if (_host != nullptr && _host->IsCurrentMovie(movie.get())) {
                root->Invoke("removeMovieClip", nullptr, 0, nullptr);
            }
            return false;
        }
        _parent       = parent.Clone();
        _root         = root.Clone();
        _instanceName = instanceName;
        if (!_parent || !_root || !root->SetMember("kcdcNativePage", SDK::SFlashVarValue::Bool(true)) || !AcceptsCallback(movie.get()) || !parent->SetMemberObject(instanceName.c_str(), root.Get()) || !AcceptsCallback(movie.get())) {
            Unmount();
            return false;
        }
        return true;
    }

    bool NativeUI::CanCreate(const std::string &key) const {
        return _movie && _root && !_closing && !key.empty() && key != "$root" && _objects.find(key) == _objects.end();
    }

    SDK::IFlashVariableObject *NativeUI::Find(const std::string &key) const {
        if (_closing) {
            return nullptr;
        }
        if (key == "$root") {
            return _root.Get();
        }
        const auto found = _objects.find(key);
        return found != _objects.end() ? found->second.Get() : nullptr;
    }

    bool NativeUI::CreateClip(const std::string &key, const std::string &parentKey, const char *instanceName, const char *exportName, const std::string &initKey) {
        SDK::IFlashVariableObject *parent = Find(parentKey);
        if (!CanCreate(key) || parent == nullptr || !parent->IsDisplayObject() || instanceName == nullptr || instanceName[0] == '\0') {
            return false;
        }
        const std::shared_ptr<SDK::IFlashPlayer> movie = _movie;
        SDK::FlashVariableObjectRef parentOwner(parent->Clone());
        if (!parentOwner || parentOwner->HasMember(instanceName) || !CanCreate(key)) {
            return false;
        }
        SDK::FlashVariableObjectRef initOwner;
        if (!initKey.empty()) {
            SDK::IFlashVariableObject *init = Find(initKey);
            if (exportName == nullptr || init == nullptr || !init->IsObject()) {
                return false;
            }
            initOwner.Reset(init->Clone());
            if (!initOwner) {
                return false;
            }
        }
        SDK::FlashVariableObjectRef clip;
        const bool created = exportName != nullptr ? parentOwner->AttachMovie(clip.Receive(), exportName, instanceName, -1, initOwner.Get()) : parentOwner->CreateEmptyMovieClip(clip.Receive(), instanceName);
        if (!created || !clip || !CanCreate(key)) {
            return false;
        }
        if (!parentOwner->SetMemberObject(instanceName, clip.Get())) {
            clip->Invoke("removeMovieClip", nullptr, 0, nullptr);
            return false;
        }
        if (!CanCreate(key)) {
            return false;
        }
        _objects.emplace(key, std::move(clip));
        _children.emplace(key, Child {parentKey, instanceName, false});
        return true;
    }

    bool NativeUI::CreateTextField(const std::string &key, const std::string &parentKey, const char *instanceName, double x, double y, double width, double height) {
        SDK::IFlashVariableObject *parent = Find(parentKey);
        if (!CanCreate(key) || parent == nullptr || !parent->IsDisplayObject() || instanceName == nullptr || instanceName[0] == '\0') {
            return false;
        }
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(width) || !std::isfinite(height) || width <= 0.0 || height <= 0.0) {
            return false;
        }
        const std::shared_ptr<SDK::IFlashPlayer> movie = _movie;
        SDK::FlashVariableObjectRef parentOwner(parent->Clone());
        if (!parentOwner || parentOwner->HasMember(instanceName) || !CanCreate(key)) {
            return false;
        }
        SDK::SFlashVarValue depth {};
        if (!parentOwner->Invoke("getNextHighestDepth", nullptr, 0, &depth) || !CanCreate(key)) {
            return false;
        }
        const SDK::SFlashVarValue arguments[] = {SDK::SFlashVarValue::String(instanceName), depth, SDK::SFlashVarValue::Double(x), SDK::SFlashVarValue::Double(y), SDK::SFlashVarValue::Double(width), SDK::SFlashVarValue::Double(height)};
        if (!parentOwner->Invoke("createTextField", arguments, 6, nullptr) || !CanCreate(key)) {
            return false;
        }
        SDK::FlashVariableObjectRef field;
        if (!parentOwner->GetMemberObject(instanceName, field.Receive()) || !field || !field->IsDisplayObject() || !CanCreate(key)) {
            return false;
        }
        _objects.emplace(key, std::move(field));
        _children.emplace(key, Child {parentKey, instanceName, true});
        return true;
    }

    SDK::IFlashVariableObject *NativeUI::MovieRoot() {
        if (SDK::IFlashVariableObject *cached = Find("$movie"); cached != nullptr) {
            return cached;
        }
        return CaptureGlobal("$movie", "_root") ? Find("$movie") : nullptr;
    }

    bool NativeUI::CallHostHelper(const char *helper, const std::string &receiverKey, const std::string &argumentKey) {
        SDK::IFlashVariableObject *receiver = Find(receiverKey);
        SDK::IFlashVariableObject *root     = receiver != nullptr ? MovieRoot() : nullptr;
        if (root == nullptr || !root->HasMember(helper)) {
            return false;
        }
        const std::shared_ptr<SDK::IFlashPlayer> movie = _movie;
        SDK::FlashVariableObjectRef rootOwner(root->Clone());
        SDK::FlashVariableObjectRef receiverOwner(receiver->Clone());
        SDK::FlashVariableObjectRef argumentOwner;
        if (!argumentKey.empty()) {
            SDK::IFlashVariableObject *argument = Find(argumentKey);
            if (argument == nullptr) {
                return false;
            }
            argumentOwner.Reset(argument->Clone());
        }
        else if (!movie->CreateObject(nullptr, nullptr, 0, argumentOwner.Receive())) {
            return false;
        }
        if (!rootOwner || !receiverOwner || !argumentOwner) {
            return false;
        }
        // An empty argument key clears the binding, which the helper takes as
        // anything that is not a movie clip.
        const SDK::IFlashVariableObject *arguments[] = {receiverOwner.Get(), argumentOwner.Get()};
        SDK::SFlashVarValue result {};
        const bool called = rootOwner->InvokeObjects(helper, arguments, 2, &result);
        return called && AcceptsCallback(movie.get()) && result.type == SDK::EFlashVariableType::Bool && result.b;
    }

    bool NativeUI::SetMask(const std::string &receiverKey, const std::string &maskKey) {
        return CallHostHelper("KCDC_SetMask", receiverKey, maskKey);
    }

    bool NativeUI::SetHitArea(const std::string &receiverKey, const std::string &areaKey) {
        return CallHostHelper("KCDC_SetHitArea", receiverKey, areaKey);
    }

    bool NativeUI::LoadClip(const std::string &receiverKey, const char *url) {
        if (url == nullptr || url[0] == '\0') {
            return false;
        }
        const SDK::SFlashVarValue argument = SDK::SFlashVarValue::String(url);
        return InvokeScalars(receiverKey, "loadMovie", {argument});
    }

    bool NativeUI::SetTextStyle(const std::string &receiverKey, const TextStyle &style) {
        SDK::IFlashVariableObject *receiver = Find(receiverKey);
        if (receiver == nullptr || !receiver->IsDisplayObject() || !std::isfinite(style.size) || style.size <= 0.0) {
            return false;
        }
        const std::shared_ptr<SDK::IFlashPlayer> movie = _movie;
        SDK::FlashVariableObjectRef receiverOwner(receiver->Clone());
        SDK::FlashVariableObjectRef format;
        if (!receiverOwner || !movie->CreateObject("TextFormat", nullptr, 0, format.Receive()) || !format) {
            return false;
        }
        bool applied = true;
        if (!style.font.empty()) {
            applied = format->SetMember("font", SDK::SFlashVarValue::String(style.font.c_str())) && applied;
        }
        applied = format->SetMember("size", SDK::SFlashVarValue::Double(style.size)) && applied;
        applied = format->SetMember("color", SDK::SFlashVarValue::Int(style.color)) && applied;
        applied = format->SetMember("bold", SDK::SFlashVarValue::Bool(style.bold)) && applied;
        applied = format->SetMember("italic", SDK::SFlashVarValue::Bool(style.italic)) && applied;
        if (!style.align.empty()) {
            applied = format->SetMember("align", SDK::SFlashVarValue::String(style.align.c_str())) && applied;
        }
        // Not embedded: the shared font library is consulted only for fields that do not ask for embedded ones.
        applied                                      = receiverOwner->SetMember("embedFonts", SDK::SFlashVarValue::Bool(false)) && applied;
        const SDK::IFlashVariableObject *arguments[] = {format.Get()};
        applied                                      = receiverOwner->InvokeObjects("setNewTextFormat", arguments, 1, nullptr) && applied;
        applied                                      = receiverOwner->InvokeObjects("setTextFormat", arguments, 1, nullptr) && applied;
        return applied && AcceptsCallback(movie.get());
    }

    bool NativeUI::CreateObject(const std::string &key, const char *className, const SDK::SFlashVarValue *arguments, unsigned int count) {
        if (!CanCreate(key) || (count != 0 && arguments == nullptr)) {
            return false;
        }
        const std::shared_ptr<SDK::IFlashPlayer> movie = _movie;
        SDK::FlashVariableObjectRef object;
        if (!movie->CreateObject(className, arguments, count, object.Receive()) || !object || !CanCreate(key)) {
            return false;
        }
        _objects.emplace(key, std::move(object));
        return true;
    }

    bool NativeUI::CreateArray(const std::string &key) {
        if (!CanCreate(key)) {
            return false;
        }
        const std::shared_ptr<SDK::IFlashPlayer> movie = _movie;
        SDK::FlashVariableObjectRef object;
        if (!movie->CreateArray(object.Receive()) || !object || !CanCreate(key)) {
            return false;
        }
        _objects.emplace(key, std::move(object));
        return true;
    }

    bool NativeUI::Construct(const std::string &key, const char *className, const std::vector<std::string> &argumentKeys) {
        return CallBridge(key, nullptr, className, argumentKeys);
    }

    bool NativeUI::CallFactory(const std::string &key, const std::string &receiverKey, const char *method, const std::vector<std::string> &argumentKeys) {
        SDK::IFlashVariableObject *receiver = Find(receiverKey);
        return receiver != nullptr && CallBridge(key, receiver, method, argumentKeys);
    }

    bool NativeUI::CallBridge(const std::string &key, SDK::IFlashVariableObject *receiver, const char *name, const std::vector<std::string> &argumentKeys) {
        if (!CanCreate(key) || name == nullptr) {
            return false;
        }
        const std::shared_ptr<SDK::IFlashPlayer> movie = _movie;
        const char *method                             = receiver != nullptr ? "KCDC_Invoke" : "KCDC_Construct";
        SDK::FlashVariableObjectRef host;
        SDK::FlashVariableObjectRef items;
        SDK::FlashVariableObjectRef output;
        SDK::FlashVariableObjectRef methodName;
        SDK::FlashVariableObjectRef receiverOwner(receiver != nullptr ? receiver->Clone() : nullptr);
        if (receiver != nullptr && !receiverOwner) {
            return false;
        }
        if (!movie->GetVariableObject("_root", host.Receive()) || !host || !host->HasMember(method)) {
            return false;
        }
        if (!movie->CreateArray(items.Receive()) || !items || !movie->CreateObject(nullptr, nullptr, 0, output.Receive()) || !output || !movie->CreateString(name, methodName.Receive()) || !methodName) {
            return false;
        }
        unsigned int index = 0;
        for (const std::string &argumentKey : argumentKeys) {
            SDK::IFlashVariableObject *argument = Find(argumentKey);
            if (argument == nullptr || !items->SetElementObject(index++, argument)) {
                return false;
            }
        }
        const SDK::IFlashVariableObject *arguments[] = {receiverOwner.Get(), methodName.Get(), items.Get(), output.Get()};
        SDK::SFlashVarValue result {};
        const bool called = receiver != nullptr ? host->InvokeObjects(method, arguments, 4, &result) : host->InvokeObjects(method, arguments + 1, 3, &result);
        if (!called || !CanCreate(key) || result.type != SDK::EFlashVariableType::Bool || !result.b) {
            return false;
        }
        SDK::FlashVariableObjectRef object;
        if (!output->GetMemberObject("value", object.Receive()) || !object || !object->IsObject() || !CanCreate(key)) {
            return false;
        }
        _objects.emplace(key, std::move(object));
        return true;
    }

    bool NativeUI::CreateValue(const std::string &key, const SDK::SFlashVarValue &value) {
        if (!CanCreate(key)) {
            return false;
        }
        const std::shared_ptr<SDK::IFlashPlayer> movie = _movie;
        SDK::FlashVariableObjectRef array;
        SDK::FlashVariableObjectRef object;
        if (!movie->CreateArray(array.Receive()) || !array || !array->SetElement(0, value) || !array->GetElementObject(0, object.Receive()) || !object || !CanCreate(key)) {
            return false;
        }
        _objects.emplace(key, std::move(object));
        return true;
    }

    bool NativeUI::CaptureMember(const std::string &key, const std::string &parentKey, const char *member) {
        SDK::IFlashVariableObject *parent = Find(parentKey);
        if (!CanCreate(key) || parent == nullptr || member == nullptr) {
            return false;
        }
        const std::shared_ptr<SDK::IFlashPlayer> movie = _movie;
        SDK::FlashVariableObjectRef parentOwner(parent->Clone());
        SDK::FlashVariableObjectRef object;
        if (!parentOwner || !parentOwner->GetMemberObject(member, object.Receive()) || !object || !CanCreate(key)) {
            return false;
        }
        _objects.emplace(key, std::move(object));
        return true;
    }

    bool NativeUI::RefreshMember(const std::string &key, const std::string &parentKey, const char *member) {
        SDK::IFlashVariableObject *parent = Find(parentKey);
        if (_closing || key == "$root" || _objects.find(key) == _objects.end() || parent == nullptr || member == nullptr) {
            return false;
        }
        const std::shared_ptr<SDK::IFlashPlayer> movie = _movie;
        SDK::FlashVariableObjectRef parentOwner(parent->Clone());
        SDK::FlashVariableObjectRef object;
        if (!parentOwner || !parentOwner->GetMemberObject(member, object.Receive()) || !object || !AcceptsCallback(movie.get())) {
            return false;
        }
        _objects.at(key) = std::move(object);
        return true;
    }

    bool NativeUI::SetObject(const std::string &receiverKey, const char *member, const std::string &valueKey) {
        SDK::IFlashVariableObject *receiver = Find(receiverKey);
        SDK::IFlashVariableObject *value    = Find(valueKey);
        if (receiver == nullptr || value == nullptr || member == nullptr) {
            return false;
        }
        const std::shared_ptr<SDK::IFlashPlayer> movie = _movie;
        SDK::FlashVariableObjectRef receiverOwner(receiver->Clone());
        SDK::FlashVariableObjectRef valueOwner(value->Clone());
        return receiverOwner && valueOwner && receiverOwner->SetMemberObject(member, valueOwner.Get()) && AcceptsCallback(movie.get());
    }

    bool NativeUI::CaptureGlobal(const std::string &key, const char *path) {
        if (!CanCreate(key) || path == nullptr) {
            return false;
        }
        const std::shared_ptr<SDK::IFlashPlayer> movie = _movie;
        SDK::FlashVariableObjectRef object;
        if (!movie->GetVariableObject(path, object.Receive()) || !object || !CanCreate(key)) {
            return false;
        }
        _objects.emplace(key, std::move(object));
        return true;
    }

    bool NativeUI::SetValue(const std::string &receiverKey, const char *member, const SDK::SFlashVarValue &value) {
        SDK::IFlashVariableObject *receiver = Find(receiverKey);
        if (receiver == nullptr || member == nullptr) {
            return false;
        }
        const std::shared_ptr<SDK::IFlashPlayer> movie = _movie;
        SDK::FlashVariableObjectRef receiverOwner(receiver->Clone());
        return receiverOwner && receiverOwner->SetMember(member, value) && AcceptsCallback(movie.get());
    }

    bool NativeUI::DeleteMember(const std::string &receiverKey, const char *member) {
        SDK::IFlashVariableObject *receiver = Find(receiverKey);
        if (receiver == nullptr || member == nullptr) {
            return false;
        }
        const std::shared_ptr<SDK::IFlashPlayer> movie = _movie;
        SDK::FlashVariableObjectRef receiverOwner(receiver->Clone());
        return receiverOwner && receiverOwner->DeleteMember(member) && AcceptsCallback(movie.get());
    }

    bool NativeUI::SetDisplayInfo(const std::string &receiverKey, const SDK::SFlashDisplayInfo &info) {
        SDK::IFlashVariableObject *receiver = Find(receiverKey);
        if (receiver == nullptr || !receiver->IsDisplayObject()) {
            return false;
        }
        const std::shared_ptr<SDK::IFlashPlayer> movie = _movie;
        SDK::FlashVariableObjectRef receiverOwner(receiver->Clone());
        return receiverOwner && receiverOwner->SetDisplayInfo(info) && AcceptsCallback(movie.get());
    }

    bool NativeUI::SetVisible(const std::string &receiverKey, bool visible) {
        SDK::SFlashDisplayInfo info;
        info.SetVisible(visible);
        return SetDisplayInfo(receiverKey, info);
    }

    bool NativeUI::GetDisplayInfo(const std::string &receiverKey, SDK::SFlashDisplayInfo &info) const {
        info                                = {};
        SDK::IFlashVariableObject *receiver = Find(receiverKey);
        if (receiver == nullptr || !receiver->IsDisplayObject()) {
            return false;
        }
        const std::shared_ptr<SDK::IFlashPlayer> movie = _movie;
        SDK::FlashVariableObjectRef receiverOwner(receiver->Clone());
        SDK::SFlashDisplayInfo current;
        if (!receiverOwner || !receiverOwner->GetDisplayInfo(current) || !AcceptsCallback(movie.get())) {
            return false;
        }
        info = current;
        return true;
    }

    bool NativeUI::SetColorTransform(const std::string &receiverKey, const SDK::SFlashCxform &transform) {
        SDK::IFlashVariableObject *receiver = Find(receiverKey);
        if (receiver == nullptr || !receiver->IsDisplayObject()) {
            return false;
        }
        const std::shared_ptr<SDK::IFlashPlayer> movie = _movie;
        SDK::FlashVariableObjectRef receiverOwner(receiver->Clone());
        return receiverOwner && receiverOwner->SetColorTransform(transform) && AcceptsCallback(movie.get());
    }

    bool NativeUI::GetColorTransform(const std::string &receiverKey, SDK::SFlashCxform &transform) const {
        transform                           = {};
        SDK::IFlashVariableObject *receiver = Find(receiverKey);
        if (receiver == nullptr || !receiver->IsDisplayObject()) {
            return false;
        }
        const std::shared_ptr<SDK::IFlashPlayer> movie = _movie;
        SDK::FlashVariableObjectRef receiverOwner(receiver->Clone());
        SDK::SFlashCxform current;
        if (!receiverOwner || !receiverOwner->GetColorTransform(current) || !AcceptsCallback(movie.get())) {
            return false;
        }
        transform = current;
        return true;
    }

    bool NativeUI::SetText(const std::string &receiverKey, const char *text, bool html) {
        SDK::IFlashVariableObject *receiver = Find(receiverKey);
        if (receiver == nullptr || text == nullptr || !receiver->IsDisplayObject()) {
            return false;
        }
        const std::shared_ptr<SDK::IFlashPlayer> movie = _movie;
        SDK::FlashVariableObjectRef receiverOwner(receiver->Clone());
        if (!receiverOwner) {
            return false;
        }
        const bool changed = html ? receiverOwner->SetTextHTML(text) : receiverOwner->SetText(text);
        return changed && AcceptsCallback(movie.get());
    }

    bool NativeUI::GetText(const std::string &receiverKey, std::string &text, bool html) const {
        text.clear();
        SDK::IFlashVariableObject *receiver = Find(receiverKey);
        if (receiver == nullptr || !receiver->IsDisplayObject()) {
            return false;
        }
        const std::shared_ptr<SDK::IFlashPlayer> movie = _movie;
        SDK::FlashVariableObjectRef receiverOwner(receiver->Clone());
        if (!receiverOwner) {
            return false;
        }
        SDK::SFlashVarValue value {};
        const bool read = html ? receiverOwner->GetTextHTML(value) : receiverOwner->GetText(value);
        if (!read || !AcceptsCallback(movie.get()) || value.type != SDK::EFlashVariableType::ConstStrPtr || value.str == nullptr) {
            return false;
        }
        text.assign(value.str);
        return true;
    }

    bool NativeUI::GotoFrame(const std::string &receiverKey, unsigned int frame, bool play) {
        SDK::IFlashVariableObject *receiver = Find(receiverKey);
        if (receiver == nullptr || frame == 0 || !receiver->IsDisplayObject()) {
            return false;
        }
        const std::shared_ptr<SDK::IFlashPlayer> movie = _movie;
        SDK::FlashVariableObjectRef receiverOwner(receiver->Clone());
        if (!receiverOwner) {
            return false;
        }
        const bool changed = play ? receiverOwner->GotoAndPlayFrame(frame) : receiverOwner->GotoAndStopFrame(frame);
        return changed && AcceptsCallback(movie.get());
    }

    bool NativeUI::GotoLabel(const std::string &receiverKey, const char *label, bool play) {
        SDK::IFlashVariableObject *receiver = Find(receiverKey);
        if (receiver == nullptr || label == nullptr || !receiver->IsDisplayObject()) {
            return false;
        }
        const std::shared_ptr<SDK::IFlashPlayer> movie = _movie;
        SDK::FlashVariableObjectRef receiverOwner(receiver->Clone());
        if (!receiverOwner) {
            return false;
        }
        const bool changed = play ? receiverOwner->GotoAndPlayLabel(label) : receiverOwner->GotoAndStopLabel(label);
        return changed && AcceptsCallback(movie.get());
    }

    bool NativeUI::CaptureArgument(const std::string &key, const SDK::IActionScriptFunction::Params &params, unsigned int index, bool replace) {
        if (!AcceptsCallback(params.pFromPlayer) || key.empty() || key == "$root" || index >= params.numArgs || params.pArgs == nullptr || params.pArgs[index] == nullptr) {
            return false;
        }
        const auto existing = _objects.find(key);
        if (existing != _objects.end() && !replace) {
            return false;
        }
        SDK::FlashVariableObjectRef value(params.pArgs[index]->Clone());
        if (!value) {
            return false;
        }
        _objects.insert_or_assign(key, std::move(value));
        return true;
    }

    bool NativeUI::CreateCallback(const std::string &key, std::function<void(const SDK::IActionScriptFunction::Params &, SDK::IActionScriptFunction::IReturnValue *)> callback) {
        if (!CanCreate(key) || !callback) {
            return false;
        }
        if (_host == nullptr) {
            return false;
        }
        const std::uintptr_t id = _host->RegisterCallback(this, std::move(callback));
        if (id == 0) {
            return false;
        }
        SDK::FlashVariableObjectRef function;
        if (!_movie->CreateFunction(function.Receive(), _host, reinterpret_cast<void *>(id)) || !function) {
            _host->RemoveCallback(id);
            return false;
        }
        _callbackIds.push_back(id);
        _objects.emplace(key, std::move(function));
        return true;
    }

    bool NativeUI::Invoke(const std::string &receiverKey, const char *method, const std::vector<std::string> &argumentKeys, SDK::SFlashVarValue *result) {
        _lastResultReceiver.Reset();
        if (result != nullptr) {
            *result = {};
        }
        SDK::IFlashVariableObject *receiver = Find(receiverKey);
        if (receiver == nullptr || method == nullptr) {
            return false;
        }
        const std::shared_ptr<SDK::IFlashPlayer> movie = _movie;
        SDK::FlashVariableObjectRef receiverOwner(receiver->Clone());
        if (!receiverOwner) {
            return false;
        }
        std::vector<SDK::FlashVariableObjectRef> argumentOwners;
        std::vector<const SDK::IFlashVariableObject *> arguments;
        arguments.reserve(argumentKeys.size());
        for (const std::string &key : argumentKeys) {
            SDK::IFlashVariableObject *argument = Find(key);
            if (argument == nullptr) {
                return false;
            }
            argumentOwners.emplace_back(argument->Clone());
            if (!argumentOwners.back()) {
                return false;
            }
            arguments.push_back(argumentOwners.back().Get());
        }
        const bool called = arguments.empty() ? receiverOwner->Invoke(method, nullptr, 0, result) : receiverOwner->InvokeObjects(method, arguments.data(), static_cast<unsigned int>(arguments.size()), result);
        if (!AcceptsCallback(movie.get())) {
            if (result != nullptr) {
                *result = {};
            }
            return false;
        }
        if (result != nullptr) {
            _lastResultReceiver = std::move(receiverOwner);
        }
        return called;
    }

    bool NativeUI::InvokeScalars(const std::string &receiverKey, const char *method, const std::vector<SDK::SFlashVarValue> &arguments, SDK::SFlashVarValue *result) {
        _lastResultReceiver.Reset();
        if (result != nullptr) {
            *result = {};
        }
        SDK::IFlashVariableObject *receiver = Find(receiverKey);
        if (receiver == nullptr || method == nullptr) {
            return false;
        }
        const std::shared_ptr<SDK::IFlashPlayer> movie = _movie;
        SDK::FlashVariableObjectRef receiverOwner(receiver->Clone());
        if (!receiverOwner) {
            return false;
        }
        const bool called = receiverOwner->Invoke(method, arguments.data(), static_cast<unsigned int>(arguments.size()), result);
        if (!AcceptsCallback(movie.get())) {
            if (result != nullptr) {
                *result = {};
            }
            return false;
        }
        if (result != nullptr) {
            _lastResultReceiver = std::move(receiverOwner);
        }
        return called;
    }

    bool NativeUI::AddCleanup(const std::string &receiverKey, const char *method, const std::vector<std::string> &argumentKeys) {
        SDK::IFlashVariableObject *receiver = Find(receiverKey);
        if (receiver == nullptr || method == nullptr) {
            return false;
        }
        const std::shared_ptr<SDK::IFlashPlayer> movie = _movie;
        Cleanup cleanup;
        cleanup.receiver.Reset(receiver->Clone());
        cleanup.method = method;
        if (!cleanup.receiver || !cleanup.receiver->HasMember(method) || !AcceptsCallback(movie.get())) {
            return false;
        }
        for (const std::string &key : argumentKeys) {
            SDK::IFlashVariableObject *argument = Find(key);
            if (argument == nullptr) {
                return false;
            }
            cleanup.arguments.emplace_back(argument->Clone());
            if (!cleanup.arguments.back()) {
                return false;
            }
        }
        _cleanup.push_back(std::move(cleanup));
        return true;
    }

    bool NativeUI::RemoveObject(const std::string &key) {
        if (_closing || key.empty() || key == "$root") {
            return false;
        }
        const auto found = _objects.find(key);
        if (found == _objects.end()) {
            return false;
        }
        const std::shared_ptr<SDK::IFlashPlayer> movie = _movie;
        SDK::FlashVariableObjectRef object             = std::move(found->second);
        _objects.erase(found);
        _lastResultReceiver.Reset();
        bool success           = true;
        const auto found_child = _children.find(key);
        if (found_child != _children.end()) {
            const Child record = found_child->second;
            _children.erase(found_child);
            // The child goes before its parent member: deleting the name
            // first would leave the clip on the display list unnamed.
            if (object) {
                success = object->Invoke(record.textField ? "removeTextField" : "removeMovieClip", nullptr, 0, nullptr);
            }
            SDK::IFlashVariableObject *parent = Find(record.parentKey);
            if (_movie && parent != nullptr) {
                SDK::FlashVariableObjectRef parentOwner(parent->Clone());
                success = parentOwner && parentOwner->DeleteMember(record.instanceName.c_str()) && success;
            }
        }
        object.Reset();
        return success && AcceptsCallback(movie.get());
    }

    bool NativeUI::GetValue(const std::string &receiverKey, const char *member, SDK::SFlashVarValue &value, std::string &text) const {
        value = {};
        text.clear();
        SDK::IFlashVariableObject *receiver = Find(receiverKey);
        if (receiver == nullptr || member == nullptr) {
            return false;
        }
        const std::shared_ptr<SDK::IFlashPlayer> movie = _movie;
        SDK::FlashVariableObjectRef receiverOwner(receiver->Clone());
        SDK::SFlashVarValue current {};
        if (!receiverOwner || !receiverOwner->GetMember(member, current) || !AcceptsCallback(movie.get())) {
            return false;
        }
        // A string result lives in the receiver's last-result cache, which dies with the clone below.
        if (current.type == SDK::EFlashVariableType::ConstStrPtr && current.str != nullptr) {
            text.assign(current.str);
            current.str = text.c_str();
        }
        else if (current.type == SDK::EFlashVariableType::ConstStrPtr) {
            return false;
        }
        value = current;
        return true;
    }

    bool NativeUI::Unmount() {
        if (_closing) {
            return !_movie;
        }
        _closing = true;
        // An AS cleanup can cause a movie unload notification. Keep its
        // receiver/arguments alive across the call and stop after invalidation.
        const std::shared_ptr<SDK::IFlashPlayer> movie = _movie;
        std::vector<Cleanup> cleanup                   = std::move(_cleanup);
        bool success                                   = true;
        for (auto entry = cleanup.rbegin(); entry != cleanup.rend() && _movie; ++entry) {
            std::vector<const SDK::IFlashVariableObject *> arguments;
            arguments.reserve(entry->arguments.size());
            for (const SDK::FlashVariableObjectRef &argument : entry->arguments) {
                arguments.push_back(argument.Get());
            }
            const bool called = arguments.empty() ? entry->receiver->Invoke(entry->method.c_str(), nullptr, 0, nullptr) : entry->receiver->InvokeObjects(entry->method.c_str(), arguments.data(), static_cast<unsigned int>(arguments.size()), nullptr);
            success           = called && success;
        }
        if (_root) {
            SDK::FlashVariableObjectRef root   = _root.Clone();
            SDK::FlashVariableObjectRef parent = _parent.Clone();
            const std::string instanceName     = _instanceName;
            if (root && parent) {
                success = root->Invoke("removeMovieClip", nullptr, 0, nullptr) && success;
                if (_movie) {
                    success = parent->DeleteMember(instanceName.c_str()) && success;
                }
            }
            else {
                success = false;
            }
        }
        Invalidate();
        return success;
    }

    void NativeUI::Detach() {
        Invalidate();
        _host = nullptr;
    }

    void NativeUI::Invalidate() {
        _closing = true;
        if (_host != nullptr) {
            for (std::uintptr_t id : _callbackIds) {
                _host->RemoveCallback(id);
            }
        }
        _callbackIds.clear();
        _cleanup.clear();
        _objects.clear();
        _children.clear();
        _lastResultReceiver.Reset();
        _root.Reset();
        _parent.Reset();
        _instanceName.clear();
        _movie.reset();
    }
} // namespace KCDC::Core
