#pragma once

#include "i_flash_load_movie_handler.h"

namespace SDK {
    /**
     * `gEnv->pScaleformHelper` (+0x168), a `CScaleformHelper` created by
     * 0x1819DEAD4 (vtable 0x1846A75C0). Only the handler slot is proven.
     */
    struct IScaleformHelper {
        virtual void unk_fn0000() = 0;
        virtual void unk_fn0001() = 0;
        virtual void unk_fn0002() = 0;
        virtual void unk_fn0003() = 0;
        virtual void unk_fn0004() = 0; // SetTranslatorWordWrappingMode
        virtual void unk_fn0005() = 0; // OnLanguageChanged
        virtual void unk_fn0006() = 0;
        virtual void unk_fn0007() = 0;
        virtual void unk_fn0008() = 0;
        virtual void unk_fn0009() = 0;
        virtual void unk_fn0010() = 0;
        /** Slot 11 (0x181A73260): stores the handler in 0x185665B00. One handler, no chaining. */
        virtual void SetFlashLoadMovieHandler(IFlashLoadMovieHandler *handler) = 0;
    };

    static_assert(sizeof(IScaleformHelper) == 0x8, "IScaleformHelper size mismatch");
} // namespace SDK
