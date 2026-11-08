#include "c_weapon_preset_database.h"

#include "../../patterns.h"

#include <utils/hooking/hooking.h>

namespace SDK::wh::entitymodule {
    C_WeaponPresetDatabase *C_WeaponPresetDatabase::GetInstance() {
        return reinterpret_cast<C_WeaponPresetDatabase *>(gPatterns.C_WeaponPresetDatabase__Instance);
    }

    const C_WeaponPreset *C_WeaponPresetDatabase::FindByGuid(const CryGUID &presetId) const {
        return hook::this_call<const C_WeaponPreset *>(gPatterns.C_WeaponPresetDatabase__FindByGuid, this, &presetId);
    }
} // namespace SDK::wh::entitymodule
