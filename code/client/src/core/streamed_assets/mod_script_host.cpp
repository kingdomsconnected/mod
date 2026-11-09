#include <utils/safe_win32.h>

#include "mod_script_host.h"

#include "core/obfuscation.h"

#include "sdk/cryengine/scriptsystem/i_script_system.h"

#include "shared/streamed_assets/lua_script_scan.h"

#include <logging/logger.h>
#include <utils/crypto.h>

#include <fstream>
#include <iterator>

namespace KCDC::Core::StreamedAssets {
    namespace {
        // Everything a mod's script can reach goes through this environment; `_G` is the restricted
        // view, so a name built at run time finds what a plain name finds. Binds read through
        // untouched: the list below is what is taken away, never what is let in.
        constexpr char kHostChunk[] = R"lua(
__kcdc_mod_host = (function()
    local G = _G
    local type, pairs, ipairs, pcall, xpcall, tostring, select, setmetatable, getmetatable, rawget, loadstring, setfenv, getfenv = type, pairs, ipairs, pcall, xpcall, tostring, select, setmetatable, getmetatable, rawget, loadstring, setfenv, getfenv
    local SYSTEM, SCRIPT, UIACTION, STRING, OS, DEBUG, IO, TABLE, MATH = System, Script, UIAction, string, os, debug, io, table, math

    local DENIED = { package = true, require = true, loadlib = true, dofile = true, loadfile = true, loadstring = true, load = true, getfenv = true, setfenv = true, newproxy = true, io = true }
    -- Console commands that load files, change the session or run text later.
    local BLOCKED_COMMANDS = { exec = true, map = true, connect = true, disconnect = true, quit = true, exit = true, bind = true, load = true, save = true, loadgame = true, savegame = true }

    local host = { generation = 0 }
    local session = nil

    local function log(text) SYSTEM.LogAlways("[KCDC mod] " .. text) end
    local function normalize(path) return STRING.lower((STRING.gsub(path, "\\", "/"))) end
    local traceback = DEBUG and DEBUG.traceback or tostring

    local function proxy(real, overrides)
        return setmetatable({}, { __metatable = false, __index = function(_, key)
            local value = overrides[key]
            if value ~= nil then return value end
            return real[key]
        end })
    end

    local function compile(source, name, env)
        if STRING.sub(source, 1, 1) == "\27" then return nil, "precompiled code is refused" end
        local chunk, failure = loadstring(source, name)
        if chunk then setfenv(chunk, env) end
        return chunk, failure
    end

    -- Whether the mod has this script at all, then whether it ran.
    local function run(mod, path)
        local key = normalize(path)
        local source = mod.sources[key]
        if not source then return false end
        mod.loaded[key] = true
        local chunk, failure = compile(source, "@" .. key, mod.env)
        if not chunk then
            log(mod.name .. ": " .. tostring(failure))
            return true, false
        end
        local ok, problem = xpcall(chunk, traceback)
        if not ok then log(mod.name .. ": " .. tostring(problem)) end
        return true, ok
    end

    -- A spawned trigger's command is compiled and run in the game's own globals when it fires.
    local function spawnRunsScript(params)
        local properties = type(params) == "table" and params.properties
        if type(properties) ~= "table" then return false end
        for key, value in pairs(properties) do
            if type(key) == "string" and STRING.find(STRING.lower(key), "scriptcommand", 1, true) and value ~= "" then return true end
        end
        return false
    end

    local function parseCommand(part)
        local name, rest = STRING.match(part, "^%s*([^%s=]+)(.*)$")
        return name, rest
    end

    local function runModCommand(name, ...)
        local command = session and session.commands[name]
        if not command then return end
        local arguments = { ... }
        local text = STRING.gsub(command.code, "%%(%d)", function(digit) return arguments[digit + 0] or "" end)
        local chunk, failure = compile(text, "@command " .. name, command.env)
        if not chunk then
            log(name .. ": " .. tostring(failure))
            return
        end
        local ok, problem = xpcall(chunk, traceback)
        if not ok then log(name .. ": " .. tostring(problem)) end
    end
    host.command = runModCommand

    local function makeEnvironment(mod, granted)
        local current = session
        local env = {}
        local overrides = {}
        setmetatable(env, { __metatable = false, __index = function(_, key)
            local value = overrides[key]
            if value ~= nil then return value end
            if DENIED[key] or (type(key) == "string" and STRING.sub(key, 1, 6) == "__kcdc") then return nil end
            return G[key]
        end })
        mod.env = env
        overrides._G = env
        overrides.rawget = function(t, key)
            if t == env then return env[key] end
            return rawget(t, key)
        end
        overrides.getmetatable = function(value)
            if type(value) == "string" then return nil end
            return getmetatable(value)
        end

        local safeString = {}
        for key, value in pairs(STRING) do
            if key ~= "dump" then safeString[key] = value end
        end
        overrides.string = safeString
        overrides.debug = { traceback = traceback }

        local fullOs = false
        for name in STRING.gmatch(granted, "[^,]+") do
            if name == "os" or STRING.sub(name, 1, 3) == "os." then fullOs = true end
            if name == "io" then overrides.io = IO end
            if name == "loadstring" then
                overrides.loadstring = function(source, name) return compile(source, name or "=mod", env) end
            end
            if name == "load" then
                overrides.load = function(reader, name)
                    local pieces = {}
                    while true do
                        local piece = reader()
                        if piece == nil or piece == "" then break end
                        pieces[#pieces + 1] = piece
                    end
                    return compile(TABLE.concat(pieces), name or "=mod", env)
                end
            end
            if name == "dofile" or name == "loadfile" then
                local function read(path)
                    local file, failure = IO.open(path, "rb")
                    if not file then return nil, failure end
                    local text = file:read("*a")
                    file:close()
                    return compile(text, "@" .. path, env)
                end
                if name == "loadfile" then overrides.loadfile = read end
                if name == "dofile" then
                    overrides.dofile = function(path)
                        local chunk, failure = read(path)
                        if not chunk then error(failure, 2) end
                        return chunk()
                    end
                end
            end
        end
        overrides.os = fullOs and OS or { time = OS.time, date = OS.date, clock = OS.clock, difftime = OS.difftime }

        local function timer(ms, callback, ...)
            local generation = host.generation
            return SCRIPT.SetTimer(ms, function(...)
                if host.generation == generation then return callback(...) end
            end, ...)
        end
        overrides.Script = proxy(SCRIPT, {
            ReloadScript = function(path, ...)
                local found, ok = run(mod, path)
                if found then return ok end
                return SCRIPT.ReloadScript(path, ...)
            end,
            LoadScript = function(path, ...)
                if mod.sources[normalize(path)] then
                    if mod.loaded[normalize(path)] then return true end
                    local _, ok = run(mod, path)
                    return ok
                end
                return SCRIPT.LoadScript(path, ...)
            end,
            SetTimer = timer,
            SetTimerForFunction = function(ms, name, argument)
                return timer(ms, function()
                    local value = env
                    for part in STRING.gmatch(name, "[^.]+") do
                        value = type(value) == "table" and value[part] or nil
                    end
                    if type(value) == "function" then value(argument) end
                end)
            end,
        })

        overrides.System = proxy(SYSTEM, {
            AddCCommand = function(name, code, help)
                if type(name) ~= "string" or type(code) ~= "string" or not STRING.match(name, "^[%w_]+$") then
                    log(mod.name .. ": refused a console command with an unusable name")
                    return
                end
                current.commands[name] = { code = code, env = env }
                local highest = 0
                for digit in STRING.gmatch(code, "%%(%d)") do highest = MATH.max(highest, digit + 0) end
                local arguments = ""
                for index = 1, highest do arguments = arguments .. ', "%' .. index .. '"' end
                return SYSTEM.AddCCommand(name, '__kcdc_mod_host.command("' .. name .. '"' .. arguments .. ")", help)
            end,
            ExecuteCommand = function(text, ...)
                if type(text) ~= "string" then return end
                local parts = {}
                for part in STRING.gmatch(text, "[^;]+") do
                    local name = parseCommand(part)
                    if name then
                        local folded = STRING.lower(name)
                        local first = STRING.sub(name, 1, 1)
                        if first == "#" or first == "@" or BLOCKED_COMMANDS[folded] then
                            log(mod.name .. ": refused console command '" .. name .. "'")
                            return
                        end
                        parts[#parts + 1] = part
                    end
                end
                for _, part in ipairs(parts) do
                    local name, rest = parseCommand(part)
                    -- A mod's own command never goes through the console, whose argument text
                    -- would land inside a line run in the game's own globals.
                    if current.commands[name] then
                        local arguments = {}
                        for word in STRING.gmatch(rest, "%S+") do arguments[#arguments + 1] = word end
                        runModCommand(name, unpack(arguments))
                    else
                        SYSTEM.ExecuteCommand(part, ...)
                    end
                end
            end,
            SetCVar = function(name, value, ...)
                local key = STRING.lower(tostring(name))
                if current.cvars[key] == nil then
                    local ok, old = pcall(SYSTEM.GetCVar, name)
                    if ok and old ~= nil then current.cvars[key] = { name = name, value = old } end
                end
                return SYSTEM.SetCVar(name, value, ...)
            end,
            SpawnEntity = function(params, ...)
                if spawnRunsScript(params) then
                    log(mod.name .. ": refused an entity that would run a script command")
                    return nil
                end
                return SYSTEM.SpawnEntity(params, ...)
            end,
        })

        overrides.UIAction = proxy(UIACTION, {
            RegisterEventSystemListener = function(listener, system, event, callback)
                current.listeners[#current.listeners + 1] = { listener = listener, callback = callback }
                return UIACTION.RegisterEventSystemListener(listener, system, event, callback)
            end,
        })
    end

    local function snapshot()
        local copies = {}
        for name, value in pairs(G) do
            if type(value) == "table" and value ~= G and name ~= "__kcdc_mod_host" then
                local fields = {}
                for key, field in pairs(value) do fields[key] = field end
                copies[value] = fields
            end
        end
        return copies
    end

    -- Takes back a function a mod wrote into one of the game's tables. Only what the mod wrote:
    -- the rest of the game keeps whatever it changed meanwhile.
    local function restoreGameTables(ended)
        local environments = {}
        for _, mod in pairs(ended.mods) do environments[mod.env] = true end
        local restored = 0
        for tableValue, fields in pairs(ended.snapshot) do
            for key, value in pairs(tableValue) do
                if value ~= fields[key] and type(value) == "function" then
                    local ok, environment = pcall(getfenv, value)
                    if ok and environments[environment] then
                        tableValue[key] = fields[key]
                        restored = restored + 1
                    end
                end
            end
        end
        return restored
    end

    function host.begin()
        host.generation = host.generation + 1
        session = { mods = {}, commands = {}, cvars = {}, listeners = {}, snapshot = snapshot() }
    end

    function host.mod(name, granted)
        local mod = { name = name, sources = {}, loaded = {} }
        session.mods[name] = mod
        makeEnvironment(mod, granted)
    end

    function host.add(name, path, source)
        session.mods[name].sources[path] = source
    end

    function host.start()
        local names = {}
        for name in pairs(session.mods) do names[#names + 1] = name end
        TABLE.sort(names)
        for _, name in ipairs(names) do
            local mod = session.mods[name]
            local entries = {}
            for path in pairs(mod.sources) do
                if STRING.match(path, "^scripts/mods/[^/]+%.lua$") then entries[#entries + 1] = path end
            end
            TABLE.sort(entries)
            for _, path in ipairs(entries) do run(mod, path) end
            log(name .. ": " .. #entries .. " entry script(s) started")
        end
    end

    function host.stop()
        local ended = session
        if not ended then return end
        host.generation = host.generation + 1
        session = nil
        for _, entry in ipairs(ended.listeners) do
            pcall(UIACTION.UnregisterEventSystemListener, entry.listener, entry.callback)
        end
        local cvars = 0
        for _, entry in pairs(ended.cvars) do
            pcall(SYSTEM.SetCVar, entry.name, entry.value)
            cvars = cvars + 1
        end
        local restored = restoreGameTables(ended)
        log("session ended: " .. #ended.listeners .. " listener(s) removed, " .. cvars .. " console variable(s) and " .. restored .. " game function(s) restored")
    end

    return host
end)()
)lua";

        std::string JoinRisky(const std::vector<std::string> &risky) {
            std::string joined;
            for (const std::string &name : risky) {
                joined += (joined.empty() ? "" : ",") + name;
            }
            return joined;
        }

        KCDC::Shared::StreamedAssets::LuaScriptApprovals ReadApprovals(const std::filesystem::path &file) {
            std::ifstream stream(file, std::ios::binary);
            const std::string text {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
            return KCDC::Shared::StreamedAssets::LuaScriptApprovals::Parse(text);
        }
    } // namespace

    void ModScriptHost::ClearStaged() {
        _staged.clear();
    }

    bool ModScriptHost::Stage(const std::string &resource, const std::string &pakPath, std::vector<Source> sources, std::string &error) {
        Mod mod;
        mod.resource = resource;
        std::string hashed;
        std::set<std::string> risky;
        for (const Source &source : sources) {
            const KCDC::Shared::StreamedAssets::LuaScriptScan scan = KCDC::Shared::StreamedAssets::ScanLuaScript(source.second);
            if (scan.unreadable) {
                error = "A script from '" + resource + "' is precompiled and was refused";
                return false;
            }
            risky.insert(scan.risky.begin(), scan.risky.end());
            hashed += source.first;
            hashed.push_back('\n');
            hashed += source.second;
            hashed.push_back('\n');
        }
        mod.digest  = Framework::Utils::Crypto::Sha256Hex(hashed);
        mod.risky   = std::vector<std::string>(risky.begin(), risky.end());
        mod.sources = std::move(sources);
        _staged.push_back(std::move(mod));
        // <mod>/cache/servers/<server>/<pak>: an approval holds across servers, so beside them.
        _approvalsFile = std::filesystem::path(pakPath).parent_path().parent_path().parent_path() / "script_approvals.txt";
        return true;
    }

    std::vector<std::string> ModScriptHost::DescribeUnapproved() const {
        const KCDC::Shared::StreamedAssets::LuaScriptApprovals approvals = ReadApprovals(_approvalsFile);
        std::vector<std::string> lines;
        for (const Mod &mod : _staged) {
            if (!mod.risky.empty() && !approvals.Contains(mod.resource, mod.digest)) {
                std::string asks = JoinRisky(mod.risky);
                for (std::size_t comma = asks.find(','); comma != std::string::npos; comma = asks.find(',', comma + 2)) {
                    asks.insert(comma + 1, " ");
                }
                lines.push_back(mod.resource + ": " + asks);
            }
        }
        return lines;
    }

    void ModScriptHost::ApproveStaged() {
        KCDC::Shared::StreamedAssets::LuaScriptApprovals approvals = ReadApprovals(_approvalsFile);
        for (const Mod &mod : _staged) {
            if (!mod.risky.empty()) {
                approvals.Add(mod.resource, mod.digest);
            }
        }
        std::ofstream stream(_approvalsFile, std::ios::binary | std::ios::trunc);
        stream << approvals.ToText();
    }

    bool ModScriptHost::Call(std::string_view method, std::initializer_list<const char *> arguments) const {
        SDK::IScriptSystem *scripts = SDK::IScriptSystem::GetInstance();
        const std::string table(KCDC_HIDE("__kcdc_mod_host"));
        if (!scripts->BeginCallNamedTableMethod(table.c_str(), std::string(method).c_str())) {
            return false;
        }
        for (const char *argument : arguments) {
            scripts->PushFuncParamAny(SDK::ScriptAnyValue(argument));
        }
        return scripts->EndCall();
    }

    bool ModScriptHost::Install() {
        if (!_installed) {
            _installed = SDK::IScriptSystem::GetInstance()->ExecuteBuffer(kHostChunk, sizeof(kHostChunk) - 1, std::string(KCDC_HIDE("KCDC mod host")).c_str(), nullptr);
        }
        return _installed;
    }

    void ModScriptHost::Start() {
        if (_staged.empty()) {
            return;
        }
        if (!Install()) {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->error("The mod script host could not be installed; the server's scripts are not running");
            return;
        }
        Stop();
        Call("begin", {});
        for (const Mod &mod : _staged) {
            const std::string granted = JoinRisky(mod.risky);
            Call("mod", {mod.resource.c_str(), granted.c_str()});
            for (const Source &source : mod.sources) {
                Call("add", {mod.resource.c_str(), source.first.c_str(), source.second.c_str()});
            }
        }
        _running = Call("start", {});
        Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("Started the scripts of {} server mod(s)", _staged.size());
    }

    void ModScriptHost::Stop() {
        if (!_running) {
            return;
        }
        _running = false;
        Call("stop", {});
        Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("Ended the server mods' scripts");
    }
} // namespace KCDC::Core::StreamedAssets
