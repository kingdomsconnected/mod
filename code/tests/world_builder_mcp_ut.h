#pragma once

#ifdef _WIN32
#include "shared/features/world_builder/mcp_file_policy.h"
#endif
#include "shared/features/world_builder/mcp_server.h"

#include <httplib.h>

#include <future>
#include <cstdio>
#include <thread>

namespace WorldBuilderMcpTests {
    inline bool Check(bool condition, const std::string &message) {
        if (!condition) {
            std::fprintf(stderr, "MCP test: %s\n", message.c_str());
        }
        return condition;
    }
    inline nlohmann::json Message(const std::string &method, nlohmann::json params = nlohmann::json::object(), bool modern = false) {
        if (modern) {
            params["_meta"] = {{"io.modelcontextprotocol/protocolVersion", "2026-07-28"}, {"io.modelcontextprotocol/clientCapabilities", nlohmann::json::object()}};
        }
        return {{"jsonrpc", "2.0"}, {"id", 1}, {"method", method}, {"params", std::move(params)}};
    }

    inline nlohmann::json Tool(const std::string &name = "edit") {
        return {{"name", name}, {"description", "Test editor dispatch"}, {"inputSchema", {{"type", "object"}, {"properties", {{"count", {{"type", "integer"}, {"minimum", 1}, {"maximum", 10}}}}}, {"required", {"count"}}, {"additionalProperties", false}}}};
    }

    inline std::shared_ptr<KCDC::Shared::WorldBuilder::McpServer::Request> AwaitRequest(KCDC::Shared::WorldBuilder::McpServer &server) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (std::chrono::steady_clock::now() < deadline) {
            if (auto request = server.Poll()) {
                return request;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return {};
    }

    struct Listener {
        KCDC::Shared::WorldBuilder::McpServer server;
        KCDC::Shared::WorldBuilder::McpServer::Config config;
        std::string error;
        bool started = false;
        Listener() {
            config.requestTimeout = std::chrono::milliseconds(50);
            for (std::uint16_t port = 47860; port < 47880 && !started; ++port) {
                config.port = port;
                started     = server.Start(config, error);
            }
        }
        httplib::Headers Headers(bool modern = false, const std::string &method = "ping") const {
            httplib::Headers headers {{"Accept", "application/json, text/event-stream"}, {"MCP-Protocol-Version", modern ? "2026-07-28" : "2025-11-25"}};
            if (modern) {
                headers.emplace("Mcp-Method", method);
            }
            return headers;
        }
    };

    struct RawSocket {
        socket_t value = INVALID_SOCKET;
        explicit RawSocket(std::uint16_t port) {
            value = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            sockaddr_in address {};
            address.sin_family      = AF_INET;
            address.sin_port        = htons(port);
            address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            if (::connect(value, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0) {
                Close();
            }
        }
        ~RawSocket() {
            Close();
        }
        void Close() {
            if (value == INVALID_SOCKET) {
                return;
            }
#ifdef _WIN32
            ::closesocket(value);
#else
            ::close(value);
#endif
            value = INVALID_SOCKET;
        }
    };
} // namespace WorldBuilderMcpTests

#ifdef _WIN32
MODULE(world_builder_mcp_files, {
    IT("confines file paths to the two trusted roots and rejects Windows alternate streams and device names", {
        const auto maps       = std::filesystem::current_path() / "mcp-test-maps";
        const auto exports    = std::filesystem::current_path() / "mcp-test-exports";
        const auto mapsText   = KCDC::Shared::WorldBuilder::McpFilePolicy::Utf8(maps);
        const auto exportText = KCDC::Shared::WorldBuilder::McpFilePolicy::Utf8(exports);
        const auto local = KCDC::Shared::WorldBuilder::McpFilePolicy::Resolve("nested/file.project.json", mapsText, exportText);
        const auto expectedLocal = KCDC::Shared::WorldBuilder::McpFilePolicy::Canonical(maps / "nested/file.project.json");
        EQUALS(WorldBuilderMcpTests::Check(std::filesystem::u8path(local) == expectedLocal, "Relative path resolved to " + local + "; expected " + KCDC::Shared::WorldBuilder::McpFilePolicy::Utf8(expectedLocal)), true);
        const auto exported = KCDC::Shared::WorldBuilder::McpFilePolicy::Resolve(KCDC::Shared::WorldBuilder::McpFilePolicy::Utf8(exports / "world.world.json"), mapsText, exportText);
        const auto expectedExport = KCDC::Shared::WorldBuilder::McpFilePolicy::Canonical(exports / "world.world.json");
        EQUALS(WorldBuilderMcpTests::Check(std::filesystem::u8path(exported) == expectedExport, "Export path resolved to " + exported + "; expected " + KCDC::Shared::WorldBuilder::McpFilePolicy::Utf8(expectedExport)), true);
        unsigned refused = 0;
        for (const auto &path : {"../mcp-test-maps-sibling/escape.project.json", "a.project.json:secret", "CON.project.json", "LPT1.json", "directory./file.json", "C:relative.project.json", "\\\\server\\share\\file.json"}) {
            try {
                KCDC::Shared::WorldBuilder::McpFilePolicy::Resolve(path, mapsText, exportText);
                WorldBuilderMcpTests::Check(false, std::string("Accepted forbidden path: ") + path);
            }
            catch (const std::runtime_error &) {
                ++refused;
            }
        }
        EQUALS(refused, 7U);
    });
});
#endif

MODULE(world_builder_mcp, {
    IT("supports legacy initialization and modern stateless discovery without claiming subscriptions", {
        KCDC::Shared::WorldBuilder::McpServer server;
        const auto legacy = server.Handle(WorldBuilderMcpTests::Message("initialize", {{"protocolVersion", "2025-11-25"}, {"capabilities", nlohmann::json::object()}, {"clientInfo", {{"name", "test"}, {"version", "1"}}}}));
        EQUALS(legacy.at("result").at("protocolVersion") == "2025-11-25", true);
        EQUALS(legacy.at("result").contains("resultType"), false);
        const auto modern = server.Handle(WorldBuilderMcpTests::Message("server/discover", nlohmann::json::object(), true), "2026-07-28");
        EQUALS(modern.at("result").at("resultType") == "complete", true);
        EQUALS(modern.at("result").at("supportedVersions").size(), 2U);
        EQUALS(modern.at("result").at("ttlMs") == 0, true);
        EQUALS(modern.at("result").at("cacheScope") == "private", true);
        EQUALS(modern.at("result").at("capabilities").at("resources").at("subscribe") == false, true);
        EQUALS(modern.at("result").at("_meta").contains("io.modelcontextprotocol/serverInfo"), true);
    });

    IT("rejects missing modern metadata, unsupported versions, batches and invalid IDs", {
        KCDC::Shared::WorldBuilder::McpServer server;
        EQUALS(server.Handle(WorldBuilderMcpTests::Message("ping"), "2026-07-28").at("error").at("code") == -32602, true);
        const auto unsupported = server.Handle(WorldBuilderMcpTests::Message("ping"), "1900-01-01");
        EQUALS(unsupported.at("error").at("code") == -32022, true);
        EQUALS(unsupported.at("error").at("data").at("supported").size(), 2U);
        EQUALS(server.Handle(nlohmann::json::array({WorldBuilderMcpTests::Message("ping")})).at("error").at("code") == -32600, true);
        auto invalid  = WorldBuilderMcpTests::Message("ping");
        invalid["id"] = nullptr;
        EQUALS(server.Handle(invalid).at("error").at("code") == -32600, true);
        invalid["id"] = 1.5;
        EQUALS(server.Handle(invalid).at("error").at("code") == -32600, true);
        EQUALS(server.Handle(WorldBuilderMcpTests::Message("no/such/method")).at("error").at("code") == -32601, true);
    });

    IT("sorts and pages catalogs and rejects stale cursors", {
        KCDC::Shared::WorldBuilder::McpServer server;
        auto tools = nlohmann::json::array();
        for (unsigned index = 0; index < 70; ++index) {
            tools.push_back(WorldBuilderMcpTests::Tool("tool_" + std::to_string(100 - index)));
        }
        server.SetTools(tools);
        const auto first = server.Handle(WorldBuilderMcpTests::Message("tools/list")).at("result");
        EQUALS(first.at("tools").size(), 64U);
        EQUALS(first.at("tools").at(0).at("name") == "tool_100", true);
        EQUALS(first.at("tools").at(0).at("outputSchema").at("type") == "object", true);
        EQUALS(first.at("tools").at(0).at("annotations").at("openWorldHint") == false, true);
        const auto next = WorldBuilderMcpTests::Message("tools/list", {{"cursor", first.at("nextCursor")}});
        EQUALS(server.Handle(next).at("result").at("tools").size(), 6U);
        server.SetTools(tools);
        EQUALS(server.Handle(next).at("error").at("code") == -32602, true);
    });

    IT("validates nested enum, anyOf, integer, bounds and additional properties before dispatch", {
        std::string error;
        nlohmann::json schema = {{"type", "object"},
            {"properties", {{"points", {{"type", "array"}, {"minItems", 1}, {"maxItems", 2}, {"items", {{"anyOf", nlohmann::json::array({{{"type", "integer"}, {"minimum", 1}, {"maximum", 3}}, {{"type", "string"}, {"enum", {"auto"}}}})}}}}}}}, {"required", {"points"}},
            {"additionalProperties", false}};
        EQUALS(KCDC::Shared::WorldBuilder::McpServer::ValidateArguments(schema, {{"points", {1, "auto"}}}, error), true);
        EQUALS(KCDC::Shared::WorldBuilder::McpServer::ValidateArguments(schema, {{"points", {1.5}}}, error), false);
        EQUALS(KCDC::Shared::WorldBuilder::McpServer::ValidateArguments(schema, {{"points", {4}}}, error), false);
        EQUALS(KCDC::Shared::WorldBuilder::McpServer::ValidateArguments(schema, {{"points", {"bad"}}}, error), false);
        EQUALS(KCDC::Shared::WorldBuilder::McpServer::ValidateArguments(schema, {{"points", {1, 2, 3}}}, error), false);
        EQUALS(KCDC::Shared::WorldBuilder::McpServer::ValidateArguments(schema, {{"points", {1}}, {"extra", true}}, error), false);
        KCDC::Shared::WorldBuilder::McpServer server;
        server.SetTools(nlohmann::json::array({WorldBuilderMcpTests::Tool()}));
        EQUALS(server.Handle(WorldBuilderMcpTests::Message("tools/call", {{"name", "edit"}, {"arguments", {{"count", 11}}}})).at("result").at("isError") == true, true);
        EQUALS(server.Poll() == nullptr, true);
    });

    IT("bridges commands to the owner thread and carries image and structured results", {
        KCDC::Shared::WorldBuilder::McpServer server;
        server.SetTools(nlohmann::json::array({WorldBuilderMcpTests::Tool()}));
        auto response      = std::async(std::launch::async, [&] {
            return server.Handle(WorldBuilderMcpTests::Message("tools/call", {{"name", "edit"}, {"arguments", {{"count", 2}}}}));
        });
        const auto request = WorldBuilderMcpTests::AwaitRequest(server);
        EQUALS(request != nullptr, true);
        EQUALS(request->method == "tools/call", true);
        EQUALS(request->name == "edit", true);
        EQUALS(request->arguments.at("count") == 2, true);
        EQUALS(server.Poll() == nullptr, true);
        request->Complete({{"operationId", "op-1"}}, false, nlohmann::json::array({{{"type", "image"}, {"mimeType", "image/png"}, {"data", "aGVsbG8="}}}));
        const auto result = response.get().at("result");
        EQUALS(result.at("isError") == false, true);
        EQUALS(result.at("structuredContent").at("operationId") == "op-1", true);
        EQUALS(result.at("content").at(0).at("type") == "image", true);
    });

    IT("reports editor failures as tool errors and rejects malformed successful output", {
        KCDC::Shared::WorldBuilder::McpServer server;
        auto tool            = WorldBuilderMcpTests::Tool();
        tool["outputSchema"] = {{"type", "object"}, {"required", {"operationId"}}};
        server.SetTools(nlohmann::json::array({tool}));
        auto response      = std::async(std::launch::async, [&] {
            return server.Handle(WorldBuilderMcpTests::Message("tools/call", {{"name", "edit"}, {"arguments", {{"count", 2}}}}));
        });
        const auto request = WorldBuilderMcpTests::AwaitRequest(server);
        EQUALS(request != nullptr, true);
        request->Complete({{"wrongField", 5}});
        const auto result = response.get();
        EQUALS(result.contains("error"), false);
        EQUALS(result.at("result").at("isError") == true, true);
        EQUALS(result.at("result").contains("structuredContent"), false);
    });

    IT("reads only registered snapshot resources and adds modern cache hints", {
        KCDC::Shared::WorldBuilder::McpServer server;
        server.SetResources(nlohmann::json::array({{{"uri", "wb://state"}, {"name", "State"}}}));
        server.SetResource("wb://state", {{"revision", 12}});
        const auto result = server.Handle(WorldBuilderMcpTests::Message("resources/read", {{"uri", "wb://state"}}, true), "2026-07-28").at("result");
        EQUALS(result.at("ttlMs") == 0, true);
        EQUALS(nlohmann::json::parse(result.at("contents").at(0).at("text").get<std::string>()).at("revision") == 12, true);
        EQUALS(server.Handle(WorldBuilderMcpTests::Message("resources/read", {{"uri", "file:///private"}})).at("error").at("code") == -32002, true);
        EQUALS(server.Poll() == nullptr, true);
    });

    IT("does not execute notification-shaped tool calls and resolves claimed requests on stop", {
        KCDC::Shared::WorldBuilder::McpServer server;
        server.SetTools(nlohmann::json::array({WorldBuilderMcpTests::Tool()}));
        auto notification = WorldBuilderMcpTests::Message("tools/call", {{"name", "edit"}, {"arguments", {{"count", 1}}}});
        notification.erase("id");
        EQUALS(server.Handle(notification).is_null(), true);
        EQUALS(server.Poll() == nullptr, true);
        auto response      = std::async(std::launch::async, [&] {
            return server.Handle(WorldBuilderMcpTests::Message("tools/call", {{"name", "edit"}, {"arguments", {{"count", 2}}}}));
        });
        const auto request = WorldBuilderMcpTests::AwaitRequest(server);
        EQUALS(request != nullptr, true);
        server.Stop();
        EQUALS(response.wait_for(std::chrono::milliseconds(100)) == std::future_status::ready, true);
        EQUALS(response.get().at("result").at("isError") == true, true);
        request->Complete({{"ignored", true}});
        EQUALS(server.Poll() == nullptr, true);
    });
});

// The host registers this as a separate --suite process because these cases
// bind a real loopback socket. Every fixture owns and stops its own listener.
MODULE(world_builder_mcp_http, {
    IT("stops while a peer leaves its HTTP headers incomplete", {
        WorldBuilderMcpTests::Listener listener;
        EQUALS(WorldBuilderMcpTests::Check(listener.started, "Listener failed: " + listener.error), true);
        WorldBuilderMcpTests::RawSocket peer(listener.config.port);
        EQUALS(WorldBuilderMcpTests::Check(peer.value != INVALID_SOCKET, "Could not connect incomplete-header peer"), true);
        const std::string partial = "POST /mcp HTTP/1.1\r\nHost: ";
        EQUALS(WorldBuilderMcpTests::Check(::send(peer.value, partial.data(), static_cast<int>(partial.size()), 0) == static_cast<int>(partial.size()), "Could not send complete header fragment"), true);
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        const auto before = std::chrono::steady_clock::now();
        listener.server.Stop();
        const auto elapsed = std::chrono::steady_clock::now() - before;
        EQUALS(WorldBuilderMcpTests::Check(elapsed < std::chrono::seconds(1), "Stop took " + std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count()) + "ms with an incomplete header"), true);
        EQUALS(listener.server.IsRunning(), false);
    });

    IT("refuses port collisions without disturbing the listener that owns the port", {
        WorldBuilderMcpTests::Listener listener;
        EQUALS(listener.started, true);
        KCDC::Shared::WorldBuilder::McpServer other;
        std::string error;
        EQUALS(other.Start(listener.config, error), false);
        EQUALS(error.empty(), false);
        EQUALS(listener.server.IsRunning(), true);
        EQUALS(other.IsRunning(), false);
    });

    IT("needs no authorization and rejects untrusted Host and Origin on every route", {
        WorldBuilderMcpTests::Listener listener;
        EQUALS(listener.started, true);
        httplib::Client client("127.0.0.1", listener.config.port);
        client.set_read_timeout(3);
        const auto bare = client.Get("/mcp");
        EQUALS(static_cast<bool>(bare), true);
        EQUALS(bare->status, 405);
        auto headers = listener.Headers();
        headers.emplace("Origin", "https://example.com");
        const auto origin = client.Get("/mcp", headers);
        EQUALS(static_cast<bool>(origin), true);
        EQUALS(origin->status, 403);
        headers = listener.Headers();
        headers.emplace("Host", "example.com:" + std::to_string(listener.config.port));
        const auto host = client.Get("/mcp", headers);
        EQUALS(static_cast<bool>(host), true);
        EQUALS(host->status, 403);
        const auto allowed = client.Get("/mcp", listener.Headers());
        EQUALS(static_cast<bool>(allowed), true);
        EQUALS(allowed->status, 405);
    });

    IT("validates modern mirrored headers and returns typed protocol errors and accepted notifications", {
        WorldBuilderMcpTests::Listener listener;
        EQUALS(listener.started, true);
        httplib::Client client("127.0.0.1", listener.config.port);
        client.set_read_timeout(3);
        const auto message = WorldBuilderMcpTests::Message("server/discover", nlohmann::json::object(), true).dump();
        const auto valid   = client.Post("/mcp", listener.Headers(true, "server/discover"), message, "application/json");
        EQUALS(static_cast<bool>(valid), true);
        EQUALS(valid->status, 200);
        EQUALS(nlohmann::json::parse(valid->body).at("result").at("resultType") == "complete", true);
        const auto mismatch = client.Post("/mcp", listener.Headers(true, "ping"), message, "application/json");
        EQUALS(static_cast<bool>(mismatch), true);
        EQUALS(mismatch->status, 400);
        EQUALS(nlohmann::json::parse(mismatch->body).at("error").at("code") == -32020, true);
        listener.server.SetResources(nlohmann::json::array({{{"uri", "wb://state"}, {"name", "State"}}}));
        listener.server.SetResource("wb://state", {{"revision", 1}});
        auto encodedHeaders = listener.Headers(true, "resources/read");
        encodedHeaders.emplace("Mcp-Name", "=?base64?d2I6Ly9zdGF0ZQ==?=");
        const auto encoded = client.Post("/mcp", encodedHeaders, WorldBuilderMcpTests::Message("resources/read", {{"uri", "wb://state"}}, true).dump(), "application/json");
        EQUALS(static_cast<bool>(encoded), true);
        EQUALS(encoded->status, 200);
        const auto unknown = client.Post("/mcp", listener.Headers(true, "unknown"), WorldBuilderMcpTests::Message("unknown", nlohmann::json::object(), true).dump(), "application/json");
        EQUALS(static_cast<bool>(unknown), true);
        EQUALS(unknown->status, 404);
        auto notification = WorldBuilderMcpTests::Message("notifications/initialized");
        notification.erase("id");
        const auto accepted = client.Post("/mcp", listener.Headers(), notification.dump(), "application/json");
        EQUALS(static_cast<bool>(accepted), true);
        EQUALS(accepted->status, 202);
        EQUALS(accepted->body.empty(), true);
    });

    IT("limits request bodies, cancels unclaimed timed-out edits and releases its listening port", {
        WorldBuilderMcpTests::Listener listener;
        EQUALS(listener.started, true);
        listener.server.SetTools(nlohmann::json::array({WorldBuilderMcpTests::Tool()}));
        httplib::Client client("127.0.0.1", listener.config.port);
        client.set_read_timeout(3);
        auto oversizedHeaders = listener.Headers();
        oversizedHeaders.emplace("Content-Length", "1048577");
        const auto oversized = client.Post("/mcp", oversizedHeaders, "", "application/json");
        EQUALS(static_cast<bool>(oversized), true);
        EQUALS(oversized->status, 413);
        const auto timeout = client.Post("/mcp", listener.Headers(), WorldBuilderMcpTests::Message("tools/call", {{"name", "edit"}, {"arguments", {{"count", 1}}}}).dump(), "application/json");
        EQUALS(static_cast<bool>(timeout), true);
        const auto result = nlohmann::json::parse(timeout->body).at("result");
        EQUALS(result.at("isError") == true, true);
        EQUALS(nlohmann::json::parse(result.at("content").at(0).at("text").get<std::string>()).at("mayHaveExecuted") == false, true);
        EQUALS(listener.server.Poll() == nullptr, true);
        listener.server.Stop();
        EQUALS(listener.server.IsRunning(), false);
        std::string error;
        EQUALS(listener.server.Start(listener.config, error), true);
        listener.server.Stop();
    });
});
