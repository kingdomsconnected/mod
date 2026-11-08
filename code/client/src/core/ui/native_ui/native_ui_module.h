#pragma once

#include "native_ui_assets.h"
#include "native_ui_host.h"

#include "sdk/cryengine/action/i_action_map_manager.h"

#include <function2/function2.hpp>

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace KCDC::Core {
    /**
     * The registry every native movie the mod drives is opened through: one
     * host per element, the archives they were mounted from, the single input
     * focus they compete for, and the queue their events leave through.
     */
    class NativeUIModule final
        : public SDK::IActionListener
        , public KCDC::Core::NativeUIEventQueue {
      public:
        static NativeUIModule &Get();

        /** Opens, or returns, the host for one movie, mounting its archive first. */
        NativeUIHost *OpenHost(const NativeUIAssets::Movie &movie, NativeUIHost::Kind kind, std::string &error);
        NativeUIHost *FindHost(const std::string &elementName);
        bool CloseHost(const std::string &elementName);
        /** Closes every host and releases the focus map. */
        void Shutdown();

        /** Reconciles focus against the rest of the shell. One call per frame. */
        void Update();

        /** Hands one page the focus map; the previous holder loses it. */
        bool AcquireFocus(const std::string &elementName, const std::string &pageName);
        void ReleaseFocus();
        bool IsFocusRequested() const {
            return _focusMap != nullptr;
        }
        const std::string &GetFocusHost() const {
            return _focusHostName;
        }
        const std::string &GetFocusPage() const {
            return _focusPageName;
        }

        void Queue(NativeUIEvent event) override;
        /** Hands every queued event to `sink`, oldest first, then forgets them. */
        void Drain(const fu2::function<void(const NativeUIEvent &) const> &sink);

        void OnAction(const SDK::CCryName &action, int activationMode, float value) override;
        void AfterAction() override {}

        const std::string &GetStatus() const {
            return _status;
        }
        std::vector<std::string> GetHostNames() const;

        /** Proves the focus map's shape without enabling it. Diagnostic only. */
        bool ValidateFocusMap();

        /**
         * Mounts one of the mod's archives into CryPak at the game folder,
         * once per path however many owners ask; repeat calls only count.
         * Also what makes a resource's book pictures reachable (see
         * NativeUIAssets::PackageBookImage).
         */
        bool MountArchive(const std::string &archivePath, std::string &error);

      private:
        NativeUIModule() = default;

        void ReleaseArchive(const std::string &archivePath);

        struct HostEntry {
            std::unique_ptr<NativeUIHost> host;
            std::string archivePath;
        };

        std::unordered_map<std::string, HostEntry> _hosts;
        std::unordered_map<std::string, unsigned int> _archives;
        std::vector<NativeUIEvent> _events;
        bool _draining = false;

        SDK::IActionMapManager *_focusManager = nullptr;
        SDK::IActionMap *_focusMap            = nullptr; // Identity only; resolve by name before dereferencing.
        std::string _focusHostName;
        std::string _focusPageName;
        bool _actionListenerAttached = false;
        std::string _status          = "Idle";
    };
} // namespace KCDC::Core
