#pragma once

#include "../../types.h"
#include "../physics/i_physical_entity.h"
#include "i_character_instance.h"

#include <cstdint>

namespace SDK {
    /**
     * Abstract physical-skeleton base retained by ISkeletonPose.
     *
     * KCD2's interface contributes 27 primary-vtable entries including the
     * destructor. The physical operations remain opaque pending individual
     * signature proof.
     */
    struct ISkeletonPhysics {
        virtual ~ISkeletonPhysics() = default; // +0x00
        virtual void unk_fn0001() = 0;
        virtual void unk_fn0002() = 0;
        virtual void unk_fn0003() = 0;
        virtual void unk_fn0004() = 0;
        virtual void unk_fn0005() = 0;
        /**
         * The articulated bone collider, a second physical entity beside the
         * one `IEntity::GetPhysics` returns; null until the character is
         * physicalized. `C_Actor::PostPhysicalize` (0x1808AAC84) reaches
         * this slot; `CSkeletonPose`'s accessor is 0x181A77FD0.
         */
        virtual IPhysicalEntity *GetCharacterPhysics() const = 0; // +0x30
        virtual void unk_fn0007() = 0;
        virtual void unk_fn0008() = 0;
        virtual void unk_fn0009() = 0;
        virtual void unk_fn0010() = 0;
        virtual void unk_fn0011() = 0;
        virtual void unk_fn0012() = 0;
        virtual void unk_fn0013() = 0;
        virtual void unk_fn0014() = 0;
        virtual void unk_fn0015() = 0;
        virtual void unk_fn0016() = 0;
        virtual void unk_fn0017() = 0;
        virtual void unk_fn0018() = 0;
        virtual void unk_fn0019() = 0;
        virtual void unk_fn0020() = 0;
        virtual void unk_fn0021() = 0;
        virtual void unk_fn0022() = 0;
        virtual void unk_fn0023() = 0;
        virtual void unk_fn0024() = 0;
        virtual void unk_fn0025() = 0;
        virtual void unk_fn0026() = 0;
    };

    static_assert(sizeof(ISkeletonPhysics) == 0x8,
                  "ISkeletonPhysics size mismatch");

    /**
     * Runtime joint-pose interface shared by players, NPCs, and horses.
     *
     * CSkeletonPose's RTTI-proven table contains 54 entries in total. The
     * first four pose-specific slots read model-space and parent-space joint
     * transforms and their matching scale vectors from the live pose data.
     */
    struct ISkeletonPose : ISkeletonPhysics {
        virtual const QuatT &GetAbsJointByID(std::int32_t jointId) const = 0;
        virtual const QuatT &GetRelJointByID(std::int32_t jointId) const = 0;
        virtual Diag33 GetAbsJointScalingByID(std::int32_t jointId) const = 0;
        virtual Diag33 GetRelJointScalingByID(std::int32_t jointId) const = 0;
        virtual void SetPostProcessCallback(int (*callback)(ICharacterInstance *, void *), void *data) = 0; // +0xF8, 0x180B521B0
        virtual void SetForceSkeletonUpdate(std::int32_t numFrames) = 0;                                       // +0x100
        /**
         * Asks for the pose to be reset to the default skeleton's. Only sets
         * a flag (bit 0x08 of `CSkeletonPose+0x360`); the reset runs at the
         * next animation pre-update (0x18083F718) or ragdoll build
         * (0x1805259BC, 0x180B73008), whichever comes first.
         */
        virtual void SetDefaultPose() = 0; // +0x108, 0x181A6E470
        virtual void unk_fn0034() = 0;
        virtual void unk_fn0035() = 0;
        virtual void unk_fn0036() = 0;
        virtual void unk_fn0037() = 0;
        virtual void unk_fn0038() = 0;
        virtual void unk_fn0039() = 0;
        virtual void unk_fn0040() = 0;
        virtual void unk_fn0041() = 0;
        virtual void unk_fn0042() = 0;
        virtual void unk_fn0043() = 0;
        virtual void unk_fn0044() = 0;
        virtual void unk_fn0045() = 0;
        virtual void unk_fn0046() = 0;
        virtual void unk_fn0047() = 0;
        virtual void unk_fn0048() = 0;
        virtual void unk_fn0049() = 0;
        virtual void unk_fn0050() = 0;
        virtual void unk_fn0051() = 0;
        virtual void unk_fn0052() = 0;
        virtual void unk_fn0053() = 0; // +0x1A8
    };

    static_assert(sizeof(ISkeletonPose) == 0x8,
                  "ISkeletonPose size mismatch");
} // namespace SDK
