#include "prop_editor_mcp.h"
#include "prop_editor.h"
#include "prop_editor_authoring_mcp.h"
#include "prop_editor_capture.h"
#include "prop_editor_project_mcp.h"
#include <ImGuizmo.h>
#include <Windows.h>
#include <algorithm>
#include <bcrypt.h>
#include <imgui.h>
#include <stdexcept>

namespace KCDC::Features::Prop {
    namespace {
        std::string RandomToken() {
            unsigned char bytes[32];
            if (BCryptGenRandom(nullptr, bytes, sizeof(bytes), BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) {
                throw std::runtime_error("Could not generate the MCP instance ID");
            }
            constexpr char hex[] = "0123456789abcdef";
            std::string result;
            for (auto byte : bytes) {
                result += hex[byte >> 4];
                result += hex[byte & 15];
            }
            return result;
        }
        nlohmann::json Error(const std::string &message) {
            return {{"error", message}};
        }
        bool UserEditing() {
            return ImGui::IsAnyItemActive() || ImGuizmo::IsUsingAny() || ImGui::IsMouseDragging(0) || ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
        }
    } // namespace

    void EditorMcp::Availability(bool ready) {
        _available.store(ready, std::memory_order_release);
    }
    void EditorMcp::ContextChanged() {
        _generation.fetch_add(1, std::memory_order_acq_rel);
    }

    void EditorMcp::Configure() {
        if (_configured) {
            return;
        }
        _instance = RandomToken().substr(0, 16);
        std::string data, error;
        if (MapFiles::ReadPath(MapFiles::Directory() + "/mcp.settings.json", data, error)) {
            const auto settings = nlohmann::json::parse(data, nullptr, false);
            if (settings.is_object()) {
                if (settings.contains("enabled") && settings["enabled"].is_boolean()) {
                    _enabled = settings["enabled"].get<bool>();
                }
                if (settings.contains("port") && settings["port"].is_number_integer() && settings["port"] >= 1 && settings["port"] <= 65535) {
                    _port = settings["port"].get<int>();
                }
                else if (settings.contains("port")) {
                    _enabled = false;
                    _error   = "The saved MCP port is invalid; select a valid port before starting.";
                }
            }
            else {
                _error = "MCP settings could not be parsed; automatic start is disabled.";
            }
        }
        _tools = NativeTools();
        for (const auto &tool : AuthoringMcp::Tools()) {
            _tools.push_back(tool);
        }
        for (const auto &tool : ProjectMcp::Tools()) {
            _tools.push_back(tool);
        }
        for (auto &tool : _tools) {
            if (!tool.value("annotations", nlohmann::json::object()).value("readOnlyHint", false)) {
                tool["inputSchema"]["properties"]["expectedRevision"] = {{"type", "string"}, {"maxLength", 64}};
                tool["inputSchema"]["properties"]["idempotencyKey"]   = {{"type", "string"}, {"minLength", 1}, {"maxLength", 128}};
                if (tool["name"] != "wb_operation_cancel") {
                    tool["inputSchema"]["required"].push_back("expectedRevision");
                    tool["inputSchema"]["required"].push_back("idempotencyKey");
                }
            }
        }
        _server.SetTools(_tools);
        auto resources = nlohmann::json::array();
        for (const char *name : {"state", "project", "selection", "capabilities", "logs"}) {
            resources.push_back({{"uri", std::string("worldbuilder://") + name}, {"name", name}, {"mimeType", "application/json"}});
        }
        _server.SetResources(std::move(resources));
        _configured = true;
    }

    bool EditorMcp::SaveSettings() {
        std::string error;
        if (!MapFiles::WritePath(MapFiles::Directory() + "/mcp.settings.json", nlohmann::json {{"version", 1}, {"enabled", _enabled}, {"port", _port}}.dump(2), error)) {
            _error = "Could not save MCP preferences: " + error;
            return false;
        }
        return true;
    }

    void EditorMcp::Start() {
        if (_server.IsRunning()) {
            return;
        }
        if (_port < 1 || _port > 65535) {
            _error = "Port must be between 1 and 65535.";
            return;
        }
        _error.clear();
        Shared::WorldBuilder::McpServer::Config config;
        config.port  = static_cast<std::uint16_t>(_port);
        _server.Start(config, _error);
    }

    void EditorMcp::Shutdown() {
        _available.store(false, std::memory_order_release);
        ContextChanged();
        std::scoped_lock lock(_lifecycle);
        _server.Stop();
        _wasAvailable = false;
        for (auto &[id, operation] : _operations) {
            if (operation.job) {
                auto expected = McpJob::State::Queued;
                operation.job->state.compare_exchange_strong(expected, McpJob::State::Cancelled);
            }
        }
    }

    void EditorMcp::RenderTab() {
        std::scoped_lock lock(_lifecycle);
        ImGui::TextUnformatted("Local MCP server");
        ImGui::TextWrapped("Connect an MCP client on this computer to control World Builder.");
        ImGui::BeginDisabled(_server.IsRunning());
        if (ImGui::InputInt("Port", &_port, 0, 0) && _port >= 1 && _port <= 65535) {
            SaveSettings();
        }
        ImGui::EndDisabled();
        ImGui::Text("Status: %s", _server.IsRunning() ? "Running" : "Stopped");
        ImGui::Text("http://127.0.0.1:%d/mcp", _port);
        ImGui::TextDisabled("Automatic start: %s", _enabled ? "enabled" : "disabled");
        if (_server.IsRunning() || _enabled) {
            if (ImGui::Button("Stop MCP server")) {
                _enabled = false;
                if (!SaveSettings()) {
                    _error += " The saved automatic-start preference has not changed.";
                }
                for (auto &[id, operation] : _operations) {
                    if (operation.job) {
                        auto queued = McpJob::State::Queued;
                        operation.job->state.compare_exchange_strong(queued, McpJob::State::Cancelled);
                    }
                }
                _server.Stop();
            }
        }
        if (!_server.IsRunning()) {
            ImGui::BeginDisabled(!_available.load(std::memory_order_acquire));
            if (ImGui::Button("Start MCP server")) {
                if (_port < 1 || _port > 65535) {
                    _error = "Port must be between 1 and 65535.";
                }
                else {
                    _enabled = true;
                    if (SaveSettings()) {
                        Start();
                    }
                }
            }
            ImGui::EndDisabled();
        }
        if (ImGui::Button("Copy connection configuration")) {
            const nlohmann::json config {{"mcpServers", {{"worldbuilder", {{"type", "http"}, {"url", "http://127.0.0.1:" + std::to_string(_port) + "/mcp"}}}}}};
            ImGui::SetClipboardText(config.dump(2).c_str());
        }
        if (!_error.empty()) {
            ImGui::TextWrapped("%s", _error.c_str());
        }
    }

    nlohmann::json EditorMcp::State(const MapEditor &editor, const EditorSnapshot &snapshot) const {
        return {{"contractVersion", 1}, {"instance", _instance}, {"generation", std::to_string(_generation.load())}, {"revision", std::to_string(_revision)}, {"open", editor.IsOpen()}, {"offline", _available.load()}, {"settled", editor.IsSettled(snapshot) && !UserEditing()},
            {"level", snapshot.level}, {"props", snapshot.rows.size()}, {"effects", snapshot.effectRows.size()}, {"lights", snapshot.lightRows.size()}, {"areas", editor._areas.Authored().size()}, {"patrols", editor._patrols.Authored().size()}, {"status", snapshot.status}, {"collisionError", snapshot.collisionError}, {"undo", snapshot.undoLabel},
            {"redo", snapshot.redoLabel}, {"viewport", editor._viewport.ToJson()}, {"environment", editor._environment.Preview().ToJson()}, {"layers", snapshot.layers->ToJson(false)}, {"mapTravel", snapshot.mapTravel}};
    }

    void EditorMcp::Log(const std::string &name, const std::string &state) {
        _logs.push_back({{"sequence", std::to_string(++_nextOperation)}, {"tool", name}, {"state", state}});
        if (_logs.size() > 256) {
            _logs.pop_front();
        }
    }

    nlohmann::json EditorMcp::OperationResult(const Operation &operation) const {
        nlohmann::json result {{"operationId", operation.id}, {"status", operation.done ? operation.failed ? "failed" : "completed" : "queued"}, {"revision", std::to_string(_revision)}};
        if (operation.done) {
            result["result"] = operation.result;
        }
        else {
            result["pollAfterMs"] = 50;
        }
        return result;
    }

    std::optional<nlohmann::json> EditorMcp::RetainedOperationResult(const std::string &name, const nlohmann::json &arguments) const {
        const std::string key = arguments.value("idempotencyKey", std::string {});
        if (!key.empty()) {
            for (const auto &[id, operation] : _operations) {
                if (operation.key == key) {
                    if (operation.fingerprint != name + arguments.dump()) {
                        throw std::runtime_error("Idempotency key already belongs to a different request");
                    }
                    return OperationResult(operation);
                }
            }
        }
        return std::nullopt;
    }

    void EditorMcp::RefreshRevision(MapEditor &editor, const EditorSnapshot &snapshot) {
        if (!editor.IsSettled(snapshot)) {
            return;
        }
        auto document = ProjectToJson(editor.BuildDocument(snapshot));
        document["editor"].erase("camera");
        if (document != _lastDocument) {
            ++_revision;
            _lastDocument = std::move(document);
        }
    }

    void EditorMcp::Render(MapEditor &editor, const EditorSnapshot &snapshot) {
        std::scoped_lock lock(_lifecycle);
        const bool available = _available.load(std::memory_order_acquire);
        if (!available) {
            if (_wasAvailable) {
                _server.Stop();
            }
            _wasAvailable = false;
            return;
        }
        try {
            Configure();
        }
        catch (const std::exception &exception) {
            _error = exception.what();
            return;
        }
        if (!_wasAvailable) {
            _wasAvailable = true;
            if (_enabled) {
                Start();
            }
        }
        if (!_server.IsRunning()) {
            return;
        }
        const bool settled = editor.IsSettled(snapshot) && !UserEditing();
        if (settled && std::any_of(_operations.begin(), _operations.end(), [](const auto &entry) {
                return !entry.second.done;
            })) {
            RefreshRevision(editor, snapshot);
        }
        for (auto &[id, operation] : _operations) {
            if (operation.done) {
                continue;
            }
            if (operation.job) {
                const auto state = operation.job->state.load(std::memory_order_acquire);
                if (state == McpJob::State::Queued && operation.job->generation != _generation.load()) {
                    auto queued = McpJob::State::Queued;
                    operation.job->state.compare_exchange_strong(queued, McpJob::State::Cancelled);
                }
                if (state == McpJob::State::Completed || state == McpJob::State::Failed || state == McpJob::State::Cancelled) {
                    if (state == McpJob::State::Completed && operation.name != "wb_level_load" && operation.name != "wb_client_mods_apply" && snapshot.completedCommand < operation.serial) {
                        continue;
                    }
                    operation.done   = true;
                    operation.failed = state != McpJob::State::Completed;
                    operation.result = state == McpJob::State::Cancelled ? Error("Operation cancelled before execution") : operation.job->result;
                    if (!operation.failed && operation.result.contains("_authoredSelection")) {
                        const auto selected = operation.result["_authoredSelection"];
                        std::string error;
                        if (selected.contains("areas")) {
                            for (const auto &area : selected["areas"]) {
                                if (!editor._areas.Find(area.get<std::string>())) {
                                    error = "Selected area no longer exists";
                                }
                            }
                        }
                        if (selected.contains("patrol") && selected["patrol"] != "" && !editor._patrols.Find(selected["patrol"].get<std::string>())) {
                            error = "Selected patrol no longer exists";
                        }
                        if (error.empty()) {
                            if (selected.contains("areas")) {
                                editor._areas.Select(selected["areas"].get<std::vector<std::string>>(), error);
                            }
                            if (selected.contains("patrol")) {
                                editor._patrols.Select(selected["patrol"].get<std::string>(), error);
                            }
                        }
                        operation.failed = !error.empty();
                        operation.result.erase("_authoredSelection");
                        if (operation.failed) {
                            operation.result["error"] = error;
                        }
                    }
                    Log(operation.name, operation.failed ? "failed" : "completed");
                }
            }
            else if (operation.generation != Generation()) {
                operation.done   = true;
                operation.failed = true;
                operation.result = Error("Editor context changed before operation completion");
                Log(operation.name, "cancelled");
            }
            else if (snapshot.completedCommand >= operation.serial) {
                operation.done = true;
                if (operation.collisionSerial != 0) {
                    const auto receipt = snapshot.collisionResults.find(operation.collisionSerial);
                    if (receipt == snapshot.collisionResults.end() || !receipt->second.empty()) {
                        operation.failed = true;
                        operation.result["error"] = receipt == snapshot.collisionResults.end() ? "Collision result is no longer available" : receipt->second;
                    }
                }
                if (operation.name == "wb_project_open" || operation.name == "wb_project_recover" || operation.name == "wb_document_import" || operation.name == "wb_blueprint_place") {
                    operation.result["nativeStatus"] = snapshot.status;
                    if (snapshot.unresolvedDocument) {
                        operation.result["unresolved"] = {{"props", snapshot.unresolvedDocument->objects.size()}, {"effects", snapshot.unresolvedDocument->effects.size()}, {"lights", snapshot.unresolvedDocument->lights.size()}, {"world", snapshot.unresolvedDocument->world.size()}};
                    }
                }
                if (operation.name == "wb_patrol_preview_start" || operation.name == "wb_patrol_preview_stop") {
                    operation.result["preview"] = {{"active", snapshot.patrolPreviewActive}, {"status", snapshot.patrolPreviewStatus}, {"progress", snapshot.patrolProgress}};
                }
                if (operation.name == "wb_asset_preview") {
                    operation.result["active"] = snapshot.previewModel != AssetLibrary::kNone;
                    if (operation.result.contains("asset") && snapshot.previewModel == AssetLibrary::kNone) {
                        operation.failed          = true;
                        operation.result["error"] = "The native asset preview could not be created";
                        editor._mcpPreviewPinned  = false;
                    }
                }
                Log(operation.name, operation.failed ? "failed" : "completed");
            }
        }
        for (int count = 0; count < 8; ++count) {
            auto request = _server.Poll();
            if (!request) {
                break;
            }
            try {
                if (settled) {
                    RefreshRevision(editor, snapshot);
                }
                if (request->method == "resources/read") {
                    nlohmann::json result;
                    if (request->name == "worldbuilder://state") {
                        result = State(editor, snapshot);
                    }
                    else if (request->name == "worldbuilder://capabilities") {
                        result = {{"tools", _tools}, {"contractVersion", 1}};
                    }
                    else if (request->name == "worldbuilder://logs") {
                        result = {{"entries", _logs}};
                    }
                    else if (request->name == "worldbuilder://project") {
                        if (!settled || !editor.IsSettled(snapshot)) {
                            throw std::runtime_error("Editor busy; retry after the current edit completes");
                        }
                        ProjectMcp::Execute(editor, snapshot, "wb_project_get", nlohmann::json::object(), result);
                        result["documentTool"] = "wb_document_get";
                        result["revision"]     = std::to_string(_revision);
                    }
                    else if (request->name == "worldbuilder://selection") {
                        result = Dispatch(editor, snapshot, "wb_selection_get", nlohmann::json::object());
                    }
                    else {
                        throw std::runtime_error("Unknown resource");
                    }
                    request->Complete(std::move(result));
                    continue;
                }
                nlohmann::json result;
                if (const auto retained = RetainedOperationResult(request->name, request->arguments)) {
                    result = *retained;
                }
                else {
                    const bool control = request->name == "wb_get_state" || request->name.starts_with("wb_operation_") || request->name == "wb_logs_read";
                    if (!control && (!settled || !editor.IsSettled(snapshot))) {
                        throw std::runtime_error("Editor busy; retry after the current edit completes");
                    }
                    result = Dispatch(editor, snapshot, request->name, request->arguments);
                }
                auto content = nlohmann::json::array();
                if (result.contains("data") && result.value("mimeType", "") == "image/png") {
                    content.push_back({{"type", "image"}, {"mimeType", "image/png"}, {"data", result["data"]}});
                    result.erase("data");
                }
                request->Complete(std::move(result), false, std::move(content));
            }
            catch (const std::exception &exception) {
                request->Complete(Error(exception.what()), true);
                Log(request->name, "rejected");
            }
        }
    }

    nlohmann::json EditorMcp::Dispatch(MapEditor &editor, const EditorSnapshot &snapshot, const std::string &name, const nlohmann::json &arguments) {
        if (snapshot.mapTravel.value("active", false) && name != "wb_get_state" && name != "wb_map_get" && name != "wb_map_cancel" && name != "wb_map_travel" && !name.starts_with("wb_operation_") && name != "wb_logs_read" && name != "wb_capture_viewport") {
            throw std::runtime_error("Map travel is active; wait for arrival or cancel before editing");
        }
        if (name == "wb_get_state") {
            return State(editor, snapshot);
        }
        if (name == "wb_logs_read") {
            return {{"entries", _logs}, {"scope", "MCP operation audit; credentials and arguments are excluded"}};
        }
        if (name.starts_with("wb_operation_")) {
            auto found = _operations.find(arguments.at("operationId").get<std::string>());
            if (found == _operations.end()) {
                throw std::runtime_error("Unknown or expired operation");
            }
            if (name == "wb_operation_cancel" && !found->second.done) {
                if (!found->second.job) {
                    throw std::runtime_error("This operation has already been applied and cannot be cancelled");
                }
                auto expected = McpJob::State::Queued;
                if (!found->second.job->state.compare_exchange_strong(expected, McpJob::State::Cancelled)) {
                    throw std::runtime_error("Operation has already started; cancellation cannot undo it");
                }
            }
            return OperationResult(found->second);
        }
        const auto tool = std::find_if(_tools.begin(), _tools.end(), [&](const auto &entry) {
            return entry.at("name") == name;
        });
        if (tool == _tools.end()) {
            throw std::runtime_error("Unknown tool");
        }
        const bool mutation   = !tool->at("annotations").value("readOnlyHint", false);
        const std::string key = arguments.value("idempotencyKey", std::string {});
        if (const auto retained = RetainedOperationResult(name, arguments)) {
            return *retained;
        }
        if (_operations.size() >= 128) {
            const auto old = std::find_if(_operations.begin(), _operations.end(), [](const auto &entry) {
                return entry.second.done;
            });
            if (old == _operations.end()) {
                throw std::runtime_error("Operation capacity reached; wait for pending work");
            }
            _operations.erase(old);
        }
        if (mutation && arguments.contains("expectedRevision") && arguments["expectedRevision"] != std::to_string(_revision)) {
            throw std::runtime_error("Stale revision; read current state before editing");
        }
        if (mutation) {
            for (const auto &[id, operation] : _operations) {
                if (!operation.done) {
                    throw std::runtime_error("Another operation is still pending");
                }
            }
        }
        nlohmann::json result;
        auto clean = arguments;
        clean.erase("expectedRevision");
        clean.erase("idempotencyKey");
        const auto generation = Generation();
        struct DispatchScope {
            std::uint64_t &slot;
            explicit DispatchScope(std::uint64_t &slot, std::uint64_t generation): slot(slot) {
                slot = generation;
            }
            ~DispatchScope() {
                slot = 0;
            }
        } dispatchScope(editor._mcpDispatchGeneration, generation);
        const bool handled = AuthoringMcp::Execute(editor, snapshot, name, clean, result) || ProjectMcp::Execute(editor, snapshot, name, clean, result);
        std::uint64_t collisionSerial = 0;
        if (handled && mutation) {
            editor.RecordAuthoredChanges("MCP edit");
            const auto beforeCollision = editor._nextCommand.load();
            editor.PublishAreaCollision(snapshot);
            if (editor._nextCommand.load() != beforeCollision) {
                collisionSerial = editor._nextCommand.load();
            }
        }
        if (!handled) {
            result = Native(editor, name, clean);
        }
        if (mutation) {
            ++_revision;
        }
        if (result.contains("operationId")) {
            auto &operation       = _operations.at(result["operationId"].get<std::string>());
            operation.key         = key;
            operation.fingerprint = name + arguments.dump();
            return OperationResult(operation);
        }
        if (!mutation) {
            result["revision"] = std::to_string(_revision);
            return result;
        }
        Operation operation;
        operation.id          = _instance + "/" + std::to_string(++_nextOperation);
        operation.name        = name;
        operation.key         = key;
        operation.fingerprint = name + arguments.dump();
        operation.collisionSerial = collisionSerial;
        operation.serial      = editor._nextCommand.load(std::memory_order_acquire);
        operation.generation  = generation;
        operation.done        = snapshot.completedCommand >= operation.serial;
        operation.result      = std::move(result);
        const auto response   = OperationResult(operation);
        _operations.emplace(operation.id, std::move(operation));
        while (_operations.size() > 128) {
            const auto old = std::find_if(_operations.begin(), _operations.end(), [](const auto &entry) {
                return entry.second.done;
            });
            if (old == _operations.end()) {
                break;
            }
            _operations.erase(old);
        }
        Log(name, "accepted");
        return response;
    }
} // namespace KCDC::Features::Prop
