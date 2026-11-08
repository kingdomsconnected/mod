#pragma once

#include "sdk/cryengine/scaleform/i_flash_load_movie_handler.h"

#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace KCDC::Features::UI {
    /**
     * Pictures the mod puts where the game's UI looks for a texture.
     *
     * The UI builds its own `img://` URLs -- an item icon's
     * `Libs/UI/Textures/Icons/Items/<IconId>_icon.dds`, an alchemy recipe's
     * `Libs/UI/Textures/Books/Recipes/<RecipeId>_ui.dds` -- and
     * `CryGFxImageLoader::LoadImage` asks the engine's one load-movie handler
     * first for every one of them. This module fills that slot
     * (`CScaleformHelper` slot 11, which the game never calls) and answers
     * the paths it serves with pixels from memory; every other URL returns
     * null and takes the engine's own routes.
     */
    class UiImageModule final: public SDK::IFlashLoadMovieHandler {
      public:
        /** A picture as the UI takes it: 32-bit straight RGBA rows without padding. */
        struct Pixels {
            int width  = 0;
            int height = 0;
            std::vector<std::uint8_t> rgba;
        };

        static UiImageModule &Get();

        /**
         * PNG bytes through WIC, the decoder the logo uses too, onto a
         * transparent square of `side`: scaled to fit in premultiplied space
         * (no dark fringe from transparent pixels), then centred. Null for
         * image data WIC cannot read.
         */
        static std::shared_ptr<const Pixels> Decode(const std::string &png, std::uint32_t side);

        /** Answers `img://<path>` with `pixels` until it is withdrawn; case does not matter. Takes the handler slot on first use. */
        void Serve(std::string_view path, std::shared_ptr<const Pixels> pixels);
        void Withdraw(std::string_view path);
        bool Serves(std::string_view path) const;

        /** The engine's question for one `img://` URL, the text after `://`. Any thread. */
        SDK::IFlashLoadMovieImage *LoadMovie(const char *filePath) override;

      private:
        static std::string Key(std::string_view path);

        mutable std::mutex _mutex;
        // By lower-case path.
        std::map<std::string, std::shared_ptr<const Pixels>, std::less<>> _images;
        bool _installed = false;
    };
} // namespace KCDC::Features::UI
