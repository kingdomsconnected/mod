#pragma once

#include "c_ui_flash_object.h"
#include "native_shared_ref.h"
#include "s_entity_map_mark.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::guimodule {
    /**
     * The flash row for one POI on the map screen. Every field is reflected;
     * the RTTR names at 0x1801891A0 are the ones in the comments.
     *
     * Built only by `MakeFlashMapPoiMarker`, whose constructor (0x1815B9D70)
     * fills it from an `S_EntityMapMark`: the icon and the localized name from
     * the type, the order from the type's legend row, and the position from the
     * mark's source. Nothing registers it; it lives as long as a reference.
     */
    class C_UIFlashMapPoiMarker final: public C_UIFlashObject {
      public:
        C_UIFlashMapPoiMarker()                                         = delete;
        ~C_UIFlashMapPoiMarker()                                        = delete;
        C_UIFlashMapPoiMarker(const C_UIFlashMapPoiMarker &)            = delete;
        C_UIFlashMapPoiMarker &operator=(const C_UIFlashMapPoiMarker &) = delete;

        float GetX() const {
            return m_xPos;
        }

        float GetY() const {
            return m_yPos;
        }

      private:
        CryStringT<char> m_icon; // 0x18 Icon
        std::int32_t m_state;    // 0x20 State
        bool m_isFastTravel;     // 0x24 IsFastTravel
        std::byte m_padding0025[3];
        std::int32_t m_order; // 0x28 Order
        float m_xPos;         // 0x2C XPos, world metres
        float m_yPos;         // 0x30 YPos, world metres
        std::byte m_padding0034[4];

        static void AssertLayout() {
            static_assert(offsetof(C_UIFlashMapPoiMarker, m_icon) == 0x18, "C_UIFlashMapPoiMarker::m_icon offset mismatch");
            static_assert(offsetof(C_UIFlashMapPoiMarker, m_state) == 0x20, "C_UIFlashMapPoiMarker::m_state offset mismatch");
            static_assert(offsetof(C_UIFlashMapPoiMarker, m_isFastTravel) == 0x24, "C_UIFlashMapPoiMarker::m_isFastTravel offset mismatch");
            static_assert(offsetof(C_UIFlashMapPoiMarker, m_order) == 0x28, "C_UIFlashMapPoiMarker::m_order offset mismatch");
            static_assert(offsetof(C_UIFlashMapPoiMarker, m_xPos) == 0x2C, "C_UIFlashMapPoiMarker::m_xPos offset mismatch");
            static_assert(offsetof(C_UIFlashMapPoiMarker, m_yPos) == 0x30, "C_UIFlashMapPoiMarker::m_yPos offset mismatch");
        }
    };

    // MakeFlashMapPoiMarker allocates 0x48: the 0x10 control block, then this.
    static_assert(sizeof(C_UIFlashMapPoiMarker) == 0x38, "C_UIFlashMapPoiMarker size mismatch");

    using FlashObjectRef       = NativeSharedRef<C_UIFlashObject>;
    using FlashMapPoiMarkerRef = NativeSharedRef<C_UIFlashMapPoiMarker>;

    /**
     * Builds the flash row for `mark`, returning the only reference to it, or
     * an empty one when the allocation failed.
     *
     * Reads `id`, `type`, `state` and `source` and nothing else. The source is
     * called through its slot 2 for a `const Vec3 *`, once, before this
     * returns, so it only has to outlive the call.
     */
    FlashMapPoiMarkerRef MakeFlashMapPoiMarker(const S_EntityMapMark &mark);
} // namespace SDK::wh::guimodule
