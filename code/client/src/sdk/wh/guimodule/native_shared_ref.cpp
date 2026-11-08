#include "native_shared_ref.h"

#include "c_compass_mark.h"
#include "c_ui_flash_map_poi_marker.h"
#include "c_ui_flash_object.h"

#include "../../patterns.h"

#include <utils/safe_win32.h>

#include <utils/hooking/hooking.h>

#include <intrin.h>

namespace SDK::wh::guimodule {
    template <typename T>
    void NativeSharedRef<T>::Incref(SNativeRefCount *control) {
        _InterlockedIncrement(&control->uses);
    }

    template <typename T>
    void NativeSharedRef<T>::Release() {
        // Null whenever the native answered an empty reference, which is the
        // ordinary answer rather than a failure.
        if (m_control != nullptr) {
            hook::this_call<void>(gPatterns.std__Ref_count_base__Decref, m_control);
        }

        m_object  = nullptr;
        m_control = nullptr;
    }

    // Explicit, so the layout assertions are checked and both out-of-line
    // members link.
    template class NativeSharedRef<C_CompassMark>;
    template class NativeSharedRef<C_CheckpointMark>;
    template class NativeSharedRef<C_UIFlashObject>;
    template class NativeSharedRef<C_UIFlashMapPoiMarker>;
} // namespace SDK::wh::guimodule
