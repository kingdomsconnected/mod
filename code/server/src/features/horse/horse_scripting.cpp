#include "horse_scripting.h"

#include "core/scripting/script_context.h"
#include "core/scripting/wrap.h"
#include "features/player/player_scripting.h"
#include "shared/scripting/rotation_arg.h"

#include "features/horse/horse_navigation.h"
#include "features/horse/horse_verbs.h"
#include "features/player/player_roster.h"

#include "shared/game/buff_catalog.h"
#include "shared/game/cry_guid_text.h"
#include "shared/game/custom_buff_registry.h"
#include "shared/game/horse_catalog.h"
#include "shared/scripting/catalog.h"
#include "shared/scripting/npc_options.h"
#include "shared/scripting/option_field.h"

#include <core_modules.h>
#include <networking/replication/replication_manager.h>
#include <scripting/v8pp_types.h>

#include <v8pp/convert.hpp>

#include <cstdint>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace KCDC::Scripting {

    namespace {
        // The shape the spawn and destroy events have: one subject, nothing else.
        void EmitHorseEvent(const char *eventName, std::uint64_t horseNetworkId) {
            EmitReservedEvent(eventName, [horseNetworkId](v8::Isolate *isolate) {
                std::vector<v8::Local<v8::Value>> args;
                args.push_back(WrapHorse(isolate, horseNetworkId));
                return args;
            });
        }

        // Both saddle events: the animal, then whoever got on or off it.
        void EmitHorseRiderEvent(const char *eventName, std::uint64_t horseNetworkId, std::uint64_t playerNetworkId) {
            EmitReservedEvent(eventName, [horseNetworkId, playerNetworkId](v8::Isolate *isolate) {
                std::vector<v8::Local<v8::Value>> args;
                args.push_back(WrapHorse(isolate, horseNetworkId));
                args.push_back(WrapPlayer(isolate, playerNetworkId));
                return args;
            });
        }

        // The shape `gear` has in every signature that carries one.
        constexpr const char *kGearType      = "{ saddle: string | null; head: string | null; torso: string | null; shoe: string | null }";
        constexpr const char *kGearInputType = "string | { saddle?: string | null; head?: string | null; torso?: string | null; shoe?: string | null }";

        // A gear set as scripts see it: one key per slot, the item class name or null.
        v8::Local<v8::Object> GearToObject(v8::Isolate *isolate, const Shared::Game::HorseGear &gear) {
            v8::Local<v8::Context> context = isolate->GetCurrentContext();
            v8::Local<v8::Object> object   = v8::Object::New(isolate);
            for (std::size_t i = 0; i < Shared::Game::kHorseGearSlotCount; ++i) {
                const auto slot                           = static_cast<Shared::Game::HorseGearSlot>(i);
                const Shared::Game::HorseGearEntry *entry = gear.At(slot);
                v8::Local<v8::Value> value                = entry != nullptr ? v8pp::to_v8(isolate, std::string(entry->name)).As<v8::Value>() : v8::Null(isolate).As<v8::Value>();
                object->Set(context, v8pp::to_v8(isolate, std::string(Shared::Game::HorseGearSlotName(slot))), value).Check();
            }
            return object;
        }

        // Reads a preset name from `Horse.gearPresets()`, or `{ saddle, head, torso, shoe }` where a missing key or null leaves the slot empty. False with `error` set on an unknown preset or item, one in the wrong slot, or a set the game would not allow.
        bool GearFromObject(v8::Isolate *isolate, v8::Local<v8::Value> value, Shared::Game::HorseGear &out, std::string &error) {
            out = {};
            if (value.IsEmpty() || value->IsNullOrUndefined()) {
                return true;
            }
            if (value->IsString()) {
                const std::string name                          = v8pp::from_v8<std::string>(isolate, value);
                const Shared::Game::HorseGearPresetEntry *preset = Shared::Game::FindHorseGearPreset(name);
                if (preset == nullptr) {
                    error = "'" + name + "' is not a horse gear preset";
                    return false;
                }
                const Shared::Game::HorseGearError presetError = Shared::Game::HorseGearFromPreset(*preset, out);
                if (presetError != Shared::Game::HorseGearError::None) {
                    error = std::string(Shared::Game::HorseGearErrorText(presetError));
                    return false;
                }
                return true;
            }
            if (!value->IsObject()) {
                error = "expected an object keyed by slot";
                return false;
            }

            v8::Local<v8::Context> context = isolate->GetCurrentContext();
            v8::Local<v8::Object> object   = value.As<v8::Object>();
            for (std::size_t i = 0; i < Shared::Game::kHorseGearSlotCount; ++i) {
                const auto slot            = static_cast<Shared::Game::HorseGearSlot>(i);
                const std::string slotName = std::string(Shared::Game::HorseGearSlotName(slot));
                v8::Local<v8::Value> item;
                if (!object->Get(context, v8pp::to_v8(isolate, slotName)).ToLocal(&item) || item->IsNullOrUndefined()) {
                    continue;
                }
                if (!item->IsString()) {
                    error = slotName + " must be an item name or null";
                    return false;
                }
                const std::string query                   = v8pp::from_v8<std::string>(isolate, item);
                const Shared::Game::HorseGearEntry *entry = Shared::Game::FindHorseGear(query);
                if (entry == nullptr) {
                    error = "'" + query + "' is not horse gear";
                    return false;
                }
                if (entry->slot != slot) {
                    error = "'" + query + "' goes in the " + std::string(Shared::Game::HorseGearSlotName(entry->slot)) + " slot, not " + slotName;
                    return false;
                }
                out.At(slot) = entry;
            }

            const Shared::Game::HorseGearError validation = Shared::Game::ValidateHorseGear(out);
            if (validation != Shared::Game::HorseGearError::None) {
                error = std::string(Shared::Game::HorseGearErrorText(validation));
                return false;
            }
            return true;
        }

        // The handle a prototype function was called on, or nullptr.
        Horse *Self(const v8::FunctionCallbackInfo<v8::Value> &info) {
            return v8pp::class_<Horse>::unwrap_object(info.GetIsolate(), info.This());
        }

        struct NavigationOptions {
            Shared::Game::NpcSpeed speed = Shared::Game::NpcSpeed::Walk;
            float radius                 = 1.5F;
            float waitSeconds            = 0.0F;
            bool loop                    = true;
        };

        NavigationOptions ReadNavigationOptions(const v8::FunctionCallbackInfo<v8::Value> &info) {
            NavigationOptions options;
            if (info.Length() < 2 || info[1]->IsNullOrUndefined()) {
                return options;
            }
            if (!info[1]->IsObject()) {
                throw std::runtime_error("horse navigation options must be an object");
            }
            auto *isolate = info.GetIsolate();
            auto object   = info[1].As<v8::Object>();
            auto speed    = OptionField(isolate, object, "speed");
            if (!speed.IsEmpty()) {
                const auto name = v8pp::from_v8<std::string>(isolate, speed);
                options.speed   = Shared::Game::NpcSpeedFromName(name);
                if (options.speed == Shared::Game::NpcSpeed::Count) {
                    throw std::runtime_error("horse navigation speed must be walk, jog, or run");
                }
            }
            auto radius = OptionField(isolate, object, "radius");
            auto wait   = OptionField(isolate, object, "waitSeconds");
            auto loop   = OptionField(isolate, object, "loop");
            if (!radius.IsEmpty()) {
                options.radius = v8pp::from_v8<float>(isolate, radius);
            }
            if (!wait.IsEmpty()) {
                options.waitSeconds = v8pp::from_v8<float>(isolate, wait);
            }
            if (!loop.IsEmpty()) {
                options.loop = v8pp::from_v8<bool>(isolate, loop);
            }
            return options;
        }

        void JS_MoveTo(const v8::FunctionCallbackInfo<v8::Value> &info) {
            try {
                auto *self = Self(info);
                if (self == nullptr || info.Length() < 1) {
                    info.GetReturnValue().Set(false);
                    return;
                }
                const auto point   = v8pp::convert<glm::vec3>::from_v8(info.GetIsolate(), info[0]);
                const auto options = ReadNavigationOptions(info);
                info.GetReturnValue().Set(KCDC::Features::Horse::HorseNavigation::Get().MoveTo(self->ResolveHorse(), point, options.speed, options.radius));
            }
            catch (const std::exception &error) {
                info.GetIsolate()->ThrowError(v8pp::to_v8(info.GetIsolate(), error.what()));
            }
        }

        void JS_Patrol(const v8::FunctionCallbackInfo<v8::Value> &info) {
            try {
                auto *self = Self(info);
                if (self == nullptr || info.Length() < 1 || !info[0]->IsArray()) {
                    info.GetReturnValue().Set(false);
                    return;
                }
                auto route = info[0].As<v8::Array>();
                if (route->Length() > KCDC::Features::Npc::NpcOrder::kMaxPatrolPoints) {
                    info.GetReturnValue().Set(false);
                    return;
                }
                std::vector<glm::vec3> points;
                points.reserve(route->Length());
                for (std::uint32_t i = 0; i < route->Length(); ++i) {
                    v8::Local<v8::Value> point;
                    if (!route->Get(info.GetIsolate()->GetCurrentContext(), i).ToLocal(&point)) {
                        return;
                    }
                    points.push_back(v8pp::convert<glm::vec3>::from_v8(info.GetIsolate(), point));
                }
                const auto options = ReadNavigationOptions(info);
                info.GetReturnValue().Set(KCDC::Features::Horse::HorseNavigation::Get().Patrol(self->ResolveHorse(), points, options.loop, options.speed, options.waitSeconds));
            }
            catch (const std::exception &error) {
                info.GetIsolate()->ThrowError(v8pp::to_v8(info.GetIsolate(), error.what()));
            }
        }

        void JS_Hold(const v8::FunctionCallbackInfo<v8::Value> &info) {
            if (auto *self = Self(info)) {
                KCDC::Features::Horse::HorseNavigation::Get().Hold(self->ResolveHorse());
            }
        }

        // JS: horse.destroy()
        void JS_Destroy(const v8::FunctionCallbackInfo<v8::Value> &info) {
            if (auto *self = Self(info)) {
                KCDC::Features::Horse::Horses::Destroy(self->ResolveHorse());
            }
        }

        // JS: horse.giveTo(player)
        void JS_GiveTo(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);

            auto *self   = Self(info);
            auto *player = info.Length() >= 1 ? v8pp::class_<Framework::Scripting::Builtins::Entity>::unwrap_object(isolate, info[0]) : nullptr;
            if (self == nullptr) {
                return;
            }
            if (player == nullptr && (info.Length() < 1 || !info[0]->IsNull())) {
                isolate->ThrowError(v8pp::to_v8(isolate, "Horse.giveTo: expected (player | null)"));
                return;
            }
            if (!KCDC::Features::Horse::Horses::SetOwner(self->ResolveHorse(), player != nullptr ? player->GetId() : 0)) {
                isolate->ThrowError(v8pp::to_v8(isolate, "Horse.giveTo: not a connected player"));
            }
        }

        // JS: horse.rearAndThrowDown() -> boolean
        void JS_RearAndThrowDown(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *self = Self(info);
            info.GetReturnValue().Set(self != nullptr && KCDC::Features::Horse::Horses::RearAndThrowDown(self->ResolveHorse()));
        }

        // JS: horse.pullDownRider(attacker) -> boolean
        void JS_PullDownRider(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);

            auto *self     = Self(info);
            auto *attacker = info.Length() >= 1 ? v8pp::class_<Framework::Scripting::Builtins::Entity>::unwrap_object(isolate, info[0]) : nullptr;
            if (self == nullptr || attacker == nullptr) {
                info.GetReturnValue().Set(false);
                return;
            }

            info.GetReturnValue().Set(KCDC::Features::Horse::Horses::PullDownRider(self->ResolveHorse(), attacker->GetId()));
        }

        // JS: horse.dismountRider() -> Player | null
        void JS_DismountRider(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);

            auto *self                         = Self(info);
            const std::uint64_t riderNetworkId = self != nullptr ? KCDC::Features::Horse::Horses::TakeRiderOff(self->ResolveHorse(), true) : 0;
            if (riderNetworkId == 0) {
                info.GetReturnValue().SetNull();
                return;
            }
            info.GetReturnValue().Set(WrapPlayer(isolate, riderNetworkId));
        }

        // JS: horse.kill() -> boolean
        void JS_Kill(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *self = Self(info);
            info.GetReturnValue().Set(self != nullptr && KCDC::Features::Horse::Horses::Kill(self->ResolveHorse()));
        }

        // JS: horse.revive() -> boolean
        void JS_Revive(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *self = Self(info);
            info.GetReturnValue().Set(self != nullptr && KCDC::Features::Horse::Horses::Revive(self->ResolveHorse()));
        }

        // JS: horse.wake() -> boolean
        void JS_Wake(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *self = Self(info);
            info.GetReturnValue().Set(self != nullptr && KCDC::Features::Horse::Horses::Wake(self->ResolveHorse()));
        }

        const Shared::Game::BuffEntry *BuffArgument(const v8::FunctionCallbackInfo<v8::Value> &info, const char *verb) {
            v8::Isolate *isolate = info.GetIsolate();
            if (info.Length() < 1 || !info[0]->IsString()) {
                isolate->ThrowError(v8pp::to_v8(isolate, std::string("Horse.") + verb + ": expected (buff)"));
                return nullptr;
            }
            // A GUID or a table name, and the registered id of a custom effect: the same lookup the player verbs use.
            const std::string query = v8pp::from_v8<std::string>(isolate, info[0]);
            const auto *buff        = KCDC::Features::Player::Players::ResolveBuff(query);
            if (buff == nullptr) {
                isolate->ThrowError(v8pp::to_v8(isolate, std::string("Horse.") + verb + ": '" + query + "' is not a buff; `Buffs.find` resolves a GUID, a table name or a registered id"));
            }
            return buff;
        }

        // JS: horse.addBuff(buff) -> boolean
        void JS_AddBuff(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::HandleScope handleScope(info.GetIsolate());
            auto *self       = Self(info);
            const auto *buff = BuffArgument(info, "addBuff");
            info.GetReturnValue().Set(self != nullptr && buff != nullptr && KCDC::Features::Horse::Horses::AddBuff(self->ResolveHorse(), *buff));
        }

        // JS: horse.removeBuff(buff) -> boolean
        void JS_RemoveBuff(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::HandleScope handleScope(info.GetIsolate());
            auto *self       = Self(info);
            const auto *buff = BuffArgument(info, "removeBuff");
            info.GetReturnValue().Set(self != nullptr && buff != nullptr && KCDC::Features::Horse::Horses::RemoveBuff(self->ResolveHorse(), *buff));
        }

        // JS: horse.hasBuff(buff) -> boolean
        void JS_HasBuff(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::HandleScope handleScope(info.GetIsolate());
            auto *self       = Self(info);
            const auto *buff = BuffArgument(info, "hasBuff");
            info.GetReturnValue().Set(self != nullptr && buff != nullptr && KCDC::Features::Horse::Horses::HasBuff(self->ResolveHorse(), *buff));
        }

        // JS: horse.clearBuffs() -> boolean
        void JS_ClearBuffs(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::HandleScope handleScope(info.GetIsolate());
            auto *self = Self(info);
            info.GetReturnValue().Set(self != nullptr && KCDC::Features::Horse::Horses::ClearBuffs(self->ResolveHorse()));
        }

        // JS: horse.setGear(gear) -> boolean
        void JS_SetGear(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);

            auto *self = Self(info);
            Shared::Game::HorseGear gear;
            std::string error;
            if (self == nullptr || !GearFromObject(isolate, info.Length() >= 1 ? info[0] : v8::Undefined(isolate).As<v8::Value>(), gear, error)) {
                isolate->ThrowError(v8pp::to_v8(isolate, "Horse.setGear: " + (error.empty() ? std::string("expected (gear)") : error)));
                return;
            }
            info.GetReturnValue().Set(KCDC::Features::Horse::Horses::SetGear(self->ResolveHorse(), gear));
        }

        // JS: horse.equipGear(item) -> boolean
        void JS_EquipGear(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);

            auto *self = Self(info);
            if (self == nullptr || info.Length() < 1 || !info[0]->IsString()) {
                isolate->ThrowError(v8pp::to_v8(isolate, "Horse.equipGear: expected (item)"));
                return;
            }

            const Shared::Game::HorseGearEntry *entry = Shared::Game::FindHorseGear(v8pp::from_v8<std::string>(isolate, info[0]));
            auto *horse                               = self->ResolveHorse();
            if (entry == nullptr || horse == nullptr) {
                info.GetReturnValue().Set(false);
                return;
            }

            Shared::Game::HorseGear gear = KCDC::Features::Horse::Horses::GearOf(horse);
            gear.At(entry->slot)         = entry;
            info.GetReturnValue().Set(KCDC::Features::Horse::Horses::SetGear(horse, gear));
        }

        // JS: horse.removeGear(slot) -> boolean
        void JS_RemoveGear(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);

            auto *self = Self(info);
            Shared::Game::HorseGearSlot slot {};
            if (self == nullptr || info.Length() < 1 || !info[0]->IsString() || !Shared::Game::ParseHorseGearSlot(v8pp::from_v8<std::string>(isolate, info[0]), slot)) {
                isolate->ThrowError(v8pp::to_v8(isolate, "Horse.removeGear: expected (slot) -- 'saddle', 'head', 'torso' or 'shoe'"));
                return;
            }

            auto *horse                  = self->ResolveHorse();
            Shared::Game::HorseGear gear = KCDC::Features::Horse::Horses::GearOf(horse);
            if (horse == nullptr || gear.At(slot) == nullptr) {
                info.GetReturnValue().Set(false);
                return;
            }

            // A caparison lies on the saddle, so taking the saddle takes it too.
            gear.At(slot) = nullptr;
            if (slot == Shared::Game::HorseGearSlot::Saddle) {
                gear.At(Shared::Game::HorseGearSlot::Torso) = nullptr;
            }
            info.GetReturnValue().Set(KCDC::Features::Horse::Horses::SetGear(horse, gear));
        }

        // JS: Horse.gearItems(slot?) -> string[]
        void JS_GearItems(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate           = info.GetIsolate();
            v8::HandleScope handleScope(isolate);
            v8::Local<v8::Context> context = isolate->GetCurrentContext();

            bool filtered = false;
            Shared::Game::HorseGearSlot slot {};
            if (info.Length() >= 1 && !info[0]->IsNullOrUndefined()) {
                if (!info[0]->IsString() || !Shared::Game::ParseHorseGearSlot(v8pp::from_v8<std::string>(isolate, info[0]), slot)) {
                    isolate->ThrowError(v8pp::to_v8(isolate, "Horse.gearItems: slot must be 'saddle', 'head', 'torso' or 'shoe'"));
                    return;
                }
                filtered = true;
            }

            v8::Local<v8::Array> names = v8::Array::New(isolate);
            std::uint32_t index        = 0;
            for (const Shared::Game::HorseGearEntry &entry : Shared::Game::g_HorseGear) {
                if (!filtered || entry.slot == slot) {
                    names->Set(context, index++, v8pp::to_v8(isolate, std::string(entry.name))).Check();
                }
            }
            info.GetReturnValue().Set(names);
        }

        // JS: Horse.gearPresets() -> string[]
        void JS_GearPresets(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate           = info.GetIsolate();
            v8::HandleScope handleScope(isolate);
            v8::Local<v8::Context> context = isolate->GetCurrentContext();
            v8::Local<v8::Array> names     = v8::Array::New(isolate);

            std::uint32_t index = 0;
            for (const Shared::Game::HorseGearPresetEntry &preset : Shared::Game::g_HorseGearPresets) {
                names->Set(context, index++, v8pp::to_v8(isolate, std::string(preset.name))).Check();
            }
            info.GetReturnValue().Set(names);
        }

        // JS: Horse.breeds() -> string[]
        void JS_Breeds(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate           = info.GetIsolate();
            v8::HandleScope handleScope(isolate);
            v8::Local<v8::Context> context = isolate->GetCurrentContext();
            v8::Local<v8::Array> names     = v8::Array::New(isolate);

            std::uint32_t index = 0;
            for (const Shared::Game::HorseBreedEntry &breed : Shared::Game::g_HorseBreeds) {
                names->Set(context, index++, v8pp::to_v8(isolate, std::string(breed.name))).Check();
            }
            info.GetReturnValue().Set(names);
        }

        // JS: Horse.spawn(position, rotation, soul, name) -> Horse
        void JS_Spawn(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);

            glm::vec3 position {0.0F};
            glm::quat rotation {1.0F, 0.0F, 0.0F, 0.0F};
            std::string soulGuid;
            std::string name;
            Shared::Game::HorseGear gear;
            bool withGear = false;

            // The glm converters in <scripting/v8pp_types.h> are convert<> specialisations; the generic from_v8<T> would try to unwrap a registered class_<glm::vec3> first and throw, so call them directly.
            if (info.Length() >= 1 && info[0]->IsObject()) {
                position = v8pp::convert<glm::vec3>::from_v8(isolate, info[0]);
            }
            if (info.Length() >= 2) {
                rotation = ParseRotationArg(isolate, info[1], rotation);
            }
            if (info.Length() >= 3 && info[2]->IsString()) {
                soulGuid = v8pp::from_v8<std::string>(isolate, info[2]);
            }
            if (info.Length() >= 4 && info[3]->IsString()) {
                name = v8pp::from_v8<std::string>(isolate, info[3]);
            }
            if (info.Length() >= 5 && !info[4]->IsNullOrUndefined()) {
                std::string error;
                if (!GearFromObject(isolate, info[4], gear, error)) {
                    isolate->ThrowError(v8pp::to_v8(isolate, "Horse.spawn: " + error));
                    return;
                }
                withGear = true;
            }

            auto *horse = KCDC::Features::Horse::Horses::Spawn(soulGuid, position, rotation, name, withGear ? &gear : nullptr);
            if (horse == nullptr) {
                isolate->ThrowError(v8pp::to_v8(isolate, "Horse.spawn: failed to create entity"));
                return;
            }

            info.GetReturnValue().Set(WrapHorse(isolate, horse->GetNetworkID()));
        }

        // JS: Horse.all() -> Horse[]
        void JS_All(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate           = info.GetIsolate();
            v8::HandleScope handleScope(isolate);
            v8::Local<v8::Context> context = isolate->GetCurrentContext();
            v8::Local<v8::Array> horses    = v8::Array::New(isolate);

            std::uint32_t index = 0;
            if (auto *replication = Framework::CoreModules::GetReplication()) {
                replication->ForEach<Shared::Entities::HorseEntity>([&](Shared::Entities::HorseEntity *horse) {
                    horses->Set(context, index++, WrapHorse(isolate, horse->GetNetworkID())).Check();
                });
            }

            info.GetReturnValue().Set(horses);
        }

        // JS: Horse.getById(id) -> Horse | null
        void JS_GetById(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);

            if (info.Length() < 1 || !info[0]->IsNumber()) {
                isolate->ThrowError(v8pp::to_v8(isolate, "Horse.getById: expected (id)"));
                return;
            }

            const std::uint64_t networkId = static_cast<std::uint64_t>(info[0]->IntegerValue(isolate->GetCurrentContext()).FromMaybe(0));
            info.GetReturnValue().Set(WrapHorse(isolate, networkId));
        }
    } // namespace

    Shared::Entities::HorseEntity *Horse::ResolveHorse() const {
        return dynamic_cast<Shared::Entities::HorseEntity *>(Resolve());
    }

    std::string Horse::GetSoul() const {
        auto *horse = ResolveHorse();
        return horse != nullptr ? horse->soulGuid : std::string();
    }

    std::string Horse::GetName() const {
        auto *horse = ResolveHorse();
        return horse != nullptr ? horse->name : std::string();
    }

    void Horse::SetName(const std::string &name) {
        KCDC::Features::Horse::Horses::Rename(ResolveHorse(), name);
    }

    float Horse::GetInventoryCapacity() const {
        auto *horse = ResolveHorse();
        return horse != nullptr ? horse->inventoryCapacity : 0.0F;
    }

    std::uint64_t Horse::GetRiderId() const {
        auto *horse = ResolveHorse();
        return horse != nullptr ? horse->riderNetworkId : 0;
    }

    std::uint64_t Horse::GetOwnerId() const {
        auto *horse = ResolveHorse();
        return horse != nullptr ? horse->ownerNetworkId : 0;
    }

    v8::Local<v8::Value> Horse::GetOwnerHandle(v8::Isolate *isolate) const {
        v8::EscapableHandleScope handleScope(isolate);
        return handleScope.Escape(WrapPlayer(isolate, GetOwnerId()));
    }

    bool Horse::GetDestroyWithOwner() const {
        auto *horse = ResolveHorse();
        return horse == nullptr || horse->destroyWithOwner;
    }

    void Horse::SetDestroyWithOwner(bool destroy) {
        if (auto *horse = ResolveHorse()) {
            horse->destroyWithOwner = destroy;
        }
    }

    float Horse::GetHealth() const {
        auto *horse = ResolveHorse();
        return horse != nullptr ? horse->health : 0.0F;
    }

    void Horse::SetHealth(float value) {
        KCDC::Features::Horse::Horses::SetHealth(ResolveHorse(), value);
    }

    float Horse::GetMaxHealth() const {
        auto *horse = ResolveHorse();
        return horse != nullptr ? horse->maxHealth : 0.0F;
    }

    float Horse::GetStamina() const {
        auto *horse = ResolveHorse();
        return horse != nullptr ? horse->stamina : 0.0F;
    }

    float Horse::GetMaxStamina() const {
        auto *horse = ResolveHorse();
        return horse != nullptr ? horse->maxStamina : 0.0F;
    }

    bool Horse::IsAlive() const {
        auto *horse = ResolveHorse();
        return horse != nullptr && horse->alive;
    }

    bool Horse::IsUnconscious() const {
        auto *horse = ResolveHorse();
        return horse != nullptr && horse->alive && horse->unconscious;
    }

    v8::Local<v8::Value> Horse::GetGearHandle(v8::Isolate *isolate) const {
        v8::EscapableHandleScope handleScope(isolate);
        return handleScope.Escape(GearToObject(isolate, KCDC::Features::Horse::Horses::GearOf(ResolveHorse())));
    }

    v8::Local<v8::Value> Horse::GetBuffsHandle(v8::Isolate *isolate) const {
        v8::EscapableHandleScope handleScope(isolate);
        v8::Local<v8::Context> context = isolate->GetCurrentContext();
        v8::Local<v8::Array> out       = v8::Array::New(isolate);
        std::uint32_t written          = 0;
        for (const Shared::RPC::BuffId &buff : KCDC::Features::Horse::Horses::BuffsOf(ResolveHorse())) {
            std::string name;
            if (const auto *entry = Shared::Game::FindBuffById(buff)) {
                name = std::string(entry->name);
            }
            else if (const auto *custom = Shared::Game::CustomBuffRegistry::Get().Find(Shared::Game::CryGuidToString(buff.low, buff.high))) {
                name = custom->id;
            }
            else {
                name = Shared::Game::CryGuidToString(buff.low, buff.high);
            }
            out->Set(context, written++, v8pp::to_v8(isolate, name)).Check();
        }
        return handleScope.Escape(out);
    }

    bool Horse::IsGearLocked() const {
        auto *horse = ResolveHorse();
        return horse != nullptr && horse->gearLocked;
    }

    void Horse::SetGearLocked(bool locked) {
        if (auto *horse = ResolveHorse()) {
            horse->gearLocked = locked;
        }
    }

    v8::Local<v8::Value> Horse::GetRiderHandle(v8::Isolate *isolate) const {
        v8::EscapableHandleScope handleScope(isolate);
        return handleScope.Escape(WrapPlayer(isolate, GetRiderId()));
    }

    std::string Horse::ToString() const {
        std::ostringstream ss;
        ss << "Horse{ id: " << GetId() << ", name: '" << GetName() << "', soul: '" << GetSoul() << "', owner: " << GetOwnerId() << ", health: " << GetHealth() << "/" << GetMaxHealth() << ", alive: " << IsAlive() << ", capacity: " << GetInventoryCapacity() << ", rider: " << GetRiderId() << " }";
        return ss.str();
    }

    std::string Horse::GetNavigationStatus() const {
        return KCDC::Features::Horse::HorseNavigation::Get().Status(ResolveHorse());
    }

    v8pp::class_<Horse> &Horse::GetClass(v8::Isolate *isolate) {
        auto &_class = _classes[isolate];
        if (!_class) {
            // v8pp's inherit<Entity> needs the framework Entity registered first.
            Framework::Scripting::Builtins::Entity::GetClass(isolate);

            _class = std::make_unique<v8pp::class_<Horse>>(isolate, ServerCatalog(), "Horse", "Replicated KCD2 horse handle.");
            _class->auto_wrap_objects(true);
            _class->inherit<Framework::Scripting::Builtins::Entity>()
                .ctor<std::uint64_t>(v8pp::metadata::docs("void", {v8pp::metadata::param("id", "number", false, "Network entity identifier.")},
                    "Creates a script wrapper for an existing horse with this ID; use Horse.spawn() to spawn one."))
                .function("toString", &Horse::ToString,
                    v8pp::metadata::docs("string", {}, "Formats this horse handle for logging and debugging.", "The horse ID, its soul, and its rider."));

            _class->prototype_function("destroy", &JS_Destroy, v8pp::metadata::docs("void", {}, "Despawns this horse on every client after emitting horseDestroy."));
            _class->property("navigationStatus", &Horse::GetNavigationStatus, v8pp::metadata::property_docs("string", "The current route's status: idle, running, reached, blocked, or failed."));
            _class->prototype_function("moveTo", &JS_MoveTo,
                v8pp::metadata::docs("boolean",
                    {v8pp::metadata::param("position", "Vector3", false, "Destination in world coordinates."), v8pp::metadata::param("options", kNpcMoveOptionsType, true, "Defaults to walking and a 1.5 metre arrival radius.")},
                    "Routes an unridden horse over the server navigation mesh. Mounting cancels the order. Returns false without a mesh, for a dead or mounted horse, or invalid input."));
            _class->prototype_function("patrol", &JS_Patrol,
                v8pp::metadata::docs("boolean",
                    {v8pp::metadata::param("points", "Vector3[]", false, "One to 256 waypoints."), v8pp::metadata::param("options", "{ speed?: 'walk' | 'jog' | 'run'; loop?: boolean; waitSeconds?: number }", true, "Defaults to walking, looping, and no wait.")},
                    "Patrols using the NPC route state machine. Requires a loaded mesh and an alive, unridden horse. Mounting cancels the patrol."));
            _class->prototype_function("hold", &JS_Hold, v8pp::metadata::docs("void", {}, "Cancels navigation and holds an unridden horse where it stands. Does not take control from a rider."));
            _class->prototype_function("giveTo", &JS_GiveTo,
                v8pp::metadata::docs("void", {v8pp::metadata::param("player", "Player | null", false, "The horse's new owner, or null to leave it ownerless.")},
                    "Makes this horse another player's. Every client makes it that player's horse in the game's own terms -- theirs is never a stolen horse -- and `horseOwnerChanged` follows. Ownership decides nothing about who may ride it: that is `horseMounting`'s to refuse. Throws when `player` is not a connected player."));
            _class->prototype_function("rearAndThrowDown", &JS_RearAndThrowDown,
                v8pp::metadata::docs("boolean", {}, "Rears the horse on every client so it throws its rider off. The game's own animation, not a pose.", "True when the request went out; false when nobody is riding it."));
            _class->prototype_function("pullDownRider", &JS_PullDownRider,
                v8pp::metadata::docs("boolean", {v8pp::metadata::param("attacker", "Player", false, "The player dragging the rider out of the saddle.")},
                    "Has a player pull this horse's rider down, on every client.", "True when the request went out; false when nobody is riding it."));
            _class->prototype_function("dismountRider", &JS_DismountRider,
                v8pp::metadata::docs("Player | null", {}, "Takes whoever is in the saddle out of it: their own client is told to get off, and `horseDismount` is raised.",
                    "The player taken off, or null when the saddle was already empty."));
            _class->prototype_function("kill", &JS_Kill,
                v8pp::metadata::docs("boolean", {}, "Kills this horse on the client running it, the way any other death happens there: the body falls, anyone riding it comes off, and `horseDeath` follows with a null killer and the reason `scripted`.",
                    "True when the horse was alive; false when it was already dead."));
            _class->prototype_function("revive", &JS_Revive,
                v8pp::metadata::docs("boolean", {}, "Brings a dead horse back where it lies, at its full health.", "True when the horse was dead; false when it was alive."));
            _class->prototype_function("wake", &JS_Wake,
                v8pp::metadata::docs("boolean", {},
                    "Brings a knocked-out horse round now, the way the game cuts a knockout short: the client running it ends the knockout and the horse gets up on every client. Its health is left as it is -- a knockout in place of a death leaves 1. The request reaches that client a moment later, so `unconscious` stays true until it reports the horse up.",
                    "True when the request went out; false when the horse is not knocked out, is dead, or no client is running it."));

            _class->prototype_function("setGear", &JS_SetGear,
                v8pp::metadata::docs("boolean", {v8pp::metadata::param("gear", kGearInputType, false, "A preset from `Horse.gearPresets()` (`horse_noble05`), or item class names or GUIDs by slot from `Horse.gearItems()`, where a missing key or null leaves that slot empty; `{}` is a bare horse.")},
                    "Dresses this horse in exactly this gear on every client, the one running it included: what it wore before is taken off. Throws on an item that is not horse gear, one keyed under the wrong slot, or a caparison (`torso`) with no saddle under it. Not subject to `gearLocked` or `horseGearChanging`, which govern players; `horseGearChanged` follows with a null player.",
                    "True when the gear was written; false when the horse is gone."));
            _class->prototype_function("equipGear", &JS_EquipGear,
                v8pp::metadata::docs("boolean", {v8pp::metadata::param("item", "string", false, "An item class name or GUID from `Horse.gearItems()`.")},
                    "Puts one piece of gear in its own slot, replacing what was there and keeping the rest.",
                    "True when it was put on; false for an item that is not horse gear, a caparison on a horse with no saddle, or a horse that is gone."));
            _class->prototype_function("removeGear", &JS_RemoveGear,
                v8pp::metadata::docs("boolean", {v8pp::metadata::param("slot", "'saddle' | 'head' | 'torso' | 'shoe'", false, "The slot to empty.")},
                    "Takes one piece of gear off. Removing the saddle removes the caparison too, since it lies on the saddle.", "True when something was taken off; false when the slot was already empty."));
            _class->prototype_function("addBuff", &JS_AddBuff,
                v8pp::metadata::docs("boolean", {v8pp::metadata::param("buff", "string", false, "Buff GUID, the exact name the game's own buff tables use, or the id of one registered with `Buffs.register`.")},
                    "Puts a status effect on this horse, on every client: the one running it, every observer's copy and whoever joins later. A horse's stats are its soul's derived stats and the game itself moves them with buffs -- horseshoes carry `rsb+0.04`, the race boosts `mst+30,rms+0.01`, horse plate `courage+5,mst+10` -- so this is how a server changes a horse's speed (`runSpeed`, `moveSpeed`), stamina (`stamina`), courage (`courage`) or carrying capacity (`carryCapacity`); `Buffs.register` builds an effect from those modifiers. The list is state rather than a one-off order: a copy that loses the effect, or a body re-spawned after a level change, is given it again. Throws for a name that is not a buff.",
                    "True when it is on the horse's list, already or now; false for a horse that is gone or one already carrying sixteen."));
            _class->prototype_function("removeBuff", &JS_RemoveBuff,
                v8pp::metadata::docs("boolean", {v8pp::metadata::param("buff", "string", false, "Buff GUID, table name or registered id.")},
                    "Takes one of the server's effects off this horse, on every client. Effects the game put on the horse itself are not on the list and are left alone.", "True when it was on the list; false otherwise."));
            _class->prototype_function("hasBuff", &JS_HasBuff,
                v8pp::metadata::docs("boolean", {v8pp::metadata::param("buff", "string", false, "Buff GUID, table name or registered id.")},
                    "Whether this server has put that effect on the horse. The server's own record, so it answers at once; it says nothing about effects the game gave the horse.", "True when it is on the list."));
            _class->prototype_function("clearBuffs", &JS_ClearBuffs,
                v8pp::metadata::docs("boolean", {}, "Takes every effect this server put on the horse off again.", "True when there was something to take off."));
            _class->property("buffs", [](const Horse &self, v8::Isolate *isolate) { return self.GetBuffsHandle(isolate); },
                v8pp::metadata::property_docs("string[]", "The effects this server has put on the horse, as `addBuff` names them: the table name of a native effect, the registered id of a custom one, in the order they were added."));
            _class->property("gear", [](const Horse &self, v8::Isolate *isolate) { return self.GetGearHandle(isolate); },
                v8pp::metadata::property_docs(kGearType,
                    "What this horse wears, by slot: `saddle` (the saddlebags are part of it, and so is the carrying capacity they add), `head` (a bridle or chanfron), `torso` (a caparison), `shoe`. Item class names, null for an empty slot. A spawned horse wears the game's own tack for it unless `Horse.spawn` said otherwise. The gear changes the horse's stats as well as its look."));
            _class->property("gearLocked", &Horse::IsGearLocked, &Horse::SetGearLocked,
                v8pp::metadata::property_docs("boolean", "While true, players' own changes to this horse's gear are refused and put back, before `horseGearChanging` is asked. Scripts can still change it. False by default."));
            _class->property("soul", &Horse::GetSoul, v8pp::metadata::property_docs("string", "GUID of the soul this horse was spawned against, which decides its appearance."));
            _class->property("name", &Horse::GetName, &Horse::SetName,
                v8pp::metadata::property_docs("string", "Name every client shows for this horse. Assignment renames it on all of them; empty means the name its soul carries stands. A rider keeps the name it mounted with until it dismounts."));
            _class->property("inventoryCapacity", &Horse::GetInventoryCapacity,
                v8pp::metadata::property_docs("number", "What this horse can carry, in the game's own weight units, or 0 before any client has reported it. Derived by the engine, so read-only."));
            _class->property("riderId", &Horse::GetRiderId, v8pp::metadata::property_docs("number", "Network ID of the player in the saddle, or 0 when the horse is riderless."));
            _class->property("rider", [](const Horse &self, v8::Isolate *isolate) { return self.GetRiderHandle(isolate); },
                v8pp::metadata::property_docs("Player | null", "The player in the saddle, or null when the horse is riderless."));
            _class->property("mounted", &Horse::IsMounted, v8pp::metadata::property_docs("boolean", "Whether a player is currently riding this horse."));
            _class->property("ownerId", &Horse::GetOwnerId, v8pp::metadata::property_docs("number", "Network ID of the player this horse belongs to, or 0 when it is nobody's. Not necessarily its rider."));
            _class->property("owner", [](const Horse &self, v8::Isolate *isolate) { return self.GetOwnerHandle(isolate); },
                v8pp::metadata::property_docs("Player | null", "The player this horse belongs to, or null when it is nobody's. Set with `giveTo`."));
            _class->property("destroyWithOwner", &Horse::GetDestroyWithOwner, &Horse::SetDestroyWithOwner,
                v8pp::metadata::property_docs("boolean",
                    "Whether this horse is despawned when its owner disconnects. True by default. Set it to false to keep the horse in the world: it is left ownerless instead (`horseOwnerChanged` with a null player) until `giveTo` hands it to someone. Saving it across server restarts is the script's own business."));
            _class->property("health", &Horse::GetHealth, &Horse::SetHealth,
                v8pp::metadata::property_docs("number",
                    "Health, as the client running this horse last reported it off the animal's own soul -- 0 before any client has. Assigning sets it on that client, clamped to `maxHealth`; 0 kills. A dead horse ignores the write: `revive` is what brings one back."));
            _class->property("maxHealth", &Horse::GetMaxHealth, v8pp::metadata::property_docs("number", "The most health this horse can have, from its soul; 0 before any client has run it."));
            _class->property("stamina", &Horse::GetStamina,
                v8pp::metadata::property_docs("number", "Stamina, as its client last reported it -- the bar the game draws for a ridden horse, spent by galloping. Read-only: the game runs it."));
            _class->property("maxStamina", &Horse::GetMaxStamina, v8pp::metadata::property_docs("number", "The most stamina this horse can have; 0 before any client has run it."));
            _class->property("alive", &Horse::IsAlive,
                v8pp::metadata::property_docs("boolean", "False once the horse has died. A dead horse is still an entity: its body stays where it fell, nobody can ride it, and `revive` or `destroy` are the ways out."));
            _class->property("unconscious", &Horse::IsUnconscious,
                v8pp::metadata::property_docs("boolean",
                    "True while the client running this horse reports it knocked out: alive, but lying where it fell. The game knocks a horse out rather than killing it when it holds the horse immortal, as it holds a player's own horse. The horse comes round by itself when the knockout wears off, some minutes later, as in the game, or at once with `wake`. False for a dead horse, and before any client has run this one."));
        }
        return *_class;
    }

    void Horse::UnregisterIsolate(v8::Isolate *isolate) {
        _classes.erase(isolate);
    }

    void Horse::Register(v8::Isolate *isolate, v8::Local<v8::Object> global) {
        if (isolate == nullptr || global.IsEmpty()) {
            return;
        }

        auto &events = ServerCatalog().data_type("EventMap");
        events.add_property("horseIntentDone", "[horse: Horse, status: string]", "A move or patrol leg reached its destination, was blocked, or failed. Intermediate path corners do not emit this event.");
        events.add_property("horseSpawn", "[horse: Horse]", "Dispatched immediately after a horse is created and replicated, whether by `Horse.spawn`, the `/horse` command, or anything else.");
        events.add_property("horseDestroy", "[horse: Horse]", "Dispatched while a horse is being despawned, including the despawn its owner's disconnect implies. The handle still resolves, so its owner and name can be read one last time.");
        events.add_property("horseOwnerChanged", "[horse: Horse, player: Player | null]", "Dispatched after a horse is handed to another player, or left ownerless -- by `giveTo`, or by its owner disconnecting while `destroyWithOwner` is false.");
        events.add_property("horseMount", "[horse: Horse, player: Player | null]", "Dispatched after a player climbs into a saddle and the horse's authority has been handed to their client.");
        events.add_property("horseDismount", "[horse: Horse, player: Player | null]", "Dispatched after a rider leaves a saddle, including the dismount a disconnect implies, the one a destroyed or dead horse forces, and the one a rider's own death forces.");
        events.add_property("horseMounting", "[horse: Horse, player: Player]",
            "Dispatched when a player has climbed into a saddle, before the server accepts it -- the place to decide who may ride which horse. Return `false` from a handler and the ride is refused: the player's client is told to get back off, and no `horseMount` follows. Handlers run synchronously, so the decision cannot wait on anything awaited.\n\nThe player's own game has already started the mount when this runs, so a refusal plays the get-off.");
        events.add_property("horseDamage", std::string("[horse: Horse, attacker: Player | null, amount: number, reason: ") + DamageReasonType() + "]",
            "Dispatched when health comes off a horse: a blade, an arrow, a fall. The client running the horse reports it, because its copy of the animal is the one whose soul counts -- a blow resolved on the attacker's machine is handed to it first -- so `amount` is what was really taken. `attacker` is the player who dealt it, or null.");
        events.add_property("horseDeath", std::string("[horse: Horse, killer: Player | null, reason: ") + DamageReasonType() + "]",
            "Dispatched once when a horse dies, whatever killed it: its body falls where it stood, and anyone in the saddle has already been taken off (with a `horseDismount`). `killer` is the player whose blow took the last of the health, or null. It follows the `horseDamage` of the blow that caused it: the client running the horse reports both, in that order.");

        events.add_property("horseGearChanging", std::string("[horse: Horse, player: Player | null, gear: ") + kGearType + "]",
            "Dispatched when a player has changed a horse's gear -- in the game's own horse inventory screen -- before the server accepts it. `gear` is what the horse would wear. Return `false` from a handler and the change is refused: every copy of the horse, the player's own included, is dressed back in what it wore before. Not dispatched while `gearLocked` is set, which refuses first, nor for a script's own `setGear`. Handlers run synchronously.");
        events.add_property("horseGearChanged", std::string("[horse: Horse, player: Player | null, gear: ") + kGearType + "]",
            "Dispatched after a horse's gear changed on every client. `player` is the rider who changed it, or null for a script.");

        v8pp::class_<Horse> &cls = GetClass(isolate);

        cls.static_function("spawn", &JS_Spawn,
            v8pp::metadata::docs("Horse", {
                v8pp::metadata::param("position", "Vector3 | Partial<Vector3>", true, "Optional world-space spawn position; omitted components default to zero."),
                v8pp::metadata::param("rotation", "Vector3 | Quaternion", true, "Optional initial orientation: a Quaternion, or a Vector3 of Euler angles in degrees."),
                v8pp::metadata::param("soul", "string", true, "Optional breed from `Horse.breeds()` (`pebbles`), or a soul GUID from the game's tables; omitted spawns the generic riding horse."),
                v8pp::metadata::param("name", "string", true, "Optional name every client shows for the horse; omitted leaves the name its soul carries."),
                v8pp::metadata::param("gear", kGearInputType, true, "Optional gear the horse is born wearing on every client, as `setGear` takes it; `{}` spawns it bare. Omitted, it wears the game's own tack: a named horse's own preset, or one of the generic ones (`horse_common01`, `horse_common02`)."),
            }, "Spawns and replicates a horse.", "The newly spawned horse handle."));
        cls.static_function("gearPresets", &JS_GearPresets,
            v8pp::metadata::docs("string[]", {}, "The game's own sets of horse tack, from its clothing presets: the generic `horse_common`, `horse_noble`, `horse_nomad` and `horse_draft` families, and the named horses' own. Any of them goes to `setGear` or `Horse.spawn` by name.",
                "Preset names, alphabetical."));
        cls.static_function("gearItems", &JS_GearItems,
            v8pp::metadata::docs("string[]", {v8pp::metadata::param("slot", "'saddle' | 'head' | 'torso' | 'shoe'", true, "Only the gear for this slot; omitted lists all of it.")},
                "Every item class a horse can wear, from the game's own tables: saddles, bridles and chanfrons, caparisons and trappings, horseshoes.", "Item class names, saddles first."));
        cls.static_function("breeds", &JS_Breeds,
            v8pp::metadata::docs("string[]", {}, "The horses `Horse.spawn` knows by name: every horse the game gives a character of its own -- the ones its traders sell and the named horses of its quests -- plus `horse`, the generic one.",
                "Breed names, `horse` first and the rest alphabetical."));
        cls.static_function("all", &JS_All, v8pp::metadata::docs("Horse[]", {}, "Lists every horse the server currently has.", "One handle per live horse, in no particular order."));
        cls.static_function("getById", &JS_GetById,
            v8pp::metadata::docs("Horse | null", {v8pp::metadata::param("id", "number", false, "Network entity identifier.")}, "Looks a horse up by its network entity ID.", "The horse's handle, or null when no live horse has that ID."));

        cls.publish(global);
    }

    void Events::HorseIntentFinished(std::uint64_t horseNetworkId, const char *status) {
        const std::string result(status);
        EmitReservedEvent("horseIntentDone", [horseNetworkId, result](v8::Isolate *isolate) {
            return std::vector<v8::Local<v8::Value>> {WrapHorse(isolate, horseNetworkId), v8pp::to_v8(isolate, result)};
        });
    }

    void Events::HorseSpawned(std::uint64_t horseNetworkId) {
        EmitHorseEvent("horseSpawn", horseNetworkId);
    }

    void Events::HorseDestroyed(std::uint64_t horseNetworkId) {
        EmitHorseEvent("horseDestroy", horseNetworkId);
    }

    void Events::HorseMounted(std::uint64_t horseNetworkId, std::uint64_t playerNetworkId) {
        EmitHorseRiderEvent("horseMount", horseNetworkId, playerNetworkId);
    }

    void Events::HorseDismounted(std::uint64_t horseNetworkId, std::uint64_t playerNetworkId) {
        EmitHorseRiderEvent("horseDismount", horseNetworkId, playerNetworkId);
    }

    void Events::HorseOwnerChanged(std::uint64_t horseNetworkId, std::uint64_t playerNetworkId) {
        EmitHorseRiderEvent("horseOwnerChanged", horseNetworkId, playerNetworkId);
    }

    bool Events::HorseMounting(std::uint64_t horseNetworkId, std::uint64_t playerNetworkId) {
        return EmitReservedEventSync("horseMounting", [horseNetworkId, playerNetworkId](v8::Isolate *isolate) {
            std::vector<v8::Local<v8::Value>> args;
            args.push_back(WrapHorse(isolate, horseNetworkId));
            args.push_back(WrapPlayer(isolate, playerNetworkId));
            return args;
        });
    }

    void Events::HorseDamaged(std::uint64_t horseNetworkId, std::uint64_t attackerNetworkId, float amount, Shared::Game::DamageReason reason) {
        EmitReservedEvent("horseDamage", [horseNetworkId, attackerNetworkId, amount, reason](v8::Isolate *isolate) {
            std::vector<v8::Local<v8::Value>> args;
            args.push_back(WrapHorse(isolate, horseNetworkId));
            args.push_back(WrapPlayer(isolate, attackerNetworkId));
            args.push_back(v8pp::to_v8(isolate, amount));
            args.push_back(v8pp::to_v8(isolate, std::string(Shared::Game::DamageReasonName(reason))));
            return args;
        });
    }

    bool Events::HorseGearChanging(std::uint64_t horseNetworkId, std::uint64_t playerNetworkId, const Shared::Game::HorseGear &gear) {
        return EmitReservedEventSync("horseGearChanging", [horseNetworkId, playerNetworkId, gear](v8::Isolate *isolate) {
            std::vector<v8::Local<v8::Value>> args;
            args.push_back(WrapHorse(isolate, horseNetworkId));
            args.push_back(WrapPlayer(isolate, playerNetworkId));
            args.push_back(GearToObject(isolate, gear));
            return args;
        });
    }

    void Events::HorseGearChanged(std::uint64_t horseNetworkId, std::uint64_t playerNetworkId) {
        auto *replication                  = Framework::CoreModules::GetReplication();
        const Shared::Game::HorseGear gear = KCDC::Features::Horse::Horses::GearOf(replication != nullptr ? replication->GetEntity<Shared::Entities::HorseEntity>(horseNetworkId) : nullptr);
        EmitReservedEvent("horseGearChanged", [horseNetworkId, playerNetworkId, gear](v8::Isolate *isolate) {
            std::vector<v8::Local<v8::Value>> args;
            args.push_back(WrapHorse(isolate, horseNetworkId));
            args.push_back(WrapPlayer(isolate, playerNetworkId));
            args.push_back(GearToObject(isolate, gear));
            return args;
        });
    }

    void Events::HorseDied(std::uint64_t horseNetworkId, std::uint64_t killerNetworkId, Shared::Game::DamageReason reason) {
        EmitReservedEvent("horseDeath", [horseNetworkId, killerNetworkId, reason](v8::Isolate *isolate) {
            std::vector<v8::Local<v8::Value>> args;
            args.push_back(WrapHorse(isolate, horseNetworkId));
            args.push_back(WrapPlayer(isolate, killerNetworkId));
            args.push_back(v8pp::to_v8(isolate, std::string(Shared::Game::DamageReasonName(reason))));
            return args;
        });
    }
} // namespace KCDC::Scripting
