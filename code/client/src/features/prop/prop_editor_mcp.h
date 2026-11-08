#pragma once

#include "prop_editor_objects.h"
#include "shared/features/world_builder/mcp_server.h"
#include <atomic>
#include <deque>
#include <map>
#include <mutex>
#include <optional>
#include <string>

namespace KCDC::Features::Prop {
    class MapEditor;
    struct EditorSnapshot;

    struct McpJob final {
        enum class State {
            Queued,
            Running,
            Completed,
            Failed,
            Cancelled
        };
        std::atomic<State> state = State::Queued;
        std::string name;
        nlohmann::json arguments;
        nlohmann::json result;
        std::uint64_t generation = 0;
    };

    // HTTP owns no engine state. Render dispatches authored work and queues
    // native jobs; the game's existing command queue executes those jobs.
    class EditorMcp final {
      public:
        void Availability(bool ready);
        bool Available() const {
            return _available.load(std::memory_order_acquire);
        }
        std::uint64_t Generation() const {
            return _generation.load(std::memory_order_acquire);
        }
        void ContextChanged();
        void Render(MapEditor &editor, const EditorSnapshot &snapshot);
        void RenderTab();
        void Shutdown();
        void ExecuteGame(MapEditor &editor, const std::shared_ptr<McpJob> &job);

      private:
        struct Operation {
            std::string id;
            std::string name;
            std::string key;
            std::string fingerprint;
            std::uint64_t serial = 0;
            std::uint64_t collisionSerial = 0;
            std::uint64_t generation = 0;
            std::shared_ptr<McpJob> job;
            nlohmann::json result;
            bool done   = false;
            bool failed = false;
        };
        void Configure();
        bool SaveSettings();
        void Start();
        void RefreshRevision(MapEditor &editor, const EditorSnapshot &snapshot);
        nlohmann::json State(const MapEditor &editor, const EditorSnapshot &snapshot) const;
        nlohmann::json Dispatch(MapEditor &editor, const EditorSnapshot &snapshot, const std::string &name, const nlohmann::json &arguments);
        nlohmann::json OperationResult(const Operation &operation) const;
        std::optional<nlohmann::json> RetainedOperationResult(const std::string &name, const nlohmann::json &arguments) const;
        nlohmann::json Native(MapEditor &editor, const std::string &name, const nlohmann::json &arguments);
        static nlohmann::json NativeTools();
        void Log(const std::string &name, const std::string &state);

        Shared::WorldBuilder::McpServer _server;
        std::mutex _lifecycle;
        std::atomic_bool _available            = false;
        std::atomic<std::uint64_t> _generation = 1;
        bool _configured                       = false;
        bool _wasAvailable                     = false;
        bool _enabled                          = false;
        int _port                              = 7781;
        std::string _error;
        std::string _instance;
        nlohmann::json _tools;
        nlohmann::json _lastDocument;
        std::uint64_t _revision      = 1;
        std::uint64_t _nextOperation = 0;
        std::map<std::string, Operation> _operations;
        std::deque<nlohmann::json> _logs;
        // Game-thread owned, independent of the UI's level browser rows.
        std::uint64_t _browseSerial = 0;
        std::vector<LevelRow> _browseRows;
        std::vector<std::uint64_t> _browseEntityGuids;
    };
} // namespace KCDC::Features::Prop
