#pragma once

#include <utils/safe_win32.h>

#include "sdk/cryengine/scriptsystem/i_script_table.h"

#include <cstdint>
#include <string>

namespace KCDC::Features::Crafting {
    struct AlchemyScriptRequest {
        std::uint64_t actor   = 0;
        std::uint64_t station = 0;
        std::uint32_t ticket  = 0;
        std::string generation;
    };

    /**
     * Wraps AlchemyTable.OnUsed so a table use becomes a request the
     * server approves before the retained original is replayed. Everything
     * retained lives in Lua under the class table; no VM pointer is kept.
     */
    class NativeAlchemyEntry final {
      public:
        bool Ensure(SDK::IScriptTable &currentClass, const std::string &generation, std::uint64_t actor, bool enabled, std::string &error);
        bool Install(SDK::IScriptTable &currentClass, SDK::IScriptTable &target, std::string &error);
        bool ReadRequest(SDK::IScriptTable &currentClass, AlchemyScriptRequest &request, std::string &error);
        bool Replay(SDK::IScriptTable &currentClass, SDK::IScriptTable &station, SDK::IScriptTable &actor, std::uint32_t ticket, std::string &error);
        bool Retire(SDK::IScriptTable &currentClass, bool restore, std::string &error);

      private:
        bool Invoke(SDK::IScriptTable &currentClass, float operation, SDK::IScriptTable *target, SDK::IScriptTable *actor, std::uint32_t ticket, std::string &error);
    };
} // namespace KCDC::Features::Crafting
