#pragma once

#include "../../cryengine/action/i_level_system.h"
#include "../../cryengine/entitysystem/i_entity_class.h"
#include "../../cryengine/system/i_console.h"
#include "c_projectile.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::entitymodule {
    // RTTI establishes ILevelSystemListener as the only base. The module's
    // initialization at 0x1810645B8 allocates 0x2350 bytes for this owner.
    class CWeaponSystem: public ILevelSystemListener {
      public:
        CProjectile *SpawnAmmo(IEntityClass *entityClass) {
            return hook::this_call<CProjectile *>(gPatterns.CWeaponSystem__SpawnAmmo, this, entityClass);
        }

      private:
        // Layout from the constructor at 0x1811A6D08.
        void *m_pListenerModule;                // 0x08, C_WHGameModules+0x18, which it registers with as a level-system listener
        void *m_pGameModule0010;                // 0x10, C_WHGameModules+0x170
        std::byte m_tracerManagerVftable[0x8];  // 0x18, inline CTracerManager's vftable
        std::byte m_tracers[100][0x58];         // 0x20, 100 CTracer (polymorphic, ctor 0x181654D60)
        std::uint16_t m_value2280;              // 0x2280, ctor zeroes
        std::byte m_padding2282[0x6];
        // MSVC std::map {head, size} pairs; each head is a fresh 48-byte sentinel node.
        void *m_pMapHead2288;                   // 0x2288
        std::uint64_t m_mapSize2290;            // 0x2290
        void *m_pMapHead2298;                   // 0x2298
        std::uint64_t m_mapSize22A0;            // 0x22A0
        void *m_pMapHead22A8;                   // 0x22A8
        std::uint64_t m_mapSize22B0;            // 0x22B0
        void *m_values22B8[3];                  // 0x22B8, ctor zeroes
        std::uint32_t m_value22D0;              // 0x22D0, ctor zeroes last
        std::byte m_padding22D4[0x4];
        void *m_pProjectileClassMapHead;        // 0x22D8, projectile class name -> factory (Projectile, Arrow, Bolt, Decoy)
        std::uint64_t m_projectileClassMapSize; // 0x22E0
        void *m_values22E8[3];                  // 0x22E8, ctor zeroes
        void *m_pMapHead2300;                   // 0x2300, another 48-byte sentinel
        std::uint64_t m_mapSize2308;            // 0x2308
        void *m_values2310[6];                  // 0x2310, ctor zeroes
        std::uint16_t m_value2340;              // 0x2340, ctor zeroes
        std::byte m_padding2342[0x6];
        SDK::ICVar *m_pPrecacheCVar;            // 0x2348, "i_precache"

        static void AssertLayout() {
            static_assert(offsetof(CWeaponSystem, m_pListenerModule) == 0x08, "CWeaponSystem::m_pListenerModule offset mismatch");
            static_assert(offsetof(CWeaponSystem, m_tracers) == 0x20, "CWeaponSystem::m_tracers offset mismatch");
            static_assert(offsetof(CWeaponSystem, m_value2280) == 0x2280, "CWeaponSystem::m_value2280 offset mismatch");
            static_assert(offsetof(CWeaponSystem, m_pMapHead2288) == 0x2288, "CWeaponSystem::m_pMapHead2288 offset mismatch");
            static_assert(offsetof(CWeaponSystem, m_value22D0) == 0x22D0, "CWeaponSystem::m_value22D0 offset mismatch");
            static_assert(offsetof(CWeaponSystem, m_pProjectileClassMapHead) == 0x22D8, "CWeaponSystem::m_pProjectileClassMapHead offset mismatch");
            static_assert(offsetof(CWeaponSystem, m_pMapHead2300) == 0x2300, "CWeaponSystem::m_pMapHead2300 offset mismatch");
            static_assert(offsetof(CWeaponSystem, m_value2340) == 0x2340, "CWeaponSystem::m_value2340 offset mismatch");
            static_assert(offsetof(CWeaponSystem, m_pPrecacheCVar) == 0x2348, "CWeaponSystem::m_pPrecacheCVar offset mismatch");
        }
    };

    static_assert(sizeof(CWeaponSystem) == 0x2350, "CWeaponSystem size mismatch");
} // namespace SDK::wh::entitymodule
