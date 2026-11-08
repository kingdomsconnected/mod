#pragma once

#include "c_compass_mark.h"
#include "c_ui_map_content.h"
#include "e_compass_mark_type.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::guimodule {
    /**
     * The map screen. Only the runs the mod reads are mapped; everything else
     * goes through a native, and the size is not asserted because the object
     * reaches past +0x749. For the record the checkpoint path touches the
     * legend list at +0x28, the checkpoint vector at +0x588, the map-mark
     * vector at +0x5A0, the mark-to-compass pairing at +0x5B8, the
     * cursor-over-map flag at +0x608 and the Lua binding at +0x6C8.
     *
     * A marker in the vector at +0x5A0 follows a source object rather than
     * holding a position, and that source has to be a registered
     * `C_LinkableObject` (see S_EntityMapMark). A mark at a bare coordinate is
     * therefore never added there: its flash row is built directly and pushed
     * to the movie at +0x208, which is all the game's own list becomes on the
     * screen anyway.
     */
    class C_UIMap final {
      public:
        C_UIMap()                           = delete;
        ~C_UIMap()                          = delete;
        C_UIMap(const C_UIMap &)            = delete;
        C_UIMap &operator=(const C_UIMap &) = delete;

        /** Null before the GUI module has built its pages. */
        static C_UIMap *GetInstance();

        /** Set at 0x1819D53C0, cleared at 0x180562300. */
        bool IsOpen() const {
            return m_mapScreenUp != 0;
        }

        bool IsCursorOnMap() const { return m_cursorOnMap; }
        /** Reads world coordinates without changing the player's waypoint. */
        bool GetCursorWorldPosition(Vec3 &position);

        /** Whether the map the player is looking at already carries a marker. */
        bool HasCheckpoint();

        /** That marker, or an empty handle. The reference it holds is owned. */
        CheckpointMarkRef FindCheckpointForCurrentMap();

        /** Runs the game's own removal, so the mod's hook on it fires. */
        void RemoveCheckpoint();

        /**
         * Redraws the marker on the screen, which a move needs: the compass
         * re-reads the mark's Vec3 every frame but the drawn pin does not. A
         * no-op while the screen is down.
         */
        void SyncCheckpointToFlash();

        /**
         * Recollects the quest markers and pushes the difference to the
         * movie: rows for entries that are new, removals for ones that are
         * gone. The game runs it on every open and when the tracked set
         * changes; it is safe with the screen down.
         */
        void RefreshQuestMarkers();

        /** The map screen's movie, where every row the screen draws is pushed. */
        C_UIMapContent *GetContent() {
            return &m_content;
        }

        /** Whether marks of this type reach the compass and the map legend. */
        bool IsMarkTypeVisible(E_CompassMarkType type) const {
            return m_markTypeVisible[NormalizeCompassMarkType(type)] != 0;
        }

        /**
         * Sets that switch and re-evaluates the marks already placed. Types
         * that normalize onto a shared legend row move together.
         */
        void SetMarkTypeVisible(E_CompassMarkType type, bool visible);

      private:
        /**
         * The native behind the legend's toggle, taking the normalized index
         * rather than a raw type. Its mark walk is unconditional; only its
         * flash push needs the screen up.
         */
        void SetPoiMarkersOfTypeVisible(int normalizedType, bool show);

        // Constructor 0x18146C3C4.
        const void *m_pVTable;                 // 0x00
        void *m_ptr0008;                       // 0x08; ctor 0
        const void *m_pVTables0010[3];         // 0x10; the other C_UIMap tables
        std::byte m_legendList[0x1E0];         // 0x28; flash list "ApseMapLegendList" (sub_180BC8484)
        C_UIMapContent m_content; // 0x208
        void *m_pApse;                         // 0x370; the owning C_UIApse
        std::byte m_questionDialog[0xE8]; // C_UIModalDialogQuestion (its sizeof is asserted in c_ui_modal_dialog_question.h), 0x378; "ApseModalDialog"
        std::byte m_randomEventDialog[0x128]; // C_UIModalDialogRandomEvent (its sizeof is asserted in c_ui_modal_dialog_random_event.h), 0x460
        void *m_ptrs0588[6];                   // 0x588; ctor 0
        std::byte m_map05B8[0x10];             // 0x5B8; std::map {head, size}
        void *m_ptr05C8;                       // 0x5C8; ctor 0
        bool m_flag05D0;                       // 0x5D0; ctor 0
        std::byte m_padding05D1[0x7];
        bool m_flag05D8;                       // 0x5D8; ctor 0
        std::byte m_padding05D9[0x7];
        void *m_ptrs05E0[3];                   // 0x5E0; ctor 0
        bool m_flag05F8;                       // 0x5F8; ctor 0
        std::byte m_padding05F9[0x7];
        std::uint32_t m_values0600[2];         // 0x600; ctor 0
        bool m_cursorOnMap;                    // 0x608; OnCursorOnActiveAreaChanged
        std::byte m_padding0609[0x7];
        const char *m_text0610;                // 0x610; ctor points it at the empty string

        // One byte per normalized mark type, all 1 at construction. The map
        // legend's rows are built bound to these.
        std::uint8_t m_markTypeVisible[0x61]; // 0x618

        std::byte m_padding0679[0x7];
        void *m_ptrs0680[3];                   // 0x680; ctor 0
        std::byte m_object0698[0x20];          // 0x698; sub_18146C69C(this)
        void *m_ptrs06B8[2];                   // 0x6B8; ctor 0
        std::byte m_scriptBind[0x70];          // 0x6C8; wh::guimodule::C_ScriptBindMap
        std::byte m_map0738[0x10];             // 0x738; std::map {head, size}
        std::uint8_t m_mapScreenUp; // 0x748
        std::uint8_t m_value0749;              // 0x749; ctor 0 (word write with +0x748)
        std::uint8_t m_value074A;              // 0x74A; ctor 0
        std::byte m_padding074B[0x5];

        static void AssertLayout() {
            static_assert(offsetof(C_UIMap, m_legendList) == 0x28, "C_UIMap::m_legendList offset mismatch");
            static_assert(offsetof(C_UIMap, m_content) == 0x208, "C_UIMap::m_content offset mismatch");
            static_assert(offsetof(C_UIMap, m_cursorOnMap) == 0x608);
            static_assert(offsetof(C_UIMap, m_pApse) == 0x370, "C_UIMap::m_pApse offset mismatch");
            static_assert(offsetof(C_UIMap, m_randomEventDialog) == 0x460, "C_UIMap::m_randomEventDialog offset mismatch");
            static_assert(offsetof(C_UIMap, m_ptrs0588) == 0x588, "C_UIMap::m_ptrs0588 offset mismatch");
            static_assert(offsetof(C_UIMap, m_text0610) == 0x610, "C_UIMap::m_text0610 offset mismatch");
            static_assert(offsetof(C_UIMap, m_scriptBind) == 0x6C8, "C_UIMap::m_scriptBind offset mismatch");
            static_assert(offsetof(C_UIMap, m_markTypeVisible) == 0x618, "C_UIMap::m_markTypeVisible offset mismatch");
            static_assert(offsetof(C_UIMap, m_mapScreenUp) == 0x748, "C_UIMap::m_mapScreenUp offset mismatch");
            static_assert(sizeof(m_markTypeVisible) == 0x61, "C_UIMap::m_markTypeVisible size mismatch");
        }
    };

    // C_UIApse places the next member 0x750 past this one.
    static_assert(sizeof(C_UIMap) == 0x750, "C_UIMap size mismatch");
} // namespace SDK::wh::guimodule
