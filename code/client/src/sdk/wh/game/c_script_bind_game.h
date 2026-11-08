#pragma once

namespace SDK::wh::game {
    /**
     * The `Game` script table's native side. Opaque: the mod only detours
     * `SetPrefabPhaseById` (0x1809E5584), a member taking `(IFunctionHandler *,
     * ScriptHandle entity, const char *category, int phase)`, and never reads
     * the bind itself or constructs one.
     */
    class CScriptBindGame final {
      public:
        CScriptBindGame()                                   = delete;
        CScriptBindGame(const CScriptBindGame &)            = delete;
        CScriptBindGame &operator=(const CScriptBindGame &) = delete;
    };
} // namespace SDK::wh::game
