#pragma once

#include "sdk/cryengine/physics/pe_params_particle.h"
#include "sdk/types.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace SDK {
    class IPhysicalEntity;

    /** Which `pe_params` a structure is; KCD2 keeps CryPhysics' stock numbering. */
    enum EPE_Params : std::int32_t {
        ePE_params_pos        = 0,
        ePE_player_dimensions = 1,
        ePE_params_particle   = 3,
        ePE_player_dynamics   = 4,
    };

    /**
     * CryPhysics' "unused" markers, which every `pe_params` field carries until
     * it is deliberately set.
     *
     * `CLivingEntity::SetParams` (0x18084CDAC) tests each field against these
     * and skips the ones still marked, so a structure must be fully marked
     * before one field is filled in -- a zeroed one reads as "set everything
     * to zero", which for a living entity means no mass, no climb angle and no
     * collision types.
     */
    inline constexpr std::int32_t kUnusedInt      = static_cast<std::int32_t>(0x80000000);
    inline constexpr std::uint32_t kUnusedFloatBits = 0xFFBFFFFFu;

    inline float UnusedFloat() {
        float value = 0.0F;
        const std::uint32_t bits = kUnusedFloatBits;
        std::memcpy(&value, &bits, sizeof(value));
        return value;
    }

    /**
     * A living entity's dynamics, `ePE_player_dynamics`.
     *
     * Offsets verified field by field against `CLivingEntity::SetParams`
     * (0x18084CDAC), which reads `collTypes` at +0x48, `pLivingEntToIgnore` at
     * +0x50 (unused marker -1 rather than 0x80000000), `bActive` at +0x5C and
     * `bReleaseGroundColliderWhenNotActive` at +0x64. KCD2 appends three more
     * ints after the stock tail; they are carried so the structure is the
     * right size and are marked unused like everything else.
     *
     * Clearing `bActive` disables simulation except movement at the
     * requested velocity. The setter writes bit 0x40 of the flags at
     * `CLivingEntity + 0x2C8`, which gates the simulation step. Keep it clear
     * for a body placed entirely by the network, or it starts falling
     * between position updates.
     */
    struct pe_player_dynamics: pe_params {
        float kInertia;                          // +0x04
        float kInertiaAccel;                     // +0x08
        float kAirControl;                       // +0x0C
        float kAirResistance;                    // +0x10
        Vec3 gravity;                            // +0x14
        float nodSpeed;                          // +0x20
        std::int32_t bSwimming;                  // +0x24
        float mass;                              // +0x28
        std::int32_t surface_idx;                // +0x2C
        float minSlideAngle;                     // +0x30
        float maxClimbAngle;                     // +0x34
        float maxJumpAngle;                      // +0x38
        float minFallAngle;                      // +0x3C
        float maxVelGround;                      // +0x40
        float timeImpulseRecover;                // +0x44
        std::int32_t collTypes;                  // +0x48
        std::int32_t reserved_4C;
        IPhysicalEntity *pLivingEntToIgnore;     // +0x50, unused is -1
        std::int32_t bNetwork;                   // +0x58
        std::int32_t bActive;                    // +0x5C
        std::int32_t iRequestedTime;             // +0x60
        std::int32_t bReleaseGroundColliderWhenNotActive; // +0x64
        std::int32_t whReserved68;               // +0x68, KCD2 additions
        std::int32_t whReserved6C;               // +0x6C
        std::int32_t whReserved70;               // +0x70

        pe_player_dynamics() {
            type               = ePE_player_dynamics;
            const float unused = UnusedFloat();
            kInertia = kInertiaAccel = kAirControl = kAirResistance = unused;
            gravity = Vec3 {unused, unused, unused};
            nodSpeed = mass = unused;
            minSlideAngle = maxClimbAngle = maxJumpAngle = minFallAngle = unused;
            maxVelGround = timeImpulseRecover = unused;
            bSwimming = surface_idx = collTypes = reserved_4C = kUnusedInt;
            pLivingEntToIgnore = reinterpret_cast<IPhysicalEntity *>(~static_cast<std::uintptr_t>(0));
            bNetwork = bActive = iRequestedTime = bReleaseGroundColliderWhenNotActive = kUnusedInt;
            whReserved68 = whReserved6C = whReserved70 = kUnusedInt;
        }
    };

    static_assert(offsetof(pe_player_dynamics, kInertia) == 0x04, "pe_player_dynamics::kInertia offset mismatch");
    static_assert(offsetof(pe_player_dynamics, kAirControl) == 0x0C, "pe_player_dynamics::kAirControl offset mismatch");
    static_assert(offsetof(pe_player_dynamics, collTypes) == 0x48, "pe_player_dynamics::collTypes offset mismatch");
    static_assert(offsetof(pe_player_dynamics, pLivingEntToIgnore) == 0x50, "pe_player_dynamics::pLivingEntToIgnore offset mismatch");
    static_assert(offsetof(pe_player_dynamics, bActive) == 0x5C, "pe_player_dynamics::bActive offset mismatch");
    static_assert(offsetof(pe_player_dynamics, bReleaseGroundColliderWhenNotActive) == 0x64, "pe_player_dynamics::bReleaseGroundColliderWhenNotActive offset mismatch");
} // namespace SDK
