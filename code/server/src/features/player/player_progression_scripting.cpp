#include "features/player/player_progression_scripting.h"

#include "core/scripting/script_context.h"
#include "core/scripting/wrap.h"
#include "core/server.h"
#include "features/player/player_progression_module.h"

#include "shared/features/player/player_entity.h"
#include "shared/features/player/player_progression_bindings.h"
#include "shared/features/player/player_progression_restore.h"
#include "shared/game/progression_catalog.h"
#include "shared/scripting/catalog.h"
#include "shared/scripting/json_value.h"
#include "shared/scripting/option_field.h"

#include <v8pp/convert.hpp>
#include <v8pp/module.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace KCDC::Scripting {
    namespace {
        KCDC::Features::Player::Progression *Authority() {
            auto *server = Server::Get();
            return server != nullptr ? &server->GetProgression() : nullptr;
        }

        const Shared::RPC::ProgressionDigest *AcceptedOf(const Player &self) {
            auto *authority = Authority();
            return authority != nullptr ? authority->Accepted(self.GetId()) : nullptr;
        }

        Player *Self(const v8::FunctionCallbackInfo<v8::Value> &info) {
            return v8pp::class_<Player>::unwrap_object(info.GetIsolate(), info.This());
        }

        /**
         * A track argument. `allowAll` accepts `*` for every track, `allowMain`
         * accepts `main` for the main-level perk bank. Throws and answers -1
         * for anything else it cannot name.
         */
        int TrackArg(v8::Isolate *isolate, v8::Local<v8::Value> value, const char *verb, bool allowAll, bool allowMain) {
            if (value.IsEmpty() || !value->IsString()) {
                isolate->ThrowError(v8pp::to_v8(isolate, std::string(verb) + ": expected a track name"));
                return -1;
            }
            const std::string name = v8pp::from_v8<std::string>(isolate, value);
            if (allowAll && name == "*") {
                return Shared::RPC::kNoProgressionTrack;
            }
            if (allowMain && name == "main") {
                return Shared::RPC::kMainPerkTree;
            }
            const int track = Shared::Game::FindProgressionTrack(name);
            if (track < 0) {
                isolate->ThrowError(v8pp::to_v8(isolate, std::string(verb) + ": unknown track '" + name + "'; see Progression.tracks()"));
            }
            return track;
        }

        /** A perk argument, by name or GUID. Throws and answers null for one the tables do not carry. */
        const Shared::Game::PerkEntry *PerkArg(v8::Isolate *isolate, v8::Local<v8::Value> value, const char *verb) {
            if (value.IsEmpty() || !value->IsString()) {
                isolate->ThrowError(v8pp::to_v8(isolate, std::string(verb) + ": expected a perk name or GUID"));
                return nullptr;
            }
            const std::string query             = v8pp::from_v8<std::string>(isolate, value);
            const Shared::Game::PerkEntry *perk = Shared::Game::ResolvePerk(query);
            if (perk == nullptr) {
                isolate->ThrowError(v8pp::to_v8(isolate, std::string(verb) + ": unknown perk '" + query + "'; see Progression.findPerk"));
            }
            return perk;
        }

        std::vector<std::uint16_t> PerkListArg(v8::Isolate *isolate, v8::Local<v8::Value> value, const char *verb, bool &ok) {
            std::vector<std::uint16_t> perks;
            ok = false;
            if (value.IsEmpty() || !value->IsArray()) {
                isolate->ThrowError(v8pp::to_v8(isolate, std::string(verb) + ": expected an array of perk names"));
                return perks;
            }
            for (const std::string &name : v8pp::from_v8<std::vector<std::string>>(isolate, value)) {
                const Shared::Game::PerkEntry *perk = Shared::Game::ResolvePerk(name);
                if (perk == nullptr) {
                    isolate->ThrowError(v8pp::to_v8(isolate, std::string(verb) + ": unknown perk '" + name + "'"));
                    return perks;
                }
                perks.push_back(Shared::Game::PerkIndexOf(*perk));
            }
            ok = true;
            return perks;
        }

        /** A finite number at argument `index`, or `fallback`. */
        double NumberArg(const v8::FunctionCallbackInfo<v8::Value> &info, int index, double fallback) {
            const double number = info.Length() > index && info[index]->IsNumber() ? info[index].As<v8::Number>()->Value() : fallback;
            return std::isfinite(number) ? number : fallback;
        }

        // --- Progression ------------------------------------------------------

        // JS: Progression.setNativeXp(enabled) -> void
        void JS_SetNativeXp(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            if (info.Length() < 1 || !info[0]->IsBoolean()) {
                isolate->ThrowError(v8pp::to_v8(isolate, "Progression.setNativeXp: expected (enabled)"));
                return;
            }
            if (auto *authority = Authority()) {
                authority->SetNativeXp(info[0]->BooleanValue(isolate));
            }
        }

        // JS: Progression.setXpRate(track, rate) -> void
        void JS_SetXpRate(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            const int track      = TrackArg(isolate, info.Length() > 0 ? info[0] : v8::Local<v8::Value>(), "Progression.setXpRate", true, false);
            if (track < 0) {
                return;
            }
            const double rate = NumberArg(info, 1, -1.0);
            if (rate < 0.0) {
                isolate->ThrowError(v8pp::to_v8(isolate, "Progression.setXpRate: expected (track, rate) with a rate of 0 or more"));
                return;
            }
            if (auto *authority = Authority()) {
                authority->SetXpRate(static_cast<std::uint8_t>(track), rate);
            }
        }

        // JS: Progression.setXpBudget(track, xpPerMinute) -> void
        void JS_SetXpBudget(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            const int track      = TrackArg(isolate, info.Length() > 0 ? info[0] : v8::Local<v8::Value>(), "Progression.setXpBudget", true, false);
            if (track < 0) {
                return;
            }
            const double xpPerMinute = NumberArg(info, 1, -1.0);
            if (xpPerMinute < 0.0) {
                isolate->ThrowError(v8pp::to_v8(isolate, "Progression.setXpBudget: expected (track, xpPerMinute) with 0 or more"));
                return;
            }
            if (auto *authority = Authority()) {
                authority->SetXpBudget(static_cast<std::uint8_t>(track), xpPerMinute);
            }
        }

        // JS: Progression.setLevelCap(track, level) -> void
        void JS_SetLevelCap(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            const int track      = TrackArg(isolate, info.Length() > 0 ? info[0] : v8::Local<v8::Value>(), "Progression.setLevelCap", true, false);
            if (track < 0) {
                return;
            }
            const double level = NumberArg(info, 1, -1.0);
            if (level < 0.0) {
                isolate->ThrowError(v8pp::to_v8(isolate, "Progression.setLevelCap: expected (track, level)"));
                return;
            }
            if (auto *authority = Authority()) {
                authority->SetLevelCap(static_cast<std::uint8_t>(track), static_cast<std::uint32_t>(std::min(level, 255.0)));
            }
        }

        // JS: Progression.blockPerks(perks) / unblockPerks(perks) -> void
        void SetPerksBlocked(const v8::FunctionCallbackInfo<v8::Value> &info, bool blocked) {
            v8::Isolate *isolate                   = info.GetIsolate();
            bool ok                                = false;
            const std::vector<std::uint16_t> perks = PerkListArg(isolate, info.Length() > 0 ? info[0] : v8::Local<v8::Value>(), blocked ? "Progression.blockPerks" : "Progression.unblockPerks", ok);
            auto *authority                        = Authority();
            if (!ok || authority == nullptr) {
                return;
            }
            for (const std::uint16_t perk : perks) {
                authority->SetPerkBlocked(perk, blocked);
            }
            authority->BroadcastPolicy();
        }

        void JS_BlockPerks(const v8::FunctionCallbackInfo<v8::Value> &info) {
            SetPerksBlocked(info, true);
        }

        void JS_UnblockPerks(const v8::FunctionCallbackInfo<v8::Value> &info) {
            SetPerksBlocked(info, false);
        }

        // --- Player -----------------------------------------------------------

        // JS: player.getTrack(track) -> TrackProgress | null
        void JS_GetTrack(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);
            Player *self    = Self(info);
            const int track = TrackArg(isolate, info.Length() > 0 ? info[0] : v8::Local<v8::Value>(), "Player.getTrack", false, false);
            if (self == nullptr || track < 0) {
                return;
            }
            const Shared::RPC::ProgressionDigest *digest = AcceptedOf(*self);
            if (digest == nullptr) {
                info.GetReturnValue().SetNull();
                return;
            }
            info.GetReturnValue().Set(TrackProgressToV8(isolate, *digest, static_cast<std::size_t>(track)));
        }

        // JS: player.hasPerk(perk) -> boolean
        void JS_HasPerk(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);
            Player *self = Self(info);
            if (self == nullptr || info.Length() < 1 || !info[0]->IsString()) {
                isolate->ThrowError(v8pp::to_v8(isolate, "Player.hasPerk: expected (perk)"));
                return;
            }
            const Shared::Game::PerkEntry *perk          = Shared::Game::ResolvePerk(v8pp::from_v8<std::string>(isolate, info[0]));
            const Shared::RPC::ProgressionDigest *digest = AcceptedOf(*self);
            info.GetReturnValue().Set(perk != nullptr && digest != nullptr && digest->perks.Has(Shared::Game::PerkIndexOf(*perk)));
        }

        // JS: player.addXp(track, xp) -> boolean
        void JS_AddXp(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            Player *self         = Self(info);
            const int track      = TrackArg(isolate, info.Length() > 0 ? info[0] : v8::Local<v8::Value>(), "Player.addXp", false, false);
            if (self == nullptr || track < 0) {
                return;
            }
            const double xp = NumberArg(info, 1, -1.0);
            if (!std::isfinite(xp) || xp <= 0.0) {
                isolate->ThrowError(v8pp::to_v8(isolate, "Player.addXp: expected (track, xp) with a positive xp"));
                return;
            }
            auto *authority = Authority();
            info.GetReturnValue().Set(authority != nullptr && authority->AddXp(self->ResolveHuman(), static_cast<std::uint8_t>(track), xp, true));
        }

        // JS: player.setLevel(track, level) -> boolean
        void JS_SetLevel(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            Player *self         = Self(info);
            const int track      = TrackArg(isolate, info.Length() > 0 ? info[0] : v8::Local<v8::Value>(), "Player.setLevel", false, false);
            if (self == nullptr || track < 0) {
                return;
            }
            const double level = NumberArg(info, 1, -1.0);
            if (level < 0.0) {
                isolate->ThrowError(v8pp::to_v8(isolate, "Player.setLevel: expected (track, level)"));
                return;
            }
            if (!std::isfinite(level) || level != std::floor(level)) {
                isolate->ThrowError(v8pp::to_v8(isolate, "Player.setLevel: level must be a finite nonnegative integer"));
                return;
            }
            auto *authority = Authority();
            info.GetReturnValue().Set(authority != nullptr && authority->SetLevel(self->ResolveHuman(), static_cast<std::uint8_t>(track), static_cast<std::uint32_t>(std::min(level, 255.0))));
        }

        void ChangeAbsoluteXp(const v8::FunctionCallbackInfo<v8::Value> &info, bool remove) {
            v8::Isolate *isolate = info.GetIsolate();
            Player *self         = Self(info);
            const int track      = TrackArg(isolate, info.Length() > 0 ? info[0] : v8::Local<v8::Value>(), remove ? "Player.removeXp" : "Player.setXp", false, false);
            if (self == nullptr || track < 0) {
                return;
            }
            const double xp = NumberArg(info, 1, -1);
            if (!std::isfinite(xp) || xp < 0 || (remove && xp < 1.0 / 128)) {
                isolate->ThrowError(v8pp::to_v8(isolate, "XP must be finite and nonnegative; removeXp requires at least 1/128 XP"));
                return;
            }
            auto *authority = Authority();
            info.GetReturnValue().Set(authority != nullptr && (remove ? authority->RemoveXp(self->ResolveHuman(), static_cast<std::uint8_t>(track), xp) : authority->SetXp(self->ResolveHuman(), static_cast<std::uint8_t>(track), xp)));
        }
        void JS_SetXp(const v8::FunctionCallbackInfo<v8::Value> &info) {
            ChangeAbsoluteXp(info, false);
        }
        void JS_RemoveXp(const v8::FunctionCallbackInfo<v8::Value> &info) {
            ChangeAbsoluteXp(info, true);
        }

        // JS: player.addPerk(perk) / removePerk(perk) -> boolean
        void JS_AddPerk(const v8::FunctionCallbackInfo<v8::Value> &info) {
            Player *self                        = Self(info);
            const Shared::Game::PerkEntry *perk = PerkArg(info.GetIsolate(), info.Length() > 0 ? info[0] : v8::Local<v8::Value>(), "Player.addPerk");
            auto *authority                     = Authority();
            if (self != nullptr && perk != nullptr) {
                info.GetReturnValue().Set(authority != nullptr && authority->GrantPerk(self->ResolveHuman(), Shared::Game::PerkIndexOf(*perk)));
            }
        }

        void JS_RemovePerk(const v8::FunctionCallbackInfo<v8::Value> &info) {
            Player *self                        = Self(info);
            const Shared::Game::PerkEntry *perk = PerkArg(info.GetIsolate(), info.Length() > 0 ? info[0] : v8::Local<v8::Value>(), "Player.removePerk");
            auto *authority                     = Authority();
            if (self != nullptr && perk != nullptr) {
                info.GetReturnValue().Set(authority != nullptr && authority->RemovePerk(self->ResolveHuman(), Shared::Game::PerkIndexOf(*perk)));
            }
        }

        // JS: player.addPerkPoints(track, count) -> boolean
        void JS_AddPerkPoints(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            Player *self         = Self(info);
            const int tree       = TrackArg(isolate, info.Length() > 0 ? info[0] : v8::Local<v8::Value>(), "Player.addPerkPoints", false, true);
            if (self == nullptr || tree < 0) {
                return;
            }
            const double count = NumberArg(info, 1, 0.0);
            if (count < 1.0 || count > 65535.0) {
                isolate->ThrowError(v8pp::to_v8(isolate, "Player.addPerkPoints: expected (track, count) with a count from 1 to 65535"));
                return;
            }
            auto *authority = Authority();
            info.GetReturnValue().Set(authority != nullptr && authority->AddPerkPoints(self->ResolveHuman(), static_cast<std::uint8_t>(tree), static_cast<std::uint32_t>(count)));
        }

        // JS: player.respecPerks() -> boolean
        void JS_RespecPerks(const v8::FunctionCallbackInfo<v8::Value> &info) {
            Player *self    = Self(info);
            auto *authority = Authority();
            info.GetReturnValue().Set(self != nullptr && authority != nullptr && authority->Respec(self->ResolveHuman()));
        }

        // JS: player.progression -> ProgressionSnapshot | null
        v8::Local<v8::Value> Snapshot(const Player &self, v8::Isolate *isolate) {
            v8::EscapableHandleScope handleScope(isolate);
            const Shared::RPC::ProgressionDigest *digest = AcceptedOf(self);
            if (digest == nullptr) {
                return handleScope.Escape(v8::Null(isolate));
            }
            v8::Local<v8::Context> context = isolate->GetCurrentContext();
            v8::Local<v8::Object> tracks   = v8::Object::New(isolate);
            v8::Local<v8::Object> points   = v8::Object::New(isolate);
            for (std::size_t i = 0; i < Shared::RPC::kProgressionTracks && i < Shared::Game::g_ProgressionTracks.size(); ++i) {
                const std::string name      = std::string(Shared::Game::g_ProgressionTracks[i].name);
                v8::Local<v8::Object> entry = v8::Object::New(isolate);
                entry->Set(context, v8pp::to_v8(isolate, "level"), v8pp::to_v8(isolate, digest->tracks[i].level)).Check();
                entry->Set(context, v8pp::to_v8(isolate, "xp"), v8pp::to_v8(isolate, static_cast<double>(digest->tracks[i].xpQ7) / 128.0)).Check();
                tracks->Set(context, v8pp::to_v8(isolate, name), entry).Check();
                points->Set(context, v8pp::to_v8(isolate, name), v8pp::to_v8(isolate, static_cast<std::uint32_t>(digest->perkPoints[i]))).Check();
            }
            points->Set(context, v8pp::to_v8(isolate, "main"), v8pp::to_v8(isolate, static_cast<std::uint32_t>(digest->perkPoints[Shared::RPC::kMainPerkTree]))).Check();

            v8::Local<v8::Object> out = v8::Object::New(isolate);
            out->Set(context, v8pp::to_v8(isolate, "tracks"), tracks).Check();
            out->Set(context, v8pp::to_v8(isolate, "perks"), v8pp::to_v8(isolate, PerkNames(digest->perks))).Check();
            out->Set(context, v8pp::to_v8(isolate, "perkPoints"), points).Check();
            return handleScope.Escape(out);
        }

        // JS: player.restoreProgression(snapshot) -> boolean
        void JS_RestoreProgression(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);
            Player *self = Self(info);
            if (self == nullptr || info.Length() < 1 || !info[0]->IsObject()) {
                isolate->ThrowError(v8pp::to_v8(isolate, "Player.restoreProgression: expected (snapshot)"));
                return;
            }
            auto *authority                      = Authority();
            Shared::Entities::HumanEntity *human = self->ResolveHuman();
            if (authority == nullptr || human == nullptr) {
                info.GetReturnValue().Set(false);
                return;
            }

            try {
                const auto encoded = StringifyValue(isolate, info[0]);
                if (!encoded || encoded->size() > 65536) {
                    throw std::invalid_argument("Invalid or oversized progression snapshot");
                }
                const auto plan     = Shared::Game::ProgressionRestorePlan::Parse(nlohmann::json::parse(*encoded));
                const auto *current = authority->Accepted(human->GetNetworkID());
                bool ok             = true;
                for (const auto &[track, total] : plan.tracks) {
                    ok = authority->SetXp(human, track, static_cast<double>(total) / 128, false) && ok;
                }
                if (plan.replacePerks) {
                    for (std::size_t index = 0; index < Shared::Game::g_Perks.size(); ++index) {
                        const bool owned = current != nullptr && current->perks.Has(index);
                        // A snapshot from before a block does not bring the perk back.
                        const bool wanted = plan.perks.Has(index) && !authority->BlockedPerks().Has(index);
                        if (owned && !wanted) {
                            ok = authority->RemovePerk(human, static_cast<std::uint16_t>(index)) && ok;
                        }
                        else if (!owned && wanted) {
                            ok = authority->GrantPerk(human, static_cast<std::uint16_t>(index)) && ok;
                        }
                    }
                }
                for (const auto &[tree, count] : plan.points) {
                    ok = authority->SetPerkPoints(human, tree, count) && ok;
                }
                info.GetReturnValue().Set(ok);
            }
            catch (const std::exception &error) {
                isolate->ThrowError(v8pp::to_v8(isolate, error.what()));
            }
        }

        std::vector<v8::Local<v8::Value>> PlayerArgs(v8::Isolate *isolate, std::uint64_t playerNetworkId) {
            std::vector<v8::Local<v8::Value>> args;
            args.push_back(WrapPlayer(isolate, playerNetworkId));
            return args;
        }
    } // namespace

    void RegisterProgressionPlayerMembers(v8pp::class_<Player> &cls) {
        cls.property(
            "level",
            [](const Player &self) {
                const Shared::RPC::ProgressionDigest *digest = AcceptedOf(self);
                return static_cast<std::uint32_t>(digest != nullptr ? digest->mainLevel : 0);
            },
            v8pp::metadata::property_docs("number", "The player's main level, the one the game derives from the stats, as their client last reported it. 0 until the first report."));
        cls.property(
            "levels",
            [](const Player &self, v8::Isolate *isolate) {
                const Shared::RPC::ProgressionDigest *digest = AcceptedOf(self);
                return TrackLevelsToV8(isolate, digest != nullptr ? DigestLevels(*digest) : std::array<std::uint32_t, Shared::RPC::kProgressionTracks> {});
            },
            v8pp::metadata::property_docs("TrackLevels", "The level of every skill and stat, keyed by track name, as this server last accepted it. All zero until the first report."));
        cls.property(
            "perks",
            [](const Player &self) {
                const Shared::RPC::ProgressionDigest *digest = AcceptedOf(self);
                return digest != nullptr ? PerkNames(digest->perks) : std::vector<std::string>();
            },
            v8pp::metadata::property_docs("string[]", "Every perk the player owns, by name."));
        cls.property(
            "progression",
            [](const Player &self, v8::Isolate *isolate) {
                return Snapshot(self, isolate);
            },
            v8pp::metadata::property_docs("ProgressionSnapshot | null",
                "Everything about this player's progression, as plain JSON to store however the resource likes. A player starts every session with a fresh character, so `restoreProgression` with a stored snapshot is how progression outlives a disconnect. Null until their client's first report."));

        cls.prototype_function("getTrack", &JS_GetTrack,
            v8pp::metadata::docs("TrackProgress | null", {v8pp::metadata::param("track", "string", false, "A track name, from `Progression.tracks()`.")}, "Where this player stands on one track: level, XP towards the next one, and unspent perk points.",
                "The progress, or null before their client's first report. Throws for a track name the tables do not carry."));
        cls.prototype_function("hasPerk", &JS_HasPerk, v8pp::metadata::docs("boolean", {v8pp::metadata::param("perk", "string", false, "A perk's name or GUID.")}, "Whether this player owns a perk.", "True when their last report carried it."));
        cls.prototype_function("addXp", &JS_AddXp,
            v8pp::metadata::docs("boolean", {v8pp::metadata::param("track", "string", false, "A track name. Not `fencing`: the game derives it from the weapon skills."), v8pp::metadata::param("xp", "number", false, "XP, in the game's own units.")},
                "Grants XP on one track, through the game's own path on the player's client -- so their perks' XP multipliers apply, a level crossed pops the game's own level-up, and a level grants its perk points. Rates, budgets and caps do not apply: those rule over what a player earns, and this is the server giving.",
                "True when the order went out. Throws for an unknown track or an xp that is not positive."));
        cls.prototype_function("setLevel", &JS_SetLevel,
            v8pp::metadata::docs("boolean", {v8pp::metadata::param("track", "string", false, "A track name. Not `fencing`."), v8pp::metadata::param("level", "number", false, "Exact level, from zero to the track cap; resets XP inside that level.")},
                "Sets an exact level and clears its XP remainder, including on a live player. Increases use native XP effects and grant points; decreases notify native property listeners and recompute induced skills. Decreasing does not remove perks or revoke points: change those explicitly, or restore a complete snapshot.",
                "True when the order went out; false for a level past the cap."));
        cls.prototype_function("setXp", &JS_SetXp,
            v8pp::metadata::docs("boolean",
                {v8pp::metadata::param("track", "string", false, "A progression track other than fencing."), v8pp::metadata::param("xp", "number", false, "Absolute cumulative XP from level zero, in game XP units. Quantized down to 1/128 XP; cannot exceed the track cap.")},
                "Sets a track's total earned XP exactly, including reductions inside a level and across levels. Does not revoke perks or unspent points.", "True when the order went out; false for an unavailable player or XP above the cap."));
        cls.prototype_function("removeXp", &JS_RemoveXp,
            v8pp::metadata::docs("boolean", {v8pp::metadata::param("track", "string", false, "A progression track other than fencing."), v8pp::metadata::param("xp", "number", false, "Positive XP to remove, at least 1/128. Clamps at zero total XP.")},
                "Subtracts unscaled cumulative XP from the reported track plus pending orders. Requires the first progression report. Level and remainder can both decrease; perks and points stay unchanged.", "True when the order went out."));
        cls.prototype_function("addPerk", &JS_AddPerk,
            v8pp::metadata::docs("boolean", {v8pp::metadata::param("perk", "string", false, "A perk's name or GUID.")},
                "Gives this player a perk outright, spending no point and skipping the perk screen's requirements. It still has to be one the game can add: an owned or blocked perk is turned down by the game itself.", "True when the order went out. Throws for an unknown perk."));
        cls.prototype_function("removePerk", &JS_RemovePerk,
            v8pp::metadata::docs("boolean", {v8pp::metadata::param("perk", "string", false, "A perk's name or GUID.")}, "Takes a perk away. The point it cost is not refunded; `respecPerks` is the way to hand points back.",
                "True when the order went out. Throws for an unknown perk."));
        cls.prototype_function("addPerkPoints", &JS_AddPerkPoints,
            v8pp::metadata::docs("boolean", {v8pp::metadata::param("track", "string", false, "The tree to add to: a track name, or `main` for the main-level bank."), v8pp::metadata::param("count", "number", false, "How many, from 1 to 65535.")},
                "Gives this player unspent perk points in one tree, on top of what their levels earned.", "True when the order went out."));
        cls.prototype_function("respecPerks", &JS_RespecPerks,
            v8pp::metadata::docs("boolean", {}, "The game's own respec: every perk learnt on the perk screen is taken away and every tree's points are rebuilt from the player's current levels. Perks the game granted on its own stay.", "True when the order went out."));
        cls.prototype_function("restoreProgression", &JS_RestoreProgression,
            v8pp::metadata::docs("boolean", {v8pp::metadata::param("snapshot", "ProgressionSnapshot", false, "What `player.progression` returned, as the resource stored it.")},
                "Restores the supplied tracks exactly, including lower levels and XP remainders on live players. Supplied perks replace the reported perk set, and supplied point banks are set after progression events drain. Omitted sections stay unchanged. The entire snapshot is validated before any order is sent. Fencing is recomputed from weapon skills. A restore is not a gain: a raised track shows no level-up popup and grants no perk points.",
                "True when every order went out."));
    }

    void Progression::Register(v8::Isolate *isolate, v8::Local<v8::Object> global) {
        if (isolate == nullptr || global.IsEmpty()) {
            return;
        }

        auto &events = ServerCatalog().data_type("EventMap");
        events.add_property("playerXpGaining", "[player: Player, track: string, xp: number, source: string]",
            "Dispatched when a player's own game produced XP -- a hit landed, a lock picked, a page read -- and asks for it. Return `false` to refuse it; the gain then never happens. `xp` is before this server's rate and the player's own multipliers. `source` is the game's name for what caused it: `Attack`, `LockpickingResult` and so on, or empty. Nothing is asked while `Progression.setNativeXp(false)` is in force.");
        events.add_property("playerXpGained", "[player: Player, track: string, xp: number]", "Dispatched when XP landed on a player's character, from their game or from this server, after every multiplier.");
        events.add_property("playerLevelUp", "[player: Player, track: string, level: number, perkPoints: number]", "Dispatched when one of a player's tracks reaches a new level. `perkPoints` is what that track's tree now has unspent.");
        events.add_property("playerPerkLearning", "[player: Player, perk: string]",
            "Dispatched when a player confirmed a perk on the perk screen and every rule of the game allows it: the tree's level, the parent perk, the exclusive partner, a point to spend, and this server's block list. Return `false` to refuse it; the point is not spent and their client raises `progressionPerkRefused`.");
        events.add_property("playerPerkAdded", "[player: Player, perk: string, source: \"learned\" | \"server\" | \"native\"]",
            "Dispatched when a perk appeared on a player's character: learnt on the perk screen, given by this server, or granted by the game on its own -- a codex entry, a recipe, a combat move it teaches.");
        events.add_property("playerPerkRemoved", "[player: Player, perk: string, source: \"server\" | \"native\"]", "Dispatched when a perk left a player's character: taken or respecced by this server, or removed by the game.");
        events.add_property("playerProgressionRejected", "[player: Player, reason: string]",
            "Dispatched when a player's client reported progression this server never ordered: a track that rose further than any gain it granted could take it, or a perk-screen perk nobody learnt. The report is kept -- the game has no way to take a level back -- so what happens to the player is the handler's call; a kick is the usual answer.");

        v8pp::module progression(isolate, ServerCatalog(), "Progression", "The game's skills, stats and perks, and the rules this server puts on how players move along them.");
        RegisterProgressionCatalogFunctions(progression);

        progression.property(
            "nativeXp",
            []() {
                auto *authority = Authority();
                return authority == nullptr || authority->NativeXp();
            },
            v8pp::metadata::property_docs("boolean", "Whether players' own games may earn XP. See `setNativeXp`."));
        progression.property(
            "blockedPerks",
            []() {
                auto *authority = Authority();
                return authority != nullptr ? PerkNames(authority->BlockedPerks()) : std::vector<std::string>();
            },
            v8pp::metadata::property_docs("string[]", "The perks this server blocks."));

        progression.function("setNativeXp", &JS_SetNativeXp,
            v8pp::metadata::docs("void", {v8pp::metadata::param("enabled", "boolean", false, "False to stop players' own games from earning XP at all.")},
                "Switches off XP from play. Every gain the game produces is dropped on the player's own client, and only `player.addXp` and `player.setLevel` move a track -- for servers where progression comes from jobs, trainers or quests a resource runs.", "Nothing."));
        progression.function("setXpRate", &JS_SetXpRate,
            v8pp::metadata::docs("void",
                {v8pp::metadata::param("track", "string", false, "A track name, or `*` for every track."), v8pp::metadata::param("rate", "number", false, "What a gain is multiplied by before it is granted: 1 is the game's own pace, 0.5 half, 0 none at all.")},
                "Scales the XP players earn by playing. Applied before the player's own perk multipliers, which still work as they do in single player. `player.addXp` is not scaled.", "Nothing."));
        progression.function("setXpBudget", &JS_SetXpBudget,
            v8pp::metadata::docs("void",
                {v8pp::metadata::param("track", "string", false, "A track name, or `*` for every track."), v8pp::metadata::param("xpPerMinute", "number", false, "How much XP a player may earn on the track per minute, before multipliers. 0 is unlimited.")},
                "Limits how fast a track can be ground. The budget holds a minute's worth and refills steadily, so a burst after a quiet spell goes through and a loop -- two players sparring for sword XP, a lock picked over and over -- runs dry. `player.addXp` is not limited.",
                "Nothing."));
        progression.function("setLevelCap", &JS_SetLevelCap,
            v8pp::metadata::docs("void", {v8pp::metadata::param("track", "string", false, "A track name, or `*` for every track."), v8pp::metadata::param("level", "number", false, "The highest level XP from play may reach, at most the game's own 30.")},
                "Stops a track at a level. Gains close to it are granted exactly what is left, so no multiplier carries the track past it. `player.setLevel` and `player.addXp` can still go beyond: the cap rules over what is earned.", "Nothing."));
        progression.function("blockPerks", &JS_BlockPerks,
            v8pp::metadata::docs("void", {v8pp::metadata::param("perks", "string[]", false, "Perk names or GUIDs.")},
                "Takes perks out of the game: every player who owns one gives it up, the perk screen shows it as blocked and refuses to learn it, the game's own grants of it are turned down or taken back, and `player.restoreProgression` leaves it out. A point spent on it is not given back; `player.respecPerks` returns every point. `player.addPerk` still grants one, as the script's own decision.",
                "Nothing. Throws for an unknown perk."));
        progression.function("unblockPerks", &JS_UnblockPerks, v8pp::metadata::docs("void", {v8pp::metadata::param("perks", "string[]", false, "Perk names or GUIDs.")}, "Lifts `blockPerks`.", "Nothing. Throws for an unknown perk."));

        progression.publish(global);
    }

    bool Events::PlayerXpGaining(std::uint64_t playerNetworkId, const std::string &track, double xp, const std::string &source) {
        return EmitReservedEventSync("playerXpGaining", [playerNetworkId, &track, xp, &source](v8::Isolate *isolate) {
            std::vector<v8::Local<v8::Value>> args = PlayerArgs(isolate, playerNetworkId);
            args.push_back(v8pp::to_v8(isolate, track));
            args.push_back(v8pp::to_v8(isolate, xp));
            args.push_back(v8pp::to_v8(isolate, source));
            return args;
        });
    }

    void Events::PlayerXpGained(std::uint64_t playerNetworkId, const std::string &track, double xp) {
        EmitReservedEvent("playerXpGained", [playerNetworkId, &track, xp](v8::Isolate *isolate) {
            std::vector<v8::Local<v8::Value>> args = PlayerArgs(isolate, playerNetworkId);
            args.push_back(v8pp::to_v8(isolate, track));
            args.push_back(v8pp::to_v8(isolate, xp));
            return args;
        });
    }

    void Events::PlayerLevelUp(std::uint64_t playerNetworkId, const std::string &track, std::uint32_t level, std::uint32_t perkPoints) {
        EmitReservedEvent("playerLevelUp", [playerNetworkId, &track, level, perkPoints](v8::Isolate *isolate) {
            std::vector<v8::Local<v8::Value>> args = PlayerArgs(isolate, playerNetworkId);
            args.push_back(v8pp::to_v8(isolate, track));
            args.push_back(v8pp::to_v8(isolate, level));
            args.push_back(v8pp::to_v8(isolate, perkPoints));
            return args;
        });
    }

    bool Events::PlayerPerkLearning(std::uint64_t playerNetworkId, const std::string &perk) {
        return EmitReservedEventSync("playerPerkLearning", [playerNetworkId, &perk](v8::Isolate *isolate) {
            std::vector<v8::Local<v8::Value>> args = PlayerArgs(isolate, playerNetworkId);
            args.push_back(v8pp::to_v8(isolate, perk));
            return args;
        });
    }

    void Events::PlayerPerkAdded(std::uint64_t playerNetworkId, const std::string &perk, const char *source) {
        EmitReservedEvent("playerPerkAdded", [playerNetworkId, &perk, source](v8::Isolate *isolate) {
            std::vector<v8::Local<v8::Value>> args = PlayerArgs(isolate, playerNetworkId);
            args.push_back(v8pp::to_v8(isolate, perk));
            args.push_back(v8pp::to_v8(isolate, std::string(source)));
            return args;
        });
    }

    void Events::PlayerPerkRemoved(std::uint64_t playerNetworkId, const std::string &perk, const char *source) {
        EmitReservedEvent("playerPerkRemoved", [playerNetworkId, &perk, source](v8::Isolate *isolate) {
            std::vector<v8::Local<v8::Value>> args = PlayerArgs(isolate, playerNetworkId);
            args.push_back(v8pp::to_v8(isolate, perk));
            args.push_back(v8pp::to_v8(isolate, std::string(source)));
            return args;
        });
    }

    void Events::PlayerProgressionRejected(std::uint64_t playerNetworkId, const std::string &reason) {
        EmitReservedEvent("playerProgressionRejected", [playerNetworkId, &reason](v8::Isolate *isolate) {
            std::vector<v8::Local<v8::Value>> args = PlayerArgs(isolate, playerNetworkId);
            args.push_back(v8pp::to_v8(isolate, reason));
            return args;
        });
    }
} // namespace KCDC::Scripting
