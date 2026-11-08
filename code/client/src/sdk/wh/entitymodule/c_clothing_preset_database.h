#pragma once

#include "../../types.h"
#include "../databasemodule/c_object_database_key_indexed.h"
#include "../databasemodule/c_object_tree_database.h"
#include "s_clothing_preset.h"

#include <cstddef>
#include <vector>

namespace SDK::wh::entitymodule {
    /**
     * The game's clothing presets: `Libs/Tables/item/clothing_preset*.xml`,
     * one process-global object (0x185324520).
     *
     * Rows live by value in the vector at +0x30, sorted by key; the name
     * index this class adds at +0x78 points into that vector. The constructor
     * (0x181541B30) zeroes +0x78..+0x90 after its base, which is the size.
     *
     * Every engine reader looks a preset up per call and drops the pointer --
     * `BuildPresetItemList`, `ExpandClothingPreset`, the equip-by-preset and
     * condition helpers -- which is what lets `AppendFromXml` grow the vector
     * at runtime, as `wh_db_ReloadObjectDatabase` does to all of them.
     */
    class C_ClothingPresetDatabase: public databasemodule::C_ObjectDatabaseKeyIndexed<databasemodule::C_ObjectTreeDatabase<S_ClothingPreset, std::vector>, databasemodule::C_ObjectDatabaseDefaultKeyExtractor<S_ClothingPreset>> {
      public:
        ~C_ClothingPresetDatabase() override = default;

        static C_ClothingPresetDatabase *GetInstance();

        /** The row keyed `presetId`, or null. Borrowed: an append may move it. */
        const S_ClothingPreset *FindByGuid(const CryGUID &presetId) const;

      private:
        std::byte m_nameIndex0078[0x18];

        static void AssertLayout() {
            static_assert(offsetof(C_ClothingPresetDatabase, m_nameIndex0078) == 0x78, "C_ClothingPresetDatabase::m_nameIndex0078 offset mismatch");
        }
    };

    static_assert(sizeof(C_ClothingPresetDatabase) == 0x90, "C_ClothingPresetDatabase size mismatch");
} // namespace SDK::wh::entitymodule
