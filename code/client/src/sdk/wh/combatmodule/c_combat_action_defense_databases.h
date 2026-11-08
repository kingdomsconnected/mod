#pragma once

#include "c_combat_action_table_database.h"

#include "../../patterns.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::combatmodule {
    /*
     * Every row below is the runtime record a loader builds from one
     * compiled DB row, not the compiled row itself. Each opens with the
     * shared C_AnimatedActionData prefix that 0x18092FEF4 (and its per-table
     * twins) writes through +0x3C, then the per-table converter copies the DB
     * row field by field: 0x18093278C (Hit), 0x180930828 (PerfectBlock),
     * 0x180D8ADA8 (FailedAttack) and 0x180932BBC (SyncPerfectBlockHit). The
     * column names are stripped, so each member is named for its offset and
     * commented with the DB-row offset it is copied from; the pointer runs
     * are records the converter resolves by id in other tables.
     */

    /**
     * A row of `CombatDb.PerfectBlock` (0x18531F820). Perfect block, sync
     * perfect block and master strike rows all live in this one table; the
     * loader (0x18092F170) pushes them 0x230 apart.
     */
    struct S_CombatActionPerfectBlockDBData {
      private:
        const void *m_pVTable;           // 0x0, S_Combat*Data table; C_AnimatedActionData<...> base
        std::uint32_t m_actionId;        // 0x8, the action-info record's id, from the row's +0x04 key
        std::int32_t m_fragmentId;       // 0xC, Mannequin fragment id resolved by name; written only when it resolves
        std::byte m_tagState0010[0x14];  // 0x10, tag state parsed from the row's tag string
        std::byte m_tagState0024[0x14];  // 0x24, second tag state, set when the controller def has one
        std::uint32_t m_value0038;       // 0x38, from 0x1809442F8 over +0x10
        std::uint32_t m_value003C;       // 0x3C, from 0x1809442F8 over +0x24
        std::uint8_t m_value0040;        // 0x40, copied from DB row +0x00
        std::byte m_padding0041[0x3];    // 0x41
        std::uint32_t m_value0044;       // 0x44, DB row +0x04
        std::uint8_t m_value0048;        // 0x48, DB row +0x08
        std::byte m_padding0049[0x7];    // 0x49
        std::uint64_t m_value0050;       // 0x50, DB row +0x10
        const void *m_pName0058;         // 0x58, CryString built from DB row +0x18
        std::uint32_t m_nameLength0060;  // 0x60
        std::byte m_padding0064[0x4];    // 0x64
        const void *m_pName0068;         // 0x68, CryString built from DB row +0x20
        std::uint32_t m_nameLength0070;  // 0x70
        std::byte m_padding0074[0x4];    // 0x74
        std::uint32_t m_value0078;       // 0x78, DB row +0x28
        float m_values007C[4];           // 0x7C, DB row +0x2C, 16 bytes
        std::uint32_t m_value008C;       // 0x8C, DB row +0x3C
        std::uint8_t m_value0090;        // 0x90, DB row +0x40
        std::uint8_t m_value0091;        // 0x91, DB row +0x41
        std::uint8_t m_value0092;        // 0x92, DB row +0x42
        std::byte m_padding0093[0x1];    // 0x93
        float m_transform0094[7];        // 0x94, 7 floats from DB row +0x44
        float m_transform00B0[7];        // 0xB0, 7 floats from DB row +0x60
        float m_transform00CC[7];        // 0xCC, 7 floats from DB row +0x7C
        float m_transform00E8[7];        // 0xE8, 7 floats from DB row +0x98
        std::uint32_t m_value0104;       // 0x104, DB row +0xB4
        std::uint32_t m_value0108;       // 0x108, DB row +0xB8
        std::byte m_padding010C[0x4];    // 0x10C
        const void *m_pMapHead0110;      // 0x110, MSVC std::map head built from DB row +0xC0
        std::uint64_t m_mapSize0118;     // 0x118
        std::uint8_t m_value0120;        // 0x120, DB row +0xC8
        std::byte m_padding0121[0x7];    // 0x121
        const void *m_p0128;             // 0x128, resolved from DB row +0xCC
        std::uint32_t m_value0130;       // 0x130, DB row +0xD0
        std::byte m_padding0134[0x4];    // 0x134
        const void *m_p0138;             // 0x138, record resolved from DB row id +0xD4
        const void *m_p0140;             // 0x140, record resolved from DB row id +0xD8
        const void *m_p0148;             // 0x148, record resolved from DB row id +0xDC
        const void *m_p0150;             // 0x150, record resolved from DB row id +0xE0
        const void *m_p0158;             // 0x158, record resolved from DB row id +0xE4
        const void *m_p0160;             // 0x160, record resolved from DB row id +0xE8
        const void *m_p0168;             // 0x168, record resolved from DB row id +0xEC
        const void *m_p0170;             // 0x170, record resolved from DB row id +0xF0
        const void *m_p0178;             // 0x178, record resolved from DB row id +0xF4
        const void *m_p0180;             // 0x180, record resolved from DB row id +0xF8
        const void *m_p0188;             // 0x188, record resolved from DB row id +0xFC
        const void *m_p0190;             // 0x190, record resolved from DB row id +0x100
        const void *m_p0198;             // 0x198, record resolved from DB row id +0x104
        const void *m_p01A0;             // 0x1A0, record resolved from DB row id +0x108
        const void *m_p01A8;             // 0x1A8, record resolved from DB row id +0x10C
        const void *m_p01B0;             // 0x1B0, record resolved from DB row id +0x110
        const void *m_p01B8;             // 0x1B8, record resolved from DB row id +0x114
        const void *m_p01C0;             // 0x1C0, record resolved from DB row id +0x118
        const void *m_p01C8;             // 0x1C8, record resolved from DB row id +0x11C
        const void *m_p01D0;             // 0x1D0, record resolved from DB row id +0x120
        std::uint8_t m_value01D8;        // 0x1D8, DB row +0x124
        std::uint8_t m_value01D9;        // 0x1D9, DB row +0x125
        std::byte m_padding01DA[0x2];    // 0x1DA
        std::uint32_t m_value01DC;       // 0x1DC, DB row +0x128
        std::uint32_t m_value01E0;       // 0x1E0, DB row +0x12C
        std::uint32_t m_value01E4;       // 0x1E4, DB row +0x130
        std::uint32_t m_value01E8;       // 0x1E8, DB row +0x134
        std::uint32_t m_value01EC;       // 0x1EC, DB row +0x138
        std::uint32_t m_value01F0;       // 0x1F0, DB row +0x13C
        std::uint32_t m_value01F4;       // 0x1F4, DB row +0x140
        std::uint32_t m_value01F8;       // 0x1F8, DB row +0x144
        std::uint32_t m_value01FC;       // 0x1FC, DB row +0x148
        std::uint32_t m_value0200;       // 0x200, DB row +0x14C
        std::uint32_t m_value0204;       // 0x204, DB row +0x150
        std::uint32_t m_value0208;       // 0x208, DB row +0x154
        std::uint8_t m_value020C;        // 0x20C, DB row +0x158
        std::byte m_padding020D[0x3];    // 0x20D
        std::uint32_t m_value0210;       // 0x210, DB row +0x15C
        std::uint32_t m_value0214;       // 0x214, DB row +0x160
        std::uint32_t m_value0218;       // 0x218, DB row +0x164
        std::uint32_t m_value021C;       // 0x21C, DB row +0x168
        std::uint8_t m_value0220;        // 0x220, DB row +0x16C
        std::byte m_padding0221[0x3];    // 0x221
        std::uint32_t m_value0224;       // 0x224, DB row +0x170
        std::uint32_t m_value0228;       // 0x228, DB row +0x174
        std::uint8_t m_value022C;        // 0x22C, DB row +0x178
        std::byte m_padding022D[0x3];    // 0x22D

        static void AssertLayout() {
            static_assert(offsetof(S_CombatActionPerfectBlockDBData, m_fragmentId) == 0xC, "S_CombatActionPerfectBlockDBData::m_fragmentId offset mismatch");
            static_assert(offsetof(S_CombatActionPerfectBlockDBData, m_pMapHead0110) == 0x110, "S_CombatActionPerfectBlockDBData::m_pMapHead0110 offset mismatch");
            static_assert(offsetof(S_CombatActionPerfectBlockDBData, m_value01D8) == 0x1D8, "S_CombatActionPerfectBlockDBData::m_value01D8 offset mismatch");
            static_assert(offsetof(S_CombatActionPerfectBlockDBData, m_value022C) == 0x22C, "S_CombatActionPerfectBlockDBData::m_value022C offset mismatch");
        }
    };

    static_assert(sizeof(S_CombatActionPerfectBlockDBData) == 0x230, "S_CombatActionPerfectBlockDBData size mismatch");

    /** A row of `CombatDb.FailedAttack` (0x18531F2D0); the loader (0x180D8ACAC) strides 0x138. */
    struct S_CombatActionFailedAttackDBData {
      private:
        const void *m_pVTable;           // 0x0, S_Combat*Data table; C_AnimatedActionData<...> base
        std::uint32_t m_actionId;        // 0x8, the action-info record's id, from the row's +0x04 key
        std::int32_t m_fragmentId;       // 0xC, Mannequin fragment id resolved by name; written only when it resolves
        std::byte m_tagState0010[0x14];  // 0x10, tag state parsed from the row's tag string
        std::byte m_tagState0024[0x14];  // 0x24, second tag state, set when the controller def has one
        std::uint32_t m_value0038;       // 0x38, from 0x1809442F8 over +0x10
        std::uint32_t m_value003C;       // 0x3C, from 0x1809442F8 over +0x24
        std::uint8_t m_value0040;        // 0x40, copied from DB row +0x00
        std::byte m_padding0041[0x3];    // 0x41
        std::uint32_t m_value0044;       // 0x44, DB row +0x04
        std::uint8_t m_value0048;        // 0x48, DB row +0x08
        std::byte m_padding0049[0x7];    // 0x49
        std::uint64_t m_value0050;       // 0x50, DB row +0x10
        const void *m_pName0058;         // 0x58, CryString built from DB row +0x18
        std::uint32_t m_nameLength0060;  // 0x60
        std::byte m_padding0064[0x4];    // 0x64
        const void *m_pName0068;         // 0x68, CryString built from DB row +0x20
        std::uint32_t m_nameLength0070;  // 0x70
        std::byte m_padding0074[0x4];    // 0x74
        std::uint32_t m_value0078;       // 0x78, DB row +0x28
        float m_values007C[4];           // 0x7C, DB row +0x2C, 16 bytes
        std::uint32_t m_value008C;       // 0x8C, DB row +0x3C
        std::uint8_t m_value0090;        // 0x90, DB row +0x40
        std::uint8_t m_value0091;        // 0x91, DB row +0x41
        std::uint8_t m_value0092;        // 0x92, DB row +0x42
        std::byte m_padding0093[0x1];    // 0x93
        std::uint32_t m_value0094;       // 0x94, DB row +0x44
        const void *m_pMapHead0098;      // 0x98, MSVC std::map head built from DB row +0x48
        std::uint64_t m_mapSize00A0;     // 0xA0
        std::uint8_t m_value00A8;        // 0xA8, DB row +0x50
        std::byte m_padding00A9[0x7];    // 0xA9
        const void *m_p00B0;             // 0xB0, resolved from DB row +0x54
        std::uint32_t m_value00B8;       // 0xB8, DB row +0x58
        std::byte m_padding00BC[0x4];    // 0xBC
        const void *m_p00C0;             // 0xC0, record resolved from DB row id +0x5C
        const void *m_p00C8;             // 0xC8, record resolved from DB row id +0x60
        const void *m_p00D0;             // 0xD0, record resolved from DB row id +0x64
        const void *m_p00D8;             // 0xD8, record resolved from DB row id +0x68
        const void *m_p00E0;             // 0xE0, record resolved from DB row id +0x6C
        const void *m_p00E8;             // 0xE8, record resolved from DB row id +0x70
        const void *m_p00F0;             // 0xF0, record resolved from DB row id +0x74
        const void *m_p00F8;             // 0xF8, record resolved from DB row id +0x78
        const void *m_p0100;             // 0x100, record resolved from DB row id +0x7C
        const void *m_p0108;             // 0x108, record resolved from DB row id +0x80
        const void *m_p0110;             // 0x110, record resolved from DB row id +0x84
        const void *m_p0118;             // 0x118, record resolved from DB row id +0x88
        const void *m_p0120;             // 0x120, record resolved from DB row id +0x8C
        const void *m_p0128;             // 0x128, record resolved from DB row id +0x90
        const void *m_p0130;             // 0x130, record resolved from DB row id +0x94

        static void AssertLayout() {
            static_assert(offsetof(S_CombatActionFailedAttackDBData, m_fragmentId) == 0xC, "S_CombatActionFailedAttackDBData::m_fragmentId offset mismatch");
            static_assert(offsetof(S_CombatActionFailedAttackDBData, m_pMapHead0098) == 0x98, "S_CombatActionFailedAttackDBData::m_pMapHead0098 offset mismatch");
            static_assert(offsetof(S_CombatActionFailedAttackDBData, m_p00C0) == 0xC0, "S_CombatActionFailedAttackDBData::m_p00C0 offset mismatch");
        }
    };

    static_assert(sizeof(S_CombatActionFailedAttackDBData) == 0x138, "S_CombatActionFailedAttackDBData size mismatch");

    /** A row of `CombatDb.Hit` (0x18531F600); the loader (0x18092FCFC) strides 0x1F8. */
    struct S_CombatActionHitDBData {
        /**
         * Whether the reaction built from this row has its +0x3C field set
         * to 2 through slot 19 -- CreateHitAction (0x180D49AB4) does exactly
         * that and nothing else with it.
         */
        bool SetsField003C() const {
            return m_flag01F0 != 0;
        }

      private:
        const void *m_pVTable;           // 0x0, S_Combat*Data table; C_AnimatedActionData<...> base
        std::uint32_t m_actionId;        // 0x8, the action-info record's id, from the row's +0x04 key
        std::int32_t m_fragmentId;       // 0xC, Mannequin fragment id resolved by name; written only when it resolves
        std::byte m_tagState0010[0x14];  // 0x10, tag state parsed from the row's tag string
        std::byte m_tagState0024[0x14];  // 0x24, second tag state, set when the controller def has one
        std::uint32_t m_value0038;       // 0x38, from 0x1809442F8 over +0x10
        std::uint32_t m_value003C;       // 0x3C, from 0x1809442F8 over +0x24
        std::uint8_t m_value0040;        // 0x40, copied from DB row +0x00
        std::byte m_padding0041[0x3];    // 0x41
        std::uint32_t m_value0044;       // 0x44, DB row +0x04
        std::uint8_t m_value0048;        // 0x48, DB row +0x08
        std::byte m_padding0049[0x7];    // 0x49
        std::uint64_t m_value0050;       // 0x50, DB row +0x10
        const void *m_pName0058;         // 0x58, CryString built from DB row +0x18
        std::uint32_t m_nameLength0060;  // 0x60
        std::byte m_padding0064[0x4];    // 0x64
        const void *m_pName0068;         // 0x68, CryString built from DB row +0x20
        std::uint32_t m_nameLength0070;  // 0x70
        std::byte m_padding0074[0x4];    // 0x74
        std::uint32_t m_value0078;       // 0x78, DB row +0x28
        float m_values007C[4];           // 0x7C, DB row +0x2C, 16 bytes
        std::uint32_t m_value008C;       // 0x8C, DB row +0x3C
        std::uint8_t m_value0090;        // 0x90, DB row +0x40
        std::uint8_t m_value0091;        // 0x91, DB row +0x41
        std::uint8_t m_value0092;        // 0x92, DB row +0x42
        std::byte m_padding0093[0x1];    // 0x93
        float m_transform0094[7];        // 0x94, 7 floats from DB row +0x44
        float m_transform00B0[7];        // 0xB0, 7 floats from DB row +0x60
        float m_transform00CC[7];        // 0xCC, 7 floats from DB row +0x7C
        float m_transform00E8[7];        // 0xE8, 7 floats from DB row +0x98
        std::uint32_t m_value0104;       // 0x104, DB row +0xB4
        std::uint8_t m_value0108;        // 0x108, DB row +0xB8
        std::byte m_padding0109[0x3];    // 0x109
        std::uint32_t m_value010C;       // 0x10C, DB row +0xBC
        const void *m_pMapHead0110;      // 0x110, MSVC std::map head built from DB row +0xC0
        std::uint64_t m_mapSize0118;     // 0x118
        std::uint8_t m_value0120;        // 0x120, DB row +0xC8
        std::byte m_padding0121[0x7];    // 0x121
        const void *m_p0128;             // 0x128, resolved from DB row +0xCC
        std::uint32_t m_value0130;       // 0x130, DB row +0xD0
        std::byte m_padding0134[0x4];    // 0x134
        const void *m_p0138;             // 0x138, record resolved from DB row id +0xD4
        const void *m_p0140;             // 0x140, record resolved from DB row id +0xD8
        const void *m_p0148;             // 0x148, record resolved from DB row id +0xDC
        const void *m_p0150;             // 0x150, record resolved from DB row id +0xE0
        const void *m_p0158;             // 0x158, record resolved from DB row id +0xE4
        const void *m_p0160;             // 0x160, record resolved from DB row id +0xE8
        const void *m_p0168;             // 0x168, record resolved from DB row id +0xEC
        const void *m_p0170;             // 0x170, record resolved from DB row id +0xF0
        const void *m_p0178;             // 0x178, record resolved from DB row id +0xF4
        const void *m_p0180;             // 0x180, record resolved from DB row id +0xF8
        const void *m_p0188;             // 0x188, record resolved from DB row id +0xFC
        const void *m_p0190;             // 0x190, record resolved from DB row id +0x100
        const void *m_p0198;             // 0x198, record resolved from DB row id +0x104
        const void *m_p01A0;             // 0x1A0, record resolved from DB row id +0x108
        const void *m_p01A8;             // 0x1A8, record resolved from DB row id +0x10C
        std::uint32_t m_value01B0;       // 0x1B0, DB row +0x110
        std::uint32_t m_values01B4[2];   // 0x1B4, DB row +0x114
        std::uint32_t m_value01BC;       // 0x1BC, DB row +0x11C
        std::uint32_t m_values01C0[2];   // 0x1C0, DB row +0x120
        std::uint32_t m_value01C8;       // 0x1C8, DB row +0x128
        std::byte m_padding01CC[0x4];    // 0x1CC
        const void *m_p01D0;             // 0x1D0, record resolved from DB row id +0x12C
        const void *m_p01D8;             // 0x1D8, record resolved from DB row id +0x130
        const void *m_p01E0;             // 0x1E0, record resolved from DB row id +0x134
        const void *m_p01E8;             // 0x1E8, record resolved from DB row id +0x138
        std::uint8_t m_flag01F0;         // 0x1F0, DB row +0x13C
        std::uint8_t m_value01F1;        // 0x1F1, DB row +0x13D
        std::byte m_padding01F2[0x2];    // 0x1F2
        std::uint32_t m_value01F4;       // 0x1F4, DB row +0x140

        static void AssertLayout() {
            static_assert(offsetof(S_CombatActionHitDBData, m_fragmentId) == 0xC, "S_CombatActionHitDBData::m_fragmentId offset mismatch");
            static_assert(offsetof(S_CombatActionHitDBData, m_pMapHead0110) == 0x110, "S_CombatActionHitDBData::m_pMapHead0110 offset mismatch");
            static_assert(offsetof(S_CombatActionHitDBData, m_flag01F0) == 0x1F0, "S_CombatActionHitDBData::m_flag01F0 offset mismatch");
            static_assert(offsetof(S_CombatActionHitDBData, m_value01F4) == 0x1F4, "S_CombatActionHitDBData::m_value01F4 offset mismatch");
        }
    };

    static_assert(sizeof(S_CombatActionHitDBData) == 0x1F8, "S_CombatActionHitDBData size mismatch");

    /** A row of `CombatDb.SyncPerfectBlockHit` (0x18531FC60); the loader (0x181112CC4) strides 0x1F8. */
    struct S_CombatActionSyncPerfectBlockHitDBData {
      private:
        const void *m_pVTable;           // 0x0, S_Combat*Data table; C_AnimatedActionData<...> base
        std::uint32_t m_actionId;        // 0x8, the action-info record's id, from the row's +0x04 key
        std::int32_t m_fragmentId;       // 0xC, Mannequin fragment id resolved by name; written only when it resolves
        std::byte m_tagState0010[0x14];  // 0x10, tag state parsed from the row's tag string
        std::byte m_tagState0024[0x14];  // 0x24, second tag state, set when the controller def has one
        std::uint32_t m_value0038;       // 0x38, from 0x1809442F8 over +0x10
        std::uint32_t m_value003C;       // 0x3C, from 0x1809442F8 over +0x24
        std::uint8_t m_value0040;        // 0x40, copied from DB row +0x00
        std::byte m_padding0041[0x3];    // 0x41
        std::uint32_t m_value0044;       // 0x44, DB row +0x04
        std::uint8_t m_value0048;        // 0x48, DB row +0x08
        std::byte m_padding0049[0x7];    // 0x49
        std::uint64_t m_value0050;       // 0x50, DB row +0x10
        const void *m_pName0058;         // 0x58, CryString built from DB row +0x18
        std::uint32_t m_nameLength0060;  // 0x60
        std::byte m_padding0064[0x4];    // 0x64
        const void *m_pName0068;         // 0x68, CryString built from DB row +0x20
        std::uint32_t m_nameLength0070;  // 0x70
        std::byte m_padding0074[0x4];    // 0x74
        std::uint32_t m_value0078;       // 0x78, DB row +0x28
        float m_values007C[4];           // 0x7C, DB row +0x2C, 16 bytes
        std::uint32_t m_value008C;       // 0x8C, DB row +0x3C
        std::uint8_t m_value0090;        // 0x90, DB row +0x40
        std::uint8_t m_value0091;        // 0x91, DB row +0x41
        std::uint8_t m_value0092;        // 0x92, DB row +0x42
        std::byte m_padding0093[0x1];    // 0x93
        float m_transform0094[7];        // 0x94, 7 floats from DB row +0x44
        float m_transform00B0[7];        // 0xB0, 7 floats from DB row +0x60
        float m_transform00CC[7];        // 0xCC, 7 floats from DB row +0x7C
        float m_transform00E8[7];        // 0xE8, 7 floats from DB row +0x98
        std::uint32_t m_value0104;       // 0x104, DB row +0xB4
        std::uint32_t m_value0108;       // 0x108, DB row +0xB8
        std::byte m_padding010C[0x4];    // 0x10C
        const void *m_pMapHead0110;      // 0x110, MSVC std::map head built from DB row +0xC0
        std::uint64_t m_mapSize0118;     // 0x118
        std::uint8_t m_value0120;        // 0x120, DB row +0xC8
        std::byte m_padding0121[0x7];    // 0x121
        const void *m_p0128;             // 0x128, resolved from DB row +0xCC
        std::uint32_t m_value0130;       // 0x130, DB row +0xD0
        std::byte m_padding0134[0x4];    // 0x134
        const void *m_p0138;             // 0x138, record resolved from DB row id +0xD4
        const void *m_p0140;             // 0x140, record resolved from DB row id +0xD8
        const void *m_p0148;             // 0x148, record resolved from DB row id +0xDC
        const void *m_p0150;             // 0x150, record resolved from DB row id +0xE0
        const void *m_p0158;             // 0x158, record resolved from DB row id +0xE4
        const void *m_p0160;             // 0x160, record resolved from DB row id +0xE8
        const void *m_p0168;             // 0x168, record resolved from DB row id +0xEC
        const void *m_p0170;             // 0x170, record resolved from DB row id +0xF0
        const void *m_p0178;             // 0x178, record resolved from DB row id +0xF4
        const void *m_p0180;             // 0x180, record resolved from DB row id +0xF8
        const void *m_p0188;             // 0x188, record resolved from DB row id +0xFC
        const void *m_p0190;             // 0x190, record resolved from DB row id +0x100
        const void *m_p0198;             // 0x198, record resolved from DB row id +0x104
        const void *m_p01A0;             // 0x1A0, record resolved from DB row id +0x108
        const void *m_p01A8;             // 0x1A8, record resolved from DB row id +0x10C
        std::uint32_t m_value01B0;       // 0x1B0, DB row +0x110
        std::byte m_padding01B4[0x4];    // 0x1B4
        const void *m_p01B8;             // 0x1B8, record resolved from DB row id +0x114
        const void *m_p01C0;             // 0x1C0, record resolved from DB row id +0x118
        const void *m_p01C8;             // 0x1C8, record resolved from DB row id +0x11C
        const void *m_p01D0;             // 0x1D0, record resolved from DB row id +0x120
        const void *m_p01D8;             // 0x1D8, record resolved from DB row id +0x124
        std::uint8_t m_value01E0;        // 0x1E0, DB row +0x128
        std::byte m_padding01E1[0x3];    // 0x1E1
        std::uint32_t m_value01E4;       // 0x1E4, DB row +0x12C
        std::uint32_t m_value01E8;       // 0x1E8, DB row +0x130
        std::uint32_t m_value01EC;       // 0x1EC, DB row +0x134
        std::uint8_t m_value01F0;        // 0x1F0, DB row +0x138
        std::byte m_padding01F1[0x7];    // 0x1F1

        static void AssertLayout() {
            static_assert(offsetof(S_CombatActionSyncPerfectBlockHitDBData, m_fragmentId) == 0xC, "S_CombatActionSyncPerfectBlockHitDBData::m_fragmentId offset mismatch");
            static_assert(offsetof(S_CombatActionSyncPerfectBlockHitDBData, m_pMapHead0110) == 0x110, "S_CombatActionSyncPerfectBlockHitDBData::m_pMapHead0110 offset mismatch");
            static_assert(offsetof(S_CombatActionSyncPerfectBlockHitDBData, m_value01E0) == 0x1E0, "S_CombatActionSyncPerfectBlockHitDBData::m_value01E0 offset mismatch");
            static_assert(offsetof(S_CombatActionSyncPerfectBlockHitDBData, m_value01F0) == 0x1F0, "S_CombatActionSyncPerfectBlockHitDBData::m_value01F0 offset mismatch");
        }
    };

    static_assert(sizeof(S_CombatActionSyncPerfectBlockHitDBData) == 0x1F8, "S_CombatActionSyncPerfectBlockHitDBData size mismatch");

    /**
     * A row of `CombatDb.Block` (0x18531EFA0): the block a body raises against
     * a swing, picked for it by `QueryFreeBlockRows` (0x180C56E18). The
     * loader (0x1810E18F8) builds each through 0x180935B08 into the vector
     * at +0x30, which grows (0x1810E1754) 0x1F0 apart. Only its place in the
     * table is read here.
     */
    struct S_CombatActionBlockDBData {
      private:
        std::byte m_opaque0000[0x1F0];
    };

    static_assert(sizeof(S_CombatActionBlockDBData) == 0x1F0, "S_CombatActionBlockDBData size mismatch");

    /**
     * A row of `CombatDb.Dodge` (0x18531F1C0): one dodge fragment and how far
     * it carries the body (the vector at +0x138, read by the query's row
     * filter 0x181E3A730). The loader (0x180DAE0F0) pushes rows into the
     * vector at +0x30 0x158 apart. Only its place in the table is read here.
     */
    struct S_CombatActionDodgeDBData {
      private:
        std::byte m_opaque0000[0x158];
    };

    static_assert(sizeof(S_CombatActionDodgeDBData) == 0x158, "S_CombatActionDodgeDBData size mismatch");

    class C_CombatActionPerfectBlockDatabase: public C_CombatActionTableDatabase<S_CombatActionPerfectBlockDBData> {
      public:
        static C_CombatActionPerfectBlockDatabase *GetInstance() {
            return reinterpret_cast<C_CombatActionPerfectBlockDatabase *>(gPatterns.C_CombatActionPerfectBlockDatabase__Instance);
        }
    };

    class C_CombatActionFailedAttackDatabase: public C_CombatActionTableDatabase<S_CombatActionFailedAttackDBData> {
      public:
        static C_CombatActionFailedAttackDatabase *GetInstance() {
            return reinterpret_cast<C_CombatActionFailedAttackDatabase *>(gPatterns.C_CombatActionFailedAttackDatabase__Instance);
        }
    };

    class C_CombatActionHitDatabase: public C_CombatActionTableDatabase<S_CombatActionHitDBData> {
      public:
        static C_CombatActionHitDatabase *GetInstance() {
            return reinterpret_cast<C_CombatActionHitDatabase *>(gPatterns.C_CombatActionHitDatabase__Instance);
        }
    };

    class C_CombatActionSyncPerfectBlockHitDatabase: public C_CombatActionTableDatabase<S_CombatActionSyncPerfectBlockHitDBData> {
      public:
        static C_CombatActionSyncPerfectBlockHitDatabase *GetInstance() {
            return reinterpret_cast<C_CombatActionSyncPerfectBlockHitDatabase *>(gPatterns.C_CombatActionSyncPerfectBlockHitDatabase__Instance);
        }
    };

    class C_CombatActionBlockDatabase: public C_CombatActionTableDatabase<S_CombatActionBlockDBData> {
      public:
        static C_CombatActionBlockDatabase *GetInstance() {
            return reinterpret_cast<C_CombatActionBlockDatabase *>(gPatterns.C_CombatActionBlockDatabase__Instance);
        }
    };

    class C_CombatActionDodgeDatabase: public C_CombatActionTableDatabase<S_CombatActionDodgeDBData> {
      public:
        static C_CombatActionDodgeDatabase *GetInstance() {
            return reinterpret_cast<C_CombatActionDodgeDatabase *>(gPatterns.C_CombatActionDodgeDatabase__Instance);
        }
    };
} // namespace SDK::wh::combatmodule
