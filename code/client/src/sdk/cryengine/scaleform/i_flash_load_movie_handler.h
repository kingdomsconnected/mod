#pragma once

namespace SDK {
    /**
     * Raw pixels a load-movie handler answers an image URL with.
     *
     * `CryGFxImageLoader::LoadImage` (0x18080B14C) accepts it when
     * `GetPtr`, `GetPitch`, `GetWidth` and `GetHeight` are all set
     * (0x18244E13C, slots 4, 3, 1, 2) and wraps it in a
     * `GImageInfoILMISrcXRender` (0x182446AA0). That object builds its
     * texture on the first `GetTexture` (0x18244D1D4), which takes only
     * formats 1 and 2, then calls `Release`; its destructor (0x182447F8C)
     * calls `Release` instead when no texture was built. An image the
     * loader refuses as invalid is never released.
     */
    struct IFlashLoadMovieImage {
        enum class EFmt : int {
            None      = 0,
            RGB_888   = 1,
            ARGB_8888 = 2, // bytes R, G, B, A in memory (seen in game: BGRA drew red and blue swapped)
        };

        virtual void Release()         = 0; // 0
        virtual int GetWidth() const   = 0; // 1
        virtual int GetHeight() const  = 0; // 2
        virtual int GetPitch() const   = 0; // 3
        virtual void *GetPtr() const   = 0; // 4
        virtual EFmt GetFormat() const = 0; // 5

      protected:
        ~IFlashLoadMovieImage() = default;
    };

    /**
     * The one handler `CScaleformHelper::SetFlashLoadMovieHandler` installs
     * (global 0x185665B00). `LoadImage` asks it first for every `img://`
     * URL, passing the text after `://`; null falls through to the
     * engine's own routes (TEXID, then the file by extension).
     */
    struct IFlashLoadMovieHandler {
        virtual IFlashLoadMovieImage *LoadMovie(const char *filePath) = 0; // 0

      protected:
        ~IFlashLoadMovieHandler() = default;
    };

    static_assert(sizeof(IFlashLoadMovieImage) == 0x8, "IFlashLoadMovieImage size mismatch");
    static_assert(sizeof(IFlashLoadMovieHandler) == 0x8, "IFlashLoadMovieHandler size mismatch");
} // namespace SDK
