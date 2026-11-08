#pragma once

#include "../../types.h"

#include <cstdint>

namespace SDK {
    class IRenderNode;

    /**
     * Abstract terrain interface, vtable 0x1840405C8.
     *
     * KCD2 emits exactly 20 entries. Only the virtual destructor has a
     * source-identifiable signature, so all terrain contracts remain opaque.
     */
    class ITerrain {
      public:
        virtual ~ITerrain() = default; // +0x00
        virtual void unk_fn0001() = 0;
        virtual void unk_fn0002() = 0;
        virtual void unk_fn0003() = 0;
        virtual void unk_fn0004() = 0;
        virtual IRenderNode *AddVegetationInstance(
            int staticGroupId, const Vec3 &position, float scale,
            std::uint8_t brightness, std::uint8_t angle,
            std::uint8_t angleX = 0, std::uint8_t angleY = 0) = 0; // +0x28
        virtual void unk_fn0006() = 0;
        virtual void unk_fn0007() = 0;
        virtual void unk_fn0008() = 0;
        /** Deletes every vegetation render node (render-node type 4). */
        virtual void RemoveAllVegetationInstances() = 0; // +0x48
        virtual void unk_fn0010() = 0;
        virtual void unk_fn0011() = 0;
        virtual void unk_fn0012() = 0;
        virtual void unk_fn0013() = 0;
        virtual void unk_fn0014() = 0;
        virtual void unk_fn0015() = 0;
        virtual void unk_fn0016() = 0;
        virtual void unk_fn0017() = 0;
        virtual void unk_fn0018() = 0;
        virtual void unk_fn0019() = 0; // +0x98
    };

    static_assert(sizeof(ITerrain) == 0x8, "ITerrain size mismatch");
} // namespace SDK
