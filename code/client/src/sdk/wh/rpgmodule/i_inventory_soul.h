#pragma once

#include "../../types.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::rpgmodule {
    /** Exact 52-slot primary contract implemented by C_InventorySoul. */
    class I_InventorySoul {
      public:
        /**
         * One entry of the soul's clothing- or weapon-preset override stack
         * (C_InventorySoul +0x20 and +0x38; the base preset sits at +0x50).
         *
         * The engine's own type name for this 32-byte record did not survive:
         * `C_ClothingPresetRef` in the RTTI is a polymorphic class, and this
         * has no vtable. The layout is read out of
         * `C_ScriptBindActor::EquipClothingPreset` (0x1823DCEC8), which builds
         * one on its stack, and `RemovePreset_181FE05B0`, which finds an entry
         * by `presetId` and `tag` together.
         */
        struct S_PresetOverride {
            /** The preset's key in its database. */
            CryGUID presetId; // 0x00

            /**
             * Who pushed the entry. The Lua binding writes
             * `SCRIPTBINDACTOR_OVERRIDE`; `RemovePreset` only finds an entry
             * whose tag compares equal, so each pusher removes its own.
             */
            CryStringT<char> tag; // 0x10

            /** Written 1 by every native pusher; its reader is not proven. */
            std::uint8_t unknown18; // 0x18

            /** Nonzero stamps the preset's items with item flag 0x40, which wins the outfit solver's per-slot ranking. */
            std::uint8_t priorityItems; // 0x19

            std::byte m_padding001A[0x6]; // 0x1A
        };

        /**
         * What happens to the items of the preset being replaced, in
         * `ReleasePresetItems_181FE1E70`: `Delete` takes them out of the
         * inventory; the other two leave them there, `Unequip` taking off the
         * ones that were worn.
         */
        enum class E_PresetItemMode : std::uint8_t {
            Delete  = 0,
            Unequip = 1,
            Keep    = 2,
        };

        virtual void unk_fn0000() = 0;
        virtual void unk_fn0001() = 0;
        /**
         * Puts the soul's inventory back to what its presets say it owns
         * (+0x10, `ResetInventory_18087C338`): builds the full item list --
         * the clothing override in force or the base preset, the weapon
         * presets and the inventory presets -- makes the inventory exactly
         * that (`ReconcileToItemList`: matches kept, missing made, everything
         * else removed), then applies the outfit. What the soul's own reset
         * (0x1803F37C8) runs, with `force` set, which also sets `createAll`.
         */
        virtual void ResetInventory(bool force, bool createAll) = 0;
        virtual void unk_fn0003()                               = 0;
        virtual void unk_fn0004()                               = 0;
        virtual void unk_fn0005()                               = 0;
        virtual void unk_fn0006()                               = 0;
        virtual void unk_fn0007()                               = 0;
        virtual void unk_fn0008()                               = 0;
        virtual void unk_fn0009()                               = 0;
        /**
         * Pushes `preset` on top of the clothing stack (+0x50,
         * `PushClothingPreset_181FE1A00`), makes the items it names that the
         * inventory lacks, deals with the previous top's items as `itemMode`
         * says, and with `applyNow` applies the outfit. Taken by value: the
         * callee destroys `preset.tag`, so it has to be an engine-built string
         * handed over, never a borrowed one.
         */
        virtual void PushClothingPreset(S_PresetOverride preset, E_PresetItemMode itemMode, bool applyNow) = 0;

        /** The weapon-stack twin of `PushClothingPreset` (+0x58, `PushWeaponPreset_181FE1AE0`). */
        virtual void PushWeaponPreset(S_PresetOverride preset, E_PresetItemMode itemMode, bool applyNow) = 0;

        /**
         * Takes the entry matching `preset` (id and tag) off the clothing
         * stack, or the weapon stack when `weaponStack` (+0x60,
         * `RemovePreset_181FE05B0`). Taking the top reconciles to the entry
         * now below it; `skipCreate` stops that making its missing items.
         * Same ownership of `preset.tag` as the push.
         */
        virtual void RemovePreset(S_PresetOverride preset, E_PresetItemMode itemMode, int weaponStack, bool applyNow, bool skipCreate) = 0;
        virtual void unk_fn0013()                                                                                                      = 0;
        virtual void unk_fn0014()                                                                                                      = 0;
        virtual void unk_fn0015()                                                                                                      = 0;
        virtual void unk_fn0016()                                                                                                      = 0;
        virtual void unk_fn0017()                                                                                                      = 0;
        virtual void unk_fn0018()                                                                                                      = 0;
        virtual void unk_fn0019()                                                                                                      = 0;
        virtual void unk_fn0020()                                                                                                      = 0;
        virtual void unk_fn0021()                                                                                                      = 0;
        virtual void unk_fn0022()                                                                                                      = 0;
        virtual void unk_fn0023()                                                                                                      = 0;
        virtual void unk_fn0024()                                                                                                      = 0;
        virtual void unk_fn0025()                                                                                                      = 0;
        virtual void unk_fn0026()                                                                                                      = 0;
        virtual void unk_fn0027()                                                                                                      = 0;
        virtual void unk_fn0028()                                                                                                      = 0;
        virtual void unk_fn0029()                                                                                                      = 0;
        virtual void unk_fn0030()                                                                                                      = 0;
        virtual void unk_fn0031()                                                                                                      = 0;
        virtual void unk_fn0032()                                                                                                      = 0;
        virtual void unk_fn0033()                                                                                                      = 0;
        virtual void unk_fn0034()                                                                                                      = 0;
        virtual void unk_fn0035()                                                                                                      = 0;
        virtual void unk_fn0036()                                                                                                      = 0;
        virtual void unk_fn0037()                                                                                                      = 0;
        virtual void unk_fn0038()                                                                                                      = 0;
        virtual void unk_fn0039()                                                                                                      = 0;
        virtual void unk_fn0040()                                                                                                      = 0;
        virtual void unk_fn0041()                                                                                                      = 0;
        virtual void unk_fn0042()                                                                                                      = 0;
        /**
         * The id of the clothing preset in force: the stack's top entry's
         * `presetId`, or the base preset's id at +0x50 when the stack is empty
         * (+0x158, `GetClothingPreset_18087E308`). Only a GUID -- the base is
         * a bare `CryGUID`, not an `S_PresetOverride`, so nothing past the
         * first 16 bytes may be read, and every engine caller reads only those.
         */
        virtual const CryGUID *GetClothingPreset() const = 0;

        /** The weapon-stack twin of `GetClothingPreset`, the base at +0x60 (+0x160, `GetWeaponPreset_18087E2F4`). */
        virtual const CryGUID *GetWeaponPreset() const = 0;
        virtual void unk_fn0045()                      = 0;
        virtual void unk_fn0046()                      = 0;
        virtual void unk_fn0047()                      = 0;
        virtual void unk_fn0048()                      = 0;
        virtual void unk_fn0049()                      = 0;
        virtual void unk_fn0050()                      = 0;
        virtual void unk_fn0051()                      = 0;

      protected:
        ~I_InventorySoul() = default;

      private:
        static void AssertLayout() {
            static_assert(offsetof(S_PresetOverride, tag) == 0x10, "I_InventorySoul::S_PresetOverride::tag offset mismatch");
            static_assert(offsetof(S_PresetOverride, unknown18) == 0x18, "I_InventorySoul::S_PresetOverride::unknown18 offset mismatch");
            static_assert(offsetof(S_PresetOverride, priorityItems) == 0x19, "I_InventorySoul::S_PresetOverride::priorityItems offset mismatch");
        }
    };

    static_assert(sizeof(I_InventorySoul::S_PresetOverride) == 0x20, "I_InventorySoul::S_PresetOverride size mismatch");

    static_assert(sizeof(I_InventorySoul) == 0x8, "I_InventorySoul size mismatch");
} // namespace SDK::wh::rpgmodule
