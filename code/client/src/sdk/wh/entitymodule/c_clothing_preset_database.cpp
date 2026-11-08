#include "c_clothing_preset_database.h"

#include "../../patterns.h"

#include <utils/hooking/hooking.h>

namespace SDK::wh::entitymodule {
    C_ClothingPresetDatabase *C_ClothingPresetDatabase::GetInstance() {
        return reinterpret_cast<C_ClothingPresetDatabase *>(gPatterns.C_ClothingPresetDatabase__Instance);
    }

    const S_ClothingPreset *C_ClothingPresetDatabase::FindByGuid(const CryGUID &presetId) const {
        return hook::this_call<const S_ClothingPreset *>(gPatterns.C_ClothingPresetDatabase__FindByGuid, this, &presetId);
    }
} // namespace SDK::wh::entitymodule
