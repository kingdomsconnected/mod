#pragma once

#include <nlohmann/json.hpp>

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

namespace KCDC::Shared::WorldBuilder {
    // Owns only protocol state. Poll and Complete bridge HTTP workers to the
    // editor thread; the transport never calls the engine or editor directly.
    class McpServer final {
      public:
        struct Config {
            std::uint16_t port = 0;
            std::chrono::milliseconds requestTimeout {3000};
        };

        class Request final {
          public:
            const std::string method;
            const std::string name;
            const nlohmann::json arguments;

            // Structured data must be an object. Optional content holds MCP
            // text/image blocks (for example an editor screenshot).
            void Complete(nlohmann::json structured, bool isError = false, nlohmann::json content = nlohmann::json::array());

          private:
            friend class McpServer;
            Request(std::string method, std::string name, nlohmann::json arguments, nlohmann::json id, std::chrono::milliseconds timeout);
            const nlohmann::json _id;
            std::mutex _mutex;
            std::condition_variable _ready;
            std::chrono::steady_clock::time_point _deadline;
            bool _claimed = false;
            bool _done    = false;
            bool _isError = false;
            nlohmann::json _structured;
            nlohmann::json _content;
        };

        McpServer();
        ~McpServer();
        McpServer(const McpServer &)            = delete;
        McpServer &operator=(const McpServer &) = delete;

        // Lifecycle calls belong to one owner thread. Stop wakes outstanding
        // requests before joining workers and releases the listening port.
        bool Start(const Config &config, std::string &error);
        void Stop();
        bool IsRunning() const;

        void SetTools(nlohmann::json tools);
        void SetResources(nlohmann::json resources);
        void SetResource(const std::string &uri, nlohmann::json value);
        std::shared_ptr<Request> Poll();

        // In-process protocol entry point for regression tests. Host/Origin and
        // HTTP mirrored-header checks are performed by the HTTP adapter.
        nlohmann::json Handle(const nlohmann::json &message, const std::string &protocolVersion = "2025-11-25");
        static bool ValidateArguments(const nlohmann::json &schema, const nlohmann::json &arguments, std::string &error);

      private:
        struct State;
        std::unique_ptr<State> _state;
        nlohmann::json Dispatch(const std::string &method, const std::string &name, const nlohmann::json &arguments, const nlohmann::json &id, bool &isError, nlohmann::json &content);
    };
} // namespace KCDC::Shared::WorldBuilder
