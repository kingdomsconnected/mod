#include "c_ui_flash_map_poi_marker.h"

#include "../../patterns.h"

#include <utils/safe_win32.h>

#include <utils/hooking/hooking.h>

namespace SDK::wh::guimodule {
    FlashMapPoiMarkerRef MakeFlashMapPoiMarker(const S_EntityMapMark &mark) {
        // A free function returning a shared_ptr by value: the hidden return
        // pointer is its first argument. Both words are written on every path.
        FlashMapPoiMarkerRef row;
        hook::call<void>(gPatterns.MakeFlashMapPoiMarker, row.Slot(), &mark);
        return row;
    }
} // namespace SDK::wh::guimodule
