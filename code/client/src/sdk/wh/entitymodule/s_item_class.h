#pragma once

#include "../../types.h"
#include "s_item_class_guid.h"
#include <cstddef>
#include <cstdint>

namespace SDK::wh::entitymodule {
    /**
     * The class ids `S_ItemClass::IsOfClass` answers for, where the id has
     * been proven. Each item class answers true for its own id and for every
     * class it derives from.
     */
    enum class E_ItemClassId : std::int32_t {
        /**
         * The row types the stat overrides write, from the id each one's
         * `S_ItemClassWrapper<Row, Base, Id>` RTTI names: `$00`, `$01` and
         * `$02` on the melee, missile and ammo wrappers (0x18430E930,
         * 0x18430E8E0, 0x1842FF6A0).
         */
        MeleeWeapon   = 1,
        MissileWeapon = 2,
        Ammo          = 3,

        /**
         * Worn armor. `ExpandClothingPreset_181EDE6F0` keeps only these, and
         * `EquipItemRaw` routes them to the armor slots.
         */
        Armor = 4,

        /** `S_ItemClass` itself: its own `IsOfClass` is `id == 0x15` (0x182A1F824). */
        Item = 21,

        /** Weapon-slot items; EquipItemRaw routes class 24 to its slot array. */
        WeaponEquip = 24,

        PlayerItem = 25,
    };

    /**
     * An item class: one row of the `Libs/Tables/item/item*.xml` tables, held
     * by `C_ItemDatabase` through a `std::shared_ptr`.
     *
     * Polymorphic, with one subclass per kind of item. The GUID/name prefix,
     * class test and divisibility slot are proven; the remaining data and
     * slots stay opaque. Objects are only borrowed from the database.
     */
    struct S_ItemClass {
        virtual ~S_ItemClass()    = default;
        virtual void unk_fn0001() = 0;
        virtual void unk_fn0002() = 0;

        /** Whether this class is `classId` or derives from it (+0x18). */
        virtual bool IsOfClass(E_ItemClassId classId) const = 0;

        virtual void unk_fn0004() = 0;
        virtual void unk_fn0005() = 0;
        virtual void unk_fn0006() = 0;
        // CreateItems (0x181F0E1B0) uses slot 7 to keep a single stack.
        virtual bool IsDivisible() const = 0;

        const S_ItemClassGUID &GetGuid() const {
            return m_guid;
        }
        const char *GetInternalName() const {
            return m_name.m_pData;
        }

      private:
        S_ItemClassGUID m_guid;
        CryStringT<char> m_name;
        std::byte m_opaque0020[0x18];

        static void AssertLayout() {
            static_assert(offsetof(S_ItemClass, m_guid) == 0x08);
            static_assert(offsetof(S_ItemClass, m_name) == 0x18);
        }
    };
    // Constructor 0x180754CE4; clone allocation 0x182A459D0.
    static_assert(sizeof(S_ItemClass) == 0x38);
} // namespace SDK::wh::entitymodule
