#pragma once

#include "shared/features/player/player_appearance_bindings.h"
#include "shared/features/player/player_entity.h"
#include "shared/features/player/player_soul_state.h"
#include "shared/features/player/player_stat_bindings.h"
#include "shared/item_stack.h"

#include <scripting/builtins/vector3.h>

#include <v8.h>
#include <v8pp/class.hpp>
#include <v8pp/convert.hpp>
#include <v8pp/metadata.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// The readonly view of a replicated body, bound once and used by both sides.
//
// A player's body is described by one thing -- the HumanEntity its owner publishes -- and the server's Player and the client's Player are two handles onto it. Written twice, the two would drift, and a stat that reads differently depending on which side asked is the kind of difference nobody notices until a gamemode is wrong.
//
// Readonly by construction: the owning client is authoritative for its own body, so a setter would either be a lie or a pose the owner overwrites on its next capture. Verbs that ask an owner to change something live on the server's Player, where the RPC that carries the ask is in scope.
namespace KCDC::Scripting {
    namespace Detail {
        // One item class as the 32 hex digits the wire and the equipment string use, or empty.
        inline std::string FormatItemClass(const KCDC::Shared::RPC::ItemClassId &itemClass) {
            if (!itemClass.IsValid()) {
                return std::string();
            }
            std::string text;
            itemClass.AppendPackedText(text);
            return text;
        }

        // Null when there is no body or its stream was quarantined. Every stat reader goes through this, so a player whose soul has not arrived reads as zero throughout rather than as a mix of zeroes and values from before the withdrawal.
        template <typename T>
        const KCDC::Shared::Game::HumanSoulState *Soul(const T &self) {
            const KCDC::Shared::Entities::HumanEntity *human = self.ResolveHuman();
            return human != nullptr && human->soul.valid ? &human->soul : nullptr;
        }

        template <typename T>
        float Resource(const T &self, std::size_t index, bool maximum) {
            const KCDC::Shared::Game::HumanSoulState *soul = Soul(self);
            return soul == nullptr ? 0.0F : (maximum ? soul->resources[index].maximum : soul->resources[index].current);
        }

        template <typename T>
        float Condition(const T &self, std::size_t index) {
            const KCDC::Shared::Game::HumanSoulState *soul = Soul(self);
            return soul == nullptr ? 0.0F : soul->conditions[index];
        }

        // A `kDerivedPlayerStats` row: a gameplay condition from the soul, or a script reading.
        template <typename T>
        float DerivedStat(const T &self, std::size_t index) {
            if (index < KCDC::Shared::Game::kPlayerConditionCount) {
                return Condition(self, index);
            }
            const KCDC::Shared::Entities::HumanEntity *human = self.ResolveHuman();
            return Soul(self) == nullptr ? 0.0F : human->statReadings.values[index - KCDC::Shared::Game::kPlayerConditionCount];
        }

        // JS: player.getStat(stat) / player.getDerivedStat(stat) -> number
        template <typename T, bool Derived>
        void JS_GetStat(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            const T *self        = v8pp::class_<T>::unwrap_object(isolate, info.This());
            if (self == nullptr || info.Length() != 1 || !info[0]->IsString()) {
                isolate->ThrowError(v8pp::to_v8(isolate, "Player stat getter: expected one stat enum value"));
                return;
            }
            const std::string name = v8pp::from_v8<std::string>(isolate, info[0]);
            if constexpr (Derived) {
                const std::size_t index = KCDC::Shared::Game::FindPlayerStat(KCDC::Shared::Game::kDerivedPlayerStats, name);
                if (index == KCDC::Shared::Game::kDerivedPlayerStats.size()) {
                    isolate->ThrowError(v8pp::to_v8(isolate, "Unknown DerivedPlayerStat"));
                    return;
                }
                info.GetReturnValue().Set(DerivedStat(*self, index));
            }
            else {
                const std::size_t index = KCDC::Shared::Game::FindPlayerStat(KCDC::Shared::Game::kPlayerStats, name);
                if (index == KCDC::Shared::Game::kPlayerStats.size()) {
                    isolate->ThrowError(v8pp::to_v8(isolate, "Unknown PlayerStat"));
                    return;
                }
                info.GetReturnValue().Set(Resource(*self, index, false));
            }
        }

        template <typename T>
        float Stat(const T &self, std::size_t index) {
            const KCDC::Shared::Game::HumanSoulState *soul = Soul(self);
            return soul == nullptr ? 0.0F : soul->stats[index];
        }

        // The keys the two stat arrays and the two skill arrays are packed under, in snapshot order.
        inline const char *const *StatNames() {
            static const char *const names[KCDC::Shared::Game::HumanSoulState::kStatCount] {"strength", "agility", "vitality"};
            return names;
        }

        inline const char *const *SkillNames() {
            static const char *const names[KCDC::Shared::Game::HumanSoulState::kSkillSelectors.size()] {"fencing", "survival", "defense", "sword", "heavyWeapons", "marksmanship", "dagger", "largeWeapons", "unarmed"};
            return names;
        }

        // One float array as a plain object; every key present, zeroed when there is no snapshot.
        template <typename Values>
        v8::Local<v8::Value> PackNamedFloats(v8::Isolate *isolate, const char *const *names, const Values *values, std::size_t count) {
            v8::EscapableHandleScope handleScope(isolate);
            v8::Local<v8::Context> context = isolate->GetCurrentContext();
            v8::Local<v8::Object> packed   = v8::Object::New(isolate);
            for (std::size_t i = 0; i < count; ++i) {
                packed->Set(context, v8pp::to_v8(isolate, names[i]), v8pp::to_v8(isolate, values == nullptr ? 0.0F : values[i])).Check();
            }
            return handleScope.Escape(packed);
        }
    } // namespace Detail

    // Documents the shapes the grouped stat and skill properties return.
    inline void RegisterHumanDataTypes(v8pp::metadata::registry &catalog) {
        RegisterAppearanceDataTypes(catalog);

        auto &stats = catalog.data_type("SoulStats", "The three character attributes a body's soul carries, in the game's own units.");
        for (std::size_t i = 0; i < KCDC::Shared::Game::HumanSoulState::kStatCount; ++i) {
            stats.add_property(Detail::StatNames()[i], "number", "Attribute value, or 0 while the body has published no soul.");
        }

        auto &skills = catalog.data_type("SoulSkills", "The nine skills a hit or a draw is evaluated against, in the game's own units.");
        for (std::size_t i = 0; i < KCDC::Shared::Game::HumanSoulState::kSkillSelectors.size(); ++i) {
            skills.add_property(Detail::SkillNames()[i], "number", "Skill level, or 0 while the body has published no soul.");
        }
    }

    // Binds every readonly body property onto `cls`. `T` needs one member: `HumanEntity *ResolveHuman() const`, null for a handle whose body is gone. Nothing here touches the engine, so it is equally correct on a server that has never loaded the game.
    template <typename T>
    void RegisterHumanProperties(v8pp::class_<T> &cls) {
        cls.prototype_function("getStat", &Detail::JS_GetStat<T, false>,
            v8pp::metadata::docs("number", {v8pp::metadata::param("stat", PlayerStatType(KCDC::Shared::Game::kPlayerStats), false, "A PlayerStat value.")},
                "Reads the latest reported stat in native units. Returns zero when no valid body snapshot exists; check player.ready to distinguish that from a real zero. Throws for an unknown stat or invalid arguments."));
        cls.prototype_function("getDerivedStat", &Detail::JS_GetStat<T, true>,
            v8pp::metadata::docs("number", {v8pp::metadata::param("stat", PlayerStatType(KCDC::Shared::Game::kDerivedPlayerStats), false, "A DerivedPlayerStat value.")},
                "Reads a computed stat from the latest body snapshot. Bleeding, Sleeping, Consciousness, Drunkenness and Poisoning follow the body as it changes; the others are sampled about every 250 ms to a hundredth of a native unit, then replicated. Values use native units and can be negative; they are not universally percentages. Returns zero without a valid snapshot. Throws for an unknown stat or invalid arguments. These readings do not override another player's native calculations."));

        // --- Identity ---

        cls.property("nickname", [](const T &self) { const KCDC::Shared::Entities::HumanEntity *human = self.ResolveHuman(); return human != nullptr ? human->nickname : std::string(); }, v8pp::metadata::property_docs("string", "The name this player connected under, or an empty string once their body is gone."));
        cls.property("playerIndex", [](const T &self) { const KCDC::Shared::Entities::HumanEntity *human = self.ResolveHuman(); return human != nullptr ? static_cast<std::uint32_t>(human->playerIndex) : 0xFFFFU; }, v8pp::metadata::property_docs("number", "Connection slot this player holds, or 65535 when unassigned. Stable for the length of the session and reused afterwards."));
        cls.property("appearance", [](const T &self, v8::Isolate *isolate) { const KCDC::Shared::Entities::HumanEntity *human = self.ResolveHuman(); return AppearanceToV8(isolate, human != nullptr ? human->appearance : KCDC::Shared::Game::CharacterAppearance()); }, v8pp::metadata::property_docs("Appearance", "What this player's body looks like, as their own client last published it. Every part is empty until something chooses one, which is the default body -- and why everyone looks the same until a resource says otherwise.\n\nRead back rather than assumed after `setAppearance`: the owning client is authoritative for its own body, so a new look appears here once they have actually put it on."));
        cls.property("disguise", [](const T &self, v8::Isolate *isolate) { const KCDC::Shared::Entities::HumanEntity *human = self.ResolveHuman(); return human != nullptr && !human->disguise.IsEmpty() ? v8::Local<v8::Value>(v8pp::to_v8(isolate, human->disguise.model)) : v8::Local<v8::Value>(v8::Null(isolate)); },
            v8pp::metadata::property_docs("string | null", "The prop-catalog mesh this player's body is drawn as, by its `objects/...cgf` path, or null while they look like themselves. Published by their own client, so it follows `setDisguise` once they have actually put it on."));
        cls.property("visible", [](const T &self) { const KCDC::Shared::Entities::HumanEntity *human = self.ResolveHuman(); return human == nullptr || !human->bodyHidden; },
            v8pp::metadata::property_docs("boolean", "Whether this player's body is drawn at all -- itself, what it wears and what it carries. True for everybody until something hides them. Published by their own client, so it follows `setVisible` once they have actually stopped drawing. A hidden player is only invisible: they still stand where they stand, still collide, can still be hit, and NPCs still see them."));

        // --- Readiness ---

        cls.property("ready", [](const T &self) { const KCDC::Shared::Entities::HumanEntity *human = self.ResolveHuman(); return human != nullptr && human->transformReady && human->soul.valid; }, v8pp::metadata::property_docs("boolean", "Whether this player's body has both a pose and a soul, which is what everyone else waits for before spawning a puppet for them. False for the first moments of a connection."));
        cls.property("alive", [](const T &self) { const KCDC::Shared::Game::HumanSoulState *soul = Detail::Soul(self); return soul != nullptr && soul->resources[KCDC::Shared::Game::HumanSoulState::kHealth].current > 0.0F; }, v8pp::metadata::property_docs("boolean", "Whether this player has health left. False also while no soul has been published."));
        cls.property("canAct", [](const T &self) { const KCDC::Shared::Game::HumanSoulState *soul = Detail::Soul(self); return soul != nullptr && soul->CanAct(); }, v8pp::metadata::property_docs("boolean", "Whether the body can be driven at all: alive, conscious and not asleep."));
        cls.property("healthPercent", [](const T &self) { const KCDC::Shared::Game::HumanSoulState *soul = Detail::Soul(self); return soul != nullptr ? soul->HealthPercent() : 0.0F; }, v8pp::metadata::property_docs("number", "Health as a percentage from 0 to 100 -- what the nametag bar draws. Derived from the pair behind it, so it survives a maximum that changes."));

        // --- Resources, in the engine's own units ---

        cls.property("health", [](const T &self) { return Detail::Resource(self, KCDC::Shared::Game::HumanSoulState::kHealth, false); }, v8pp::metadata::property_docs("number", "Current health, in the game's own units. 0 while no soul has been published."));
        cls.property("maxHealth", [](const T &self) { return Detail::Resource(self, KCDC::Shared::Game::HumanSoulState::kHealth, true); }, v8pp::metadata::property_docs("number", "Health capacity, in the game's own units."));
        cls.property("stamina", [](const T &self) { return Detail::Resource(self, KCDC::Shared::Game::HumanSoulState::kStamina, false); }, v8pp::metadata::property_docs("number", "Current stamina, in the game's own units."));
        cls.property("maxStamina", [](const T &self) { return Detail::Resource(self, KCDC::Shared::Game::HumanSoulState::kStamina, true); }, v8pp::metadata::property_docs("number", "Stamina capacity, in the game's own units. Collapses to 0 for a tick around death, which is real rather than a bad read."));
        cls.property("healthyStamina", [](const T &self) { const KCDC::Shared::Game::HumanSoulState *soul = Detail::Soul(self); return soul != nullptr ? soul->healthyStamina : 0.0F; }, v8pp::metadata::property_docs("number", "The stamina ceiling the body's injuries currently allow, which is at or below maxStamina."));
        cls.property("exhaust", [](const T &self) { return Detail::Resource(self, KCDC::Shared::Game::HumanSoulState::kExhaust, false); }, v8pp::metadata::property_docs("number", "Current energy reserve, in the game's own units. Higher means better rested."));
        cls.property("maxExhaust", [](const T &self) { return Detail::Resource(self, KCDC::Shared::Game::HumanSoulState::kExhaust, true); }, v8pp::metadata::property_docs("number", "Maximum energy reserve, in the game's own units."));
        cls.property("hunger", [](const T &self) { return Detail::Resource(self, KCDC::Shared::Game::HumanSoulState::kHunger, false); }, v8pp::metadata::property_docs("number", "Current nourishment, in the game's own units."));
        cls.property("maxHunger", [](const T &self) { return Detail::Resource(self, KCDC::Shared::Game::HumanSoulState::kHunger, true); }, v8pp::metadata::property_docs("number", "Nourishment capacity, in the game's own units."));

        // --- Conditions ---

        cls.property("bleeding", [](const T &self) { return Detail::Condition(self, KCDC::Shared::Game::HumanSoulState::kBleeding); }, v8pp::metadata::property_docs("number", "How heavily the body is bleeding; 0 when it is not."));
        cls.property("sleeping", [](const T &self) { return Detail::Condition(self, KCDC::Shared::Game::HumanSoulState::kSleeping); }, v8pp::metadata::property_docs("number", "Sleepiness the game has accumulated for this body; above 0 means asleep."));
        cls.property("consciousness", [](const T &self) { return Detail::Condition(self, KCDC::Shared::Game::HumanSoulState::kConsciousness); }, v8pp::metadata::property_docs("number", "How conscious the body is; 0 is knocked out."));
        cls.property("drunkenness", [](const T &self) { return Detail::Condition(self, KCDC::Shared::Game::HumanSoulState::kDrunkenness); }, v8pp::metadata::property_docs("number", "How drunk the body is; 0 is sober."));
        cls.property("poisoning", [](const T &self) { return Detail::Condition(self, KCDC::Shared::Game::HumanSoulState::kPoisoning); }, v8pp::metadata::property_docs("number", "How poisoned the body is; 0 is clean."));

        // --- Attributes and skills ---

        cls.property("strength", [](const T &self) { return Detail::Stat(self, 0); }, v8pp::metadata::property_docs("number", "Strength, in the game's own units."));
        cls.property("agility", [](const T &self) { return Detail::Stat(self, 1); }, v8pp::metadata::property_docs("number", "Agility, in the game's own units."));
        cls.property("vitality", [](const T &self) { return Detail::Stat(self, 2); }, v8pp::metadata::property_docs("number", "Vitality, in the game's own units."));
        cls.property("relativeStats", [](const T &self, v8::Isolate *isolate) { const KCDC::Shared::Game::HumanSoulState *soul = Detail::Soul(self); return Detail::PackNamedFloats(isolate, Detail::StatNames(), soul != nullptr ? soul->relativeStats.data() : nullptr, KCDC::Shared::Game::HumanSoulState::kStatCount); }, v8pp::metadata::property_docs("SoulStats", "The same three attributes as the engine's relative values, which is what its own modifiers are expressed in."));
        cls.property("skills", [](const T &self, v8::Isolate *isolate) { const KCDC::Shared::Game::HumanSoulState *soul = Detail::Soul(self); return Detail::PackNamedFloats(isolate, Detail::SkillNames(), soul != nullptr ? soul->skills.data() : nullptr, KCDC::Shared::Game::HumanSoulState::kSkillSelectors.size()); }, v8pp::metadata::property_docs("SoulSkills", "The nine combat and survival skills a hit or a draw is evaluated against. Read as a whole rather than one at a time: the snapshot carries them together."));
        cls.property("relativeSkills", [](const T &self, v8::Isolate *isolate) { const KCDC::Shared::Game::HumanSoulState *soul = Detail::Soul(self); return Detail::PackNamedFloats(isolate, Detail::SkillNames(), soul != nullptr ? soul->relativeSkills.data() : nullptr, KCDC::Shared::Game::HumanSoulState::kSkillSelectors.size()); }, v8pp::metadata::property_docs("SoulSkills", "The same nine skills as the engine's relative values."));

        // --- The body in the world ---

        cls.property("velocity", [](const T &self) { const KCDC::Shared::Entities::HumanEntity *human = self.ResolveHuman(); return human != nullptr ? Framework::Scripting::Builtins::Vector3(human->velocity) : Framework::Scripting::Builtins::Vector3(); }, v8pp::metadata::property_docs("Vector3", "On the owning machine, the velocity the body's own animation was driven by this frame, not the one its physics settled on. Everywhere else, measured from successive replicated positions."));
        cls.property("lookDirection", [](const T &self) { const KCDC::Shared::Entities::HumanEntity *human = self.ResolveHuman(); return human != nullptr && human->headLookReady ? Framework::Scripting::Builtins::Vector3(human->headLookDirection) : Framework::Scripting::Builtins::Vector3(); }, v8pp::metadata::property_docs("Vector3", "World-space direction the head and eyes are turned towards. Zero while the body has reported none."));
        cls.property("inAir", [](const T &self) { const KCDC::Shared::Entities::HumanEntity *human = self.ResolveHuman(); return human != nullptr && human->inAir; }, v8pp::metadata::property_docs("boolean", "Whether the body is off the ground -- fallen or mid-jump -- as its own physics reports it, debounced past the flicker a stair step causes."));
        cls.property(
            "crouched",
            [](const T &self) {
                const KCDC::Shared::Entities::HumanEntity *human = self.ResolveHuman();
                return human != nullptr && human->input.crouch;
            },
            v8pp::metadata::property_docs("boolean", "Whether this player is crouched, as their own game's crouch action reports it. False once their body is gone."));
        cls.property("physicsProfile", [](const T &self) { const KCDC::Shared::Game::HumanSoulState *soul = Detail::Soul(self); return static_cast<std::uint32_t>(soul != nullptr ? soul->physicsProfile : KCDC::Shared::Game::HumanSoulState::kUnknownPhysicsProfile); }, v8pp::metadata::property_docs("number", "The ragdoll physics profile the body is in, or 255 when it has none to report."));

        // --- Fighting ---

        cls.property("fistsUp", [](const T &self) { const KCDC::Shared::Entities::HumanEntity *human = self.ResolveHuman(); return human != nullptr && human->fistsUp; }, v8pp::metadata::property_docs("boolean", "Whether this player has their fists -- or their weapon -- up. Stays true after the arm comes down, which is how the engine holds it."));
        cls.property("guard", [](const T &self) { const KCDC::Shared::Entities::HumanEntity *human = self.ResolveHuman(); return static_cast<std::uint32_t>(human != nullptr ? human->guardRequest : 0); }, v8pp::metadata::property_docs("number", "The arm guard in the engine's own levels: 0 arms down, 1 the guard a player holds."));
        cls.property("combatZone", [](const T &self) { const KCDC::Shared::Entities::HumanEntity *human = self.ResolveHuman(); return static_cast<std::int32_t>(human != nullptr ? human->combatZone : -1); }, v8pp::metadata::property_docs("number", "Which direction of the combat star this player is aiming at, as a row of the game's zone table, or -1 when they are aiming at none."));

        // --- What they are carrying ---

        cls.property("rightHandItem", [](const T &self) { const KCDC::Shared::Entities::HumanEntity *human = self.ResolveHuman(); return human != nullptr ? Detail::FormatItemClass(human->handItemClass[KCDC::Shared::Entities::HumanEntity::kRightHand]) : std::string(); }, v8pp::metadata::property_docs("string", "Item class drawn in the right hand as 32 hex digits, or an empty string when the hand is empty. A class rather than an item: no engine-local identifier crosses the wire."));
        cls.property("leftHandItem", [](const T &self) { const KCDC::Shared::Entities::HumanEntity *human = self.ResolveHuman(); return human != nullptr ? Detail::FormatItemClass(human->handItemClass[KCDC::Shared::Entities::HumanEntity::kLeftHand]) : std::string(); }, v8pp::metadata::property_docs("string", "Item class drawn in the left hand as 32 hex digits, or an empty string when the hand is empty."));
        cls.property("equipment", [](const T &self) {
            std::vector<std::string> worn;
            const KCDC::Shared::Entities::HumanEntity *human = self.ResolveHuman();
            if (human != nullptr) {
                // The wire packs the classes back to back with no separator: a fixed stride rather than something to parse.
                for (std::size_t offset = 0; offset + 32 <= human->equipment.size(); offset += 32) {
                    worn.push_back(human->equipment.substr(offset, 32));
                }
            }
            return worn;
        }, v8pp::metadata::property_docs("string[]", "Item classes this player is wearing, each as 32 hex digits. What they actually have on rather than a preset, so a bare body reads as an empty array."));
        cls.property(
            "visorClosed",
            [](const T &self) {
                const KCDC::Shared::Entities::HumanEntity *human = self.ResolveHuman();
                return human != nullptr && human->visorClosed;
            },
            v8pp::metadata::property_docs("boolean", "Whether this player has a helmet visor down, as their own game reports it. False when they wear no helmet with a visor, and once their body is gone. There is no setter: the player raises and lowers it in their own game."));
    }
} // namespace KCDC::Scripting
