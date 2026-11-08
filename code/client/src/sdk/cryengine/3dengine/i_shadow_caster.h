#pragma once

#include "../../types.h"

#include <cstddef>
#include <cstdint>

namespace SDK {
    /**
     * Abstract shadow-casting render interface, vtable 0x183A33A70.
     *
     * Eight entries in stock CryEngine order: destructor, HasOcclusionmap,
     * ComputeLod, Render, GetBBox, FillBBox, GetEntityCharacter,
     * GetRenderNodeType. The bounds accessors and the type are named from
     * CBrush's implementations; the rest stay opaque. Its deleting destructor
     * proves one opaque data word after the vptr.
     */
    class IShadowCaster {
      public:
        virtual ~IShadowCaster() = default; // +0x00
        virtual void unk_fn0001() = 0;
        virtual void unk_fn0002() = 0;
        virtual void unk_fn0003() = 0;
        // +0x20 is GetBBox, which returns the same box by value through a
        // hidden return pointer. Left unnamed: FillBBox answers the same
        // question without an sret to get right.
        virtual void unk_fn0004() = 0;

        /** The node's world bounding box. Empty for a node with no geometry. */
        virtual void FillBBox(AABB &bounds) const = 0; // +0x28
        virtual void unk_fn0006() = 0;
        virtual EERType GetRenderNodeType() = 0; // +0x38

      private:
        // The IRenderNode constructor (0x180422D34) zeroes 0x08..0x0E and
        // leaves 0x0F alone: stock MAX_GSM_CACHED_LODS_NUM is 3, so the two
        // cache arrays, the bias and the unused byte fill the word exactly.
        std::uint8_t m_shadowCacheLastRendered[3]; // 0x08
        std::uint8_t m_shadowCacheLod[3];          // 0x0B
        std::int8_t m_cShadowLodBias;              // 0x0E
        std::int8_t m_unused;                      // 0x0F, never written by the constructor

        static void AssertLayout() {
            static_assert(offsetof(IShadowCaster, m_shadowCacheLod) == 0x0B, "IShadowCaster::m_shadowCacheLod offset mismatch");
            static_assert(offsetof(IShadowCaster, m_cShadowLodBias) == 0x0E, "IShadowCaster::m_cShadowLodBias offset mismatch");
        }
    };

    static_assert(sizeof(IShadowCaster) == 0x10,
                  "IShadowCaster size mismatch");
} // namespace SDK
