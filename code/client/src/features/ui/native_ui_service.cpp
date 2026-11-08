#include "native_ui_service.h"

#include "core/ui/native_ui/native_ui_module.h"
#include "features/ui/native_element.h"
#include "features/ui/native_ui_scripting.h"

#include <logging/logger.h>
#include <utility>

namespace KCDC::Features::UI {
    namespace {
        constexpr std::size_t kMaxScreensPerResource = 16;
        constexpr int kResourceMovieLayer            = 46;
    } // namespace

    void NativeUIService::Tick() {
        KCDC::Core::NativeUIModule &module = KCDC::Core::NativeUIModule::Get();
        module.Update();
        module.Drain([this](const KCDC::Core::NativeUIEvent &event) {
            OnEvent(event);
        });
    }

    void NativeUIService::Reset() {
        // Before the screens: an element handle holds a listener registration
        // and possibly an instance reference, neither of which may outlive the engine.
        KCDC::Features::UI::NativeElement::ReleaseAll();
        // While the isolate is alive: a V8 global that outlives one cannot be released.
        KCDC::Scripting::DropNativeUIHandlers();
        if (_screens.empty()) {
            return;
        }
        Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->debug("Native UI: dropping {} screen(s) with the session", _screens.size());
        std::vector<std::uint32_t> ids;
        ids.reserve(_screens.size());
        for (const auto &entry : _screens) {
            ids.push_back(entry.first);
        }
        for (std::uint32_t id : ids) {
            CloseScreen(id);
        }
        _screens.clear();
    }

    std::uint32_t NativeUIService::OpenLibraryScreen(const std::string &resource, const std::string &library, std::string &error) {
        KCDC::Core::NativeUIAssets::Movie movie;
        if (!KCDC::Core::NativeUIAssets::BundledHost(library, movie, error)) {
            return 0;
        }
        return Open(resource, movie, KCDC::Core::NativeUIHost::Kind::Composition, error);
    }

    std::uint32_t NativeUIService::OpenMovieScreen(const std::string &resource, const std::string &moviePath, const std::vector<std::string> &assetPaths, int layer, std::string &error) {
        KCDC::Core::NativeUIAssets::Movie movie;
        if (!KCDC::Core::NativeUIAssets::PackageResourceMovie(resource, moviePath, assetPaths, layer > 0 ? layer : kResourceMovieLayer, movie, error)) {
            return 0;
        }
        return Open(resource, movie, KCDC::Core::NativeUIHost::Kind::Plain, error);
    }

    std::uint32_t NativeUIService::Open(const std::string &resource, const KCDC::Core::NativeUIAssets::Movie &movie, KCDC::Core::NativeUIHost::Kind kind, std::string &error) {
        if (resource.empty()) {
            error = "a native screen needs an owning resource";
            return 0;
        }
        std::size_t owned = 0;
        for (const auto &entry : _screens) {
            owned += entry.second.resource == resource ? 1 : 0;
        }
        if (owned >= kMaxScreensPerResource) {
            error = "a resource may hold at most " + std::to_string(kMaxScreensPerResource) + " native screens";
            return 0;
        }
        KCDC::Core::NativeUIHost *host = KCDC::Core::NativeUIModule::Get().OpenHost(movie, kind, error);
        if (host == nullptr) {
            return 0;
        }
        const std::uint32_t id = _nextScreen++;
        Screen screen;
        screen.resource = resource;
        screen.host     = movie.elementName;
        screen.page     = "kcdc_screen_" + std::to_string(id);
        screen.pending  = true;
        auto inserted   = _screens.emplace(id, std::move(screen));
        Mount(id, inserted.first->second);
        return id;
    }

    void NativeUIService::Mount(std::uint32_t id, Screen &screen) {
        if (!screen.pending) {
            return;
        }
        KCDC::Core::NativeUIHost *host = KCDC::Core::NativeUIModule::Get().FindHost(screen.host);
        if (host == nullptr || !host->IsReady()) {
            return; // Its readiness event brings us back.
        }
        std::shared_ptr<KCDC::Core::NativeUI> page = host->CreatePage(screen.page);
        if (!page) {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->warn("Native UI: page {} could not be mounted in {}", screen.page, screen.host);
            return;
        }
        screen.mounted = page;
        screen.pending = false;

        // Queued rather than published: a host that was already open mounts
        // inside `createScreen`, where the script has registered nothing yet.
        KCDC::Core::NativeUIEvent ready;
        ready.kind = KCDC::Core::NativeUIEventKind::Ready;
        ready.host = screen.host;
        ready.page = screen.page;
        KCDC::Core::NativeUIModule::Get().Queue(std::move(ready));
    }

    bool NativeUIService::CloseScreen(std::uint32_t screen) {
        const auto found = _screens.find(screen);
        if (found == _screens.end()) {
            return false;
        }
        const std::string host = found->second.host;
        const std::string page = found->second.page;
        if (KCDC::Core::NativeUIModule::Get().GetFocusHost() == host && KCDC::Core::NativeUIModule::Get().GetFocusPage() == page) {
            KCDC::Core::NativeUIModule::Get().ReleaseFocus();
        }
        if (KCDC::Core::NativeUIHost *owner = KCDC::Core::NativeUIModule::Get().FindHost(host); owner != nullptr) {
            owner->ClosePage(page);
        }
        _screens.erase(found);
        DropHostIfUnused(host);
        return true;
    }

    void NativeUIService::CloseResource(const std::string &resource) {
        std::vector<std::uint32_t> ids;
        for (const auto &entry : _screens) {
            if (entry.second.resource == resource) {
                ids.push_back(entry.first);
            }
        }
        for (std::uint32_t id : ids) {
            CloseScreen(id);
        }
        if (!ids.empty()) {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->debug("[{}] Closed {} native screen(s) on resource stop", resource, ids.size());
        }
    }

    void NativeUIService::DropHostIfUnused(const std::string &host) {
        for (const auto &entry : _screens) {
            if (entry.second.host == host) {
                return;
            }
        }
        // The movie is the cost; the element registration stays behind either way.
        KCDC::Core::NativeUIModule::Get().CloseHost(host);
    }

    std::shared_ptr<KCDC::Core::NativeUI> NativeUIService::GetPage(std::uint32_t screen) const {
        const auto found = _screens.find(screen);
        return found != _screens.end() ? found->second.mounted.lock() : std::shared_ptr<KCDC::Core::NativeUI> {};
    }

    KCDC::Core::NativeUIHost *NativeUIService::GetHost(std::uint32_t screen) const {
        const auto found = _screens.find(screen);
        return found != _screens.end() ? KCDC::Core::NativeUIModule::Get().FindHost(found->second.host) : nullptr;
    }

    bool NativeUIService::IsMounted(std::uint32_t screen) const {
        return static_cast<bool>(GetPage(screen));
    }

    std::string NativeUIService::GetResource(std::uint32_t screen) const {
        const auto found = _screens.find(screen);
        return found != _screens.end() ? found->second.resource : std::string {};
    }

    std::uint32_t NativeUIService::FindScreen(const std::string &host, const std::string &page) const {
        for (const auto &entry : _screens) {
            if (entry.second.host == host && entry.second.page == page) {
                return entry.first;
            }
        }
        return 0;
    }

    bool NativeUIService::Focus(std::uint32_t screen) {
        const auto found = _screens.find(screen);
        if (found == _screens.end() || found->second.mounted.expired()) {
            return false;
        }
        return KCDC::Core::NativeUIModule::Get().AcquireFocus(found->second.host, found->second.page);
    }

    void NativeUIService::Blur(std::uint32_t screen) {
        if (HasFocus(screen)) {
            KCDC::Core::NativeUIModule::Get().ReleaseFocus();
        }
    }

    bool NativeUIService::HasFocus(std::uint32_t screen) const {
        const auto found = _screens.find(screen);
        if (found == _screens.end()) {
            return false;
        }
        const KCDC::Core::NativeUIModule &module = KCDC::Core::NativeUIModule::Get();
        return module.IsFocusRequested() && module.GetFocusHost() == found->second.host && module.GetFocusPage() == found->second.page;
    }

    bool NativeUIService::SetVisible(std::uint32_t screen, bool visible) {
        std::shared_ptr<KCDC::Core::NativeUI> page = GetPage(screen);
        if (!page) {
            return false;
        }
        // The subtree, not the movie: other screens share it.
        SDK::SFlashDisplayInfo info;
        info.SetVisible(visible);
        return page->SetDisplayInfo("$root", info);
    }

    std::string NativeUIService::NextKey(std::uint32_t screen) {
        const auto found = _screens.find(screen);
        if (found == _screens.end()) {
            return {};
        }
        return "k" + std::to_string(found->second.nextKey++);
    }

    void NativeUIService::AddSink(EventSink sink) {
        if (sink) {
            _sinks.emplace_back(std::move(sink));
        }
    }

    void NativeUIService::OnEvent(const KCDC::Core::NativeUIEvent &event) {
        // A host-wide event carries no page; one about a screen goes straight out.
        if (event.page.empty()) {
            switch (event.kind) {
            case KCDC::Core::NativeUIEventKind::Ready: {
                std::vector<std::uint32_t> waiting;
                for (const auto &entry : _screens) {
                    if (entry.second.host == event.host && entry.second.pending) {
                        waiting.push_back(entry.first);
                    }
                }
                for (std::uint32_t id : waiting) {
                    const auto found = _screens.find(id);
                    if (found != _screens.end()) {
                        Mount(id, found->second);
                    }
                }
                return;
            }
            case KCDC::Core::NativeUIEventKind::Unload: {
                // Collected first: a handler may close its screen.
                std::vector<std::pair<std::string, std::string>> lost;
                for (auto &entry : _screens) {
                    if (entry.second.host != event.host) {
                        continue;
                    }
                    entry.second.mounted.reset();
                    entry.second.pending = true;
                    lost.emplace_back(entry.second.host, entry.second.page);
                }
                for (const auto &entry : lost) {
                    KCDC::Core::NativeUIEvent unloaded;
                    unloaded.kind = KCDC::Core::NativeUIEventKind::Unload;
                    unloaded.host = entry.first;
                    unloaded.page = entry.second;
                    Publish(unloaded);
                }
                return;
            }
            default: break;
            }
        }
        Publish(event);
    }

    void NativeUIService::Publish(const KCDC::Core::NativeUIEvent &event) {
        for (const EventSink &sink : _sinks) {
            sink(event);
        }
    }
} // namespace KCDC::Features::UI
