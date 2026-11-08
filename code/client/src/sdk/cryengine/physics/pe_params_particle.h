#pragma once

#include "../../types.h"
#include "i_physical_entity.h"
#include "pe_params.h"

#include <bit>

namespace SDK {
    // Partial updates require unused sentinels; zero changes physics.
    struct pe_params_particle: pe_params {
        static constexpr float kUnusedFloat                     = std::bit_cast<float>(0xFFBFFFFFU);
        static constexpr std::int32_t kUnusedInt                = INT32_MIN;
        static constexpr std::int32_t kNoCollisionPierceability = 16;

        pe_params_particle(): pe_params {3} {}

        std::uint32_t flags = 0x80000000U; // 0x04
        float mass          = kUnusedFloat;
        float size          = kUnusedFloat;
        float thickness     = kUnusedFloat;
        Vec3 heading {kUnusedFloat, kUnusedFloat, kUnusedFloat}; // 0x14
        float velocity           = kUnusedFloat;                 // 0x20
        float kAirResistance     = kUnusedFloat;
        float kWaterResistance   = kUnusedFloat;
        float accThrust          = kUnusedFloat;
        float accLift            = kUnusedFloat;
        std::int32_t surface_idx = kUnusedInt; // 0x34
        Vec3 wspin {kUnusedFloat, kUnusedFloat, kUnusedFloat};
        Vec3 gravity {kUnusedFloat, kUnusedFloat, kUnusedFloat};
        Vec3 waterGravity {kUnusedFloat, kUnusedFloat, kUnusedFloat};
        Vec3 normal {kUnusedFloat, kUnusedFloat, kUnusedFloat};
        Vec3 rollAxis {kUnusedFloat, kUnusedFloat, kUnusedFloat};
        Quat q0 {kUnusedFloat, kUnusedFloat, kUnusedFloat, kUnusedFloat}; // 0x74; w at +0x80
        float minBounceVel = kUnusedFloat;
        float minVel       = kUnusedFloat;

        std::byte m_opaque008C[4] {};

        IPhysicalEntity *pColliderToIgnore = reinterpret_cast<IPhysicalEntity *>(UINTPTR_MAX); // 0x90
        std::int32_t iPierceability        = kUnusedInt;                                       // 0x98
        std::int32_t collTypes             = kUnusedInt;
        std::int32_t areaCheckPeriod       = kUnusedInt;
        std::int32_t dontPlayHitEffect     = kUnusedInt;

      private:
        static void AssertLayout() {
            static_assert(offsetof(pe_params_particle, flags) == 0x04);
            static_assert(offsetof(pe_params_particle, mass) == 0x08);
            static_assert(offsetof(pe_params_particle, size) == 0x0C);
            static_assert(offsetof(pe_params_particle, thickness) == 0x10);
            static_assert(offsetof(pe_params_particle, heading) == 0x14);
            static_assert(offsetof(pe_params_particle, velocity) == 0x20);
            static_assert(offsetof(pe_params_particle, kAirResistance) == 0x24);
            static_assert(offsetof(pe_params_particle, kWaterResistance) == 0x28);
            static_assert(offsetof(pe_params_particle, accThrust) == 0x2C);
            static_assert(offsetof(pe_params_particle, accLift) == 0x30);
            static_assert(offsetof(pe_params_particle, surface_idx) == 0x34);
            static_assert(offsetof(pe_params_particle, wspin) == 0x38);
            static_assert(offsetof(pe_params_particle, gravity) == 0x44);
            static_assert(offsetof(pe_params_particle, waterGravity) == 0x50);
            static_assert(offsetof(pe_params_particle, normal) == 0x5C);
            static_assert(offsetof(pe_params_particle, rollAxis) == 0x68);
            static_assert(offsetof(pe_params_particle, q0) == 0x74);
            static_assert(offsetof(pe_params_particle, minBounceVel) == 0x84);
            static_assert(offsetof(pe_params_particle, minVel) == 0x88);
            static_assert(offsetof(pe_params_particle, pColliderToIgnore) == 0x90);
            static_assert(offsetof(pe_params_particle, iPierceability) == 0x98);
            static_assert(offsetof(pe_params_particle, collTypes) == 0x9C);
            static_assert(offsetof(pe_params_particle, areaCheckPeriod) == 0xA0);
            static_assert(offsetof(pe_params_particle, dontPlayHitEffect) == 0xA4);
        }
    };

    static_assert(sizeof(pe_params_particle) == 0xA8, "pe_params_particle size mismatch");
} // namespace SDK
