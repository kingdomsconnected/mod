#include "mcp_server.h"

#include <function2/function2.hpp>
#include <httplib.h>
#include <openssl/evp.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <deque>
#include <list>
#include <map>
#include <set>
#include <stdexcept>
#include <thread>
#include <vector>

namespace KCDC::Shared::WorldBuilder {
    namespace {
        constexpr const char *kModern         = "2026-07-28";
        constexpr const char *kLegacy         = "2025-11-25";
        constexpr std::size_t kMaxBody        = 1024 * 1024;
        constexpr std::size_t kMaxPending     = 64;
        constexpr unsigned kMaxCallsPerSecond = 128;
        constexpr std::size_t kPageSize       = 64;

        nlohmann::json Error(const nlohmann::json &id, int code, const std::string &message) {
            return {{"jsonrpc", "2.0"}, {"id", id}, {"error", {{"code", code}, {"message", message}}}};
        }

        nlohmann::json Info() {
            return {{"name", "kcdc-world-builder"}, {"title", "Kingdoms Connected World Builder"}, {"version", "1.0.0"}};
        }

        nlohmann::json Capabilities() {
            return {{"tools", {{"listChanged", false}}}, {"resources", {{"subscribe", false}, {"listChanged", false}}}};
        }

        bool ValidType(const nlohmann::json &value, const std::string &type) {
            if (type == "object") {
                return value.is_object();
            }
            if (type == "array") {
                return value.is_array();
            }
            if (type == "string") {
                return value.is_string();
            }
            if (type == "boolean") {
                return value.is_boolean();
            }
            if (type == "null") {
                return value.is_null();
            }
            if (type == "number") {
                return value.is_number() && std::isfinite(value.get<double>());
            }
            if (type == "integer") {
                return value.is_number_integer() || (value.is_number_float() && std::isfinite(value.get<double>()) && std::floor(value.get<double>()) == value.get<double>());
            }
            return false;
        }

        bool Validate(const nlohmann::json &schema, const nlohmann::json &value, std::string &error, const std::string &path, unsigned depth, std::size_t &budget) {
            auto fail = [&](const std::string &reason) {
                error = path + ": " + reason;
                return false;
            };
            if (depth > 48 || budget == 0) {
                return fail("validation complexity limit exceeded");
            }
            --budget;
            if (schema.is_boolean()) {
                return schema.get<bool>() || fail("value is disallowed");
            }
            if (!schema.is_object()) {
                return fail("invalid schema");
            }
            if (schema.contains("anyOf")) {
                bool matched = false;
                for (const auto &choice : schema.at("anyOf")) {
                    std::string ignored;
                    if (Validate(choice, value, ignored, path, depth + 1, budget)) {
                        matched = true;
                        break;
                    }
                }
                if (!matched) {
                    return fail("does not match any allowed shape");
                }
            }
            if (schema.contains("type")) {
                const auto &type = schema.at("type");
                bool valid       = type.is_string() && ValidType(value, type.get<std::string>());
                if (type.is_array()) {
                    for (const auto &entry : type) {
                        valid = valid || (entry.is_string() && ValidType(value, entry.get<std::string>()));
                    }
                }
                if (!valid) {
                    return fail("incorrect value type");
                }
            }
            if (schema.contains("enum") && std::find(schema.at("enum").begin(), schema.at("enum").end(), value) == schema.at("enum").end()) {
                return fail("value is outside the enum");
            }
            if (schema.contains("const") && value != schema.at("const")) {
                return fail("value does not match const");
            }
            if (value.is_number()) {
                const double number = value.get<double>();
                if (!std::isfinite(number)) {
                    return fail("number is not finite");
                }
                if (schema.contains("minimum") && number < schema.at("minimum").get<double>()) {
                    return fail("below minimum");
                }
                if (schema.contains("maximum") && number > schema.at("maximum").get<double>()) {
                    return fail("above maximum");
                }
                if (schema.contains("exclusiveMinimum") && number <= schema.at("exclusiveMinimum").get<double>()) {
                    return fail("below exclusive minimum");
                }
                if (schema.contains("exclusiveMaximum") && number >= schema.at("exclusiveMaximum").get<double>()) {
                    return fail("above exclusive maximum");
                }
            }
            if (value.is_string()) {
                const auto &text         = value.get_ref<const std::string &>();
                const std::size_t length = std::count_if(text.begin(), text.end(), [](unsigned char c) {
                    return (c & 0xC0) != 0x80;
                });
                if (schema.contains("minLength") && length < schema.at("minLength").get<std::size_t>()) {
                    return fail("string is too short");
                }
                if (schema.contains("maxLength") && length > schema.at("maxLength").get<std::size_t>()) {
                    return fail("string is too long");
                }
            }
            if (value.is_array()) {
                if (schema.contains("minItems") && value.size() < schema.at("minItems").get<std::size_t>()) {
                    return fail("array is too short");
                }
                if (schema.contains("maxItems") && value.size() > schema.at("maxItems").get<std::size_t>()) {
                    return fail("array is too long");
                }
                for (std::size_t index = 0; index < value.size(); ++index) {
                    if (schema.contains("items") && !Validate(schema.at("items"), value.at(index), error, path + "[" + std::to_string(index) + "]", depth + 1, budget)) {
                        return false;
                    }
                }
            }
            if (value.is_object()) {
                if (schema.contains("required")) {
                    for (const auto &key : schema.at("required")) {
                        if (!value.contains(key.get<std::string>())) {
                            return fail("missing required property " + key.get<std::string>());
                        }
                    }
                }
                const auto properties = schema.value("properties", nlohmann::json::object());
                for (auto entry = value.begin(); entry != value.end(); ++entry) {
                    if (properties.contains(entry.key())) {
                        if (!Validate(properties.at(entry.key()), entry.value(), error, path + "." + entry.key(), depth + 1, budget)) {
                            return false;
                        }
                    }
                    else if (schema.contains("additionalProperties") && !Validate(schema.at("additionalProperties"), entry.value(), error, path + "." + entry.key(), depth + 1, budget)) {
                        return false;
                    }
                }
            }
            return true;
        }

        // Schemas are authored in this binary, never supplied by an MCP peer.
        // Refuse unsupported assertion keywords instead of silently ignoring
        // an intended constraint. No schema can fetch a remote reference.
        void CheckSchema(const nlohmann::json &schema, unsigned depth = 0) {
            if (depth > 48) {
                throw std::invalid_argument("MCP schema nesting limit exceeded");
            }
            if (schema.is_boolean()) {
                return;
            }
            if (!schema.is_object()) {
                throw std::invalid_argument("MCP schema must be an object or boolean");
            }
            for (auto entry = schema.begin(); entry != schema.end(); ++entry) {
                const auto &key = entry.key();
                if (key == "properties") {
                    for (const auto &child : entry.value()) {
                        CheckSchema(child, depth + 1);
                    }
                }
                else if (key == "items" || key == "additionalProperties") {
                    CheckSchema(entry.value(), depth + 1);
                }
                else if (key == "anyOf") {
                    for (const auto &child : entry.value()) {
                        CheckSchema(child, depth + 1);
                    }
                }
                else if (key == "$schema") {
                    if (entry.value() != "https://json-schema.org/draft/2020-12/schema") {
                        throw std::invalid_argument("Unsupported MCP schema dialect");
                    }
                }
                else if (key != "type" && key != "enum" && key != "const" && key != "required" && key != "minimum" && key != "maximum" && key != "exclusiveMinimum" && key != "exclusiveMaximum" && key != "minLength" && key != "maxLength" && key != "minItems" && key != "maxItems"
                         && key != "description" && key != "title" && key != "default" && key != "examples") {
                    throw std::invalid_argument("Unsupported MCP schema keyword: " + key);
                }
            }
        }

        bool HeaderNameMatches(const std::string &header, const std::string &body) {
            if (header.size() >= 11 && header.compare(0, 9, "=?base64?") == 0 && header.compare(header.size() - 2, 2, "?=") == 0) {
                std::string encoded(4 * ((body.size() + 2) / 3) + 1, '\0');
                const int length = EVP_EncodeBlock(reinterpret_cast<unsigned char *>(encoded.data()), reinterpret_cast<const unsigned char *>(body.data()), static_cast<int>(body.size()));
                if (length < 0) {
                    return false;
                }
                encoded.resize(static_cast<std::size_t>(length));
                return encoded == header.substr(9, header.size() - 11);
            }
            return header == body;
        }

        // This httplib revision otherwise has an unbounded socket work queue.
        // Back pressure keeps at most four active and eight queued sockets.
        // std::function here is required by the existing vendor virtual ABI;
        // no application lambda is converted to it.
        class HttpQueue final: public httplib::TaskQueue {
          public:
            HttpQueue() {
                for (unsigned index = 0; index < 4; ++index) {
                    _workers.emplace_back([this] {
                        Run();
                    });
                }
            }
            bool enqueue(std::function<void()> task) override {
                std::unique_lock lock(_mutex);
                _changed.wait(lock, [this] {
                    return _queue.size() < 8;
                });
                _queue.emplace_back(std::move(task));
                _changed.notify_all();
                return true;
            }
            void shutdown() override {
                {
                    std::lock_guard lock(_mutex);
                    _stopping = true;
                }
                _changed.notify_all();
                for (auto &worker : _workers) {
                    worker.join();
                }
            }

          private:
            void Run() {
                for (;;) {
                    fu2::function<void() const> task;
                    {
                        std::unique_lock lock(_mutex);
                        _changed.wait(lock, [this] {
                            return _stopping || !_queue.empty();
                        });
                        if (_queue.empty()) {
                            return;
                        }
                        task = std::move(_queue.front());
                        _queue.pop_front();
                    }
                    _changed.notify_all();
                    task();
                }
            }
            std::mutex _mutex;
            std::condition_variable _changed;
            std::deque<fu2::function<void() const>> _queue;
            std::vector<std::thread> _workers;
            bool _stopping = false;
        };

        httplib::TaskQueue *NewHttpQueue() {
            return new HttpQueue();
        }

        void ExclusiveSocketOptions(socket_t socket) {
#ifdef _WIN32
            // The vendored default combines REUSEADDR and EXCLUSIVEADDRUSE;
            // Windows rejects that combination. Never share an editor port.
            const int enabled = 1;
            setsockopt(socket, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char *>(&enabled), sizeof(enabled));
#else
            httplib::default_socket_options(socket);
#endif
        }

        void ShutdownSocket(socket_t socket) {
#ifdef _WIN32
            ::shutdown(socket, SD_BOTH);
#else
            ::shutdown(socket, SHUT_RDWR);
#endif
        }

        void CloseSocket(socket_t socket) {
#ifdef _WIN32
            ::closesocket(socket);
#else
            ::close(socket);
#endif
        }

        // The vendored Stream timeout resets after every read. An absolute
        // deadline also bounds clients that continually trickle header bytes.
        class DeadlineStream final: public httplib::Stream {
          public:
            DeadlineStream(socket_t socket, const std::atomic<bool> &stopping): _socket(socket), _stopping(stopping), _start(std::chrono::steady_clock::now()), _deadline(_start + std::chrono::seconds(10)) {
#ifdef _WIN32
                u_long enabled = 1;
                ioctlsocket(socket, FIONBIO, &enabled);
#else
                fcntl(socket, F_SETFL, fcntl(socket, F_GETFL, 0) | O_NONBLOCK);
#endif
            }
            bool is_readable() const override {
                return Wait(false);
            }
            bool wait_readable() const override {
                return Wait(false);
            }
            bool wait_writable() const override {
                return Wait(true);
            }
            time_t duration() const override {
                return static_cast<time_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - _start).count());
            }
            ssize_t read(char *data, std::size_t size) override {
                if (!Wait(false)) {
                    return -1;
                }
                return ::recv(_socket, data, static_cast<int>(std::min<std::size_t>(size, 65536)), 0);
            }
            ssize_t write(const char *data, std::size_t size) override {
                if (!Wait(true)) {
                    return -1;
                }
#ifdef MSG_NOSIGNAL
                return ::send(_socket, data, static_cast<int>(std::min<std::size_t>(size, 65536)), MSG_NOSIGNAL);
#else
                return ::send(_socket, data, static_cast<int>(std::min<std::size_t>(size, 65536)), 0);
#endif
            }
            void get_remote_ip_and_port(std::string &ip, int &port) const override {
                Address(ip, port, true);
            }
            void get_local_ip_and_port(std::string &ip, int &port) const override {
                Address(ip, port, false);
            }
            socket_t socket() const override {
                return _socket;
            }

          private:
            bool Wait(bool write) const {
                const auto until = std::min(_deadline, std::chrono::steady_clock::now() + std::chrono::seconds(2));
                while (!_stopping.load(std::memory_order_acquire)) {
                    const auto remaining = std::chrono::duration_cast<std::chrono::microseconds>(until - std::chrono::steady_clock::now()).count();
                    if (remaining <= 0) {
                        return false;
                    }
                    // Winsock does not guarantee that shutdown interrupts an
                    // already blocked select. Bound cancellation independently
                    // of both the idle timeout and the header deadline.
                    const auto bounded = std::min<std::int64_t>(remaining, 50000);
                    timeval timeout {static_cast<long>(bounded / 1000000), static_cast<long>(bounded % 1000000)};
                    fd_set sockets;
                    FD_ZERO(&sockets);
                    FD_SET(_socket, &sockets);
                    const int ready = ::select(static_cast<int>(_socket + 1), write ? nullptr : &sockets, write ? &sockets : nullptr, nullptr, &timeout);
                    if (ready != 0) {
                        return ready > 0 && !_stopping.load(std::memory_order_acquire);
                    }
                }
                return false;
            }
            void Address(std::string &ip, int &port, bool remote) const {
                sockaddr_in address {};
#ifdef _WIN32
                int size = sizeof(address);
#else
                socklen_t size = sizeof(address);
#endif
                const int status = remote ? getpeername(_socket, reinterpret_cast<sockaddr *>(&address), &size) : getsockname(_socket, reinterpret_cast<sockaddr *>(&address), &size);
                if (status == 0) {
                    ip   = "127.0.0.1";
                    port = ntohs(address.sin_port);
                }
            }
            socket_t _socket;
            const std::atomic<bool> &_stopping;
            std::chrono::steady_clock::time_point _start;
            std::chrono::steady_clock::time_point _deadline;
        };

        class LocalHttpServer final: public httplib::Server {
          public:
            void StopConnections() {
                _stopping.store(true, std::memory_order_release);
                stop();
                std::lock_guard lock(_mutex);
                for (const auto socket : _sockets) {
                    ShutdownSocket(socket);
                }
            }

          private:
            bool process_and_close_socket(socket_t socket) override {
                {
                    std::lock_guard lock(_mutex);
                    if (_stopping.load(std::memory_order_acquire)) {
                        CloseSocket(socket);
                        return false;
                    }
                    _sockets.insert(socket);
                }
                bool result = false;
                try {
                    DeadlineStream stream(socket, _stopping);
                    std::string remoteIp;
                    std::string localIp;
                    int remotePort = 0;
                    int localPort  = 0;
                    stream.get_remote_ip_and_port(remoteIp, remotePort);
                    stream.get_local_ip_and_port(localIp, localPort);
                    bool closed = false;
                    result      = process_request(stream, remoteIp, remotePort, localIp, localPort, true, closed, nullptr);
                }
                catch (...) { /* Socket cleanup still owns failures outside the route handler. */
                }
                {
                    std::lock_guard lock(_mutex);
                    _sockets.erase(socket);
                    ShutdownSocket(socket);
                    CloseSocket(socket);
                }
                return result;
            }
            std::mutex _mutex;
            std::set<socket_t> _sockets;
            std::atomic<bool> _stopping {false};
        };
    } // namespace

    struct McpServer::State {
        std::mutex mutex;
        Config config;
        nlohmann::json tools     = nlohmann::json::array();
        nlohmann::json resources = nlohmann::json::array();
        std::map<std::string, nlohmann::json> snapshots;
        std::list<std::shared_ptr<Request>> requests;
        std::unique_ptr<LocalHttpServer> http;
        std::thread listener;
        std::atomic<bool> running {false};
        bool stopping                 = false;
        std::uint64_t catalogRevision = 0;
        std::chrono::steady_clock::time_point rateWindow {};
        unsigned callsInWindow = 0;
    };

    McpServer::Request::Request(std::string requestMethod, std::string requestName, nlohmann::json requestArguments, nlohmann::json id, std::chrono::milliseconds timeout)
        : method(std::move(requestMethod))
        , name(std::move(requestName))
        , arguments(std::move(requestArguments))
        , _id(std::move(id))
        , _deadline(std::chrono::steady_clock::now() + timeout) {}

    void McpServer::Request::Complete(nlohmann::json structured, bool isError, nlohmann::json content) {
        std::lock_guard lock(_mutex);
        if (_done) {
            return;
        }
        if (!structured.is_object() || !content.is_array()) {
            structured = {{"error", "Handler returned an invalid result"}};
            content    = nlohmann::json::array();
            isError    = true;
        }
        _structured = std::move(structured);
        _content    = std::move(content);
        _isError    = isError;
        _done       = true;
        _ready.notify_all();
    }

    McpServer::McpServer(): _state(std::make_unique<State>()) {}
    McpServer::~McpServer() {
        Stop();
    }
    bool McpServer::IsRunning() const {
        return _state->running.load();
    }

    bool McpServer::Start(const Config &config, std::string &error) {
        if (IsRunning()) {
            error = "MCP server is already running";
            return false;
        }
        Stop();
        if (config.port == 0 || config.requestTimeout.count() < 1 || config.requestTimeout.count() > 5000) {
            error = "A nonzero port and a timeout of 1-5000ms are required";
            return false;
        }
        {
            std::lock_guard lock(_state->mutex);
            _state->config        = config;
            _state->stopping      = false;
            _state->rateWindow    = std::chrono::steady_clock::now();
            _state->callsInWindow = 0;
        }
        _state->http        = std::make_unique<LocalHttpServer>();
        auto &http          = *_state->http;
        http.new_task_queue = &NewHttpQueue;
        http.set_socket_options(&ExclusiveSocketOptions);
        http.set_payload_max_length(kMaxBody);
        http.set_read_timeout(2);
        http.set_write_timeout(2);
        http.set_keep_alive_max_count(1);
        http.set_pre_routing_handler(fu2::function<httplib::Server::HandlerResponse(const httplib::Request &, httplib::Response &) const>([this](const auto &request, auto &response) {
            response.set_header("Cache-Control", "no-store");
            response.set_header("X-Content-Type-Options", "nosniff");
            const auto port   = std::to_string(_state->config.port);
            const auto host   = request.get_header_value("Host");
            const auto origin = request.get_header_value("Origin");
            if (request.get_header_value_count("Host") != 1 || request.get_header_value_count("Origin") > 1 || (host != "127.0.0.1:" + port && host != "localhost:" + port)
                || (request.has_header("Origin") && origin != "http://127.0.0.1:" + port && origin != "http://localhost:" + port)) {
                response.status = 403;
                return httplib::Server::HandlerResponse::Handled;
            }
            if (request.path != "/mcp") {
                response.status = 404;
                return httplib::Server::HandlerResponse::Handled;
            }
            if (request.method != "POST") {
                response.status = 405;
                response.set_header("Allow", "POST");
                return httplib::Server::HandlerResponse::Handled;
            }
            return httplib::Server::HandlerResponse::Unhandled;
        }));
        http.Post("/mcp", fu2::function<void(const httplib::Request &, httplib::Response &, const httplib::ContentReader &) const>([this](const auto &request, auto &response, const auto &reader) {
            const auto accept = request.get_header_value("Accept");
            if (accept.find("application/json") == std::string::npos || accept.find("text/event-stream") == std::string::npos) {
                response.status = 406;
                return;
            }
            const auto contentType = request.get_header_value("Content-Type");
            if (contentType != "application/json" && contentType.rfind("application/json;", 0) != 0) {
                response.status = 415;
                return;
            }
            if (request.has_header("Content-Encoding")) {
                response.status = 415;
                return;
            }
            if (request.has_header("Content-Length")) {
                const auto length = request.get_header_value("Content-Length");
                if (length.empty() || length.size() > 10
                    || !std::all_of(length.begin(), length.end(),
                        [](unsigned char c) {
                            return c >= '0' && c <= '9';
                        })
                    || std::stoull(length) > kMaxBody) {
                    response.status = 413;
                    return;
                }
            }
            std::string body;
            bool tooLarge = false;
            if (!reader(fu2::function<bool(const char *, std::size_t) const>([&](const char *data, std::size_t length) {
                    if (length > kMaxBody - body.size()) {
                        tooLarge = true;
                        return false;
                    }
                    body.append(data, length);
                    return true;
                }))) {
                response.status = tooLarge ? 413 : 400;
                return;
            }
            nlohmann::json result;
            auto message = nlohmann::json::parse(
                body,
                [](int depth, nlohmann::json::parse_event_t, nlohmann::json &) {
                    if (depth > 64) {
                        throw std::invalid_argument("JSON nesting limit exceeded");
                    }
                    return true;
                },
                false);
            if (message.is_discarded()) {
                result          = Error(nullptr, -32700, "Parse error");
                response.status = 400;
            }
            else {
                const auto id       = message.is_object() ? message.value("id", nlohmann::json(nullptr)) : nlohmann::json(nullptr);
                const auto params   = message.is_object() && message.contains("params") && message.at("params").is_object() ? message.at("params") : nlohmann::json::object();
                const auto metadata = params.contains("_meta") && params.at("_meta").is_object() ? params.at("_meta") : nlohmann::json::object();
                const auto version  = request.get_header_value("MCP-Protocol-Version");
                const auto method   = message.is_object() && message.contains("method") && message.at("method").is_string() ? message.at("method").get<std::string>() : std::string();
                const bool modern   = version == kModern || metadata.contains("io.modelcontextprotocol/protocolVersion");
                bool mismatch       = false;
                if (modern) {
                    mismatch = request.get_header_value_count("MCP-Protocol-Version") != 1 || !metadata.contains("io.modelcontextprotocol/protocolVersion") || metadata.at("io.modelcontextprotocol/protocolVersion") != version;
                    mismatch = mismatch || request.get_header_value_count("Mcp-Method") != 1 || request.get_header_value("Mcp-Method") != method;
                    if (method == "tools/call" || method == "resources/read" || method == "prompts/get") {
                        const auto field = method == "resources/read" ? "uri" : "name";
                        mismatch         = mismatch || request.get_header_value_count("Mcp-Name") != 1 || !params.contains(field) || !params.at(field).is_string() || !HeaderNameMatches(request.get_header_value("Mcp-Name"), params.at(field).get<std::string>());
                    }
                }
                else if (version.empty() && method != "initialize") {
                    mismatch = true;
                }
                if (version == kModern && (!metadata.contains("io.modelcontextprotocol/protocolVersion") || !metadata.contains("io.modelcontextprotocol/clientCapabilities"))) {
                    result          = Error(id, -32602, "Required request metadata is missing");
                    response.status = 400;
                }
                else if (mismatch) {
                    result          = Error(id, -32020, "Required MCP headers are missing or do not match the request body");
                    response.status = 400;
                }
                else {
                    result = Handle(message, version.empty() ? kLegacy : version);
                    if (result.is_null()) {
                        response.status = 202;
                    }
                    else if (result.contains("error")) {
                        const int code = result.at("error").at("code");
                        if (code == -32600 || code == -32602 || code == -32022) {
                            response.status = 400;
                        }
                        else if (modern && code == -32601) {
                            response.status = 404;
                        }
                    }
                }
            }
            if (!result.is_null()) {
                response.set_content(result.dump(), "application/json");
            }
        }));
        http.set_exception_handler(fu2::function<void(const httplib::Request &, httplib::Response &, std::exception_ptr) const>([](const auto &, auto &response, std::exception_ptr) {
            response.status = 400;
            response.set_content(Error(nullptr, -32600, "Malformed or excessively nested request").dump(), "application/json");
        }));
        if (!http.bind_to_port("127.0.0.1", config.port)) {
            error = "Cannot bind MCP to 127.0.0.1:" + std::to_string(config.port);
            _state->http.reset();
            return false;
        }
        _state->running  = true;
        _state->listener = std::thread([this] {
            _state->http->listen_after_bind();
            _state->running = false;
        });
        // httplib::stop only closes an already-running listener. Wait for the
        // listen loop to enter before returning so immediate Stop cannot race.
        while (!_state->http->is_running() && _state->running.load()) {
            std::this_thread::yield();
        }
        if (!_state->running) {
            error = "MCP listener failed to start";
            Stop();
            return false;
        }
        error.clear();
        return true;
    }

    void McpServer::Stop() {
        {
            std::lock_guard lock(_state->mutex);
            _state->stopping = true;
            for (const auto &request : _state->requests) {
                bool claimed;
                {
                    std::lock_guard requestLock(request->_mutex);
                    claimed = request->_claimed;
                }
                request->Complete({{"error", "MCP server stopped"}, {"mayHaveExecuted", claimed}}, true);
            }
        }
        if (_state->http) {
            _state->http->StopConnections();
        }
        if (_state->listener.joinable()) {
            _state->listener.join();
        }
        _state->running = false;
        _state->http.reset();
    }

    void McpServer::SetTools(nlohmann::json tools) {
        if (!tools.is_array()) {
            throw std::invalid_argument("MCP tools must be an array");
        }
        for (auto &tool : tools) {
            if (!tool.is_object() || !tool.contains("name") || !tool.at("name").is_string() || !tool.contains("inputSchema") || tool.at("inputSchema").value("type", "") != "object") {
                throw std::invalid_argument("Invalid MCP tool definition");
            }
            CheckSchema(tool.at("inputSchema"));
            if (!tool.contains("outputSchema")) {
                tool["outputSchema"] = {{"type", "object"}};
            }
            CheckSchema(tool.at("outputSchema"));
            if (tool.at("outputSchema").value("type", "") != "object") {
                throw std::invalid_argument("MCP output schema must describe an object");
            }
            if (!tool.contains("annotations")) {
                tool["annotations"] = {{"readOnlyHint", false}, {"destructiveHint", true}, {"idempotentHint", false}, {"openWorldHint", false}};
            }
        }
        std::sort(tools.begin(), tools.end(), [](const auto &left, const auto &right) {
            return left.at("name") < right.at("name");
        });
        for (std::size_t index = 1; index < tools.size(); ++index) {
            if (tools[index - 1].at("name") == tools[index].at("name")) {
                throw std::invalid_argument("Duplicate MCP tool name");
            }
        }
        std::lock_guard lock(_state->mutex);
        _state->tools = std::move(tools);
        ++_state->catalogRevision;
    }

    void McpServer::SetResources(nlohmann::json resources) {
        if (!resources.is_array()) {
            throw std::invalid_argument("MCP resources must be an array");
        }
        for (const auto &resource : resources) {
            if (!resource.is_object() || !resource.contains("uri") || !resource.at("uri").is_string() || !resource.contains("name") || !resource.at("name").is_string()) {
                throw std::invalid_argument("Invalid MCP resource definition");
            }
        }
        std::sort(resources.begin(), resources.end(), [](const auto &left, const auto &right) {
            return left.at("uri") < right.at("uri");
        });
        for (std::size_t index = 1; index < resources.size(); ++index) {
            if (resources[index - 1].at("uri") == resources[index].at("uri")) {
                throw std::invalid_argument("Duplicate MCP resource URI");
            }
        }
        std::lock_guard lock(_state->mutex);
        _state->resources = std::move(resources);
        _state->snapshots.clear();
        ++_state->catalogRevision;
    }

    void McpServer::SetResource(const std::string &uri, nlohmann::json value) {
        std::lock_guard lock(_state->mutex);
        if (std::none_of(_state->resources.begin(), _state->resources.end(), [&](const auto &resource) {
                return resource.at("uri") == uri;
            })) {
            throw std::invalid_argument("Unknown MCP resource URI");
        }
        _state->snapshots[uri] = std::move(value);
    }

    std::shared_ptr<McpServer::Request> McpServer::Poll() {
        std::lock_guard lock(_state->mutex);
        if (_state->stopping) {
            return {};
        }
        for (const auto &request : _state->requests) {
            std::lock_guard requestLock(request->_mutex);
            if (!request->_done && !request->_claimed && std::chrono::steady_clock::now() < request->_deadline) {
                request->_claimed = true;
                return request;
            }
        }
        return {};
    }

    nlohmann::json McpServer::Dispatch(const std::string &method, const std::string &name, const nlohmann::json &arguments, const nlohmann::json &id, bool &isError, nlohmann::json &content) {
        std::shared_ptr<Request> request;
        {
            std::lock_guard lock(_state->mutex);
            if (_state->stopping || _state->requests.size() >= kMaxPending) {
                isError = true;
                return {{"error", _state->stopping ? "MCP server is stopped" : "MCP request queue is full"}, {"mayHaveExecuted", false}};
            }
            const auto now = std::chrono::steady_clock::now();
            if (now - _state->rateWindow >= std::chrono::seconds(1)) {
                _state->rateWindow    = now;
                _state->callsInWindow = 0;
            }
            if (_state->callsInWindow >= kMaxCallsPerSecond) {
                isError = true;
                return {{"error", "MCP request rate limit reached; wait before retrying"}, {"mayHaveExecuted", false}};
            }
            ++_state->callsInWindow;
            request = std::shared_ptr<Request>(new Request(method, name, arguments, id, _state->config.requestTimeout));
            _state->requests.push_back(request);
        }
        nlohmann::json result;
        {
            std::unique_lock lock(request->_mutex);
            if (!request->_ready.wait_until(lock, request->_deadline, [&] {
                    return request->_done;
                })) {
                request->_done       = true;
                request->_isError    = true;
                request->_structured = {{"error", request->_claimed ? "Editor request timed out after dispatch; inspect editor state before retrying" : "Editor request timed out before execution"}, {"mayHaveExecuted", request->_claimed}};
            }
            result  = request->_structured;
            content = request->_content.is_array() ? request->_content : nlohmann::json::array();
            isError = request->_isError;
        }
        {
            std::lock_guard lock(_state->mutex);
            _state->requests.remove(request);
        }
        return result;
    }

    bool McpServer::ValidateArguments(const nlohmann::json &schema, const nlohmann::json &arguments, std::string &error) {
        std::size_t budget = 100000;
        error.clear();
        try {
            CheckSchema(schema);
            return Validate(schema, arguments, error, "$", 0, budget);
        }
        catch (const std::exception &) {
            error = "Invalid or unsupported argument schema";
            return false;
        }
    }

    nlohmann::json McpServer::Handle(const nlohmann::json &message, const std::string &protocolVersion) {
        const auto id = message.is_object() ? message.value("id", nlohmann::json(nullptr)) : nlohmann::json(nullptr);
        if (!message.is_object() || message.value("jsonrpc", nlohmann::json()) != "2.0" || !message.contains("method") || !message.at("method").is_string() || (message.contains("id") && !id.is_string() && !id.is_number_integer())) {
            return Error(nullptr, -32600, "Invalid request");
        }
        if (message.contains("params") && !message.at("params").is_object()) {
            return Error(id, -32602, "Parameters must be an object");
        }
        const auto method = message.at("method").get<std::string>();
        const auto params = message.value("params", nlohmann::json::object());
        // One-way messages never invoke an editor action or produce a reply.
        if (!message.contains("id")) {
            if (protocolVersion == kLegacy && method == "notifications/cancelled" && params.contains("requestId")) {
                std::lock_guard lock(_state->mutex);
                for (const auto &request : _state->requests) {
                    if (request->_id != params.at("requestId")) {
                        continue;
                    }
                    std::lock_guard requestLock(request->_mutex);
                    if (!request->_done) {
                        request->_done       = true;
                        request->_isError    = true;
                        request->_structured = {{"error", "Request cancelled"}, {"mayHaveExecuted", request->_claimed}};
                        request->_ready.notify_all();
                    }
                }
            }
            return nullptr;
        }
        const bool modern = protocolVersion == kModern;
        if (protocolVersion != kModern && protocolVersion != kLegacy) {
            auto result             = Error(id, -32022, "Unsupported protocol version");
            result["error"]["data"] = {{"supported", {kModern, kLegacy}}, {"requested", protocolVersion}};
            return result;
        }
        if (modern) {
            if (!params.contains("_meta") || !params.at("_meta").is_object()) {
                return Error(id, -32602, "Required request metadata is missing");
            }
            const auto &metadata = params.at("_meta");
            if (!metadata.contains("io.modelcontextprotocol/protocolVersion") || metadata.at("io.modelcontextprotocol/protocolVersion") != kModern || !metadata.contains("io.modelcontextprotocol/clientCapabilities")
                || !metadata.at("io.modelcontextprotocol/clientCapabilities").is_object()) {
                return Error(id, -32602, "Protocol version and client capabilities are required on every request");
            }
        }
        auto success = [&](nlohmann::json result, bool cacheable = false) {
            if (modern) {
                result["resultType"] = "complete";
                if (cacheable) {
                    result["ttlMs"]      = 0;
                    result["cacheScope"] = "private";
                }
            }
            return nlohmann::json {{"jsonrpc", "2.0"}, {"id", id}, {"result", std::move(result)}};
        };
        if (method == "initialize" && !modern) {
            if (!params.contains("protocolVersion") || !params.at("protocolVersion").is_string() || !params.contains("capabilities") || !params.at("capabilities").is_object() || !params.contains("clientInfo") || !params.at("clientInfo").is_object()) {
                return Error(id, -32602, "Invalid initialization parameters");
            }
            return success({{"protocolVersion", kLegacy}, {"capabilities", Capabilities()}, {"serverInfo", Info()}, {"instructions", "Offline World Builder. Read state and revision before edits; inspect operation handles before retrying mutations."}});
        }
        if (method == "server/discover" && modern) {
            return success({{"supportedVersions", {kModern, kLegacy}}, {"capabilities", Capabilities()}, {"_meta", {{"io.modelcontextprotocol/serverInfo", Info()}}},
                               {"instructions", "Offline World Builder. Read state and revision before edits; inspect operation handles before retrying mutations."}},
                true);
        }
        if (method == "ping") {
            return success(nlohmann::json::object());
        }
        if (method == "resources/templates/list") {
            return success({{"resourceTemplates", nlohmann::json::array()}}, true);
        }
        if (method == "tools/list" || method == "resources/list") {
            std::lock_guard lock(_state->mutex);
            const auto &catalog = method == "tools/list" ? _state->tools : _state->resources;
            std::size_t offset  = 0;
            if (params.contains("cursor")) {
                if (!params.at("cursor").is_string()) {
                    return Error(id, -32602, "Invalid catalog cursor");
                }
                const auto cursor = params.at("cursor").get<std::string>();
                const auto prefix = std::to_string(_state->catalogRevision) + ":" + method + ":";
                if (cursor.rfind(prefix, 0) != 0) {
                    return Error(id, -32602, "Catalog cursor is stale or invalid; restart listing");
                }
                const auto number = cursor.substr(prefix.size());
                if (number.empty() || number.size() > 10 || !std::all_of(number.begin(), number.end(), [](unsigned char c) {
                        return c >= '0' && c <= '9';
                    })) {
                    return Error(id, -32602, "Invalid catalog cursor");
                }
                offset = static_cast<std::size_t>(std::stoull(number));
                if (offset >= catalog.size() || offset % kPageSize != 0) {
                    return Error(id, -32602, "Invalid catalog cursor");
                }
            }
            auto page      = nlohmann::json::array();
            const auto end = std::min(catalog.size(), offset + kPageSize);
            for (auto index = offset; index < end; ++index) {
                page.push_back(catalog.at(index));
            }
            nlohmann::json result = {{method == "tools/list" ? "tools" : "resources", std::move(page)}};
            if (end < catalog.size()) {
                result["nextCursor"] = std::to_string(_state->catalogRevision) + ":" + method + ":" + std::to_string(end);
            }
            return success(std::move(result), true);
        }
        if (method == "tools/call") {
            if (!params.contains("name") || !params.at("name").is_string()) {
                return Error(id, -32602, "Tool name is required");
            }
            const auto name      = params.at("name").get<std::string>();
            const auto arguments = params.value("arguments", nlohmann::json::object());
            if (!arguments.is_object()) {
                return Error(id, -32602, "Tool arguments must be an object");
            }
            nlohmann::json schema;
            nlohmann::json outputSchema;
            {
                std::lock_guard lock(_state->mutex);
                const auto found = std::find_if(_state->tools.begin(), _state->tools.end(), [&](const auto &tool) {
                    return tool.at("name") == name;
                });
                if (found == _state->tools.end()) {
                    return Error(id, -32602, "Unknown tool");
                }
                schema       = found->at("inputSchema");
                outputSchema = found->at("outputSchema");
            }
            std::string validationError;
            if (!ValidateArguments(schema, arguments, validationError)) {
                return success({{"isError", true}, {"content", nlohmann::json::array({{{"type", "text"}, {"text", validationError}}})}});
            }
            bool isError    = false;
            auto content    = nlohmann::json::array();
            auto structured = Dispatch(method, name, arguments, id, isError, content);
            if (!isError && !ValidateArguments(outputSchema, structured, validationError)) {
                isError    = true;
                structured = {{"error", "Tool returned a result outside its output schema"}};
                content.clear();
            }
            if (std::none_of(content.begin(), content.end(), [](const auto &entry) {
                    return entry.is_object() && entry.value("type", "") == "text";
                })) {
                content.push_back({{"type", "text"}, {"text", structured.dump()}});
            }
            nlohmann::json result = {{"content", std::move(content)}, {"isError", isError}};
            // Failed calls need not satisfy the success schema. Omit structured
            // content in that case so clients do not parse it as successful data.
            if (!isError) {
                result["structuredContent"] = std::move(structured);
            }
            return success(std::move(result));
        }
        if (method == "resources/read") {
            if (!params.contains("uri") || !params.at("uri").is_string()) {
                return Error(id, -32602, "Resource URI is required");
            }
            const auto uri = params.at("uri").get<std::string>();
            nlohmann::json value;
            bool snapshot = false;
            {
                std::lock_guard lock(_state->mutex);
                if (std::none_of(_state->resources.begin(), _state->resources.end(), [&](const auto &resource) {
                        return resource.at("uri") == uri;
                    })) {
                    return Error(id, modern ? -32602 : -32002, "Unknown resource");
                }
                const auto found = _state->snapshots.find(uri);
                if (found != _state->snapshots.end()) {
                    value    = found->second;
                    snapshot = true;
                }
            }
            if (!snapshot) {
                bool isError = false;
                nlohmann::json content;
                value = Dispatch(method, uri, nlohmann::json::object(), id, isError, content);
                if (isError) {
                    return Error(id, -32603, value.value("error", "Resource read failed"));
                }
            }
            return success({{"contents", nlohmann::json::array({{{"uri", uri}, {"mimeType", "application/json"}, {"text", value.dump()}}})}}, true);
        }
        return Error(id, -32601, "Method not found");
    }
} // namespace KCDC::Shared::WorldBuilder
