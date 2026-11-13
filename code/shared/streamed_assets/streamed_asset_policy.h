#pragma once

#include <utils/streamed_assets/policy.h>

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace KCDC::Shared::StreamedAssets {
    // The two lanes a resource streams through.
    //
    // `stream` carries new assets. Every entry sits at `<category>/kcdc/<resource>/...`, which the
    // game ships nothing under, so nothing in such a pak can shadow a game file. Three engine folders
    // with fixed roots are the exceptions, each still namespaced by a `kcdc_<resource>_` file prefix:
    // FMOD banks (`sounds/fmod/build/pc/`), whose loader takes names relative to that folder, and
    // particle libraries (`libs/particles/`), which FindEffect loads by library name.
    //
    // `replace` carries overrides of stock paths and is mounted with FLAGS_OVERRIDE_PAK so it also
    // wins over the level's own paks. It is where a KCD2 mod's data paks go, extracted: its assets,
    // and its database table patches (`libs/tables/.../<table>__<id>.xml`).
    //
    // `localization` carries a KCD2 mod's string patches, `<language>/<file>__<id>.xml`, mounted
    // under the game's localization folder and merged into the string table like the game merges an
    // installed mod's.
    //
    // `level` carries a KCD2 mod's level edits, `levels/<level>/...`: terrain, navigation, physics,
    // far-view meshes and the level's object list, each file replacing the game's whole, as an
    // installed mod's level paks do. The level reads them as it loads, so a pak applies from the
    // next level load only.
    inline constexpr char kLaneStream[]       = "stream";
    inline constexpr char kLaneReplace[]      = "replace";
    inline constexpr char kLaneLocalization[] = "localization";
    inline constexpr char kLaneLevel[]        = "level";
    // `script` carries a KCD2 mod's Lua, `scripts/.../<name>.lua`. It is never mounted, so the game
    // cannot find these files on its own: the client reads them out of the pak and runs them in a
    // restricted environment (ModScriptHost).
    inline constexpr char kLaneScript[] = "script";

    // The folder segment that sandboxes the stream lane.
    inline constexpr std::string_view kNamespaceSegment = "kcdc";

    // The Mannequin fragment every streamed clip plays through. The client declares it in the human
    // animation files as they load, one option per clip the level's database was built with.
    inline constexpr char kStreamedClipFragment[] = "KCDC_StreamedClip";

    /**
     * KCDC's statement of what a streamed pak may carry, applied by the server when it builds one and
     * by the client before it mounts one.
     */
    class Policy final: public Framework::Utils::StreamedAssets::Policy {
      public:
        static const std::shared_ptr<const Policy> &Get();

        const std::vector<std::string> &GetLanes() const override;

        /**
         * Normalises an entry path (lowercase, `/`) and checks it against the lane. Every directory
         * segment must be free of dots: CMatMan keys a material by its name cut at the first dot.
         */
        bool NormalizeEntry(std::string_view lane, std::string_view resource, std::string_view raw, std::string &out, std::string &reason) const override;

        /** Already-compressed and streamed kinds are stored; the rest is deflated. */
        bool PrefersStore(std::string_view normalized) const override;

        /** Leaves out a level's entity XML that would run a script, and precompiled Lua; every other entry ships. */
        bool ShouldShip(std::string_view lane, std::string_view resource, std::string_view normalized, const std::string &sourceFile, std::string &reason) const override;

        /** A level file that places entities: `levels/<level>/objects_*.xml`, `mission_*.xml` or `layers/*.xml`. */
        static bool IsLevelEntityXml(std::string_view normalized);

        /**
         * Whether a level's entity XML would hand the game a script to run. A trigger's or a flow
         * node's `ScriptCommand` is compiled and run when it fires, in the game's own Lua state, so
         * the check fails closed: anything but an empty value counts.
         */
        static bool LevelXmlRunsScript(std::string_view xml);

        /** A mod's entry script, `scripts/mods/<name>.lua`: the file the game itself would start a mod from. */
        static bool IsModEntryScript(std::string_view normalized);

        static bool IsAnimationClip(std::string_view normalized);
        static bool IsAnimationDatabase(std::string_view normalized);
        static bool IsTextureBase(std::string_view normalized);
        static bool IsMaterial(std::string_view normalized);
        static bool IsStaticMesh(std::string_view normalized);

        /** An ATL controls file: `sounds/kcdc/<resource>/atl/<name>.xml`. */
        static bool IsAudioControls(std::string_view normalized);

        /** An FMOD bank: `sounds/fmod/build/pc/kcdc_<resource>_<name>[.strings|.assets|.streams].bank`. */
        static bool IsFmodBank(std::string_view normalized);

        /** A particle library: `libs/particles/kcdc_<resource>_<name>.xml`. */
        static bool IsParticleLibrary(std::string_view normalized);

        /** A database table patch: `libs/tables/.../<table>__<id>.xml`, merged row by row into `<table>`. */
        static bool IsTablePatch(std::string_view normalized);

        /** A localization lane entry's language: `<language>/<file>__<id>.xml` answers `<language>`. */
        static std::string_view LanguageOf(std::string_view normalized);

        /** The stock string file a localization patch merges into: `<file>.xml`. */
        static std::string PatchedFileOf(std::string_view normalized);

        /**
         * Whether a model path has the shape of a streamed static mesh: `objects/kcdc/<resource>/...cgf`.
         * Only the shape -- whether a server actually streams it is the caller's check.
         */
        static bool IsStreamedModelPath(std::string_view path);

        /** The same for a clip: `animations/kcdc/<resource>/...caf`. */
        static bool IsStreamedClipPath(std::string_view path);

        /**
         * Whether a particle effect name has the shape of one out of a streamed library:
         * `kcdc_<resource>_<library>.<rest>`, the library part lowercase. Effect names keep the
         * case their library spells them in; only the library has to match a file.
         */
        static bool IsStreamedEffectName(std::string_view name);

        /** The stream entry a streamed effect's library is: `libs/particles/<library>.xml`. */
        static std::string ParticleLibraryEntryOf(std::string_view effectName);

        /** The resource a stream-lane path belongs to, or empty. */
        static std::string ResourceOf(std::string_view normalized);

      private:
        /** `<category>/kcdc/<resource>/...<extension>`, lowercase, with nothing that climbs out of it. */
        static bool IsStreamedPath(std::string_view path, std::string_view category, std::string_view extension);
    };
} // namespace KCDC::Shared::StreamedAssets
