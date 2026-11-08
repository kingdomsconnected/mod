#include "native_ui_module.h"

#include "core/application.h"

#include "sdk/cryengine/input/i_input.h"
#include "sdk/cryengine/system/i_cry_pak.h"
#include "sdk/cryengine/system/i_system.h"

#include <filesystem>
#include <logging/logger.h>
#include <utility>

namespace KCDC::Core {
    namespace {
        constexpr char kFocusMapName[] = "kcdc_native_ui_focus";

        constexpr char kFocusMapXml[] = "<actionmap name=\"kcdc_native_ui_focus\" priority=\"menu\" exclusivity=\"1\">"
                                        "<action name=\"kcdc_ui_accept\" onPress=\"1\" onHold=\"1\" onRelease=\"1\" holdTriggerDelay=\"0.25\" holdRepeatDelay=\"0\" xboxpad=\"xi_a\" pspad=\"pad_cross\"/>"
                                        "<action name=\"kcdc_ui_back\" onPress=\"1\" onRelease=\"1\" xboxpad=\"xi_b\" pspad=\"pad_circle\"/>"
                                        "<action name=\"kcdc_ui_left\" onPress=\"1\" onHold=\"1\" onRelease=\"1\" holdTriggerDelay=\"0.5\" holdRepeatDelay=\"0.15\">"
                                        "<xboxpad><inputdata input=\"xi_dpad_left\"/><inputdata input=\"xi_thumbl_left\"/><inputdata input=\"xi_thumbr_left\"/></xboxpad>"
                                        "<pspad><inputdata input=\"pad_left\"/><inputdata input=\"pad_stickl_left\"/><inputdata input=\"pad_stickr_left\"/></pspad>"
                                        "</action>"
                                        "<action name=\"kcdc_ui_right\" onPress=\"1\" onHold=\"1\" onRelease=\"1\" holdTriggerDelay=\"0.5\" holdRepeatDelay=\"0.15\">"
                                        "<xboxpad><inputdata input=\"xi_dpad_right\"/><inputdata input=\"xi_thumbl_right\"/><inputdata input=\"xi_thumbr_right\"/></xboxpad>"
                                        "<pspad><inputdata input=\"pad_right\"/><inputdata input=\"pad_stickl_right\"/><inputdata input=\"pad_stickr_right\"/></pspad>"
                                        "</action>"
                                        "<action name=\"kcdc_ui_up\" onPress=\"1\" onHold=\"1\" onRelease=\"1\" holdTriggerDelay=\"0.5\" holdRepeatDelay=\"0.15\">"
                                        "<xboxpad><inputdata input=\"xi_dpad_up\"/><inputdata input=\"xi_thumbl_up\"/><inputdata input=\"xi_thumbr_up\"/></xboxpad>"
                                        "<pspad><inputdata input=\"pad_up\"/><inputdata input=\"pad_stickl_up\"/><inputdata input=\"pad_stickr_up\"/></pspad>"
                                        "</action>"
                                        "<action name=\"kcdc_ui_down\" onPress=\"1\" onHold=\"1\" onRelease=\"1\" holdTriggerDelay=\"0.5\" holdRepeatDelay=\"0.15\">"
                                        "<xboxpad><inputdata input=\"xi_dpad_down\"/><inputdata input=\"xi_thumbl_down\"/><inputdata input=\"xi_thumbr_down\"/></xboxpad>"
                                        "<pspad><inputdata input=\"pad_down\"/><inputdata input=\"pad_stickl_down\"/><inputdata input=\"pad_stickr_down\"/></pspad>"
                                        "</action>"
                                        "</actionmap>";
    } // namespace

    NativeUIModule &NativeUIModule::Get() {
        static NativeUIModule instance;
        return instance;
    }

    bool NativeUIModule::MountArchive(const std::string &archivePath, std::string &error) {
        const auto found = _archives.find(archivePath);
        if (found != _archives.end()) {
            ++found->second;
            return true;
        }
        if (!std::filesystem::path(archivePath).is_absolute()) {
            error = "archive path must be absolute";
            return false;
        }
        SDK::ICryPak *pak = SDK::ICryPak::GetInstance();
        if (pak == nullptr) {
            error = "CryPak is not initialized";
            return false;
        }
        if (!pak->MountArchive(archivePath.c_str())) {
            error = "CryPak mount failed: " + archivePath;
            return false;
        }
        _archives.emplace(archivePath, 1u);
        Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("Mounted native UI archive {}", archivePath);
        return true;
    }

    void NativeUIModule::ReleaseArchive(const std::string &archivePath) {
        const auto found = _archives.find(archivePath);
        if (found == _archives.end()) {
            return;
        }
        if (found->second > 0) {
            --found->second;
        }
        // Deliberately still mounted: the elements it registered stay in the
        // engine's registry whatever we do, and one of them holding a
        // bootstrapper over assets pulled out from under it is not worth a few
        // hundred kilobytes. They go at shutdown.
    }

    NativeUIHost *NativeUIModule::OpenHost(const NativeUIAssets::Movie &movie, NativeUIHost::Kind kind, std::string &error) {
        if (movie.elementName.empty() || movie.archivePath.empty() || movie.descriptorPath.empty()) {
            error = "an incomplete native movie cannot be opened";
            return nullptr;
        }
        const auto found = _hosts.find(movie.elementName);
        if (found != _hosts.end()) {
            if (found->second.host->GetKind() != kind) {
                error = "a different kind of host already owns " + movie.elementName;
                return nullptr;
            }
            return found->second.host.get();
        }
        if (!MountArchive(movie.archivePath, error)) {
            return nullptr;
        }
        auto host = std::make_unique<NativeUIHost>(*this, movie.elementName, kind);
        if (!host->Open(movie.descriptorPath)) {
            error = host->GetStatus();
            ReleaseArchive(movie.archivePath);
            return nullptr;
        }
        NativeUIHost *opened = host.get();
        _hosts.emplace(movie.elementName, HostEntry {std::move(host), movie.archivePath});
        _status = "Opened native UI host " + movie.elementName;
        return opened;
    }

    NativeUIHost *NativeUIModule::FindHost(const std::string &elementName) {
        const auto found = _hosts.find(elementName);
        return found != _hosts.end() ? found->second.host.get() : nullptr;
    }

    bool NativeUIModule::CloseHost(const std::string &elementName) {
        const auto found = _hosts.find(elementName);
        if (found == _hosts.end()) {
            return false;
        }
        if (_focusHostName == elementName) {
            ReleaseFocus();
        }
        const std::string archivePath = found->second.archivePath;
        found->second.host->Close();
        _hosts.erase(found);
        ReleaseArchive(archivePath);
        return true;
    }

    void NativeUIModule::Shutdown() {
        ReleaseFocus();
        // Copied first: closing a host can queue events.
        std::vector<std::string> names = GetHostNames();
        for (const std::string &name : names) {
            CloseHost(name);
        }
        _hosts.clear();
        if (SDK::ICryPak *pak = SDK::ICryPak::GetInstance(); pak != nullptr) {
            for (const auto &archive : _archives) {
                pak->UnmountArchive(archive.first.c_str());
            }
        }
        _archives.clear();
        _events.clear();
        _status = "Shut down";
    }

    std::vector<std::string> NativeUIModule::GetHostNames() const {
        std::vector<std::string> names;
        names.reserve(_hosts.size());
        for (const auto &entry : _hosts) {
            names.push_back(entry.first);
        }
        return names;
    }

    void NativeUIModule::Queue(NativeUIEvent event) {
        constexpr std::size_t kMaxQueued = 4096;
        if (_events.size() >= kMaxQueued) {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->warn("Native UI event queue is full; dropping {} from {}", event.name, event.host);
            return;
        }
        _events.emplace_back(std::move(event));
    }

    void NativeUIModule::Drain(const fu2::function<void(const NativeUIEvent &) const> &sink) {
        if (_draining || _events.empty() || !sink) {
            return;
        }
        // Handlers can queue more; those wait for the next drain, which keeps one frame bounded.
        std::vector<NativeUIEvent> events;
        events.swap(_events);
        _draining = true;
        for (const NativeUIEvent &event : events) {
            sink(event);
        }
        _draining = false;
    }

    bool NativeUIModule::AcquireFocus(const std::string &elementName, const std::string &pageName) {
        NativeUIHost *host = FindHost(elementName);
        if (host == nullptr || !host->IsReady()) {
            _status = "Native focus needs an open, ready host";
            return false;
        }
        if (!host->FindPage(pageName)) {
            _status = "Native focus needs a live page";
            return false;
        }
        if (_focusMap != nullptr) {
            if (_focusHostName == elementName && _focusPageName == pageName) {
                Update();
                return _focusMap != nullptr;
            }
            ReleaseFocus();
        }
        if (host->GetKind() == NativeUIHost::Kind::Composition && !host->SelectInputPage(pageName)) {
            _status = "Native focus could not select the page for input";
            return false;
        }
        SDK::ISystem *system            = SDK::ISystem::GetInstance();
        SDK::IActionMapManager *manager = SDK::IActionMapManager::GetInstance();
        SDK::IInput *input              = SDK::IInput::GetInstance();
        if (system == nullptr || manager == nullptr || input == nullptr || !input->IsEventPostingEnabled() || !gApplication || gApplication->AreControlsLocked()) {
            _status = "Native focus requires available game input and no other mod input lock";
            return false;
        }
        if (manager->GetActionMap(kFocusMapName) != nullptr) {
            _status = "Native focus map name is already owned";
            return false;
        }
        SDK::XmlNodeRef node = system->LoadXmlFromBuffer(kFocusMapXml, sizeof(kFocusMapXml) - 1, false, false);
        if (!node.IsValid()) {
            _status = "Native focus XML parsing failed";
            return false;
        }
        SDK::IActionMap *map = manager->CreateActionMap(kFocusMapName);
        if (map == nullptr) {
            _status = "Native focus map creation failed";
            return false;
        }
        if (!map->LoadFromXML(node) || map->GetPriority() != 11 || !map->IsExclusive()) {
            manager->RemoveActionMap(kFocusMapName);
            _status = "Native focus map configuration failed";
            return false;
        }
        _focusManager           = manager;
        _focusMap               = map;
        _focusHostName          = elementName;
        _focusPageName          = pageName;
        _actionListenerAttached = manager->AddExtraActionListener(this, kFocusMapName);
        if (!_actionListenerAttached) {
            ReleaseFocus();
            _status = "Native action listener registration failed";
            return false;
        }
        manager->EnableActionMap(kFocusMapName, true);
        Update();
        Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("Native UI focus acquisition for {}/{}: {}", elementName, pageName, _status);
        return _focusMap != nullptr;
    }

    void NativeUIModule::ReleaseFocus() {
        if (_focusMap != nullptr) {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("Native UI focus release requested for {}/{}", _focusHostName, _focusPageName);
        }
        NativeUIHost *host = FindHost(_focusHostName);
        if (host != nullptr) {
            host->SetInputDelivery(false);
            host->ClearInputPage();
        }
        SDK::IActionMapManager *manager = SDK::IActionMapManager::GetInstance();
        if (_focusMap != nullptr && manager != nullptr && manager == _focusManager && manager->GetActionMap(kFocusMapName) == _focusMap) {
            manager->EnableActionMap(kFocusMapName, false);
            if (_actionListenerAttached) {
                manager->RemoveExtraActionListener(this, kFocusMapName);
            }
            manager->RemoveActionMap(kFocusMapName);
        }
        _focusMap               = nullptr;
        _focusManager           = nullptr;
        _actionListenerAttached = false;
        _focusHostName.clear();
        _focusPageName.clear();
    }

    void NativeUIModule::Update() {
        if (_focusMap == nullptr) {
            return;
        }
        NativeUIHost *host = FindHost(_focusHostName);
        if (host == nullptr || !host->IsReady() || !host->FindPage(_focusPageName)) {
            ReleaseFocus();
            _status = "Native focus released after the page went away";
            return;
        }
        SDK::IActionMapManager *manager = SDK::IActionMapManager::GetInstance();
        SDK::IInput *input              = SDK::IInput::GetInstance();
        if (!host->IsVisible() || !gApplication || gApplication->AreControlsLocked()) {
            ReleaseFocus();
            _status = "Native focus released";
            return;
        }
        SDK::IActionMap *map = manager != nullptr && manager == _focusManager ? manager->GetActionMap(kFocusMapName) : nullptr;
        if (map != _focusMap || input == nullptr || !input->IsEventPostingEnabled()) {
            ReleaseFocus();
            _status = "Native focus released after input or map invalidation";
            return;
        }
        const bool active = map->Enabled() && map->IsActive();
        host->SetInputDelivery(active);
        _status = active ? "Native focus active" : "Native focus waiting for a higher-priority game context";
    }

    void NativeUIModule::OnAction(const SDK::CCryName &action, int activationMode, float) {
        if (!_actionListenerAttached) {
            return;
        }
        NativeUIHost *host = FindHost(_focusHostName);
        if (host == nullptr) {
            return;
        }
        // Synchronous, including releases raised while the map deactivates; only the script copy is queued.
        host->DispatchAction(action.c_str(), activationMode);
        NativeUIEvent event;
        event.kind           = NativeUIEventKind::Action;
        event.host           = _focusHostName;
        event.page           = _focusPageName;
        event.name           = action.c_str();
        event.activationMode = activationMode;
        Queue(std::move(event));
    }

    bool NativeUIModule::ValidateFocusMap() {
        SDK::ISystem *system            = SDK::ISystem::GetInstance();
        SDK::IActionMapManager *manager = SDK::IActionMapManager::GetInstance();
        if (system == nullptr || manager == nullptr) {
            _status = "Focus validation requires the initialized game system";
            return false;
        }
        constexpr char name[] = "kcdc_native_ui_probe_validation";
        if (manager->GetActionMap(name) != nullptr) {
            _status = "Focus validation map already exists; inspect its owner before retrying";
            return false;
        }
        constexpr char xml[] = "<actionmap name=\"kcdc_native_ui_probe_validation\" priority=\"modal_dialog\" exclusivity=\"1\"/>";
        SDK::XmlNodeRef node = system->LoadXmlFromBuffer(xml, sizeof(xml) - 1, false, false);
        if (!node.IsValid()) {
            _status = "Native focus-map XML parsing failed";
            return false;
        }
        SDK::IActionMap *map = manager->CreateActionMap(name);
        if (map == nullptr) {
            _status = "Native focus-map creation failed";
            return false;
        }
        const bool loaded           = map->LoadFromXML(node);
        const unsigned int priority = map->GetPriority();
        const bool exclusive        = map->IsExclusive();
        const bool enabled          = map->Enabled();
        const bool active           = map->IsActive();
        // The diagnostic owns only this new map and never enables it.
        const bool removed = manager->RemoveActionMap(name);
        const bool absent  = manager->GetActionMap(name) == nullptr;
        const bool passed  = loaded && priority == 9 && exclusive && !enabled && !active && removed && absent;
        _status            = passed ? "Focus-map configuration passed: " : "Focus-map configuration failed: ";
        _status += "XML=" + std::to_string(loaded) + " priority=" + std::to_string(priority) + " exclusive=" + std::to_string(exclusive);
        _status += " enabled=" + std::to_string(enabled) + " active=" + std::to_string(active) + " removed=" + std::to_string(removed && absent);
        return passed;
    }
} // namespace KCDC::Core
