#pragma once

#include <cstdint>

namespace SDK::wh::entitymodule {
    /**
     * Where on a body a weapon lives, and the index of the slot it occupies
     * in `C_ItemAttachmentManager`.
     *
     * Named and valued by the game's RTTR registration of
     * wh::entitymodule::WeaponEquipSlot inside
     * `RegisterReflectionWeaponClasses` at 0x180CFFDC8, which is also where
     * every `WeaponClass` row's `equip_slot` column is read into.
     *
     * The eight enumerators are the eight item slots the attachment manager
     * carries, in this order. The first six pair up into the three weapon
     * sets a player toggles between -- set N is `2 * N` and `2 * N + 1` --
     * while Torch and Dagger sit outside the sets and are drawn on their own.
     */
    struct E_WeaponEquipSlot {
        enum Type : std::uint32_t {
            PrimaryMainHand   = 0,
            PrimaryOffHand    = 1,
            SecondaryMainHand = 2,
            SecondaryOffHand  = 3,
            Oversized         = 4,
            OversizedOff      = 5,
            Torch             = 6,
            Dagger            = 7,

            Count = 8,
        };

        static constexpr bool IsSameWeaponSet(Type first, Type second) {
            return first < Torch && second < Torch && first / 2 == second / 2;
        }

        static const char *Name(Type slot) {
            switch (slot) {
            case PrimaryMainHand: return "PrimaryMainHand";
            case PrimaryOffHand: return "PrimaryOffHand";
            case SecondaryMainHand: return "SecondaryMainHand";
            case SecondaryOffHand: return "SecondaryOffHand";
            case Oversized: return "Oversized";
            case OversizedOff: return "OversizedOff";
            case Torch: return "Torch";
            case Dagger: return "Dagger";
            default: return "?";
            }
        }
    };

    /**
     * Which pair of equip slots a fighter currently has selected.
     *
     * Not RTTR-registered: these are the `WS_PRIMARY` and `WS_SECONDARY`
     * constants `C_ScriptBindHuman` publishes to Lua, and the range
     * `C_ScriptBindHuman::ToggleWeaponSet` accepts -- it rejects anything
     * above 2. `Any` is not a set a body can be in; it is the wildcard
     * `C_ItemAttachmentManager::IsWeaponDrawn` takes to mean "either hand,
     * whatever set", and `SetSelectedWeaponSet` refuses to store it.
     *
     * Which weapons a set holds follows from the `weapon_class.xml` rows
     * rather than from anything here: swords and shields equip to the
     * Primary pair, bows, crossbows and the rifle to the Secondary pair, and
     * the halberd alone to the Oversized pair.
     */
    struct E_WeaponSet {
        enum Type : std::int32_t {
            Primary   = 0,
            Secondary = 1,
            Oversized = 2,

            Count = 3,

            Any = 3,
        };

        /** The equip slot a set's main hand occupies. */
        static constexpr E_WeaponEquipSlot::Type MainHandSlot(Type set) {
            return static_cast<E_WeaponEquipSlot::Type>(2 * static_cast<std::int32_t>(set));
        }

        /** The equip slot a set's off hand occupies. */
        static constexpr E_WeaponEquipSlot::Type OffHandSlot(Type set) {
            return static_cast<E_WeaponEquipSlot::Type>(2 * static_cast<std::int32_t>(set) + 1);
        }

        static const char *Name(Type set) {
            switch (set) {
            case Primary: return "Primary";
            case Secondary: return "Secondary";
            case Oversized: return "Oversized";
            case Any: return "Any";
            default: return "?";
            }
        }
    };

    /**
     * Which hand `C_ScriptBindHuman::GetItemInHand` indexes.
     *
     * The `HS_RIGHT` and `HS_LEFT` constants the same constructor publishes.
     * Note the order: right is zero, which is the opposite of what the
     * main/off-hand slot pairing above would suggest.
     */
    struct E_HandSlotId {
        enum Type : std::int32_t {
            Right = 0,
            Left  = 1,

            Count = 2,
        };
    };

    static_assert(sizeof(E_WeaponEquipSlot::Type) == 0x4, "E_WeaponEquipSlot::Type size mismatch");
    static_assert(sizeof(E_WeaponSet::Type) == 0x4, "E_WeaponSet::Type size mismatch");
    static_assert(sizeof(E_HandSlotId::Type) == 0x4, "E_HandSlotId::Type size mismatch");
} // namespace SDK::wh::entitymodule
