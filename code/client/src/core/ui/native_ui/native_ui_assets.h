#pragma once

#include <string>
#include <vector>

namespace KCDC::Core {
    /**
     * Where a native movie comes from, and how it is made reachable.
     *
     * The engine loads a movie only through CryPak and the mod may not write
     * into the game's folders, so what a resource ships is repackaged into an
     * archive of the mod's own, carrying the movie and the `UIElements` XML
     * that declares it.
     */
    class NativeUIAssets {
      public:
        struct Movie {
            std::string archivePath;    // Absolute; mounted before opening.
            std::string descriptorPath; // CryPak path inside that archive.
            std::string elementName;
        };

        /** One game movie a bundled host imports, as inventory.json records it. */
        struct Library {
            std::string name; // What `createScreen({library})` takes; empty for the default.
            std::string host;
            std::string movie;
            std::vector<std::string> exports; // Linkage names `attach` accepts.
        };

        /** Every library in the bundle, in the generator's order. Parsed once. */
        static const std::vector<Library> &Libraries();

        static const std::string &ModuleDirectory();
        /** The bundled composition hosts, built by tools/build_native_ui_host.py. */
        static std::string BundledArchive();
        /** `library` is the movie stem, empty for the default host. */
        static bool BundledHost(const std::string &library, Movie &out, std::string &error);

        /**
         * Repackages one movie a resource ships into a mountable archive.
         *
         * Paths are resource-relative and read out of the resource's own
         * mount, so nothing but what the server sent can be packaged. The
         * movie is stored under both extensions, because the loader swaps
         * `.gfx` for `.swf` only when `wh_gfx_useSWF` is set.
         */
        static bool PackageResourceMovie(const std::string &resourceName, const std::string &moviePath, const std::vector<std::string> &assetPaths, int layer, Movie &out, std::string &error);

        /**
         * Repackages one DDS a resource ships where the book movie looks for
         * an inline `<img src>`: `Libs/UI/Textures/Books/Unique/` + src +
         * `_ui.dds`, the prefix and suffixes GeneralBook.gfx appends itself.
         *
         * `source` receives what the page text writes as the `src`
         * attribute. It carries the resource and a content hash, so two
         * resources never collide and a changed file is a new texture rather
         * than one CryPak and the renderer have already cached. The archive
         * is written once per content and must be mounted before the page is
         * shown.
         */
        static bool PackageBookImage(const std::string &resourceName, const std::string &imagePath, std::string &source, std::string &archivePath, std::string &error);

        /**
         * The CryPak path of a picture `PackageBookImage` packaged, from the
         * `source` it handed out; empty for anything that is not one. What a
         * material slot takes, where page markup takes `source` itself.
         */
        static std::string BookImagePath(const std::string &source);

      private:
        static bool IsSafeRelativePath(const std::string &path);
    };
} // namespace KCDC::Core
