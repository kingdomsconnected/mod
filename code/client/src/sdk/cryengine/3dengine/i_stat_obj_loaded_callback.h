#pragma once

namespace SDK {
    /**
     * Completion observer used by asynchronous static-object loading.
     *
     * RTTI identifies this global-namespace type as a struct and places it as
     * the sole base of CStatObjAsyncLoader. That implementation's table at
     * 0x183A3D9D0 contains exactly the destructor and one callback, while its
     * first concrete field at +0x08 proves the interface size.
     */
    struct IStatObjLoadedCallback {
        virtual ~IStatObjLoadedCallback() = default;
        virtual void unk_fn0001() = 0;
    };

    static_assert(sizeof(IStatObjLoadedCallback) == 0x8,
                  "IStatObjLoadedCallback size mismatch");
} // namespace SDK
