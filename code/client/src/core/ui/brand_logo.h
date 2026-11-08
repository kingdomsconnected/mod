#pragma once

#include <imgui.h>

#include <cstdint>
#include <vector>

namespace KCDC::Core {
    /**
     * The mod's logo in the bottom-left corner of a session's screen,
     * `files/kcdc/logo.png`.
     *
     * Its size is a fraction of the back buffer's height, and it is drawn at
     * exactly the pixels it covers: the file is resampled on the CPU with
     * WIC's Fant filter whenever that size changes, then uploaded as an ImGui
     * user texture. Handing the GPU the full-size art instead would minify it
     * through a bilinear sampler with no mips, which aliases the thin strokes
     * of the blackletter at 1080p and wastes the file at 4K.
     *
     * Everything here runs on the game thread inside an ImGui widget
     * callback, under the wrapper's render lock, which is what makes creating
     * and retiring textures safe against the present hook.
     */
    class BrandLogo final {
      public:
        void Render();

      private:
        /** Decodes the file once into premultiplied RGBA. False, and never retried, when it cannot. */
        bool Load();

        /** Replaces the texture with one resampled to `width` by `height`. */
        bool Rebuild(int width, int height);

        /** Frees the textures the backend has finished destroying. */
        void CollectRetired();

        std::vector<std::uint8_t> _source;
        int _sourceWidth  = 0;
        int _sourceHeight = 0;
        bool _loadFailed  = false;

        // Owned here, registered with the ImGui context. A replaced texture
        // is only queued for destruction: the backend frees its GPU copy some
        // frames later, and only then may the data itself go.
        ImTextureData *_texture = nullptr;
        std::vector<ImTextureData *> _retired;
    };
} // namespace KCDC::Core
