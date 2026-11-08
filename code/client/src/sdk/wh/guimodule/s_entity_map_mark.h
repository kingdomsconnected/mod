#pragma once

#include "e_compass_mark_type.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::guimodule {
    /**
     * One mark on the map screen, as the vector at `C_UIMap +0x5A0` holds it
     * behind a `shared_ptr`. Built at 0x180C4E2E8.
     *
     * There is no position here. The mark follows its source object, which
     * the game types as `void *` in every signature that takes one
     * (`FindOrCreateMark(int, void *, int)`): in practice it is always a
     * `wh::xgenaimodule::C_LinkableObject`, read through that class's slot 2,
     * `GetWorldPosition`, for a `const Vec3 *`.
     *
     * Never put one in `C_UIMap +0x5A0` whose source is not a real registered
     * `C_LinkableObject`: re-showing a legend category rebuilds a compass mark
     * from it, and that registers the source with the xgenai object manager.
     * `MakeFlashMapPoiMarker` is the one native that reads this struct and
     * nothing else, which is what makes a mod-built one safe to hand it.
     */
    struct S_EntityMapMark {
        std::int32_t id          = 0;                             // 0x00, the flash row's Id
        E_CompassMarkType type   = E_CompassMarkType::GeneralPoi; // 0x04
        std::int32_t state       = 0;                             // 0x08
        std::byte padding000C[4] = {};
        const void *source       = nullptr; // 0x10
        std::int32_t useCount    = 0;       // 0x18, AddMark/RemoveMark bookkeeping
        std::byte padding001C[4] = {};
    };

    static_assert(offsetof(S_EntityMapMark, id) == 0x00, "S_EntityMapMark::id offset mismatch");
    static_assert(offsetof(S_EntityMapMark, type) == 0x04, "S_EntityMapMark::type offset mismatch");
    static_assert(offsetof(S_EntityMapMark, state) == 0x08, "S_EntityMapMark::state offset mismatch");
    static_assert(offsetof(S_EntityMapMark, source) == 0x10, "S_EntityMapMark::source offset mismatch");
    static_assert(offsetof(S_EntityMapMark, useCount) == 0x18, "S_EntityMapMark::useCount offset mismatch");
    static_assert(sizeof(S_EntityMapMark) == 0x20, "S_EntityMapMark size mismatch");
} // namespace SDK::wh::guimodule
