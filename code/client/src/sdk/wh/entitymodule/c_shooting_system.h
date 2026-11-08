#pragma once

#include "../../patterns.h"
#include "c_projectile.h"
#include "c_missile_weapon_runtime_data.h"
#include "s_item_class_guid.h"
#include "../../types.h"

#include <utils/hooking/hooking.h>

#include <cstddef>
#include <cstdint>

namespace SDK::wh::entitymodule {
    class C_Actor;
    class C_Item;
    class CProjectile;
    class CArrow;

    // Origin-correction layout; the native source type name is unknown.
    struct ProjectileOriginCorrectionLayout {
        Vec3 up;
        bool enabled = false;

        std::byte m_padding000D[0x3] {};

      private:
        static void AssertLayout() {
            static_assert(offsetof(ProjectileOriginCorrectionLayout, up) == 0x0, "ProjectileOriginCorrectionLayout::up offset mismatch");
            static_assert(offsetof(ProjectileOriginCorrectionLayout, enabled) == 0xC, "ProjectileOriginCorrectionLayout::enabled offset mismatch");
        }
    };

    static_assert(sizeof(ProjectileOriginCorrectionLayout) == 0x10, "ProjectileOriginCorrectionLayout size mismatch");

    struct MissileLaunch {
        struct ItemProperties {
            std::uint32_t quality = 1;
            float health          = 1.0F;
            CryGUID poisonBuff {};
            std::uint32_t poisonCharges = 0;

            // Weapon and ammo runtime data share the equipment base.
            void Capture(C_Item *item) {
                quality = item->InvokeGetQualityRaw();
                health  = item->GetHealth();

                if (auto *runtime = static_cast<C_WeaponEquipRuntimeData *>(item->GetRuntimeData())) {
                    poisonBuff    = runtime->GetPoisonBuff();
                    poisonCharges = runtime->GetPoisonCharges();
                }
            }
        };

        CProjectile *projectile = nullptr; // identity only; never serialized
        S_ItemClassGUID weaponClass {};
        S_ItemClassGUID ammunitionClass {};
        Vec3 position {};
        Vec3 direction {};
        Vec3 velocity {};
        ItemProperties weaponProperties;
        ItemProperties ammunitionProperties;
        bool consumeAmmunition = true;
    };

    // Native singleton; its source class name is unknown.
    class C_ShootingSystem185484990 {
      public:
        static C_ShootingSystem185484990 *GetInstance() {
            return hook::call<C_ShootingSystem185484990 *>(gPatterns.C_ShootingSystem__GetInstance);
        }

        void SpawnAndLaunchProjectile(C_Actor *actor, EntityId target, const Vec3 &direction, const ProjectileOriginCorrectionLayout &correction, Vec3 &position, C_Item *weapon, C_Item *ammunition, float speed, bool consumeAmmunition) {
            hook::this_call<void>(gPatterns.C_ShootingSystem__SpawnAndLaunchProjectile, this, actor, target, &direction, &correction, &position, weapon, ammunition, speed, consumeAmmunition);
        }
    };

    static_assert(sizeof(C_ShootingSystem185484990) == 0x1, "C_ShootingSystem185484990 size mismatch");
} // namespace SDK::wh::entitymodule
