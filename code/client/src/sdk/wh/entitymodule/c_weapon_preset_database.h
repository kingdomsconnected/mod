#pragma once

#include "../../types.h"
#include "../databasemodule/c_object_database_key_indexed.h"
#include "../databasemodule/c_object_tree_database.h"
#include "c_weapon_preset.h"

#include <cstddef>
#include <vector>

namespace SDK::wh::entitymodule {
    /**
     * The game's weapon presets: `Libs/Tables/item/weapon_preset*.xml`, one
     * process-global object (0x185325FA0).
     *
     * The same shape as `C_ClothingPresetDatabase`: rows by value at +0x30,
     * a name index at +0x78, and a constructor (0x181541AF4) that zeroes
     * +0x78..+0x90 after its base.
     */
    class C_WeaponPresetDatabase: public databasemodule::C_ObjectDatabaseKeyIndexed<databasemodule::C_ObjectTreeDatabase<C_WeaponPreset, std::vector>, databasemodule::C_ObjectDatabaseDefaultKeyExtractor<C_WeaponPreset>> {
      public:
        ~C_WeaponPresetDatabase() override = default;

        static C_WeaponPresetDatabase *GetInstance();

        /** The row keyed `presetId`, or null. Borrowed: an append may move it. */
        const C_WeaponPreset *FindByGuid(const CryGUID &presetId) const;

      private:
        std::byte m_nameIndex0078[0x18];

        static void AssertLayout() {
            static_assert(offsetof(C_WeaponPresetDatabase, m_nameIndex0078) == 0x78, "C_WeaponPresetDatabase::m_nameIndex0078 offset mismatch");
        }
    };

    static_assert(sizeof(C_WeaponPresetDatabase) == 0x90, "C_WeaponPresetDatabase size mismatch");
} // namespace SDK::wh::entitymodule
