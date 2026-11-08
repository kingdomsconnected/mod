#include "native_alchemy_entry.h"

#include "alchemy_entry_continuation.h"
#include "core/obfuscation.h"

#include <cmath>
#include <string_view>

namespace KCDC::Features::Crafting {
    namespace {
        constexpr char kOwnerKey[]      = "__kcdcAlchemyAdmission";
        constexpr char kDispatcherKey[] = "__dispatcher";

        bool EndCall(SDK::IScriptSystem &system, std::string &error) {
            if (system.EndCall()) {
                error.clear();
                return true;
            }
            SDK::ScriptAnyValue value;
            if (system.PopStackValue(value) && value.type == SDK::EScriptAnyType::String && value.str != nullptr) {
                error = value.str;
            }
            else {
                error = std::string(KCDC_HIDE("Alchemy script invocation failed"));
            }
            return false;
        }
    } // namespace

    bool NativeAlchemyEntry::Ensure(SDK::IScriptTable &currentClass, const std::string &generation, std::uint64_t actor, bool enabled, std::string &error) {
        auto control = currentClass.GetTable(std::string(KCDC_HIDE(kOwnerKey)).c_str());
        if (control) {
            const char *existing = nullptr;
            bool retired         = false;
            if (control->GetValue(std::string(KCDC_HIDE("generation")).c_str(), existing) && existing != nullptr && generation == existing && control->GetValue(std::string(KCDC_HIDE("retired")).c_str(), retired) && !retired) {
                control->SetValueAny(std::string(KCDC_HIDE("actor")).c_str(), SDK::ScriptAnyValue::Handle(actor));
                control->SetValue(std::string(KCDC_HIDE("enabled")).c_str(), enabled);
                return Install(currentClass, currentClass, error);
            }
            if (!Retire(currentClass, true, error)) {
                return false;
            }
        }
        auto *system = currentClass.GetScriptSystem();
        if (system == nullptr || generation.empty() || actor == 0) {
            error = std::string(KCDC_HIDE("Alchemy script context is unavailable"));
            return false;
        }
        const auto script = KCDC_HIDE(R"alchemy_lua(-- Requests keep scalar entity handles, never self/user tables.
local startApi = assert(Alchemy, "Alchemy bindings are unavailable")
local startFunction = assert(startApi.StartAlchemy, "Alchemy native entry callback is unavailable")
local ownerControl = nil
local records = {}
local originals = {}
local nextTicket = 0
local function clearRequest(control)
    control.pending = false
    control.station = nil
    control.user = nil
    control.ticket = nil
    control.requestGeneration = nil
    control.source = nil
end
local function install(control, target)
    assert(type(target) == "table", "Alchemy entry table is unavailable")
    local current = target.OnUsed
    assert(type(current) == "function", "Alchemy entry callback is unavailable")
    local previous = records[target]
    if previous and current == previous.wrapper then
        return
    end
    local original = originals[current] or current
    local wrapper = function(self, user, ...)
        if control.retired then
            return
        end
        if not control.enabled then
            return original(self, user, ...)
        end
        if type(self) ~= "table" or type(user) ~= "table" or self.id == nil or user.id == nil then
            control.failure = "Invalid alchemy entry context"
            return
        end
        if user.id ~= control.actor then
            control.failure = "Alchemy entry actor changed"
            return
        end
        -- The interactor calls an action's func with trailing arguments of
        -- its own; the shipped OnUsed reads only (self, user), so the
        -- approved replay passes exactly those and the rest are dropped.
        if control.pending or control.replaying then
            return
        end
        nextTicket = nextTicket + 1
        if nextTicket > 16777215 then
            control.failure = "Alchemy entry ticket range exhausted"
            return
        end
        control.pending = true
        control.station = self.id
        control.user = user.id
        control.ticket = nextTicket
        control.requestGeneration = control.generation
        control.source = target
    end
    originals[wrapper] = original
    records[target] = { original = original, wrapper = wrapper }
    target.OnUsed = wrapper
end

return function(operation, control, target, actor, ticket)
    if ownerControl and ownerControl ~= control then
        error("Alchemy dispatcher belongs to another authority generation")
    end
    ownerControl = control
    if operation == 0 then
        assert(not control.retired, "Alchemy entry generation is retired")
        return install(control, target)
    elseif operation == 1 then
        -- C++ has re-resolved the station and body and holds the server's
        -- approval; this checks the script generation itself.
        if control.retired or not control.enabled or not control.pending or control.ticket ~= ticket or control.requestGeneration ~= control.generation then
            control.failure = "Stale alchemy entry continuation"
            return
        end
        if type(target) ~= "table" or type(actor) ~= "table" or target.id ~= control.station or actor.id ~= control.user or actor.id ~= control.actor then
            control.failure = "Alchemy entry identity changed"
            return
        end
        local source = control.source
        local record = records[source]
        if not record or source.OnUsed ~= record.wrapper then
            control.failure = "Alchemy entry callback was replaced"
            return
        end
        local original = record.original
        local nativeStart = startApi.StartAlchemy
        if Alchemy ~= startApi or type(nativeStart) ~= "function" or nativeStart ~= startFunction then
            control.failure = "Alchemy native entry callback was replaced"
            return
        end
        local expectedUser, expectedStation = control.user, control.station
        local expectedGeneration = control.requestGeneration
        local startCalled, startAccepted = false, false
        local function checkedStart(userId, stationId, ...)
            if startCalled or select("#", ...) ~= 0 or userId ~= expectedUser or stationId ~= expectedStation then
                error("Alchemy native entry arguments changed")
            end
            startCalled = true
            -- The C++ bridge returns C_Alchemy::Start's Boolean through
            -- EndFunctionAny for this call; retail returns nothing.
            startAccepted = nativeStart(userId, stationId) == true
            if not startAccepted then
                error("Native alchemy start was not accepted")
            end
            -- A native callback can retire or replace this generation before
            -- returning. Do not let the remaining OnUsed effects run then.
            if control.retired or not control.enabled or control.generation ~= expectedGeneration or control.actor ~= expectedUser or Alchemy ~= startApi or startApi.StartAlchemy ~= checkedStart then
                error("Alchemy entry context changed during native start")
            end
            return true
        end
        clearRequest(control)
        control.replaying = true
        control.startedTicket = ticket
        startApi.StartAlchemy = checkedStart
        local ok, result = pcall(original, target, actor)
        local ownsStartFunction = startApi.StartAlchemy == checkedStart
        local stillOwned = Alchemy == startApi and ownsStartFunction
        if ownsStartFunction then
            startApi.StartAlchemy = nativeStart
        end
        control.replaying = false
        if not ok or not stillOwned or not startCalled or not startAccepted then
            control.failure = "Alchemy entry continuation failed"
            error(ok and control.failure or result)
        end
        control.completedTicket = ticket
    elseif operation == 2 then
        clearRequest(control)
    elseif operation == 3 then
        control.retired = true
        clearRequest(control)
    elseif operation == 4 then
        -- Restore only the callbacks this generation still owns: a script
        -- reload may have installed newer ones.
        control.retired = true
        clearRequest(control)
        for source, record in pairs(records) do
            if source.OnUsed == record.wrapper then
                source.OnUsed = record.original
            end
        end
        records = {}
        originals = {}
    else
        error("Invalid alchemy entry dispatcher operation")
    end
end
)alchemy_lua");
        const std::string_view text(script);
        if (!system->ExecuteBuffer(text.data(), text.size(), std::string(KCDC_HIDE("native entry dispatch")).c_str(), nullptr)) {
            error = std::string(KCDC_HIDE("Alchemy entry dispatcher could not be installed"));
            return false;
        }
        SDK::ScriptAnyValue function;
        function.type = SDK::EScriptAnyType::Function;
        if (!system->PopStackValue(function) || function.type != SDK::EScriptAnyType::Function || function.function == 0) {
            error = std::string(KCDC_HIDE("Alchemy entry dispatcher returned no function"));
            return false;
        }
        auto *created = system->CreateTable(false);
        if (created == nullptr) {
            system->ReleaseFunc(function.function);
            error = std::string(KCDC_HIDE("Alchemy entry control allocation failed"));
            return false;
        }
        created->AddRef();
        SDK::ScriptTablePtr owned(created);
        created->SetValueAny(std::string(KCDC_HIDE(kDispatcherKey)).c_str(), function);
        system->ReleaseFunc(function.function);
        created->SetValue(std::string(KCDC_HIDE("generation")).c_str(), generation.c_str());
        created->SetValue(std::string(KCDC_HIDE("enabled")).c_str(), enabled);
        created->SetValue(std::string(KCDC_HIDE("retired")).c_str(), false);
        created->SetValueAny(std::string(KCDC_HIDE("actor")).c_str(), SDK::ScriptAnyValue::Handle(actor));
        currentClass.SetValue(std::string(KCDC_HIDE(kOwnerKey)).c_str(), created);
        return Install(currentClass, currentClass, error);
    }

    bool NativeAlchemyEntry::Invoke(SDK::IScriptTable &currentClass, float operation, SDK::IScriptTable *target, SDK::IScriptTable *actor, std::uint32_t ticket, std::string &error) {
        auto control = currentClass.GetTable(std::string(KCDC_HIDE(kOwnerKey)).c_str());
        auto *system = currentClass.GetScriptSystem();
        if (!control || system == nullptr) {
            error = std::string(KCDC_HIDE("Alchemy dispatcher generation is missing"));
            return false;
        }
        SDK::ScriptAnyValue function;
        function.type = SDK::EScriptAnyType::Function;
        if (!control->GetValueAny(std::string(KCDC_HIDE(kDispatcherKey)).c_str(), function) || function.type != SDK::EScriptAnyType::Function || function.function == 0) {
            error = std::string(KCDC_HIDE("Alchemy dispatcher function is missing"));
            return false;
        }
        if (!system->BeginCallFunction(function.function)) {
            system->ReleaseFunc(function.function);
            error = std::string(KCDC_HIDE("Alchemy dispatcher invocation could not begin"));
            return false;
        }
        system->PushFuncParamAny(SDK::ScriptAnyValue(operation));
        system->PushFuncParamAny(SDK::ScriptAnyValue(control.get()));
        system->PushFuncParamAny(target != nullptr ? SDK::ScriptAnyValue(target) : SDK::ScriptAnyValue::Nil());
        system->PushFuncParamAny(actor != nullptr ? SDK::ScriptAnyValue(actor) : SDK::ScriptAnyValue::Nil());
        system->PushFuncParamAny(SDK::ScriptAnyValue(static_cast<float>(ticket)));
        const bool completed = EndCall(*system, error);
        system->ReleaseFunc(function.function);
        if (!completed) {
            return false;
        }
        const char *failure = nullptr;
        if (operation < 3.0F && control->GetValue(std::string(KCDC_HIDE("failure")).c_str(), failure) && failure != nullptr && *failure != '\0') {
            error = failure;
            return false;
        }
        error.clear();
        return true;
    }

    bool NativeAlchemyEntry::Install(SDK::IScriptTable &currentClass, SDK::IScriptTable &target, std::string &error) {
        return Invoke(currentClass, 0.0F, &target, nullptr, 0, error);
    }

    bool NativeAlchemyEntry::ReadRequest(SDK::IScriptTable &currentClass, AlchemyScriptRequest &request, std::string &error) {
        auto control = currentClass.GetTable(std::string(KCDC_HIDE(kOwnerKey)).c_str());
        bool pending = false;
        if (!control || !control->GetValue(std::string(KCDC_HIDE("pending")).c_str(), pending) || !pending) {
            error.clear();
            return false;
        }
        SDK::ScriptAnyValue actor, station;
        actor.type = station.type = SDK::EScriptAnyType::Handle;
        float ticket              = 0.0F;
        const char *generation    = nullptr;
        if (!control->GetValueAny(std::string(KCDC_HIDE("user")).c_str(), actor) || actor.type != SDK::EScriptAnyType::Handle || !control->GetValueAny(std::string(KCDC_HIDE("station")).c_str(), station) || station.type != SDK::EScriptAnyType::Handle
            || !control->GetValue(std::string(KCDC_HIDE("ticket")).c_str(), ticket) || !std::isfinite(ticket) || ticket < 1.0F || ticket > AlchemyEntryContinuation::kMaximumLuaTicket || std::trunc(ticket) != ticket
            || !control->GetValue(std::string(KCDC_HIDE("requestGeneration")).c_str(), generation) || generation == nullptr) {
            error = std::string(KCDC_HIDE("Alchemy script request is malformed"));
            return false;
        }
        request = {actor.handle, station.handle, static_cast<std::uint32_t>(ticket), generation};
        error.clear();
        return true;
    }

    bool NativeAlchemyEntry::Replay(SDK::IScriptTable &currentClass, SDK::IScriptTable &station, SDK::IScriptTable &actor, std::uint32_t ticket, std::string &error) {
        if (!Invoke(currentClass, 1.0F, &station, &actor, ticket, error)) {
            return false;
        }
        auto control    = currentClass.GetTable(std::string(KCDC_HIDE(kOwnerKey)).c_str());
        float completed = 0.0F;
        if (!control || !control->GetValue(std::string(KCDC_HIDE("completedTicket")).c_str(), completed) || completed != static_cast<float>(ticket)) {
            error = std::string(KCDC_HIDE("Alchemy callback did not complete its admitted ticket"));
            return false;
        }
        return true;
    }

    bool NativeAlchemyEntry::Retire(SDK::IScriptTable &currentClass, bool restore, std::string &error) {
        auto control = currentClass.GetTable(std::string(KCDC_HIDE(kOwnerKey)).c_str());
        if (!control) {
            error.clear();
            return true;
        }
        control->SetValue(std::string(KCDC_HIDE("retired")).c_str(), true);
        const bool completed = Invoke(currentClass, restore ? 4.0F : 3.0F, nullptr, nullptr, 0, error);
        if (restore && completed) {
            currentClass.SetValueAny(std::string(KCDC_HIDE(kOwnerKey)).c_str(), SDK::ScriptAnyValue::Nil());
        }
        return completed;
    }
} // namespace KCDC::Features::Crafting
