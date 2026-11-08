#include "features/player/player_progression_scripting.h"

#include "core/application.h"
#include "core/scripting/script_context.h"
#include "features/player/player_progression_service.h"

#include "shared/features/player/player_progression_bindings.h"
#include "shared/game/progression_catalog.h"
#include "shared/scripting/catalog.h"

#include <v8pp/convert.hpp>
#include <v8pp/module.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace KCDC::Scripting {
    namespace {
        /** The spelling `progressionPerkRefused` hands a script. */
        const char *RefusalName(Shared::RPC::PerkRefusal reason) {
            switch (reason) {
            case Shared::RPC::PerkRefusal::Points: return "points";
            case Shared::RPC::PerkRefusal::Level: return "level";
            case Shared::RPC::PerkRefusal::Parent: return "parent";
            case Shared::RPC::PerkRefusal::Exclusive: return "exclusive";
            case Shared::RPC::PerkRefusal::Blocked: return "blocked";
            case Shared::RPC::PerkRefusal::Script: return "script";
            case Shared::RPC::PerkRefusal::Owned: return "owned";
            case Shared::RPC::PerkRefusal::Hidden: return "hidden";
            default: return "unknown";
            }
        }

        const KCDC::Features::Player::PlayerProgressionService &Service() {
            return KCDC::Core::gApplication->GetPlayerProgressionService();
        }

        /** The perks a handle's body owns: the live digest for the local player, the replicated set for everyone else. */
        Shared::RPC::PerkSet PerksOf(const Player &self) {
            if (self.IsLocal()) {
                return Service().LocalDigest().perks;
            }
            const Shared::Entities::HumanEntity *human = self.ResolveHuman();
            return human != nullptr ? human->progression.perks : Shared::RPC::PerkSet {};
        }

        Player *Self(const v8::FunctionCallbackInfo<v8::Value> &info) {
            return v8pp::class_<Player>::unwrap_object(info.GetIsolate(), info.This());
        }

        // JS: player.getTrack(track) -> TrackProgress | null
        void JS_GetTrack(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);

            Player *self = Self(info);
            if (self == nullptr || info.Length() < 1 || !info[0]->IsString()) {
                isolate->ThrowError(v8pp::to_v8(isolate, "Player.getTrack: expected (track)"));
                return;
            }
            const int track = Shared::Game::FindProgressionTrack(v8pp::from_v8<std::string>(isolate, info[0]));
            if (track < 0) {
                isolate->ThrowError(v8pp::to_v8(isolate, "Player.getTrack: unknown track; see Progression.tracks()"));
                return;
            }
            if (!self->IsLocal() || !Service().HasLocalDigest()) {
                info.GetReturnValue().SetNull();
                return;
            }
            info.GetReturnValue().Set(TrackProgressToV8(isolate, Service().LocalDigest(), static_cast<std::size_t>(track)));
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
            const Shared::Game::PerkEntry *perk = Shared::Game::ResolvePerk(v8pp::from_v8<std::string>(isolate, info[0]));
            info.GetReturnValue().Set(perk != nullptr && PerksOf(*self).Has(Shared::Game::PerkIndexOf(*perk)));
        }
    } // namespace

    void RegisterProgressionPlayerMembers(v8pp::class_<Player> &cls) {
        cls.property(
            "level",
            [](const Player &self) {
                if (self.IsLocal()) {
                    return static_cast<std::uint32_t>(Service().LocalDigest().mainLevel);
                }
                const Shared::Entities::HumanEntity *human = self.ResolveHuman();
                return static_cast<std::uint32_t>(human != nullptr ? human->progression.mainLevel : 0);
            },
            v8pp::metadata::property_docs("number", "The player's main level, the one the game derives from the stats. 0 until known."));
        cls.property(
            "levels",
            [](const Player &self, v8::Isolate *isolate) {
                if (self.IsLocal()) {
                    return TrackLevelsToV8(isolate, DigestLevels(Service().LocalDigest()));
                }
                const Shared::Entities::HumanEntity *human = self.ResolveHuman();
                return TrackLevelsToV8(isolate, human != nullptr ? human->progression.levels : std::array<std::uint8_t, Shared::RPC::kProgressionTracks> {});
            },
            v8pp::metadata::property_docs("TrackLevels", "The level of every skill and stat, keyed by track name. The local player reads its own character; anyone else reads what the server last accepted from theirs, and all zero until it has."));
        cls.property(
            "perks",
            [](const Player &self) {
                return PerkNames(PerksOf(self));
            },
            v8pp::metadata::property_docs("string[]", "Every perk the player owns, by name -- the perk screen's and the hidden ones the game grants on its own."));

        cls.prototype_function("getTrack", &JS_GetTrack,
            v8pp::metadata::docs("TrackProgress | null", {v8pp::metadata::param("track", "string", false, "A track name, from `Progression.tracks()`.")},
                "Where the local player stands on one track: level, XP towards the next one, and unspent perk points. Only the local player carries XP and points; for anyone else this is null.",
                "The progress, or null for any other player. Throws for a track name the tables do not carry."));
        cls.prototype_function("hasPerk", &JS_HasPerk,
            v8pp::metadata::docs("boolean", {v8pp::metadata::param("perk", "string", false, "A perk's name or GUID.")}, "Whether the player owns a perk.", "True when they do; false for a perk they do not own, or a name the tables do not carry."));
    }

    void Progression::Register(v8::Isolate *isolate, v8::Local<v8::Object> global) {
        if (isolate == nullptr || global.IsEmpty()) {
            return;
        }

        auto &events = ClientCatalog().data_type("EventMap");
        events.add_property("progressionXpGained", "[track: string, xp: number]", "Dispatched when the local player's XP on a track went up: after the server granted a gain and the game applied it, with its own multipliers. `xp` is what landed, in the game's own units.");
        events.add_property("progressionLevelUp", "[track: string, level: number, perkPoints: number]", "Dispatched when a track of the local player's reaches a new level. `perkPoints` is what that track's tree now has unspent.");
        events.add_property("progressionPerkAdded", "[perk: string]", "Dispatched when the local player gained a perk: learnt on the perk screen and granted by the server, given by a server resource, or granted by the game on its own.");
        events.add_property("progressionPerkRemoved", "[perk: string]", "Dispatched when the local player lost a perk.");
        events.add_property("progressionPerkRefused", "[perk: string, reason: \"points\" | \"level\" | \"parent\" | \"exclusive\" | \"blocked\" | \"script\" | \"owned\" | \"hidden\"]",
            "Dispatched when the server turned down a perk the local player confirmed on the perk screen. The game's own screen says nothing when that happens, so this is where a resource tells the player why.");

        // The data types go in with the Player class, which is built once per
        // process; this runs again on every reconnect.
        v8pp::module progression(isolate, ClientCatalog(), "Progression", "The game's skills, stats and perks. Read-only on the client: every change to a player's progression is the server's.");
        RegisterProgressionCatalogFunctions(progression);
        progression.publish(global);
    }

    void Events::ProgressionXpGained(const std::string &track, double xp) {
        EmitReservedEvent("progressionXpGained", [&track, xp](v8::Isolate *isolate) {
            return std::vector<v8::Local<v8::Value>> {v8pp::to_v8(isolate, track), v8pp::to_v8(isolate, xp)};
        });
    }

    void Events::ProgressionLevelUp(const std::string &track, std::uint32_t level, std::uint32_t perkPoints) {
        EmitReservedEvent("progressionLevelUp", [&track, level, perkPoints](v8::Isolate *isolate) {
            return std::vector<v8::Local<v8::Value>> {v8pp::to_v8(isolate, track), v8pp::to_v8(isolate, level), v8pp::to_v8(isolate, perkPoints)};
        });
    }

    void Events::ProgressionPerkAdded(const std::string &perk) {
        EmitReservedEvent("progressionPerkAdded", [&perk](v8::Isolate *isolate) {
            return std::vector<v8::Local<v8::Value>> {v8pp::to_v8(isolate, perk)};
        });
    }

    void Events::ProgressionPerkRemoved(const std::string &perk) {
        EmitReservedEvent("progressionPerkRemoved", [&perk](v8::Isolate *isolate) {
            return std::vector<v8::Local<v8::Value>> {v8pp::to_v8(isolate, perk)};
        });
    }

    void Events::ProgressionPerkRefused(const std::string &perk, Shared::RPC::PerkRefusal reason) {
        EmitReservedEvent("progressionPerkRefused", [&perk, reason](v8::Isolate *isolate) {
            return std::vector<v8::Local<v8::Value>> {v8pp::to_v8(isolate, perk), v8pp::to_v8(isolate, std::string(RefusalName(reason)))};
        });
    }
} // namespace KCDC::Scripting
