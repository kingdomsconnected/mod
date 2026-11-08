#pragma once

#include "shared/features/player/player_progression.h"
#include "shared/game/progression_catalog.h"
#include "shared/scripting/option_field.h"

#include <scripting/scripting_catalog.h>

#include <v8.h>
#include <v8pp/convert.hpp>
#include <v8pp/metadata.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// The shapes the progression bindings hand back on both sides, and the
// functions that build them. Tracks are named as the game's own tables spell
// them -- `weapon_sword`, `strength` -- and perks by their `perk_name` or GUID.
namespace KCDC::Scripting {
    namespace Detail {
        inline std::string PerkVisibilityName(std::uint8_t visibility) {
            switch (visibility) {
            case Shared::Game::PerkVisibility::kSystemHidden: return "system";
            case Shared::Game::PerkVisibility::kGameplayHidden: return "hidden";
            case Shared::Game::PerkVisibility::kVisible: return "visible";
            default: return "obsolete";
            }
        }

        /** The track name a perk's points come from, `main` for the main-level bank, or null for a tree no player shows. */
        inline v8::Local<v8::Value> PerkTreeName(v8::Isolate *isolate, const Shared::Game::PerkEntry &perk) {
            const int tree = Shared::Game::PerkTreeOf(perk);
            if (tree == Shared::RPC::kMainPerkTree) {
                return v8pp::to_v8(isolate, std::string("main"));
            }
            if (tree >= 0 && static_cast<std::size_t>(tree) < Shared::Game::g_ProgressionTracks.size()) {
                return v8pp::to_v8(isolate, std::string(Shared::Game::g_ProgressionTracks[tree].name));
            }
            return v8::Null(isolate);
        }
    } // namespace Detail

    inline void RegisterProgressionDataTypes(v8pp::metadata::registry &catalog) {
        auto &track = catalog.data_type("TrackInfo", "One skill or stat a player levels, as the game's own tables describe it.");
        track.add_property("name", "string", "The name every progression verb takes, as the game's tables spell it: `weapon_sword`, `thievery`, `strength`.");
        track.add_property("kind", "\"skill\" | \"stat\"", "Whether it is a skill or one of the stats the skills sit on.");
        track.add_property("cap", "number", "The highest level the game lets it reach.");
        track.add_property("induced", "boolean", "True for fencing alone: it is never earned, the game derives it from the five weapon skills, so XP handed to it is refused.");

        auto &perk = catalog.data_type("PerkInfo", "One perk, as the game's own perk tables describe it.");
        perk.add_property("name", "string", "The perk's `perk_name`, unique across the tables, and the spelling every perk verb takes alongside its GUID.");
        perk.add_property("id", "string", "The perk's GUID.");
        perk.add_property("uiName", "string", "The localisation key the perk screen shows, or empty for a perk it never shows.");
        perk.add_property("track", "string | null", "The track whose points pay for it: a track name, `main` for the main-level bank, or null for a perk no player's tree carries.");
        perk.add_property("level", "number", "The level that track has to reach before the perk can be learnt. 0 for none.");
        perk.add_property("parent", "string | null", "The perk that has to be owned first, or null.");
        perk.add_property("visibility", "\"system\" | \"hidden\" | \"visible\" | \"obsolete\"",
            "`visible` perks are the ones on the perk screen and the only ones a player can learn; `hidden` ones appear once something grants them; `system` ones implement game features and are never shown.");
        perk.add_property("kinds", "string[]",
            "What the perk does when owned, from the game's side tables: `buff` (a real status effect), `script`, `technique` (a combat move), `ability`, `param` (rewrites a game rule), `companion` (the dog), `recipe`, `codex`. Empty for a perk that only gates others.");
        perk.add_property("autolearnable", "boolean", "Whether the game grants it on its own once its requirements are met, rather than on the perk screen.");

        auto &progress = catalog.data_type("TrackProgress", "Where a player stands on one track.");
        progress.add_property("level", "number", "The level reached.");
        progress.add_property("xp", "number", "XP earned towards the next level, in the game's own units.");
        progress.add_property("xpToNext", "number", "XP the next level costs in total, from the start of this one. `xp / xpToNext` is the bar the character sheet draws.");
        progress.add_property("perkPoints", "number", "Unspent perk points in this track's tree.");

        auto &levels = catalog.data_type("TrackLevels", "A player's level on every track, keyed by track name.");
        for (const Shared::Game::ProgressionTrack &entry : Shared::Game::g_ProgressionTracks) {
            levels.add_property(std::string(entry.name), "number", "The level reached.");
        }

        auto &snapshot =
            catalog.data_type("ProgressionSnapshot", "Everything a server needs to put a player's progression back: plain JSON a resource can store however it likes. A player starts every session with a fresh character, so this is how progression outlives a disconnect.");
        snapshot.add_property("tracks", "Record<string, { level: number; xp: number }>", "Level and XP into the next level, per track name.");
        snapshot.add_property("perks", "string[]", "Every perk owned, by name.");
        snapshot.add_property("perkPoints", "Record<string, number>", "Unspent points per tree: track names, and `main`.");
    }

    inline v8::Local<v8::Object> TrackInfoToV8(v8::Isolate *isolate, std::size_t index) {
        v8::EscapableHandleScope handleScope(isolate);
        v8::Local<v8::Context> context          = isolate->GetCurrentContext();
        const Shared::Game::ProgressionTrack &t = Shared::Game::g_ProgressionTracks[index];
        v8::Local<v8::Object> out               = v8::Object::New(isolate);
        out->Set(context, v8pp::to_v8(isolate, "name"), v8pp::to_v8(isolate, std::string(t.name))).Check();
        out->Set(context, v8pp::to_v8(isolate, "kind"), v8pp::to_v8(isolate, std::string(t.kind == Shared::Game::ProgressionTrackKind::Skill ? "skill" : "stat"))).Check();
        out->Set(context, v8pp::to_v8(isolate, "cap"), v8pp::to_v8(isolate, static_cast<std::uint32_t>(t.cap))).Check();
        out->Set(context, v8pp::to_v8(isolate, "induced"), v8pp::to_v8(isolate, Shared::Game::IsInducedTrack(index))).Check();
        return handleScope.Escape(out);
    }

    inline v8::Local<v8::Object> PerkInfoToV8(v8::Isolate *isolate, const Shared::Game::PerkEntry &perk) {
        v8::EscapableHandleScope handleScope(isolate);
        v8::Local<v8::Context> context = isolate->GetCurrentContext();
        v8::Local<v8::Object> out      = v8::Object::New(isolate);

        std::vector<std::string> kinds;
        for (std::size_t bit = 0; bit < Shared::Game::PerkKind::kCount; ++bit) {
            if ((perk.kinds & (1U << bit)) != 0) {
                kinds.emplace_back(Shared::Game::PerkKind::kNames[bit]);
            }
        }
        const Shared::Game::PerkEntry *parent = perk.parent >= 0 ? Shared::Game::PerkAt(static_cast<std::size_t>(perk.parent)) : nullptr;

        out->Set(context, v8pp::to_v8(isolate, "name"), v8pp::to_v8(isolate, std::string(perk.name))).Check();
        out->Set(context, v8pp::to_v8(isolate, "id"), v8pp::to_v8(isolate, std::string(perk.guid))).Check();
        out->Set(context, v8pp::to_v8(isolate, "uiName"), v8pp::to_v8(isolate, std::string(perk.uiName))).Check();
        out->Set(context, v8pp::to_v8(isolate, "track"), Detail::PerkTreeName(isolate, perk)).Check();
        out->Set(context, v8pp::to_v8(isolate, "level"), v8pp::to_v8(isolate, static_cast<std::uint32_t>(perk.level))).Check();
        out->Set(context, v8pp::to_v8(isolate, "parent"), parent != nullptr ? v8pp::to_v8(isolate, std::string(parent->name)) : v8::Local<v8::Value>(v8::Null(isolate))).Check();
        out->Set(context, v8pp::to_v8(isolate, "visibility"), v8pp::to_v8(isolate, Detail::PerkVisibilityName(perk.visibility))).Check();
        out->Set(context, v8pp::to_v8(isolate, "kinds"), v8pp::to_v8(isolate, kinds)).Check();
        out->Set(context, v8pp::to_v8(isolate, "autolearnable"), v8pp::to_v8(isolate, perk.autolearnable)).Check();
        return handleScope.Escape(out);
    }

    /** One track of a digest as a `TrackProgress`. */
    inline v8::Local<v8::Object> TrackProgressToV8(v8::Isolate *isolate, const Shared::RPC::ProgressionDigest &digest, std::size_t index) {
        v8::EscapableHandleScope handleScope(isolate);
        v8::Local<v8::Context> context              = isolate->GetCurrentContext();
        const Shared::Game::ProgressionTrack &track = Shared::Game::g_ProgressionTracks[index];
        const Shared::RPC::TrackState &state        = digest.tracks[index];
        v8::Local<v8::Object> out                   = v8::Object::New(isolate);
        out->Set(context, v8pp::to_v8(isolate, "level"), v8pp::to_v8(isolate, state.level)).Check();
        out->Set(context, v8pp::to_v8(isolate, "xp"), v8pp::to_v8(isolate, static_cast<double>(state.xpQ7) / 128.0)).Check();
        out->Set(context, v8pp::to_v8(isolate, "xpToNext"), v8pp::to_v8(isolate, static_cast<double>(Shared::Game::XpQ7ToNextLevel(track, state.level)) / 128.0)).Check();
        out->Set(context, v8pp::to_v8(isolate, "perkPoints"), v8pp::to_v8(isolate, static_cast<std::uint32_t>(digest.perkPoints[index]))).Check();
        return handleScope.Escape(out);
    }

    /** A `TrackLevels` from any per-track level array. */
    template <typename Levels>
    inline v8::Local<v8::Object> TrackLevelsToV8(v8::Isolate *isolate, const Levels &levels) {
        v8::EscapableHandleScope handleScope(isolate);
        v8::Local<v8::Context> context = isolate->GetCurrentContext();
        v8::Local<v8::Object> out      = v8::Object::New(isolate);
        for (std::size_t i = 0; i < Shared::Game::g_ProgressionTracks.size() && i < Shared::RPC::kProgressionTracks; ++i) {
            out->Set(context, v8pp::to_v8(isolate, std::string(Shared::Game::g_ProgressionTracks[i].name)), v8pp::to_v8(isolate, static_cast<std::uint32_t>(levels[i]))).Check();
        }
        return handleScope.Escape(out);
    }

    /** The names of every perk in a set, in catalog order. */
    inline std::vector<std::string> PerkNames(const Shared::RPC::PerkSet &perks) {
        std::vector<std::string> names;
        for (std::size_t index = 0; index < Shared::Game::g_Perks.size(); ++index) {
            if (perks.Has(index)) {
                names.emplace_back(Shared::Game::g_Perks[index].name);
            }
        }
        return names;
    }

    /** The level array of a digest, for `TrackLevelsToV8`. */
    inline std::array<std::uint32_t, Shared::RPC::kProgressionTracks> DigestLevels(const Shared::RPC::ProgressionDigest &digest) {
        std::array<std::uint32_t, Shared::RPC::kProgressionTracks> levels {};
        for (std::size_t i = 0; i < levels.size(); ++i) {
            levels[i] = digest.tracks[i].level;
        }
        return levels;
    }

    /** `Progression.perks(filter?)`: every catalog perk matching an optional `{ track?, visibleOnly? }`. */
    inline v8::Local<v8::Array> FilteredPerksToV8(v8::Isolate *isolate, const std::string &track, bool visibleOnly) {
        v8::EscapableHandleScope handleScope(isolate);
        v8::Local<v8::Context> context = isolate->GetCurrentContext();
        v8::Local<v8::Array> out       = v8::Array::New(isolate);
        std::uint32_t written          = 0;
        const int trackIndex           = track == "main" ? static_cast<int>(Shared::RPC::kMainPerkTree) : Shared::Game::FindProgressionTrack(track);
        if (!track.empty() && trackIndex < 0) {
            return handleScope.Escape(out);
        }
        for (const Shared::Game::PerkEntry &perk : Shared::Game::g_Perks) {
            if (visibleOnly && perk.visibility != Shared::Game::PerkVisibility::kVisible) {
                continue;
            }
            if (!track.empty() && Shared::Game::PerkTreeOf(perk) != trackIndex) {
                continue;
            }
            out->Set(context, written++, PerkInfoToV8(isolate, perk)).Check();
        }
        return handleScope.Escape(out);
    }

    /** `Progression.tracks`. */
    inline void JS_ProgressionTracks(const v8::FunctionCallbackInfo<v8::Value> &info) {
        v8::Isolate *isolate = info.GetIsolate();
        v8::HandleScope handleScope(isolate);
        v8::Local<v8::Context> context = isolate->GetCurrentContext();
        v8::Local<v8::Array> out       = v8::Array::New(isolate);
        for (std::size_t i = 0; i < Shared::Game::g_ProgressionTracks.size(); ++i) {
            out->Set(context, static_cast<std::uint32_t>(i), TrackInfoToV8(isolate, i)).Check();
        }
        info.GetReturnValue().Set(out);
    }

    /** `Progression.findPerk`. */
    inline void JS_ProgressionFindPerk(const v8::FunctionCallbackInfo<v8::Value> &info) {
        v8::Isolate *isolate = info.GetIsolate();
        v8::HandleScope handleScope(isolate);
        if (info.Length() < 1 || !info[0]->IsString()) {
            isolate->ThrowError(v8pp::to_v8(isolate, "Progression.findPerk: expected (query)"));
            return;
        }
        const Shared::Game::PerkEntry *perk = Shared::Game::ResolvePerk(v8pp::from_v8<std::string>(isolate, info[0]));
        if (perk == nullptr) {
            info.GetReturnValue().SetNull();
            return;
        }
        info.GetReturnValue().Set(PerkInfoToV8(isolate, *perk));
    }

    /** `Progression.perks`. */
    inline void JS_ProgressionPerks(const v8::FunctionCallbackInfo<v8::Value> &info) {
        v8::Isolate *isolate = info.GetIsolate();
        v8::HandleScope handleScope(isolate);
        info.GetReturnValue().Set(FilteredPerksToV8(isolate, StringOption(isolate, info, 0, "track"), BoolOption(isolate, info, 0, "visibleOnly", false)));
    }

    /** `Progression.xpForLevel`. */
    inline void JS_ProgressionXpForLevel(const v8::FunctionCallbackInfo<v8::Value> &info) {
        v8::Isolate *isolate = info.GetIsolate();
        v8::HandleScope handleScope(isolate);
        if (info.Length() < 2 || !info[0]->IsString() || !info[1]->IsNumber()) {
            isolate->ThrowError(v8pp::to_v8(isolate, "Progression.xpForLevel: expected (track, level)"));
            return;
        }
        const int track = Shared::Game::FindProgressionTrack(v8pp::from_v8<std::string>(isolate, info[0]));
        if (track < 0) {
            isolate->ThrowError(v8pp::to_v8(isolate, "Progression.xpForLevel: unknown track; see Progression.tracks"));
            return;
        }
        const double level          = info[1].As<v8::Number>()->Value();
        const std::uint32_t clamped = level <= 0.0 ? 0 : level >= 255.0 ? 255 : static_cast<std::uint32_t>(level);
        info.GetReturnValue().Set(static_cast<double>(Shared::Game::TotalXpQ7ForLevel(Shared::Game::g_ProgressionTracks[track], clamped)) / 128.0);
    }

    /** Registers the catalog half of the `Progression` global, which both sides publish identically. */
    template <typename Module>
    inline void RegisterProgressionCatalogFunctions(Module &module) {
        module.function("tracks", &JS_ProgressionTracks, v8pp::metadata::docs("TrackInfo[]", {}, "Every skill and stat a player levels: the fifteen skills the character sheet shows, then strength, agility, vitality, speech and prestige.", "The tracks, in that order."));

        module.function("findPerk", &JS_ProgressionFindPerk,
            v8pp::metadata::docs("PerkInfo | null", {v8pp::metadata::param("query", "string", false, "A perk's name, as the game's perk tables spell it, or its GUID.")}, "Looks a perk up.", "What the tables say about it, or null for a name they do not carry."));

        module.function("perks", &JS_ProgressionPerks,
            v8pp::metadata::docs("PerkInfo[]", {v8pp::metadata::param("filter", "{ track?: string; visibleOnly?: boolean }", true, "`track` keeps the perks one tree pays for -- a track name, or `main`; `visibleOnly` keeps the perks the perk screen shows.")},
                "Lists the game's perks: 945 of them, most of them hidden system perks. `Progression.perks({ visibleOnly: true })` is the perk screen.", "The perks, sorted by name."));

        module.function("xpForLevel", &JS_ProgressionXpForLevel,
            v8pp::metadata::docs("number", {v8pp::metadata::param("track", "string", false, "A track name."), v8pp::metadata::param("level", "number", false, "The level to reach.")},
                "The total XP a track costs from level 0 to `level`, on the game's own curve: each level costs `base + diff * level`, with base and diff from the game's RPG tables.", "The XP. Throws for a track name the tables do not carry."));
    }
} // namespace KCDC::Scripting
