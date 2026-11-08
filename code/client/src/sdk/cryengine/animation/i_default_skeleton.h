#pragma once

#include "../../types.h"

#include <cstdint>

namespace SDK {
    // BoneIDList roles, resolved per skeleton.
    enum ESkeletonBoneID : std::int16_t {
        BONEID_NECK = 9,
        BONEID_HEAD = 10,
    };

    // IDefaultSkeleton vtable: 0x18406B870.
    struct IDefaultSkeleton {
        virtual ~IDefaultSkeleton()                                              = default; // +0x00
        virtual std::uint32_t GetJointCount() const                              = 0;
        virtual std::int32_t GetJointParentIDByID(std::int32_t jointId) const    = 0;
        virtual std::int32_t GetControllerIDByID(std::int32_t jointId) const     = 0;
        virtual std::int32_t GetJointIDByCRC32(std::uint32_t crc32) const        = 0;
        virtual std::uint32_t GetJointCRC32ByID(std::int32_t jointId) const      = 0;
        virtual const char *GetJointNameByID(std::int32_t jointId) const         = 0;
        virtual std::int32_t GetJointIDByName(const char *name) const            = 0;
        virtual const QuatT &GetDefaultAbsJointByID(std::uint32_t jointId) const = 0;
        virtual const QuatT &GetDefaultRelJointByID(std::uint32_t jointId) const = 0;
        virtual void unk_fn0010()                                                = 0;
        virtual void unk_fn0011()                                                = 0;
        virtual void unk_fn0012()                                                = 0;
        virtual void unk_fn0013()                                                = 0;
        virtual void unk_fn0014()                                                = 0;
        virtual void unk_fn0015()                                                = 0;
        virtual void unk_fn0016()                                                = 0;
        virtual void unk_fn0017()                                                = 0;
        virtual void unk_fn0018()                                                = 0;
        virtual void unk_fn0019()                                                = 0;
        // Returns a signed joint index; -1 means unavailable (0x180641224).
        virtual std::int16_t GetJointIDByBoneID(std::int16_t boneId) const = 0; // +0xA0
        virtual void unk_fn0021()                                          = 0;
        virtual void unk_fn0022()                                          = 0; // +0xB0
    };

    static_assert(sizeof(IDefaultSkeleton) == 0x8, "IDefaultSkeleton size mismatch");
} // namespace SDK
