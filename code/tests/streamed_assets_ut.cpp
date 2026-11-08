#include "shared/features/animation/animation_playback.h"
#include "shared/streamed_assets/streamed_asset_policy.h"
#include "shared/features/vfx/vfx_effect_ref.h"

#include <utils/streamed_assets/pak_archive.h>

#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

// KCDC's side of streamed assets: the policy every pak is held to on both ends, and the shapes the
// gameplay APIs accept. The transport, the cache and the pak format are the Framework's, tested there.

namespace {
    void Require(bool value, const char *message) {
        if (!value) {
            throw std::runtime_error(message);
        }
    }

    const KCDC::Shared::StreamedAssets::Policy &Policy() {
        return *KCDC::Shared::StreamedAssets::Policy::Get();
    }

    bool Accepts(const char *lane, const char *raw, std::string *normalized = nullptr) {
        std::string out;
        std::string reason;
        const bool ok = Policy().NormalizeEntry(lane, "demo", raw, out, reason);
        if (ok && normalized != nullptr) {
            *normalized = out;
        }
        return ok;
    }

    void PolicyTests() {
        const char *stream  = KCDC::Shared::StreamedAssets::kLaneStream;
        const char *replace = KCDC::Shared::StreamedAssets::kLaneReplace;

        std::string normalized;
        Require(Accepts(stream, "Objects\\KCDC\\demo\\Chair.cgf", &normalized) && normalized == "objects/kcdc/demo/chair.cgf", "stream paths are lowercased with forward slashes");
        Require(Accepts(stream, "textures/kcdc/demo/wood.dds.3") && Accepts(stream, "textures/kcdc/demo/wood.dds.12a") && Accepts(stream, "textures/kcdc/demo/wood.dds.a"), "split texture chunks are assets");
        Require(!Accepts(stream, "textures/kcdc/demo/wood.dds.0") && !Accepts(stream, "textures/kcdc/demo/wood.dds.100"), "chunk indices stay within 1..99");
        Require(!Accepts(stream, "objects/kcdc/other/chair.cgf"), "a stream entry belongs to its own resource");
        Require(!Accepts(stream, "objects/chair.cgf") && !Accepts(stream, "objects/manmade/demo/chair.cgf"), "a stream entry sits under the kcdc namespace");
        Require(!Accepts(stream, "scripts/kcdc/demo/evil.lua") && !Accepts(stream, "objects/kcdc/demo/evil.lua"), "scripts are never assets");
        Require(!Accepts(stream, "objects/kcdc/demo/../../x.cgf") && !Accepts(stream, "objects/kcdc/demo//x.cgf"), "no relative or empty segments");
        Require(!Accepts(stream, "objects/kcdc/demo/v1.2/x.cgf"), "a dot in a folder would collide material keys");
        Require(!Accepts(stream, "objects/kcdc/demo/x y.cgf") && !Accepts(stream, "C:/objects/kcdc/demo/x.cgf"), "no spaces or drive letters");

        Require(Accepts(stream, "sounds/kcdc/demo/atl/controls.xml"), "ATL controls sit in the resource's atl folder");
        Require(!Accepts(stream, "sounds/kcdc/demo/controls.xml") && !Accepts(stream, "objects/kcdc/demo/atl/controls.xml") && !Accepts(stream, "sounds/kcdc/demo/atl/deeper/controls.xml"), "an XML is only ever ATL controls, and only there");
        Require(Accepts(stream, "sounds/fmod/build/pc/kcdc_demo_music.bank") && Accepts(stream, "sounds/fmod/build/pc/kcdc_demo_master.strings.bank") && Accepts(stream, "sounds/fmod/build/pc/kcdc_demo_music.streams.bank"), "FMOD banks carry the resource prefix");
        Require(!Accepts(stream, "sounds/fmod/build/pc/master.bank") && !Accepts(stream, "sounds/fmod/build/pc/kcdc_other_music.bank") && !Accepts(stream, "sounds/fmod/build/pc/kcdc_demo_.bank"), "a bank cannot shadow the game's or another resource's");
        Require(Accepts(stream, "libs/particles/kcdc_demo_sparks.xml") && !Accepts(stream, "libs/particles/fire.xml") && !Accepts(stream, "libs/particles/kcdc_demo_sparks.lua"), "particle libraries carry the resource prefix and are XML");
        Require(!Accepts(stream, "libs/tables/kcdc_demo_items.xml"), "only the particle folder of libs is open");

        Require(Accepts(replace, "objects/manmade/common_furniture/bucket/bucket_a_diff.dds"), "a stock texture can be replaced");
        Require(!Accepts(replace, "libs/tables/item/item.xml") && !Accepts(replace, "scripts/entities/x.lua") && !Accepts(replace, "levels/kutnohorsko/x.cgf"), "code, tables and levels are not replaceable");
        Require(!Accepts(replace, "animations/humans/male/walk.caf"), "stock clips are not replaced globally");
        Require(!Accepts(replace, "objects/kcdc/demo/x.cgf"), "the kcdc namespace belongs to the stream lane");
        Require(!Accepts("other", "objects/kcdc/demo/x.cgf"), "an unknown lane takes nothing");

        Require(Accepts(replace, "Libs/Tables/Item/weapon__my_mod.xml") && KCDC::Shared::StreamedAssets::Policy::IsTablePatch("libs/tables/item/weapon__my_mod.xml"), "a mod's table patch rides the replace lane");
        Require(Accepts(replace, "libs/tables/weapon__my_mod.xml") == KCDC::Shared::StreamedAssets::Policy::IsTablePatch("libs/tables/weapon__my_mod.xml"), "what the lane takes under libs/tables is what the sync treats as a patch");
        Require(!Accepts(replace, "libs/tables/item/weapon.xml") && !Accepts(replace, "libs/tables/item/weapon__.xml") && !Accepts(replace, "libs/tables/item/weapon__my-mod.xml") && !Accepts(replace, "libs/tables/item/__x.xml") && !Accepts(replace, "libs/tables/item/weapon__x.tbl"), "a table is only ever patched");
        Require(!Accepts(replace, "libs/storm/storm__x.xml") && !Accepts(replace, "scripts/mods/x.lua") && !Accepts(replace, "libs/tables/x.lua"), "Storm, Lua and the rest of libs stay closed");

        const char *localization = KCDC::Shared::StreamedAssets::kLaneLocalization;
        Require(Accepts(localization, "English/text_ui_items__my_mod.xml", &normalized) && normalized == "english/text_ui_items__my_mod.xml", "a string patch sits in its language's folder");
        Require(!Accepts(localization, "english/text_ui_items.xml") && !Accepts(localization, "text_ui_items__x.xml") && !Accepts(localization, "english/sub/text__x.xml") && !Accepts(localization, "english2/text__x.xml"), "localization takes only patches, one language folder deep");
        Require(KCDC::Shared::StreamedAssets::Policy::LanguageOf("english/text_ui_items__my_mod.xml") == "english" && KCDC::Shared::StreamedAssets::Policy::PatchedFileOf("english/text_ui_items__my_mod.xml") == "text_ui_items.xml", "a patch names its language and the file it merges into");

        Require(Policy().PrefersStore("textures/kcdc/demo/a.dds.2") && Policy().PrefersStore("sounds/kcdc/demo/a.ogg") && Policy().PrefersStore("sounds/fmod/build/pc/kcdc_demo_music.bank") && !Policy().PrefersStore("objects/kcdc/demo/a.mtl"), "streamed and compressed kinds are stored");

        Require(KCDC::Shared::StreamedAssets::Policy::IsAudioControls("sounds/kcdc/demo/atl/controls.xml") && !KCDC::Shared::StreamedAssets::Policy::IsAudioControls("sounds/kcdc/demo/controls.xml"), "ATL controls are recognised by place");
        Require(KCDC::Shared::StreamedAssets::Policy::IsFmodBank("sounds/fmod/build/pc/kcdc_demo_music.bank") && KCDC::Shared::StreamedAssets::Policy::IsParticleLibrary("libs/particles/kcdc_demo_sparks.xml"), "banks and libraries are recognised");
    }

    void ShapeTests() {
        Require(KCDC::Shared::StreamedAssets::Policy::IsStreamedModelPath("objects/kcdc/demo/chair.cgf"), "a streamed model path");
        Require(!KCDC::Shared::StreamedAssets::Policy::IsStreamedModelPath("objects/manmade/chair.cgf") && !KCDC::Shared::StreamedAssets::Policy::IsStreamedModelPath("objects/kcdc/demo/Chair.cgf") && !KCDC::Shared::StreamedAssets::Policy::IsStreamedModelPath("objects/kcdc/demo/../x.cgf"), "a model path is lowercase, namespaced and does not climb");
        Require(KCDC::Shared::StreamedAssets::Policy::IsStreamedClipPath("animations/kcdc/demo/wave.caf") && !KCDC::Shared::StreamedAssets::Policy::IsStreamedClipPath("animations/kcdc/demo/wave.cgf"), "a streamed clip path");

        Require(KCDC::Shared::StreamedAssets::Policy::IsStreamedEffectName("kcdc_demo_sparks.Hits.Bright"), "effect names keep their library's case past the library");
        Require(!KCDC::Shared::StreamedAssets::Policy::IsStreamedEffectName("KCDC_demo_sparks.hits") && !KCDC::Shared::StreamedAssets::Policy::IsStreamedEffectName("wh_particels.fires.torch_a") && !KCDC::Shared::StreamedAssets::Policy::IsStreamedEffectName("kcdc_demo_sparks") && !KCDC::Shared::StreamedAssets::Policy::IsStreamedEffectName("kcdc_demo_sparks..x"), "a streamed effect names a lowercase kcdc library and an effect in it");
        Require(KCDC::Shared::StreamedAssets::Policy::ParticleLibraryEntryOf("kcdc_demo_sparks.Hits.Bright") == "libs/particles/kcdc_demo_sparks.xml", "an effect's library is the file FindEffect loads");

        const KCDC::Shared::Game::VfxEffectRef effect = KCDC::Shared::Game::VfxEffectRef::Streamed("kcdc_demo_sparks.hits.bright");
        const std::optional<KCDC::Shared::Game::VfxEffectEntry> entry = effect.Entry();
        Require(entry && entry->library == "kcdc_demo_sparks" && entry->group == "hits" && entry->continuous, "a streamed effect describes itself like a catalog row");
        Require(!KCDC::Shared::Game::VfxEffectRef::Streamed("fires.torch").Entry() && !KCDC::Shared::Game::VfxEffectRef {}.Entry(), "a wrong name or no row describes nothing");

        KCDC::Shared::Game::AnimationPlayback playback;
        playback.active   = true;
        playback.fragment = KCDC::Shared::StreamedAssets::kStreamedClipFragment;
        playback.clip     = "animations/kcdc/demo/wave.caf";
        Require(playback.IsSane(), "a clip plays through the streamed clip fragment");
        playback.fragment = "Wave";
        Require(!playback.IsSane(), "a clip under any other fragment is refused");
        playback.fragment = KCDC::Shared::StreamedAssets::kStreamedClipFragment;
        playback.clip     = "animations/humans/male/wave.caf";
        Require(!playback.IsSane(), "a stock clip path is not a streamed one");
    }

    void ArchiveTests() {
        const std::filesystem::path root = std::filesystem::temp_directory_path() / "kcdc_streamed_assets_ut";
        std::filesystem::remove_all(root);
        std::filesystem::create_directories(root);
        std::ofstream(root / "a.mtl", std::ios::binary) << "<Material/>";
        std::ofstream(root / "controls.xml", std::ios::binary) << "<ATLConfig/>";

        std::string error;
        const std::vector<Framework::Utils::StreamedAssets::PakSourceEntry> entries = {{"objects/kcdc/demo/a.mtl", (root / "a.mtl").string()}, {"sounds/kcdc/demo/atl/controls.xml", (root / "controls.xml").string()}};
        Require(Framework::Utils::StreamedAssets::PakArchive::Write((root / "stream.pak").string(), entries, Policy(), error), "the Framework writes a pak KCDC's policy accepts");

        std::vector<Framework::Utils::StreamedAssets::PakEntry> listed;
        Require(Framework::Utils::StreamedAssets::PakArchive::Validate((root / "stream.pak").string(), Policy(), KCDC::Shared::StreamedAssets::kLaneStream, "demo", listed, error) && listed.size() == 2, "it validates on the client under the same policy");
        Require(!Framework::Utils::StreamedAssets::PakArchive::Validate((root / "stream.pak").string(), Policy(), KCDC::Shared::StreamedAssets::kLaneReplace, "demo", listed, error), "a stream pak is refused on the replace lane");
        Require(!Framework::Utils::StreamedAssets::PakArchive::Validate((root / "stream.pak").string(), Policy(), KCDC::Shared::StreamedAssets::kLaneStream, "other", listed, error), "a pak is checked against the resource it claims");
        std::filesystem::remove_all(root);
    }
} // namespace

int RunStreamedAssetSuite() {
    try {
        PolicyTests();
        ShapeTests();
        ArchiveTests();
    }
    catch (const std::exception &failure) {
        std::cerr << "FAILED: " << failure.what() << '\n';
        return 1;
    }
    std::cout << "streamed asset tests passed\n";
    return 0;
}
