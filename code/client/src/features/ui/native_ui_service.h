#pragma once

#include "core/feature_registry.h"
#include "core/ui/native_ui/native_ui_assets.h"
#include "core/ui/native_ui/native_ui_event.h"
#include "core/ui/native_ui/native_ui_host.h"

#include <function2/function2.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace KCDC::Features::UI {
    /**
     * Screens, and who owns them.
     *
     * A screen is one page composed inside one host movie, owned by the
     * resource that asked for it. Scripts get a number rather than either, so
     * a closed screen or an unloaded movie answers cleanly instead of through
     * a stale pointer. A screen asked for before its host reports ready is
     * recorded and mounted when that event arrives.
     */
    class NativeUIService final: public KCDC::Core::IFeatureService {
      public:
        using EventSink = fu2::function<void(const KCDC::Core::NativeUIEvent &) const>;

        /** Ticked from the shell every frame, session or not. */
        void Tick();
        /** Session teardown: every screen belongs to a session's resources. */
        void Reset() override;

        /** `library` is the game movie whose sprites it can attach, empty for the default host. */
        std::uint32_t OpenLibraryScreen(const std::string &resource, const std::string &library, std::string &error);

        /** A screen composed in a movie the resource itself ships. */
        std::uint32_t OpenMovieScreen(const std::string &resource, const std::string &moviePath, const std::vector<std::string> &assetPaths, int layer, std::string &error);

        bool CloseScreen(std::uint32_t screen);
        void CloseResource(const std::string &resource);

        /** The page behind a handle, or nothing once its movie went away. */
        std::shared_ptr<KCDC::Core::NativeUI> GetPage(std::uint32_t screen) const;
        KCDC::Core::NativeUIHost *GetHost(std::uint32_t screen) const;
        bool IsMounted(std::uint32_t screen) const;
        std::string GetResource(std::uint32_t screen) const;
        /** Screen a queued event belongs to, or zero when it belongs to none. */
        std::uint32_t FindScreen(const std::string &host, const std::string &page) const;

        bool Focus(std::uint32_t screen);
        void Blur(std::uint32_t screen);
        bool HasFocus(std::uint32_t screen) const;
        bool SetVisible(std::uint32_t screen, bool visible);

        /** A key no other handle in this screen uses. */
        std::string NextKey(std::uint32_t screen);

        /** Every sink sees every event, in the order they were raised. */
        void AddSink(EventSink sink);

      private:
        struct Screen {
            std::string resource;
            std::string host;
            std::string page;
            std::weak_ptr<KCDC::Core::NativeUI> mounted;
            std::uint32_t nextKey = 1;
            bool pending          = false;
        };

        std::uint32_t Open(const std::string &resource, const KCDC::Core::NativeUIAssets::Movie &movie, KCDC::Core::NativeUIHost::Kind kind, std::string &error);
        void Mount(std::uint32_t id, Screen &screen);
        void OnEvent(const KCDC::Core::NativeUIEvent &event);
        void Publish(const KCDC::Core::NativeUIEvent &event);
        void DropHostIfUnused(const std::string &host);

        std::unordered_map<std::uint32_t, Screen> _screens;
        std::vector<EventSink> _sinks;
        std::uint32_t _nextScreen = 1;
    };
} // namespace KCDC::Features::UI
