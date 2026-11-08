#pragma once

#include "../../patterns.h"
#include "i_texture.h"

#include <utils/hooking/hooking.h>

namespace SDK {
    /**
     * The renderer's texture. Only its `ITexture` base is modelled: the mod
     * holds `CTexture` pointers out of display contexts and asks them for
     * their id, nothing more.
     */
    class CTexture: public ITexture {
      public:
        CTexture()                            = delete;
        CTexture(const CTexture &)            = delete;
        CTexture &operator=(const CTexture &) = delete;

        /**
         * `CTexture::ReloadFile` (0x182518E20): re-reads the texture registered under `path` --
         * its sRGB twin too -- from whatever the paks now resolve that path to. A no-op for a name
         * nothing has loaded. Rebuilds a streamed texture from the current file header, so it is
         * what keeps mips consistent after the file under a resident texture changes, and it
         * clears a cached load failure (`FT_FAILED`).
         */
        static bool ReloadFile(const char *path) {
            return hook::call<bool>(gPatterns.CTexture__ReloadFile, path);
        }

      protected:
        ~CTexture() = default;
    };
} // namespace SDK
