#pragma once

#include "../../cryengine/scriptsystem/i_script_system.h"
#include "../../patterns.h"
#include "../../types.h"
#include "../shared/c_model_property.h"
#include "../shared/c_signal.h"

#include <utils/hooking/hooking.h>

#include <cstddef>
#include <cstdint>

namespace SDK::wh::entitymodule {
    /**
     * What an interaction prompt is, for the filters that enable or disable
     * it: the `inr_*` Lua globals.
     *
     * Not a compiled enum. `LoadInteractionFilter` (0x180DC050C) reads
     * `Libs/Config/interaction_filter.xml` and numbers each name from 1 in the
     * file's `<interactions>` order (`InternInteractionName`, 0x180DC08D0),
     * publishing `inr_<name>` as a Lua global only as a courtesy to scripts.
     * The values belong to the data, so they are looked up by name at runtime
     * with `FindInteractionType` rather than written here.
     */
    enum class E_InteractionType : std::uint32_t {
        None = 0
    };

    /**
     * The interaction type registered under `name` -- `"stashOpen"`, not
     * `"inr_stashOpen"` -- or `None` when the filter has not been loaded or has
     * no such interaction. A look-up only: unlike the interner it never
     * assigns a new id.
     *
     * The native returns through a pointer in RCX, spelled out here.
     */
    inline E_InteractionType FindInteractionType(const char *name) {
        E_InteractionType type = E_InteractionType::None;
        hook::call<void>(gPatterns.entitymodule__FindInteractionType, &type, name);
        return type;
    }

    /**
     * One prompt as `C_ScriptBindPlayer::AddLuaActions` (0x180A2CFC0) builds
     * it from an entry of an entity's Lua GetActions output, and hands to
     * `C_PlayerInteractor::AddLuaAction`, which copies it into the interactor.
     *
     * The type has no name of its own in the binary, so the name here is this
     * SDK's. Its layout comes from the stack slots AddLuaActions fills and
     * from its initialiser, `InitInteractorActionRecord` (0x180ABB35C); the
     * 0xE0 bytes are the span of that stack object.
     *
     * AddLuaAction reads `m_enabled` before it asks the interaction filter
     * and before it inserts the copy, so a prompt cleared here before the
     * original runs is registered disabled -- the engine's own way to refuse
     * a prompt, with no Lua involved. Cleared together with `m_uiVisible` the
     * prompt is not drawn at all (seen live); which of the two the hint UI
     * reads for that has not been traced.
     */
    struct S_InteractorActionRecord {
        CryStringT<char> m_action;               // 0x00, "use", "use_other"
        CryStringT<char> m_actionMap;            // 0x08, "player" by default
        std::byte m_opaque0010[0x4];             // 0x10
        std::uint8_t m_hintType;                 // 0x14, AHT_PRESS / RELEASE / HOLD
        std::uint8_t m_hintClass;                // 0x15, 7 is the dog's
        std::byte m_padding0016[0x2];            // 0x16
        EntityId m_targetEntityId;               // 0x18, written by AddLuaAction
        std::byte m_padding001C[0x4];            // 0x1C
        CryStringT<char> m_hint;                 // 0x20
        std::byte m_opaque0028[0x1];             // 0x28
        bool m_uiVisible;                        // 0x29
        std::byte m_padding002A[0x2];            // 0x2A
        std::int32_t m_uiOrder;                  // 0x2C
        std::byte m_opaque0030[0x10];            // 0x30
        bool m_enabled;                          // 0x40
        std::byte m_padding0041[0x7];            // 0x41
        CryStringT<char> m_reason;               // 0x48
        CryStringT<char> m_disabledBarkMetarole; // 0x50
        std::byte m_opaque0058[0x78];            // 0x58
        void *m_ptr00D0;                         // 0xD0, set: RefreshActionHints won't force-draw it while held
        std::byte m_opaque00D8[0x8];             // 0xD8
    };

    static_assert(sizeof(S_InteractorActionRecord) == 0xE0, "S_InteractorActionRecord size mismatch");
    static_assert(offsetof(S_InteractorActionRecord, m_actionMap) == 0x08, "S_InteractorActionRecord::m_actionMap offset mismatch");
    static_assert(offsetof(S_InteractorActionRecord, m_hintType) == 0x14, "S_InteractorActionRecord::m_hintType offset mismatch");
    static_assert(offsetof(S_InteractorActionRecord, m_hintClass) == 0x15, "S_InteractorActionRecord::m_hintClass offset mismatch");
    static_assert(offsetof(S_InteractorActionRecord, m_targetEntityId) == 0x18, "S_InteractorActionRecord::m_targetEntityId offset mismatch");
    static_assert(offsetof(S_InteractorActionRecord, m_hint) == 0x20, "S_InteractorActionRecord::m_hint offset mismatch");
    static_assert(offsetof(S_InteractorActionRecord, m_uiVisible) == 0x29, "S_InteractorActionRecord::m_uiVisible offset mismatch");
    static_assert(offsetof(S_InteractorActionRecord, m_uiOrder) == 0x2C, "S_InteractorActionRecord::m_uiOrder offset mismatch");
    static_assert(offsetof(S_InteractorActionRecord, m_enabled) == 0x40, "S_InteractorActionRecord::m_enabled offset mismatch");
    static_assert(offsetof(S_InteractorActionRecord, m_reason) == 0x48, "S_InteractorActionRecord::m_reason offset mismatch");
    static_assert(offsetof(S_InteractorActionRecord, m_ptr00D0) == 0xD0, "S_InteractorActionRecord::m_ptr00D0 offset mismatch");
    static_assert(offsetof(S_InteractorActionRecord, m_disabledBarkMetarole) == 0x50, "S_InteractorActionRecord::m_disabledBarkMetarole offset mismatch");

    /**
     * The local player's interactor, which turns an entity's Lua GetActions
     * output into the prompts on screen.
     *
     * `C_Player`'s constructor allocates 0x5B8 bytes for it (0x180BC7B01) and
     * keeps the pointer at +0xB08. Only the target is modelled: the primary
     * and listener vtables at +0x00 / +0x10 and everything else stay opaque.
     */
    class C_PlayerInteractor {
      public:
        C_PlayerInteractor()                                      = delete;
        C_PlayerInteractor(const C_PlayerInteractor &)            = delete;
        C_PlayerInteractor &operator=(const C_PlayerInteractor &) = delete;

        /**
         * The selected target: the entity whose prompts are being registered
         * and are on screen. AddLuaAction copies it into each record and into
         * the callback that runs the handler.
         *
         * `UpdateInteractions` (0x1807F2DD4) clears it every scan
         * (`ClearSelectedInteractionTarget`, 0x1809ECB84) and writes it back
         * only from a candidate `IsCandidateUsable` (0x18082A988) passed --
         * the game rules' Lua `IsUsable` -- so nonzero means the game is
         * showing its own prompt for that entity.
         */
        EntityId GetTargetEntityId() const {
            return m_targetEntityId;
        }

      private:
        // Laid out by the constructor 0x1809EDF08.
        const void *m_pVTable;          // 0x00
        std::uint64_t m_state0008;      // 0x08
        const void *m_pSecondaryVTable; // 0x10
        wh::shared::C_Signal m_signal0018; // 0x18
        wh::shared::C_Signal m_signal0028; // 0x28
        std::byte m_opaque0038[0x50];   // 0x38, built by 0x1809ECF64
        wh::shared::C_ModelProperty<SDK::Vec2> m_vec2Property; // 0x88
        wh::shared::C_Signal m_signal00B8; // 0xB8
        std::byte m_opaque00C8[0x8];    // 0xC8
        std::uint64_t m_state00D0;      // 0xD0, seeded from a process-wide default
        void *m_state00D8[3];           // 0xD8, zeroed by the constructor
        std::uint32_t m_state00F0;      // 0xF0
        EntityId m_targetEntityId;     // 0xF4
        std::byte m_opaque00F8[0x4C0]; // 0xF8

        static void AssertLayout() {
            static_assert(offsetof(C_PlayerInteractor, m_targetEntityId) == 0xF4, "C_PlayerInteractor::m_targetEntityId offset mismatch");
            static_assert(offsetof(C_PlayerInteractor, m_vec2Property) == 0x88, "C_PlayerInteractor::m_vec2Property offset mismatch");
            static_assert(offsetof(C_PlayerInteractor, m_signal00B8) == 0xB8, "C_PlayerInteractor::m_signal00B8 offset mismatch");
        }
    };

    static_assert(sizeof(C_PlayerInteractor) == 0x5B8, "C_PlayerInteractor size mismatch");

    /**
     * What a pressed prompt's callback captured: the handler and the entity
     * it was offered on. `InvokeLuaActionFunc` (0x180B29258) reads the handler
     * from +0 and the target from +8 and runs `func(entity, player, 0)`.
     *
     * The handler is a pointer-sized `HSCRIPTFUNCTION` holding a Lua registry
     * index, so its high half is always zero. The type has no name of its own
     * in the binary -- it is a lambda capture in `SynchronizeInteractionControls`
     * (0x1807F3FCC) -- so the name here is this SDK's.
     */
    struct S_LuaActionInvocation {
        HScriptFunction m_function;   // 0x00
        std::uint32_t m_functionHigh; // 0x04, always zero
        EntityId m_targetEntityId;    // 0x08
        std::uint32_t m_padding000C;  // 0x0C
    };

    static_assert(sizeof(S_LuaActionInvocation) == 0x10, "S_LuaActionInvocation size mismatch");
    static_assert(offsetof(S_LuaActionInvocation, m_function) == 0x00, "S_LuaActionInvocation::m_function offset mismatch");
    static_assert(offsetof(S_LuaActionInvocation, m_targetEntityId) == 0x08, "S_LuaActionInvocation::m_targetEntityId offset mismatch");

    /**
     * Runs a prompt's handler exactly as a press does: `func(entity, player,
     * 0)` through the free `InvokeLuaActionFunc` (0x180B29258), whose first
     * argument the native never reads. Goes through any detour on that entry,
     * so a gate that holds presses must let its own replays pass.
     */
    inline void InvokeLuaAction(HScriptFunction function, EntityId target) {
        S_LuaActionInvocation invocation {};
        invocation.m_function       = function;
        invocation.m_targetEntityId = target;
        hook::call<void>(gPatterns.entitymodule__InvokeLuaActionFunc, nullptr, &invocation);
    }
} // namespace SDK::wh::entitymodule
