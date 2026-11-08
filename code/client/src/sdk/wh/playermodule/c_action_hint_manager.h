#pragma once

#include "../../cryengine/action/i_action_map_event_listener.h"
#include "../../cryengine/action/i_game_object.h"
#include "../../cryengine/system/cry_name.h"
#include "../shared/c_signal.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::entitymodule {
    struct S_InteractorActionRecord;
} // namespace SDK::wh::entitymodule

namespace SDK::wh::playermodule {
    /**
     * Which hints are on screen, and what a key press on one of them means.
     *
     * Owned by `C_PlayerModule` at +0x58; its constructor (0x181748CFC) is
     * reached only from the module's, which allocates 0x140 bytes for it.
     * `C_ActionSets` pushes a map's records in when the map is enabled;
     * `RefreshActionHints` (0x1808E6A6C) keeps those whose map is still
     * active and unfiltered and hands them to the HUD-hints sink at +0x130.
     *
     * `OnAction` runs each record's press/release/hold state machine, under
     * `C_ScopedEngineLock`, and every press it resolves -- including the
     * end of a Hold ring -- reaches `HandleAction` (0x1812D82A4) with the
     * action and the hint type (0 Press, 1 Release, 2 Hold, 3 RawRelease,
     * 4 Repetitive). That is the one place a hint is known to have fired --
     * though not that it was drawn: it matches every record of an enabled,
     * unfiltered map, hidden ones included.
     */
    class C_ActionHintManager final
        : public SDK::IActionMapEventListener
        , public SDK::IActionListener {
      public:
        C_ActionHintManager()                                       = delete;
        C_ActionHintManager(const C_ActionHintManager &)            = delete;
        C_ActionHintManager &operator=(const C_ActionHintManager &) = delete;

        /**
         * The records `RefreshActionHints` kept on its last pass. It hands
         * the HUD those with `m_uiVisible` set, plus the held action's (see
         * `GetHeldAction`); the hidden rest stay here and still take presses.
         * Interaction prompts are records here too.
         */
        const entitymodule::S_InteractorActionRecord *GetRecordsBegin() const {
            return m_records.begin;
        }

        const entitymodule::S_InteractorActionRecord *GetRecordsEnd() const {
            return m_records.end;
        }

        /**
         * The action whose hold ring is filling, set with its progress by
         * 0x180C44700. `RefreshActionHints` draws its record even when hidden,
         * unless the record's +0xD0 is set. Empty when nothing is held.
         */
        const char *GetHeldAction() const {
            return m_heldAction;
        }

        /**
         * How far the held action's ring has filled: `1 - remaining / duration`,
         * from 0 to 1, as the hold timer (0x180C4462C) hands it to 0x180C44700.
         * -2 before anything was held.
         */
        float GetHeldProgress() const {
            return m_heldProgress;
        }

      private:
        ~C_ActionHintManager() override;

        void unk_fn0001() override;
        void OnAction(const SDK::CCryName &action, int activationMode, float value) override;
        void AfterAction() override;

        // Constructor 0x181748CFC.
        SDK::wh::shared::C_Signal m_signal0010;   // 0x10; dispatch unk_185665430
        SDK::wh::shared::C_Signal m_signal0020;   // 0x20; dispatch unk_185665430
        std::byte m_map0030[0x10];                // 0x30; std::map {head, size}
        std::byte m_map0040[0x10];                // 0x40; std::map {head, size}
        struct {
            entitymodule::S_InteractorActionRecord *begin;
            entitymodule::S_InteractorActionRecord *end;
            entitymodule::S_InteractorActionRecord *capacity;
        } m_records;                              // 0x50; std::vector of 0xE0 records, rebuilt by RefreshActionHints
        std::byte m_hashMap0068[0x40];            // 0x68; std::unordered_map (sub_181748EF8)
        std::byte m_hashMap00A8[0x40];            // 0xA8; std::unordered_map (sub_181748EF8)
        const char *m_heldAction;                 // 0xE8; CryStringT, ctor ""
        float m_heldProgress;                     // 0xF0; ctor -2.0f
        std::byte m_padding00F4[0x4];
        void *m_pObject00F8;                      // 0xF8; owned 0x58-byte pool allocation
        SDK::CCryName m_name0100;                 // 0x100; ctor ""
        SDK::CCryName m_name0108;                 // 0x108; ctor ""
        SDK::CCryName m_name0110;                 // 0x110; ctor ""
        std::uint16_t m_value0118;                // 0x118; ctor 0
        std::byte m_padding011A[0x2];
        std::uint32_t m_values011C[2];            // 0x11C; ctor writes a zero qword
        bool m_refreshEnabled;                    // 0x124; ctor 1; cleared while an action-help set is rebuilt (0x18085B53C)
        std::byte m_padding0125[0x3];
        std::int32_t m_value0128;                 // 0x128; ctor 0
        std::byte m_padding012C[0x4];
        void *m_ptrs0130[2];                      // 0x130; ctor 0

        static void AssertLayout() {
            static_assert(offsetof(C_ActionHintManager, m_signal0010) == 0x10, "C_ActionHintManager::m_signal0010 offset mismatch");
            static_assert(offsetof(C_ActionHintManager, m_records) == 0x50, "C_ActionHintManager::m_records offset mismatch");
            static_assert(offsetof(C_ActionHintManager, m_hashMap0068) == 0x68, "C_ActionHintManager::m_hashMap0068 offset mismatch");
            static_assert(offsetof(C_ActionHintManager, m_heldAction) == 0xE8, "C_ActionHintManager::m_heldAction offset mismatch");
            static_assert(offsetof(C_ActionHintManager, m_name0100) == 0x100, "C_ActionHintManager::m_name0100 offset mismatch");
            static_assert(offsetof(C_ActionHintManager, m_refreshEnabled) == 0x124, "C_ActionHintManager::m_refreshEnabled offset mismatch");
            static_assert(offsetof(C_ActionHintManager, m_ptrs0130) == 0x130, "C_ActionHintManager::m_ptrs0130 offset mismatch");
        }
    };

    static_assert(sizeof(C_ActionHintManager) == 0x140, "C_ActionHintManager size mismatch");
} // namespace SDK::wh::playermodule
