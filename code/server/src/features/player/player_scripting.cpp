#include "features/player/player_scripting.h"

#include "shared/features/player/player_appearance_bindings.h"

#include "core/scripting/script_context.h"
#include "core/scripting/wrap.h"
#include "core/server.h"

#include "features/animation/animation_scripting.h"
#include "features/animation/animation_verbs.h"
#include "features/dog/dog_verbs.h"
#include "features/ground_item/ground_item_scripting.h"
#include "features/horse/horse_scripting.h"
#include "features/horse/horse_verbs.h"
#include "features/inventory/inventory_scripting.h"
#include "features/player/player_item_requests.h"
#include "features/player/player_progression_scripting.h"
#include "features/player/player_roster.h"
#include "features/player/player_service.h"
#include "features/prop/prop_verbs.h"
#include "features/stash/stash_service.h"

#include "shared/features/horse/horse_entity.h"
#include "shared/features/player/player_bindings.h"
#include "shared/features/player/player_buff.h"
#include "shared/features/player/player_buff_bindings.h"
#include "shared/features/player/player_combat_hit_report.h"
#include "shared/features/player/player_disguise.h"
#include "shared/features/player/player_give_item.h"
#include "shared/features/player/player_progression_bindings.h"
#include "shared/features/player/player_take_item.h"
#include "shared/game/animation_catalog.h"
#include "shared/game/body_part_catalog.h"
#include "shared/game/buff_catalog.h"
#include "shared/game/clothing_presets.h"
#include "shared/game/item_classes.h"
#include "shared/game/prop_models.h"
#include "shared/scripting/animation_options.h"
#include "shared/scripting/catalog.h"
#include "shared/scripting/rotation_arg.h"

#include <core_modules.h>
#include <networking/replication/replication_manager.h>
#include <scripting/v8pp_types.h>
#include <scripting/value_transfer.h>

#include <mafianet/get_time.h>

#include <v8pp/convert.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace KCDC::Scripting {

    namespace {
        // The shape most of this feature's events have: one subject, nothing else.
        void EmitPlayerEvent(const char *eventName, std::uint64_t playerNetworkId) {
            EmitReservedEvent(eventName, [playerNetworkId](v8::Isolate *isolate) {
                std::vector<v8::Local<v8::Value>> args;
                args.push_back(WrapPlayer(isolate, playerNetworkId));
                return args;
            });
        }

        // The handle a prototype function was called on, or nullptr.
        Player *Self(const v8::FunctionCallbackInfo<v8::Value> &info) {
            return v8pp::class_<Player>::unwrap_object(info.GetIsolate(), info.This());
        }

        // Both carry events have one shape: who carries, and whom.
        std::vector<v8::Local<v8::Value>> CarryArgs(v8::Isolate *isolate, std::uint64_t carrierNetworkId, std::uint64_t carriedNetworkId) {
            std::vector<v8::Local<v8::Value>> args;
            args.push_back(WrapPlayer(isolate, carrierNetworkId));
            args.push_back(WrapPlayer(isolate, carriedNetworkId));
            return args;
        }

        // An item class as its catalog name, or as its 32 hex digits when the catalog has none.
        std::string ItemClassName(const Shared::RPC::ItemClassId &itemClass) {
            const Shared::Game::ItemClassEntry *entry = Shared::Game::FindItemClass(itemClass);
            return entry != nullptr ? std::string(entry->name) : Detail::FormatItemClass(itemClass);
        }

        // The three item carry events have one shape: who carries, what, and the stack on the ground it came from or became.
        std::vector<v8::Local<v8::Value>> ItemCarryArgs(v8::Isolate *isolate, std::uint64_t playerNetworkId, const Shared::RPC::ItemClassId &itemClass, std::uint64_t groundItemNetworkId) {
            std::vector<v8::Local<v8::Value>> args;
            args.push_back(WrapPlayer(isolate, playerNetworkId));
            args.push_back(v8pp::to_v8(isolate, ItemClassName(itemClass)));
            args.push_back(groundItemNetworkId != 0 ? WrapGroundItem(isolate, groundItemNetworkId) : v8::Local<v8::Value>(v8::Null(isolate)));
            return args;
        }

        // Throws when no body of this player's kind walks carrying the item; true when one does.
        bool RequireCarryWalk(v8::Isolate *isolate, const Shared::Entities::HumanEntity *player, std::uint8_t bodies, const std::string &name) {
            const bool female = player != nullptr && player->appearance.gender == Shared::Game::AppearanceGender::Female;
            if ((bodies & (female ? Shared::Game::kAnimationBodyFemale : Shared::Game::kAnimationBodyMale)) != 0) {
                return true;
            }
            isolate->ThrowError(v8pp::to_v8(isolate, "Player.carryItem: the game has no walk for a " + std::string(female ? "woman" : "man") + " carrying '" + name + "'"));
            return false;
        }

        // JS: player.carryItem(item | groundItem) -> boolean
        void JS_CarryItem(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);
            auto *self                            = Self(info);
            Shared::Entities::HumanEntity *player = self != nullptr ? self->ResolveHuman() : nullptr;

            // A stack on the ground: taken up with the game's pick-up.
            if (info.Length() >= 1 && info[0]->IsObject() && !info[0]->IsString()) {
                GroundItem *handle                             = v8pp::class_<GroundItem>::unwrap_object(isolate, info[0]);
                Shared::Entities::GroundItemEntity *groundItem = handle != nullptr ? handle->ResolveGroundItem() : nullptr;
                if (handle == nullptr) {
                    isolate->ThrowError(v8pp::to_v8(isolate, "Player.carryItem: expected (item) or (groundItem)"));
                    return;
                }
                if (groundItem != nullptr && !RequireCarryWalk(isolate, player, Shared::Game::CarryWalkBodies(groundItem->item.itemClass), ItemClassName(groundItem->item.itemClass))) {
                    return;
                }
                info.GetReturnValue().Set(player != nullptr && groundItem != nullptr && KCDC::Server::Get()->GetPlayers().OrderItemCarry(player, groundItem->item.itemClass, groundItem));
                return;
            }

            if (info.Length() < 1 || !info[0]->IsString()) {
                isolate->ThrowError(v8pp::to_v8(isolate, "Player.carryItem: expected (item) or (groundItem)"));
                return;
            }
            const std::string name                        = v8pp::from_v8<std::string>(isolate, info[0]);
            const Shared::Game::AnimationPropEntry *prop  = Shared::Game::FindAnimationProp(name);
            const Shared::Game::ItemClassEntry *itemClass = prop != nullptr ? KCDC::Features::Player::Players::ResolveItemClass(prop->name) : nullptr;
            const std::uint8_t bodies                     = prop != nullptr ? Shared::Game::CarryWalkBodies(prop->manipulation) : 0;
            if (itemClass == nullptr || bodies == 0) {
                isolate->ThrowError(v8pp::to_v8(isolate, "Player.carryItem: '" + name + "' is not something a body carries; see Animations.props() for one whose `hand` has a carrying walk"));
                return;
            }
            if (player == nullptr) {
                info.GetReturnValue().Set(false);
                return;
            }
            if (!RequireCarryWalk(isolate, player, bodies, name)) {
                return;
            }
            info.GetReturnValue().Set(KCDC::Server::Get()->GetPlayers().OrderItemCarry(player, Shared::Game::ItemClassIdFromGuid(itemClass->guid), nullptr));
        }

        // JS: player.putDownItem(options?) -> boolean
        void JS_PutDownItem(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);
            bool immediate = false;
            if (info.Length() >= 1 && info[0]->IsObject()) {
                v8::Local<v8::Value> value;
                if (info[0].As<v8::Object>()->Get(isolate->GetCurrentContext(), v8pp::to_v8(isolate, "immediate")).ToLocal(&value) && !value->IsUndefined()) {
                    immediate = value->BooleanValue(isolate);
                }
            }
            auto *self                             = Self(info);
            Shared::Entities::HumanEntity *carrier = self != nullptr ? self->ResolveHuman() : nullptr;
            info.GetReturnValue().Set(carrier != nullptr && carrier->carriedItemClass.IsValid() && KCDC::Features::Player::Players::PutDownItem(carrier, immediate, false));
        }

        // JS: player.dropInventory(options?) -> Stash | null
        void JS_DropInventory(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);
            auto *self        = Self(info);
            bool keepEquipped = false;
            if (info.Length() >= 1 && info[0]->IsObject()) {
                v8::Local<v8::Value> keep;
                if (info[0].As<v8::Object>()->Get(isolate->GetCurrentContext(), v8pp::to_v8(isolate, "keepEquipped")).ToLocal(&keep) && !keep->IsUndefined()) {
                    keepEquipped = keep->BooleanValue(isolate);
                }
            }
            auto *stash = self != nullptr ? Server::Get()->GetStashes().DropInventory(self->GetId(), keepEquipped) : nullptr;
            info.GetReturnValue().Set(stash == nullptr ? v8::Local<v8::Value>(v8::Null(isolate)) : WrapStash(isolate, stash->GetNetworkID()));
        }

        // JS: player.setOutfit(preset) -> boolean
        void JS_SetOutfit(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);
            if (info.Length() < 1 || !info[0]->IsString()) {
                isolate->ThrowError(v8pp::to_v8(isolate, "Player.setOutfit: expected (preset)"));
                return;
            }
            auto *self                               = Self(info);
            const Shared::Entities::HumanEntity *human = self != nullptr ? self->ResolveHuman() : nullptr;
            if (human == nullptr) {
                info.GetReturnValue().Set(false);
                return;
            }
            // Only the body's own gender's outfits: the game refuses a man's
            // clothing on a woman's soul and the reverse
            // (S_ArmorItemClass::IsWearableBySoul), so the other gender's
            // preset would take the clothes off and put nothing on.
            const std::string wanted                        = v8pp::from_v8<std::string>(isolate, info[0]);
            const Shared::Game::ClothingPresetEntry *preset = Shared::Game::FindClothingPreset(wanted);
            for (std::size_t i = 0; preset == nullptr && i < Shared::Game::g_ClothingPresets.size(); ++i) {
                if (Shared::Game::g_ClothingPresets[i].name == wanted && Shared::Game::g_ClothingPresets[i].gender == human->appearance.gender) {
                    preset = &Shared::Game::g_ClothingPresets[i];
                }
            }
            info.GetReturnValue().Set(preset != nullptr && preset->gender == human->appearance.gender && Server::Get()->GetInventory().ScriptWearOutfit(self->GetId(), *preset).at("ok").get<bool>());
        }

        // JS: Player.outfitPresets(gender?) -> string[]
        void JS_OutfitPresets(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            std::string gender;
            if (info.Length() >= 1 && !info[0]->IsUndefined()) {
                gender = info[0]->IsString() ? v8pp::from_v8<std::string>(isolate, info[0]) : std::string();
                if (gender != "male" && gender != "female") {
                    isolate->ThrowError(v8pp::to_v8(isolate, "Player.outfitPresets: gender must be \"male\" or \"female\""));
                    return;
                }
            }
            std::vector<std::string> names;
            names.reserve(Shared::Game::g_ClothingPresets.size());
            for (const Shared::Game::ClothingPresetEntry &preset : Shared::Game::g_ClothingPresets) {
                if (gender.empty() || (preset.gender == Shared::Game::AppearanceGender::Female) == (gender == "female")) {
                    names.emplace_back(preset.name);
                }
            }
            info.GetReturnValue().Set(v8pp::to_v8(info.GetIsolate(), names));
        }

        // JS: player.putDown() -> boolean
        void JS_PutDown(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *self = Self(info);
            Shared::Entities::HumanEntity *carrier = self != nullptr ? self->ResolveHuman() : nullptr;
            info.GetReturnValue().Set(carrier != nullptr && carrier->carriedNetworkId != 0 && KCDC::Features::Player::Players::PutDown(carrier, false));
        }

        // JS: player.playAnimation(fragment, options?) -> boolean
        void JS_PlayAnimation(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::HandleScope handleScope(info.GetIsolate());
            Shared::Game::AnimationPlayback request;
            if (!Animations::ParseRequest(info, 0, "Player.playAnimation", request)) {
                return;
            }
            auto *self = Self(info);
            info.GetReturnValue().Set(self != nullptr && KCDC::Features::Animation::Animations::Play(self->ResolveHuman(), std::move(request)));
        }

        // JS: player.stopAnimation(options?) -> boolean
        void JS_StopAnimation(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *self = Self(info);
            info.GetReturnValue().Set(self != nullptr && KCDC::Features::Animation::Animations::Stop(self->ResolveHuman(), Animations::ParseStandUp(info, 0)));
        }

        // JS: player.setFacialExpression(fragment, options?) -> boolean
        void JS_SetFacialExpression(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::HandleScope handleScope(info.GetIsolate());
            std::string fragment;
            std::string tags;
            if (!Animations::ParseExpression(info, 0, "Player.setFacialExpression", fragment, tags)) {
                return;
            }
            auto *self = Self(info);
            info.GetReturnValue().Set(self != nullptr && KCDC::Features::Animation::Animations::SetExpression(self->ResolveHuman(), std::move(fragment), std::move(tags)));
        }

        // JS: player.setTalking(talking, style?) -> boolean
        void JS_SetTalking(const v8::FunctionCallbackInfo<v8::Value> &info) {
            Shared::Game::TalkStyle style = Shared::Game::TalkStyle::None;
            if (!Animations::ParseTalking(info, 0, "Player.setTalking", style)) {
                return;
            }
            auto *self = Self(info);
            info.GetReturnValue().Set(self != nullptr && KCDC::Features::Animation::Animations::SetTalking(self->ResolveHuman(), style));
        }

        // JS: player.teleport(position, label?) -> boolean
        void JS_Teleport(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);

            auto *self = Self(info);
            if (self == nullptr || info.Length() < 1 || !info[0]->IsObject()) {
                isolate->ThrowError(v8pp::to_v8(isolate, "Player.teleport: expected (position, label?)"));
                return;
            }

            // Use the glm convert specialization directly; generic from_v8
            // attempts class unwrapping.
            const glm::vec3 position = v8pp::convert<glm::vec3>::from_v8(isolate, info[0]);
            const std::string label  = info.Length() >= 2 && info[1]->IsString() ? v8pp::from_v8<std::string>(isolate, info[1]) : std::string();

            info.GetReturnValue().Set(KCDC::Features::Player::Players::Teleport(self->ResolveHuman(), position, label));
        }

        // JS: player.spawn(position, rotation?) -> boolean
        void JS_Spawn(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);

            auto *self   = Self(info);
            auto *server = KCDC::Server::Get();
            if (self == nullptr || server == nullptr || info.Length() < 1 || !info[0]->IsObject()) {
                isolate->ThrowError(v8pp::to_v8(isolate, "Player.spawn: expected (position, rotation?)"));
                return;
            }

            const glm::vec3 position = v8pp::convert<glm::vec3>::from_v8(isolate, info[0]);
            glm::quat rotation {1.0F, 0.0F, 0.0F, 0.0F};
            if (info.Length() >= 2) {
                rotation = ParseRotationArg(isolate, info[1], rotation);
            }

            info.GetReturnValue().Set(server->GetPlayers().Place(self->ResolveHuman(), position, rotation));
        }

        // JS: player.setAppearance(appearance) -> boolean
        void JS_SetAppearance(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);

            auto *self = Self(info);
            if (self == nullptr || info.Length() < 1 || !info[0]->IsObject()) {
                isolate->ThrowError(v8pp::to_v8(isolate, "Player.setAppearance: expected (appearance)"));
                return;
            }

            // Absent keys keep what they are already wearing, so one part
            // can change without restating the rest.
            Shared::Game::CharacterAppearance appearance;
            if (const Shared::Entities::HumanEntity *human = self->ResolveHuman()) {
                appearance = human->appearance;
            }
            if (!AppearanceFromV8(isolate, info[0], appearance)) {
                isolate->ThrowError(v8pp::to_v8(isolate, "Player.setAppearance: every part must be a string, and gender must be \"male\" or \"female\""));
                return;
            }

            info.GetReturnValue().Set(KCDC::Features::Player::Players::SetAppearance(self->ResolveHuman(), appearance));
        }

        // JS: player.setWorldBuilderEnabled(enabled) -> boolean
        void JS_SetWorldBuilderEnabled(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *self = Self(info);
            if (self == nullptr || info.Length() != 1 || !info[0]->IsBoolean()) {
                info.GetIsolate()->ThrowError(v8pp::to_v8(info.GetIsolate(), "Player.setWorldBuilderEnabled: expected (boolean)"));
                return;
            }
            info.GetReturnValue().Set(KCDC::Features::Player::Players::SetWorldBuilderEnabled(self->ResolveHuman(), info[0]->BooleanValue(info.GetIsolate())));
        }

        // JS: player.setMovementMode(mode) -> boolean
        void JS_SetMovementMode(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);

            auto *self = Self(info);
            if (self == nullptr || info.Length() < 1 || !info[0]->IsObject()) {
                isolate->ThrowError(v8pp::to_v8(isolate, "Player.setMovementMode: expected (mode)"));
                return;
            }

            v8::Local<v8::Context> context = isolate->GetCurrentContext();
            v8::Local<v8::Object> mode     = info[0].As<v8::Object>();

            // A key left out is off, not "leave it alone": the server keeps
            // no copy of what this client is currently under.
            bool walkByDefault = false;
            bool walkEnforced  = false;
            v8::Local<v8::Value> value;
            if (mode->Get(context, v8pp::to_v8(isolate, "walkByDefault")).ToLocal(&value) && !value->IsUndefined()) {
                walkByDefault = value->BooleanValue(isolate);
            }
            if (mode->Get(context, v8pp::to_v8(isolate, "walkEnforced")).ToLocal(&value) && !value->IsUndefined()) {
                walkEnforced = value->BooleanValue(isolate);
            }

            info.GetReturnValue().Set(KCDC::Features::Player::Players::SetMovementMode(self->ResolveHuman(), walkByDefault, walkEnforced));
        }

        // JS: player.giveItem(item, amount?) -> boolean
        void JS_GiveItem(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);

            auto *self = Self(info);
            if (self == nullptr || info.Length() < 1 || !info[0]->IsString()) {
                isolate->ThrowError(v8pp::to_v8(isolate, "Player.giveItem: expected (item, amount?)"));
                return;
            }

            const std::string query = v8pp::from_v8<std::string>(isolate, info[0]);
            const auto *item        = KCDC::Features::Player::Players::ResolveItemClass(query);
            if (item == nullptr) {
                info.GetReturnValue().Set(false);
                return;
            }

            const std::uint32_t amount = info.Length() >= 2 && info[1]->IsNumber() ? static_cast<std::uint32_t>(info[1]->IntegerValue(isolate->GetCurrentContext()).FromMaybe(0)) : 1;
            info.GetReturnValue().Set(KCDC::Features::Player::Players::GiveItem(self->ResolveHuman(), *item, amount));
        }

        /** What a `takeItem` promise settles with. */
        v8::Local<v8::Value> RemovalToObject(v8::Isolate *isolate, const KCDC::Features::Player::ItemRequests::Outcome &outcome) {
            v8::Local<v8::Context> context = isolate->GetCurrentContext();
            v8::Local<v8::Object> object   = v8::Object::New(isolate);

            object->Set(context, v8pp::to_v8(isolate, "removed"), v8pp::to_v8(isolate, outcome.removed)).Check();
            object->Set(context, v8pp::to_v8(isolate, "requested"), v8pp::to_v8(isolate, outcome.requested)).Check();
            object->Set(context, v8pp::to_v8(isolate, "ok"), v8pp::to_v8(isolate, outcome.answered && outcome.removed == outcome.requested)).Check();
            object->Set(context, v8pp::to_v8(isolate, "reason"), v8pp::to_v8(isolate, outcome.reason)).Check();
            return object;
        }

        // JS: player.takeItem(item, amount?) -> Promise<{ removed, requested, ok, reason }>
        void JS_TakeItem(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate           = info.GetIsolate();
            v8::HandleScope handleScope(isolate);
            v8::Local<v8::Context> context = isolate->GetCurrentContext();

            auto *self = Self(info);
            if (self == nullptr || info.Length() < 1 || !info[0]->IsString()) {
                isolate->ThrowError(v8pp::to_v8(isolate, "Player.takeItem: expected (item, amount?)"));
                return;
            }

            const std::string query    = v8pp::from_v8<std::string>(isolate, info[0]);
            const std::uint32_t amount = info.Length() >= 2 && info[1]->IsNumber() ? static_cast<std::uint32_t>(info[1]->IntegerValue(context).FromMaybe(0)) : 1;

            v8::Local<v8::Promise::Resolver> resolver = v8::Promise::Resolver::New(context).ToLocalChecked();
            info.GetReturnValue().Set(resolver->GetPromise());

            auto pending = std::make_shared<PendingPromise>(isolate, context, resolver);

            const auto *item = KCDC::Features::Player::Players::ResolveItemClass(query);
            const bool asked = item != nullptr && KCDC::Features::Player::Players::TakeItem(self->ResolveHuman(), *item, amount, [pending](const KCDC::Features::Player::ItemRequests::Outcome &outcome) {
                pending->Settle([&](v8::Isolate *iso, v8::Local<v8::Context>) {
                    return RemovalToObject(iso, outcome);
                });
            });

            if (!asked) {
                KCDC::Features::Player::ItemRequests::Outcome refused;
                refused.requested = amount;
                refused.reason    = item == nullptr ? "No item class goes by that name or GUID." : "The amount is outside what the wire accepts, or the player has no connection to ask.";
                resolver->Resolve(context, RemovalToObject(isolate, refused)).Check();
                pending->Release();
            }
        }

        // JS: player.addBuff(buff) -> boolean
        void JS_AddBuff(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);

            auto *self = Self(info);
            if (self == nullptr || info.Length() < 1 || !info[0]->IsString()) {
                isolate->ThrowError(v8pp::to_v8(isolate, "Player.addBuff: expected (buff)"));
                return;
            }

            const auto *buff = KCDC::Features::Player::Players::ResolveBuff(v8pp::from_v8<std::string>(isolate, info[0]));
            info.GetReturnValue().Set(buff != nullptr && KCDC::Features::Player::Players::AddBuff(self->ResolveHuman(), *buff));
        }

        // JS: player.removeBuff(buff) -> boolean
        void JS_RemoveBuff(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);

            auto *self = Self(info);
            if (self == nullptr || info.Length() < 1 || !info[0]->IsString()) {
                isolate->ThrowError(v8pp::to_v8(isolate, "Player.removeBuff: expected (buff)"));
                return;
            }

            const auto *buff = KCDC::Features::Player::Players::ResolveBuff(v8pp::from_v8<std::string>(isolate, info[0]));
            info.GetReturnValue().Set(buff != nullptr && KCDC::Features::Player::Players::RemoveBuff(self->ResolveHuman(), *buff));
        }

        // JS: player.clearBuffs(tag) -> boolean
        void JS_ClearBuffs(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);

            auto *self = Self(info);
            if (self == nullptr || info.Length() < 1 || !info[0]->IsString()) {
                isolate->ThrowError(v8pp::to_v8(isolate, "Player.clearBuffs: expected (tag)"));
                return;
            }

            info.GetReturnValue().Set(KCDC::Features::Player::Players::ClearBuffsByTag(self->ResolveHuman(), v8pp::from_v8<std::string>(isolate, info[0])));
        }

        // JS: player.hasBuff(buff) -> boolean
        void JS_HasBuff(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);

            auto *self = Self(info);
            if (self == nullptr || info.Length() < 1 || !info[0]->IsString()) {
                isolate->ThrowError(v8pp::to_v8(isolate, "Player.hasBuff: expected (buff)"));
                return;
            }

            const auto *buff = KCDC::Features::Player::Players::ResolveBuff(v8pp::from_v8<std::string>(isolate, info[0]));
            info.GetReturnValue().Set(buff != nullptr && KCDC::Features::Player::Players::HasBuff(self->ResolveHuman(), *buff));
        }

        // JS: player.buffs -> BuffState[]
        v8::Local<v8::Value> BuffStates(const Player &self, v8::Isolate *isolate) {
            v8::EscapableHandleScope handleScope(isolate);
            const std::vector<Shared::RPC::ObservedBuff> *reported = KCDC::Features::Player::Players::Buffs(self.ResolveHuman());
            return handleScope.Escape(reported != nullptr ? BuffStatesToV8(isolate, *reported, MafiaNet::GetTime()) : v8::Array::New(isolate));
        }

        /** A string-literal union of `names`, the way the contract spells one. */
        std::string LiteralUnion(const std::vector<std::string_view> &names) {
            std::string type;
            for (const std::string_view &name : names) {
                type += (type.empty() ? "\"" : " | \"") + std::string(name) + "\"";
            }
            return type;
        }

        /** The six limb names, straight from the catalog that resolves them. */
        std::string BodyPartType() {
            std::vector<std::string_view> names;
            for (const Shared::Game::BodyPartEntry &part : Shared::Game::g_BodyParts) {
                if (Shared::Game::IsHumanLimb(part)) {
                    names.push_back(part.scriptName);
                }
            }
            return LiteralUnion(names);
        }

        /** A limb's script name, or null for none. */
        v8::Local<v8::Value> BodyPartToV8(v8::Isolate *isolate, std::uint8_t bodyPart) {
            const Shared::Game::BodyPartEntry *limb = Shared::Game::FindHumanLimb(bodyPart);
            return limb != nullptr ? v8::Local<v8::Value>(v8pp::to_v8(isolate, std::string(limb->scriptName))) : v8::Local<v8::Value>(v8::Null(isolate));
        }

        // JS: player.injuries -> Injuries
        v8::Local<v8::Value> Injuries(const Player &self, v8::Isolate *isolate) {
            v8::EscapableHandleScope handleScope(isolate);
            v8::Local<v8::Context> context = isolate->GetCurrentContext();
            v8::Local<v8::Object> injuries = v8::Object::New(isolate);

            const std::uint32_t limbs = KCDC::Features::Player::Players::InjuredLimbs(self.ResolveHuman());
            for (const Shared::Game::BodyPartEntry &part : Shared::Game::g_BodyParts) {
                if (Shared::Game::IsHumanLimb(part)) {
                    injuries->Set(context, v8pp::to_v8(isolate, std::string(part.scriptName)), v8pp::to_v8(isolate, (limbs & (1U << part.id)) != 0)).Check();
                }
            }
            return handleScope.Escape(injuries);
        }

        void SetResource(const v8::FunctionCallbackInfo<v8::Value> &info, std::uint8_t resource, int valueIndex = 0) {
            auto *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);
            auto *self = Self(info);
            if (self == nullptr || info.Length() != valueIndex + 1 || !info[valueIndex]->IsNumber()) {
                isolate->ThrowError(v8pp::to_v8(isolate, "Player resource setter: expected one number"));
                return;
            }
            const double value = info[valueIndex]->NumberValue(isolate->GetCurrentContext()).FromMaybe(-1.0);
            if (!Shared::RPC::SetPlayerResource::IsValueSane(resource, value)) {
                isolate->ThrowError(v8pp::to_v8(isolate, "Player resource setter: expected a finite number from 0 to 1000000 representable in native units; positive health must exceed 0.00001"));
                return;
            }
            Shared::RPC::SetPlayerResource request;
            request.resource = resource;
            request.value    = static_cast<float>(value);
            info.GetReturnValue().Set(KCDC::Features::Player::Players::SetResource(self->ResolveHuman(), request));
        }

        void JS_SetStat(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *isolate = info.GetIsolate();
            if (info.Length() != 2 || !info[0]->IsString()) {
                isolate->ThrowError(v8pp::to_v8(isolate, "Player.setStat: expected a PlayerStat and a numeric value"));
                return;
            }
            const auto index = Shared::Game::FindPlayerStat(Shared::Game::kPlayerStats, v8pp::from_v8<std::string>(isolate, info[0]));
            if (index == Shared::Game::kPlayerStats.size()) {
                isolate->ThrowError(v8pp::to_v8(isolate, "Player.setStat: unknown or read-only stat"));
                return;
            }
            SetResource(info, static_cast<std::uint8_t>(index), 1);
        }

        void JS_SetHealth(const v8::FunctionCallbackInfo<v8::Value> &info) {
            SetResource(info, Shared::Game::HumanSoulState::kHealth);
        }

        void JS_SetHunger(const v8::FunctionCallbackInfo<v8::Value> &info) {
            SetResource(info, Shared::Game::HumanSoulState::kHunger);
        }

        // JS: player.heal(options?) -> boolean
        void JS_Heal(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);

            auto *self = Self(info);
            if (self == nullptr || (info.Length() >= 1 && !info[0]->IsUndefined() && !info[0]->IsObject())) {
                isolate->ThrowError(v8pp::to_v8(isolate, "Player.heal: expected (options?)"));
                return;
            }

            // Everything by default: a bare heal() is the full recovery.
            std::optional<float> health = -1.0F;
            bool injuries               = true;
            bool poisons                = true;
            bool bleeding               = true;
            if (info.Length() >= 1 && info[0]->IsObject()) {
                v8::Local<v8::Context> context = isolate->GetCurrentContext();
                v8::Local<v8::Object> options  = info[0].As<v8::Object>();
                v8::Local<v8::Value> value;
                if (options->Get(context, v8pp::to_v8(isolate, "health")).ToLocal(&value) && !value->IsUndefined()) {
                    if (value->IsNumber()) {
                        const double wanted = value->NumberValue(context).FromMaybe(-1.0);
                        if (!std::isfinite(wanted) || wanted < 0.0) {
                            isolate->ThrowError(v8pp::to_v8(isolate, "Player.heal: health must be a finite, non-negative number"));
                            return;
                        }
                        health = static_cast<float>(wanted);
                    }
                    else if (!value->BooleanValue(isolate)) {
                        health.reset();
                    }
                }
                if (options->Get(context, v8pp::to_v8(isolate, "injuries")).ToLocal(&value) && !value->IsUndefined()) {
                    injuries = value->BooleanValue(isolate);
                }
                if (options->Get(context, v8pp::to_v8(isolate, "poisons")).ToLocal(&value) && !value->IsUndefined()) {
                    poisons = value->BooleanValue(isolate);
                }
                if (options->Get(context, v8pp::to_v8(isolate, "bleeding")).ToLocal(&value) && !value->IsUndefined()) {
                    bleeding = value->BooleanValue(isolate);
                }
            }

            info.GetReturnValue().Set(KCDC::Features::Player::Players::Heal(self->ResolveHuman(), health, injuries, poisons, bleeding));
        }

        // JS: player.setDisguise(model | null) -> boolean
        void JS_SetDisguise(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);

            auto *self = Self(info);
            if (self == nullptr || info.Length() < 1 || !(info[0]->IsString() || info[0]->IsNull() || info[0]->IsUndefined())) {
                isolate->ThrowError(v8pp::to_v8(isolate, "Player.setDisguise: expected (model | null)"));
                return;
            }

            // A path or a file stem, the two spellings Prop.spawn takes; the wire only ever carries the path.
            Shared::Game::PlayerDisguise disguise;
            if (info[0]->IsString()) {
                const std::string query = v8pp::from_v8<std::string>(isolate, info[0]);
                const auto *model       = Shared::Game::PropModelAt(Shared::Game::FindPropModelIndex(query));
                if (model != nullptr) {
                    disguise.model = std::string(model->path);
                }
                else if (KCDC::Features::Prop::Props::IsStreamedModel(query)) {
                    disguise.model = query;
                }
                else {
                    isolate->ThrowError(v8pp::to_v8(isolate, "Player.setDisguise: `" + query + "` is neither a model in the prop catalog nor one this server streams"));
                    return;
                }
            }

            info.GetReturnValue().Set(KCDC::Features::Player::Players::SetDisguise(self->ResolveHuman(), disguise));
        }

        // JS: player.setVisible(visible) -> boolean
        void JS_SetVisible(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);

            auto *self = Self(info);
            if (self == nullptr || info.Length() < 1 || !info[0]->IsBoolean()) {
                isolate->ThrowError(v8pp::to_v8(isolate, "Player.setVisible: expected (visible: boolean)"));
                return;
            }

            info.GetReturnValue().Set(KCDC::Features::Player::Players::SetVisible(self->ResolveHuman(), info[0]->BooleanValue(isolate)));
        }

        // JS: player.dismount() -> Horse | null
        void JS_Dismount(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);

            auto *self  = Self(info);
            auto *human = self != nullptr ? self->ResolveHuman() : nullptr;
            if (human == nullptr) {
                info.GetReturnValue().SetNull();
                return;
            }

            // Their own client is told to get off too: only it can move their body, and without the order it would keep riding a horse the server has already handed on.
            auto *horse = KCDC::Features::Horse::Horses::RiddenBy(human->GetNetworkID());
            if (horse == nullptr) {
                info.GetReturnValue().SetNull();
                return;
            }

            const std::uint64_t horseNetworkId = horse->GetNetworkID();
            KCDC::Features::Horse::Horses::TakeRiderOff(horse, true);
            info.GetReturnValue().Set(WrapHorse(isolate, horseNetworkId));
        }

        // JS: player.mount(horse) -> boolean
        void JS_Mount(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);

            auto *self   = Self(info);
            auto *target = info.Length() >= 1 ? v8pp::class_<KCDC::Scripting::Horse>::unwrap_object(isolate, info[0]) : nullptr;
            if (self == nullptr || target == nullptr) {
                info.GetReturnValue().Set(false);
                return;
            }

            // { instant } puts them straight into the seat; the default is
            // the get-on a player plays at a horse.
            bool instant = false;
            if (info.Length() >= 2 && info[1]->IsObject()) {
                v8::Local<v8::Value> value;
                if (info[1].As<v8::Object>()->Get(isolate->GetCurrentContext(), v8pp::to_v8(isolate, "instant")).ToLocal(&value) && !value->IsUndefined()) {
                    instant = value->BooleanValue(isolate);
                }
            }
            info.GetReturnValue().Set(KCDC::Features::Horse::Horses::Mount(target->ResolveHorse(), self->GetId(), instant));
        }

        // JS: Player.all() -> Player[]
        void JS_All(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate           = info.GetIsolate();
            v8::HandleScope handleScope(isolate);
            v8::Local<v8::Context> context = isolate->GetCurrentContext();
            v8::Local<v8::Array> players   = v8::Array::New(isolate);

            std::uint32_t index = 0;
            if (auto *replication = Framework::CoreModules::GetReplication()) {
                replication->ForEach<Shared::Entities::HumanEntity>([&](Shared::Entities::HumanEntity *human) {
                    players->Set(context, index++, WrapPlayer(isolate, human->GetNetworkID())).Check();
                });
            }

            info.GetReturnValue().Set(players);
        }

        // JS: Player.getById(id) -> Player | null
        void JS_GetById(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);

            if (info.Length() < 1 || !info[0]->IsNumber()) {
                isolate->ThrowError(v8pp::to_v8(isolate, "Player.getById: expected (id)"));
                return;
            }

            const std::uint64_t networkId = static_cast<std::uint64_t>(info[0]->IntegerValue(isolate->GetCurrentContext()).FromMaybe(0));
            info.GetReturnValue().Set(WrapPlayer(isolate, networkId));
        }
    } // namespace

    std::string DamageReasonType() {
        std::vector<std::string_view> names;
        for (std::uint8_t reason = 0; reason <= Shared::Game::kMaxDamageReason; ++reason) {
            names.push_back(Shared::Game::DamageReasonName(static_cast<Shared::Game::DamageReason>(reason)));
        }
        return LiteralUnion(names);
    }

    Shared::Entities::HumanEntity *Player::ResolveHuman() const {
        return dynamic_cast<Shared::Entities::HumanEntity *>(Resolve());
    }

    v8::Local<v8::Value> Player::GetHorseHandle(v8::Isolate *isolate) const {
        v8::EscapableHandleScope handleScope(isolate);
        auto *horse = KCDC::Features::Horse::Horses::RiddenBy(GetId());
        return handleScope.Escape(horse != nullptr ? WrapHorse(isolate, horse->GetNetworkID()) : v8::Local<v8::Value>(v8::Null(isolate)));
    }

    v8::Local<v8::Value> Player::GetDogHandle(v8::Isolate *isolate) const {
        v8::EscapableHandleScope handleScope(isolate);
        auto *dog = KCDC::Features::Dog::Dogs::OwnedBy(GetId());
        return handleScope.Escape(dog != nullptr ? WrapDog(isolate, dog->GetNetworkID()) : v8::Local<v8::Value>(v8::Null(isolate)));
    }

    bool Player::IsMounted() const {
        return KCDC::Features::Horse::Horses::RiddenBy(GetId()) != nullptr;
    }

    bool Player::Revive() {
        auto *server = Server::Get();
        return server != nullptr && server->GetPlayers().Revive(ResolveHuman());
    }

    bool Player::CancelCombat(Player *opponent) {
        auto *server = Server::Get();
        return server != nullptr && opponent != nullptr && server->GetCombat().Cancel(GetId(), opponent->GetId());
    }

    std::string Player::ToString() const {
        const Shared::Entities::HumanEntity *human = ResolveHuman();
        std::ostringstream ss;
        ss << "Player{ id: " << GetId() << ", nickname: '" << (human != nullptr ? human->nickname : std::string()) << "' }";
        return ss.str();
    }

    v8pp::class_<Player> &Player::GetClass(v8::Isolate *isolate) {
        auto &_class = _classes[isolate];
        if (!_class) {
            // v8pp's inherit<Player> needs the framework Player registered first.
            Framework::Scripting::Builtins::Player::GetClass(isolate);
            RegisterHumanDataTypes(ServerCatalog());
            RegisterProgressionDataTypes(ServerCatalog());

            auto &movementMode = ServerCatalog().data_type("MovementMode", "The pace rule a server puts on one player. Both halves default to off, and a call to `setMovementMode` states both of them.");
            movementMode.add_property("walkByDefault", "boolean",
                "Whether walking, rather than jogging, is the pace this player keeps coming back to. A preference: they start each body walking and return to it after every sprint, and their own toggle key works normally in between.");
            movementMode.add_property("walkEnforced", "boolean",
                "Whether this player may not run or sprint at all. A rule rather than a preference: it holds the engine's own movement permissions, so the toggle key and the sprint key both stop raising the pace, and lifting it hands back whatever the body had before.");

            auto &passes = ServerCatalog().data_type("DamagePasses", "One number per damage type a blow is worked out in. A blade's stab, slash and smash are priced separately and added; a blow runs only the passes its attack and weapon call for.");
            passes.add_property("stab", "number", "The stab pass.");
            passes.add_property("slash", "number", "The slash pass.");
            passes.add_property("smash", "number", "The smash pass.");

            auto &modifiers = ServerCatalog().data_type("PlayerHitModifiers",
                "Scales a `playerHit` handler puts on the server's price of a melee blow, each 1 until a handler changes it. The server works the blow out again with them, step by step as the game does: a heavier attack has to get through the same armour and the same block, so doubling `attack` does not simply double the damage. Never negative.");
            modifiers.add_property("attack", "number", "Scales the attacker's attack in every pass, after their weapon, strength, skill and the swing itself.");
            modifiers.add_property("defense", "number", "Scales the victim's armour where the blow landed.");
            modifiers.add_property("block", "number", "Scales what the victim's block put up. Nothing for a blow that was not blocked.");
            modifiers.add_property("health", "number", "Scales the health the blow takes, once everything else is worked out. The game's own cap of 200 a blow is applied before it, so this can go past it.");

            auto &hit = ServerCatalog().data_type("PlayerHit",
                "A player or NPC blow, as `playerHit` shows it before the referee rules. `damage` is what the blow will take and `modifiers` scale how the server works it out; a handler may change either, from any resource, and the next handler sees the change. Every other field describes what happened and is read-only in effect. It is live only while the handlers run: one kept for later reads `damage` and `modifiers` as undefined and changes nothing.");
            hit.add_property("damage", "number",
                "Health the blow takes. For melee between players the server works it out itself, the way the game does, from the attacker's weapon and its wear, both players' strength and skills, the armour the victim wears where it landed, the block and the perks and buffs it can vouch for; for NPC melee or an arrow it is what the victim's own game worked out. Set it and it stands as set: `modifiers` are then ignored. Never negative.");
            hit.add_property("engineDamage", "number", "Health the game itself took for the blow on the machine that saw it land. Matches `damage` unless a perk that depends on the moment -- a heavy weapon's, a first strike's -- was in play, which the server leaves out.");
            hit.add_property("priced", "boolean", "Whether the server worked `damage` out itself: true for a swing, false for an arrow.");
            hit.add_property("weapon", "string | null", "The attacker's weapon as 32 hex digits, or null for a bare hand.");
            hit.add_property("attack", "DamagePasses", "The attacker's attack in each pass, after the swing's own strength and before `modifiers`.");
            hit.add_property("armor", "DamagePasses", "The victim's armour in each pass where the blow landed, before `modifiers`.");
            hit.add_property("blockDefense", "number", "What the victim's block put up, or 0 when nothing blocked it.");
            hit.add_property("bodyPartCoefficient", "number", "How much the place it landed multiplies the damage by: 1.5 for the head, 1 for the torso, 0.7 for a limb.");
            hit.add_property("modifiers", "PlayerHitModifiers", "Scales on the server's price; see `PlayerHitModifiers`. Ignored for an arrow.");
            hit.add_property("stamina", "number", "Stamina the blow cost the victim. Already spent: stamina decides their next block, which cannot wait for a ruling.");
            hit.add_property("bodyPart", BodyPartType() + " | null", "The limb it landed on, or null for none in particular.");
            hit.add_property("reason", DamageReasonType(), "The game's own reason for the damage: `combat` for a blade or a bow.");
            hit.add_property("source", "\"melee\" | \"missile\"", "Whether it was a swing or an arrow.");
            hit.add_property("blocked", "boolean", "The victim's block caught it.");
            hit.add_property("perfectBlock", "boolean", "The victim's perfect block caught it. The game takes nothing for one.");
            hit.add_property("blockBroken", "boolean", "The victim's block gave way under it.");

            auto &injuries =
                ServerCatalog().data_type("Injuries", "Which of a player's limbs carry an injury, one flag per limb. An injury is the game's own: it lowers the stamina ceiling (`healthyStamina`), can bleed, and heals slowly by itself or at once with a bandage or `player.heal`.");
            injuries.add_property("head", "boolean", "Whether the head is injured.");
            injuries.add_property("torso", "boolean", "Whether the torso is injured.");
            injuries.add_property("leftArm", "boolean", "Whether the left arm is injured.");
            injuries.add_property("rightArm", "boolean", "Whether the right arm is injured.");
            injuries.add_property("leftLeg", "boolean", "Whether the left leg is injured.");
            injuries.add_property("rightLeg", "boolean", "Whether the right leg is injured.");

            _class = std::make_unique<v8pp::class_<Player>>(isolate, ServerCatalog(), "Player", "A connected KCDC player and the body they occupy.");
            _class->auto_wrap_objects(true);
            _class->inherit<Framework::Scripting::Builtins::Player>()
                .ctor<std::uint64_t>(v8pp::metadata::docs("void", {v8pp::metadata::param("id", "number", false, "Network entity identifier.")}, "Creates a script wrapper for an existing connected player with this ID; it neither connects nor spawns anyone."))
                .function("toString", &Player::ToString, v8pp::metadata::docs("string", {}, "Formats this player handle for logging and debugging.", "The player's network entity ID and nickname."));

            // The readonly view of the body, written once and shared with the client's own Player handle.
            RegisterHumanProperties(*_class);
            RegisterProgressionPlayerMembers(*_class);

            _class->function("revive", &Player::Revive, v8pp::metadata::docs("boolean", {}, "Requests revival of this player after playerDied.", "True when sent; false when disconnected, no death was reported, or revival was already requested."));
            _class->function("cancelCombat", &Player::CancelCombat,
                v8pp::metadata::docs("boolean", {v8pp::metadata::param("opponent", "Player", false, "The other player. Both sides are cancelled.")},
                    "Ends melee combat between these two players and retires their pending blows against each other. Other opponents are left alone. A later attack can start combat again. Consent and timeout rules belong to your resource.",
                    "True when sent; false for disconnected players, the same player, or different virtual worlds."));

            _class->property("mounted", &Player::IsMounted, v8pp::metadata::property_docs("boolean", "Whether this player is in a saddle."));
            _class->property("carrying", [](const Player &self, v8::Isolate *isolate) { const Shared::Entities::HumanEntity *human = self.ResolveHuman(); return WrapPlayer(isolate, human != nullptr ? human->carriedNetworkId : 0); },
                v8pp::metadata::property_docs("Player | null", "The player whose body this one carries on their shoulder, or null. Set once `playerCarry` has fired, cleared once `playerPutDown` has."));
            _class->property("carriedBy", [](const Player &self, v8::Isolate *isolate) { Shared::Entities::HumanEntity *carrier = KCDC::Features::Player::Players::CarrierOf(self.GetId()); return WrapPlayer(isolate, carrier != nullptr ? carrier->GetNetworkID() : 0); },
                v8pp::metadata::property_docs("Player | null", "The player carrying this one's body, or null."));
            _class->property("carriedItem", [](const Player &self, v8::Isolate *isolate) -> v8::Local<v8::Value> { const Shared::Entities::HumanEntity *human = self.ResolveHuman(); if (human == nullptr || !human->carriedItemClass.IsValid()) { return v8::Null(isolate); } return v8pp::to_v8(isolate, ItemClassName(human->carriedItemClass)); },
                v8pp::metadata::property_docs("string | null", "The item this player carries in their arms, by its item name, or null. Set once `playerCarryItem` has fired, cleared once `playerPutDownItem` has."));
            _class->property("horse", [](const Player &self, v8::Isolate *isolate) { return self.GetHorseHandle(isolate); },
                v8pp::metadata::property_docs("Horse | null", "The horse this player is riding, or null when they are on foot."));
            _class->property("dog", [](const Player &self, v8::Isolate *isolate) { return self.GetDogHandle(isolate); },
                v8pp::metadata::property_docs("Dog | null", "The dog companion this player has, or null when they have none. One dog per player, as in the game."));
            _class->property("buffs", [](const Player &self, v8::Isolate *isolate) { return BuffStates(self, isolate); },
                v8pp::metadata::property_docs("BuffState[]",
                    "The status effects on this player's body, as their own client last reported them: potions, poison, injury, drunkenness, illness, unconsciousness, and anything this server added. Perks and equipment effects are not in here -- they follow state that already replicates.\n\nThis is the list of *named effects*. For how drunk, poisoned, hurt or tired someone actually is, read `drunkenness`, `poisoning`, `bleeding`, `consciousness`, `hunger` and `exhaust` instead: those are live numbers and need no name to look up.\n\nEmpty until the player's client sends its first report, shortly after they connect; `playerBuffAdded` fires for whatever it was already carrying."));

            _class->property("injuries", [](const Player &self, v8::Isolate *isolate) { return Injuries(self, isolate); },
                v8pp::metadata::property_docs("Injuries",
                    "Which limbs are injured, as the player's own client last reported them -- the same report `buffs` reads, named by limb so nobody has to know the six buff names. All false until the first report arrives. `playerInjured` and `playerInjuryHealed` fire as a flag changes.\n\nWhat an injury does is the game's own: a hurt head, torso or arm weakens its stats, a hurt leg takes away running and sprinting, and a badly hurt limb bleeds. Everybody else sees the consequences rather than the injury: the owner's slower pace, the lower stamina ceiling, bleeding, and the hurt gait the game plays once health is low. The game has no leg-specific limp animation, so there is nothing more to show."));

            _class->prototype_function("spawn", &JS_Spawn,
                v8pp::metadata::docs("boolean", {
                    v8pp::metadata::param("position", "Vector3 | Partial<Vector3>", false, "Where the body goes; a Vector3 or any object carrying x, y and z."),
                    v8pp::metadata::param("rotation", "Quaternion | Vector3", true, "Which way they face: a Quaternion, or a Vector3 of Euler degrees."),
                }, "Puts this player somewhere, as a spawn rather than as a teleport: their client holds the body still until there is real ground under it, so it cannot fall through a world that has not streamed in yet.\n\nCalled from a `playerSpawning` handler this *is* the answer to that request -- the player is still behind their loading screen, and nothing is seen. Called at any other time it moves a player who is already in the world, which is visible.\n\nNothing else names a spawn: with no handler calling this, everybody arrives at the level's own start point, because every client asks the game for the identical map start.",
                    "True when the placement was accepted; false for a position that is not somewhere in the world, or a player with no connection to ask."));

            _class->prototype_function("teleport", &JS_Teleport,
                v8pp::metadata::docs("boolean", {
                    v8pp::metadata::param("position", "Vector3 | Partial<Vector3>", false, "World-space destination; a Vector3 or any object carrying x, y and z."),
                    v8pp::metadata::param("label", "string", true, "Optional name for the destination, echoed back in the client's own teleport panel. Display only."),
                }, "Asks this player's own client to put them somewhere else. The owning client is authoritative for its body's pose, so this is a request that lands on their next frame rather than a write.",
                    "True when the request went out; false when the position is not finite or the player has no connection to ask."));
            _class->prototype_function("playAnimation", &JS_PlayAnimation,
                v8pp::metadata::docs("boolean", {
                    v8pp::metadata::param("fragment", "string", false, "A Mannequin fragment, as `Animations.list` names it. `\"\"` plays none and only puts the props and tags on, which with a hand tag like `r_bucket` makes the player carry something while they walk."),
                    v8pp::metadata::param("options", kAnimationOptionsType, true, "`clip` plays a clip the server streams instead of a fragment (`animations/kcdc/<resource>/wave.caf`, the fragment left `\"\"`), `tags` pick the variant, `loop` repeats it until stopped, `lockMovement` holds the player still, `props` puts up to two models in their hands."),
                }, "Makes this player's body play one of the game's animations, on their own screen and on everybody else's -- a wave, a bow, a woodcutter's swing with the axe in hand. It is state rather than a one-off: a player who streams in or joins while it plays sees it too, and it lasts until `stopAnimation` or the next `playAnimation`, even after a one-shot has finished.\n\nA new `playAnimation`, `stopAnimation` and `teleport` cut a running animation at once rather than waiting for it to finish. The game can cut it short too: a hit, a fall or drawing a weapon ends it, and so can the player walking off from one that leaves the legs free. `loop` starts it again when that happens, and keeps a one-shot going pass after pass with no gap between them.\n\n`playerAnimationEnd` fires once when the animation this request started is over, and says how. A loop ends only by being stopped or replaced.\n\nA hand tag is only half of holding something. `r_bucket` makes the body move as if it carried a bucket, but the bucket itself is a `props` entry.",
                    "True when the request went out; false for a player with no body yet. Throws for a prop or option it cannot use."));
            _class->prototype_function("stopAnimation", &JS_StopAnimation,
                v8pp::metadata::docs("boolean", {v8pp::metadata::param("options", kStopAnimationOptionsType, true, "`standUp` plays the game's own way back to standing when the animation held the body in a sitting or lying stance tag (`sittingNoTable`, `lyingGround`): `StandUp` or `GetUp`, in the variant that fits that stance. Without it, or for a stance the game authors no way out of (`beggarKneel`), the body goes straight back to standing.")},
                    "Ends what `playAnimation` started at once, takes its props away and hands the body back to the game. A stance tag the animation set goes with it, so without `standUp` a seated player pops upright.\n\nThe stand-up is an animation of its own: it raises its own `playerAnimationEnd`, fragment `StandUp` or `GetUp`, once the player is on their feet, and a `playAnimation` during it replaces it.",
                    "True when the request went out; false for a player with no body yet."));
            _class->prototype_function("setFacialExpression", &JS_SetFacialExpression,
                v8pp::metadata::docs("boolean", {
                    v8pp::metadata::param("fragment", "string", false, "A fragment on the face's own scope: `FE_Default`, `FE_DialogueIdle` or `FE_DialogueSpeaking` for a held mood, or an `ADLG_FA_*` gesture (`ADLG_FA_Smile`, `ADLG_FA_Wink`, `ADLG_FA_Laugh`, `ADLG_FA_Surprise`...) that plays once. `\"\"` hands the face back to the game."),
                    v8pp::metadata::param("options", kFacialExpressionOptionsType, true, "`tags` pick the variant: for the `FE_*` fragments the mood, `happy`, `angry`, `sad`, `nervous`, `pensive`, `arogant` or `drunk`, as `Animations.list(\"FE_\")` spells them."),
                }, "Sets this player's facial expression, on their own screen and on everybody else's, alongside whatever their body plays. It is state: a player who streams in sees it, and it lasts until the next call.\n\nThe expression is the eyes, brows and mood. It never moves the mouth -- the game's own `FE_DialogueSpeaking` animates only the eyes and leaves the mouth to the voice line being spoken. `setTalking` is the mouth.\n\nA mood tag is one of the body's own tags, so it can change the mood of the body's idle too, for as long as the expression holds.",
                    "True when the request went out; false for a player with no body yet. Throws for an argument it cannot use."));
            _class->prototype_function("setTalking", &JS_SetTalking,
                v8pp::metadata::docs("boolean", {
                    v8pp::metadata::param("talking", "boolean", false, "Whether the mouth moves."),
                    v8pp::metadata::param("style", kTalkStyleType, true, "How: `neutral` (the default), `happy`, `drunk` or `chew`."),
                }, "Moves this player's mouth as if they were talking, on their own screen and on everybody else's, until told to stop -- for text chat, voice chat or a scene. It is a looping talk animation, not lip-sync: it follows no words.\n\nThe game's own lip-sync wins while it plays a voice line on the same body, and the loop comes back after it.",
                    "True when the request went out; false for a player with no body yet. Throws for a style it does not know."));
            _class->prototype_function("setAppearance", &JS_SetAppearance,
                v8pp::metadata::docs("boolean", {v8pp::metadata::param("appearance", "Partial<Appearance>", false, "The parts to change. Anything left out keeps what the player is wearing, and an empty string hands that part back to the game. Names come from `Appearances.options`, except the beard, which comes from `Appearances.beards`.")},
                    "Asks this player's own client to wear a different body -- face, hair, beard and skin. The owning client is authoritative for its body, so this is a request that lands on their next frame rather than a write; read `player.appearance` back to see what they actually put on.\n\nGender is applied before the parts, because it decides which half of the game's catalog the names come from -- and it is the one part of this with a price. A soul's gender lives on its archetype, so changing it moves the player to the game's own counterpart archetype, which also carries four authored numbers: base armour, the conspicuousness and visibility pair, and unarmed attack. Nothing reads those on the puppets other people see, but on the body its owner plays they are a real change. Ask for a gender only when you mean it; a male-to-male change never touches the archetype.\n\nEveryone starts as Henry, so a server that wants people to tell each other apart has to call this.\n\nFace and beard travel together whether or not both are named. Beards are modelled per face, so changing either into a pair that was never modelled is refused whole rather than applied without the beard; `Appearances.beards` says which pairs are real.",
                    "True when the request went out; false for a name that is not in the catalog, a gender it does not belong to, a beard that face cannot wear, or a player with no connection to ask."));
            _class->prototype_function("setWorldBuilderEnabled", &JS_SetWorldBuilderEnabled,
                v8pp::metadata::docs("boolean", {v8pp::metadata::param("enabled", "boolean", false, "Whether this player may open World Builder.")},
                    "Grants or revokes World Builder access for this player. Multiplayer connections start without access. Enabling allows F7 and the client MapEditor.open API; disabling also closes an open editor. The permission lasts until changed or disconnected and does not affect other players. Offline editing is always allowed.",
                    "True when the permission was sent; false for a player with no connection. Throws unless enabled is a boolean."));
            _class->prototype_function("setMovementMode", &JS_SetMovementMode,
                v8pp::metadata::docs("boolean", {v8pp::metadata::param("mode", "Partial<MovementMode>", false, "The whole rule. A key left out is off, because the server keeps no copy of what this player is currently under.")},
                    "Puts a pace rule on this player. `walkByDefault` makes walking the pace they keep coming back to and leaves their own key working; `walkEnforced` forbids running and sprinting outright, and their key stops mattering.\n\nThe two are different things and both are worth having: a default is a preference, an enforcement is a rule. Nothing about either goes out to anybody else -- the pace every other player draws comes from this body's own published animation tags, so a walking player already looks like one everywhere.\n\nA default holds across sprints. The game itself ends every sprint in a jog, deliberately, so the client puts the walk back once the sprint is over rather than during it, and a player who chose to jog keeps jogging: only the pace the sprint took is given back. On a gamepad the stick's own deflection decides the pace, which the rule neither reads nor overrides.\n\nEnforcement holds the engine's own run and sprint permissions, the same pair the game clears while somebody carries a body, and hands back what it found when the rule is lifted. Sprinting cannot shake it off.",
                    "True when the rule went out; false for a player with no connection to ask."));
            _class->prototype_function("giveItem", &JS_GiveItem,
                v8pp::metadata::docs("boolean",
                    {
                        v8pp::metadata::param("item", "string", false, "Item class GUID, or the exact name the game's own item tables use."),
                        v8pp::metadata::param("amount", "number", true, "How many to grant; defaults to 1, and at most 10000."),
                    },
                    "Grants items into this player's inventory, which the server holds; their game shows them on the next tick and announces them with its own \"You received\" toast. `Inventory.add` does the same, can set the items' properties, and can leave out the toast.",
                    "True when the items were added; false for an unknown item, an amount outside 1..10000, or a player with no inventory."));
            _class->prototype_function("takeItem", &JS_TakeItem,
                v8pp::metadata::docs("Promise<{ removed: number; requested: number; ok: boolean; reason: string }>",
                    {
                        v8pp::metadata::param("item", "string", false, "Item class GUID, or the exact name the game's own item tables use. The same spelling `giveItem` takes."),
                        v8pp::metadata::param("amount", "number", true, "How many units to take; defaults to 1, and at most 10000. Zero is refused rather than read as \"all of them\"."),
                    },
                    "Takes items of a class out of this player's inventory, across its rows, all of them or none. The server holds the inventory, so the promise is already settled when it is returned; it stays a promise so existing scripts keep working. Their game announces the loss with its own toast; `Inventory.remove` can leave it out. To move items between players use `Inventory.transfer`, which cannot lose them half way.",
                    "An object carrying `removed` (the amount when it happened, otherwise 0), `requested`, `ok`, and `reason` (empty on success, otherwise the inventory code, such as `insufficientItems`)."));
            _class->prototype_function("dropInventory", &JS_DropInventory,
                v8pp::metadata::docs("Stash | null", {v8pp::metadata::param("options", "{ keepEquipped?: boolean }", true, "`keepEquipped` keeps the units their body wears or holds.")},
                    "Moves this player's items into a container spawned at their body, as a death drop: `playerInventoryChanged` (reason `drop`) and `stashInventoryChanged` follow, then the container is anyone's to open. At most 128 rows go. What happens to it next -- who may loot it, when it goes -- is the script's.",
                    "The container, or null when there was nothing to drop or the player has no inventory."));
            _class->prototype_function("setOutfit", &JS_SetOutfit,
                v8pp::metadata::docs("boolean", {v8pp::metadata::param("preset", "string", false, "One of the game's own outfits for this player's gender, by a name from `Player.outfitPresets(player.appearance.gender)` or its GUID.")},
                    "Dresses this player in one of the game's own outfits. Every piece of clothing they wear comes off and stays in their inventory, and a new set of the outfit's clothes is added and worn; weapons and everything else are left as they are. Their game dresses them a moment later, as it does after `Inventory.set`.",
                    "True when the inventory was changed; false for a preset that is not in the catalog or is the other gender's, a player with no inventory, or an inventory with no room."));
            _class->prototype_function("getInventory", &GetPlayerInventory, v8pp::metadata::docs("InventoryState | null", {}, "Reads this player's inventory; the same as `Inventory.get(player)`.", "A copy of it, or null for a player who is not connected."));
            _class->prototype_function("addBuff", &JS_AddBuff,
                v8pp::metadata::docs("boolean", {v8pp::metadata::param("buff", "string", false, "Buff GUID, or the exact name the game's own buff tables use. `Buffs.find` resolves either.")},
                    "Puts a status effect on this player's body. The server runs no effects of its own, so this asks their client rather than writing anything, and it lands a moment later -- `hasBuff` right after this call still says false. Watch `playerBuffAdded` for the moment it is really on.\n\nThe ask is not a promise, either: the game refuses an effect that conflicts with one already there, and a second drink folds into the first rather than stacking.",
                    "True when the instruction went out; false for an unknown buff or a player with no connection to ask."));
            _class->prototype_function("removeBuff", &JS_RemoveBuff,
                v8pp::metadata::docs("boolean", {v8pp::metadata::param("buff", "string", false, "Buff GUID, or the exact name the game's own buff tables use.")},
                    "Asks this player's own client to take a status effect off: the exact instance when a report named one, and every instance of that definition otherwise.", "True when the instruction went out; false for an unknown buff or a player with no connection to ask."));
            _class->prototype_function("clearBuffs", &JS_ClearBuffs,
                v8pp::metadata::docs("boolean", {v8pp::metadata::param("tag", "string", false, "An effect family, from `Buffs.tags` -- `poison`, `bleed`, `alcohol_drunk`, `unconscious`.")},
                    "Clears every effect in one of the game's own families at once: one call for all poisons, or all bleeding, without naming them. `Buffs.tags` lists the families. Note that `World.setTime` does not fast-forward effects -- their time runs off each client's frame delta and never reads the calendar -- so a scripted sleep has to clear what it means to end.",
                    "True when the instruction went out; false for a tag no buff table uses or a player with no connection to ask."));
            _class->prototype_function("hasBuff", &JS_HasBuff,
                v8pp::metadata::docs("boolean", {v8pp::metadata::param("buff", "string", false, "Buff GUID, or the exact name the game's own buff tables use.")},
                    "Whether this player's last report carried that status effect.", "True when it did; false for an unknown buff, or a player who has reported nothing yet."));
            _class->prototype_function("setStat", &JS_SetStat,
                v8pp::metadata::docs("boolean",
                    {v8pp::metadata::param("stat", PlayerStatType(Shared::Game::kPlayerStats), false, "Writable PlayerStat; derived stats are rejected."),
                        v8pp::metadata::param("value", "number", false, "Finite native value from 0 to 1000000, clamped to the native maximum. Positive health must exceed 0.00001; positive values must not underflow to zero.")},
                    "Sets health, stamina, energy or nourishment on the living body. Call once player.ready is true. Lowering health raises playerDamage with a null attacker; a fatal change also raises playerDied with a null killer. Zero health can kill. Does not clear buffs or revive; normal regeneration and consumption continue.",
                    "True when sent, not confirmation of application; false for a missing, unready or dead body. Throws for invalid arguments or a read-only stat. Requests for a previous life are discarded. Read getStat after the client's next report to observe the result."));
            _class->prototype_function("setHealth", &JS_SetHealth,
                v8pp::metadata::docs("boolean", {v8pp::metadata::param("value", "number", false, "Health in the same units as player.health. Clamped to maxHealth on the player's client.")},
                    "Sets this living player's health, raising or lowering it. Lowering raises playerDamage with a null attacker and the amount removed; a fatal change also raises playerDied with a null killer. This includes restoring a lower saved value. Zero can kill; this never revives a dead player. Leaves injuries, poison and bleeding in place. Call after playerSpawned or playerReady when restoring a saved value. The client applies the request asynchronously; health continues to show its last report until a new one arrives.",
                    "True when sent; false when the body is missing, not ready or dead. Throws unless value is a finite number from 0 to 1000000, with positive health greater than 0.00001 after conversion to native float units. A request for a life that has since ended is ignored."));
            _class->prototype_function("setHunger", &JS_SetHunger,
                v8pp::metadata::docs("boolean", {v8pp::metadata::param("value", "number", false, "Nourishment in the same units as player.hunger. Higher means better fed. Clamped to maxHunger on the player's client.")},
                    "Sets this living player's nourishment, raising or lowering it. Call after playerSpawned or playerReady when restoring a saved value. The client applies the request asynchronously; hunger continues to show its last report until a new one arrives.",
                    "True when sent; false when the body is missing, not ready or dead. Throws unless value is a finite number from 0 to 1000000 that does not round from positive to zero in native float units. A request for a life that has since ended is ignored."));
            _class->prototype_function("heal", &JS_Heal,
                v8pp::metadata::docs("boolean", {v8pp::metadata::param("options", "{ health?: number | boolean; injuries?: boolean; poisons?: boolean; bleeding?: boolean }", true,
                    "What to restore; everything when left out. `health`: true or absent for all of it, a number to raise it to that much (never lowers it), false to leave it. `injuries`: clear every limb injury, and with it the bleeding a badly hurt limb causes. `poisons`: clear every poison. `bleeding`: clear the standalone bleeding effect.")},
                    "Nurses this player back, on their own client and with the game's own recipe -- the one its quests use for a full heal: the `remove_injuries` and `remove_all_posions` cures, which each wipe their whole kind of effect as they land, then health raised through the soul's own setter, the way a potion raises it. What comes off raises `playerInjuryHealed` and `playerBuffRemoved` as usual.\n\nIt does not revive anybody: a dead player stays dead until `revive`.",
                    "True when the orders went out; false for a player with no connection to ask. Throws for a health that is not a finite, non-negative number."));
            _class->prototype_function("setDisguise", &JS_SetDisguise,
                v8pp::metadata::docs("boolean", {v8pp::metadata::param("model", "string | null", false, "A mesh from the prop catalog, by its `objects/...cgf` path or its file stem, or one this server streams by its full path -- anything `Prop.spawn` takes. Null puts the player back in their own body.")},
                    "Draws this player as a static mesh instead of themselves -- a barrel, a haystack, a cart wheel -- on their own screen and on everybody else's. It is state: a player who streams in or joins sees it too, and it lasts until the next call.\n\nOnly the look and the collision change. The body is hidden rather than replaced, and its collision cylinder is resized around the mesh, so the player walks and is traced against in roughly the mesh's shape -- a client's `World.raycast` with `mode: \"anything\"` finds them where the mesh is drawn, and says which player it found. The mesh turns with the body. They keep everything else a person has: they can still be hurt, bleed and die, and nothing stops them drawing a weapon, which a gamemode that does not want that has to take away.\n\nThe game's own camera sits in the head, which the mesh now covers: pair this with the client's `Camera.setThirdPerson` for the disguised player. Nametags are not touched; `setNametagVisible` is the call for that. NPCs still see a person.\n\nThe owning client is authoritative for its body, so this is a request that lands on their next frame; `player.disguise` reads what they actually have on.",
                    "True when the request went out; false for a player with no connection to ask. Throws for a model the catalog does not carry."));
            _class->prototype_function("setVisible", &JS_SetVisible,
                v8pp::metadata::docs("boolean", {v8pp::metadata::param("visible", "boolean", false, "False to stop drawing this player's body, true to draw it again.")},
                    "Stops drawing this player's body, or draws it again -- on their own screen and on everybody else's. The body goes with everything on it: what it wears, the weapons on its back and belt, and whatever it is holding. It is state, like a disguise: somebody who streams in or joins sees nothing of a hidden player either, and it lasts until the next call or until they leave.\n\nNothing but the drawing changes. They stand where they stood, keep their collider, their soul and their weapons, so a hidden player still blocks a doorway, can still be hit and traced against, still fights and still dies -- and the NPCs around them never stopped seeing a person. Nametags are separate; `setNametagVisible` is the call for that.\n\nTheir own camera sits in their head, so a hidden player simply sees no arms in first person. The owning client is authoritative for its body, so this is a request that lands on their next frame; `player.visible` reads what they actually have.",
                    "True when the request went out; false for a player with no connection to ask. Throws unless `visible` is a boolean."));
            _class->prototype_function("dismount", &JS_Dismount,
                v8pp::metadata::docs("Horse | null", {}, "Takes this player out of whatever saddle they are in: their own client gets them off, and `horseDismount` is raised.", "The horse they were taken off, or null when they were not riding one."));
            _class->prototype_function("putDown", &JS_PutDown,
                v8pp::metadata::docs("boolean", {}, "Has this player put down the body on their shoulder, with the game's own put-down. It is an instruction to their client, so the carry ends when that put-down lands: `playerPutDown` follows then, and `carrying` reads null from that moment.",
                    "True when the order went out; false when they carry nobody or have no connection."));
            _class->prototype_function("carryItem", &JS_CarryItem,
                v8pp::metadata::docs("boolean",
                    {v8pp::metadata::param("item", "string | GroundItem", false,
                        "What to carry. A name from `Animations.props()` whose `hand` the game has a carrying walk for -- the baskets (`basket_full_wood`, `basket_b_apples`, `stonebasket`, `eggbasket`, `shoppingbasket`), `firewoodChipsHand`, the buckets (`waterBucket`, `milkBucket`), and for a man only the sacks (`sack`, `sack_miller`), `barrel` and `crate_with_silver` -- puts a new one straight into their hands. A `GroundItem` of such a class has them pick that one up off the ground with the game's own bend-down; it has to be a single item within their reach.")},
                    "Has this player carry an item in their arms, the way the game's own people carry firewood, sacks and baskets: the carrying walk, and the put-down at the end. While it is carried they walk, since the game has no running with a load.\n\nIt is an instruction to their client, so the carry lands when that client reports it: `playerCarryItem` and `carriedItem` follow. A carry a script ordered is not put to `playerCarryingItem`. The item never enters their inventory. Put down -- with the game's own key or `putDownItem` -- it falls from their hands and becomes a carryable `GroundItem` where it lands, which `playerPutDownItem` hands over and anybody can take up again.\n\nTheir hands have to be free -- a drawn weapon or a torch makes their game refuse the carry, and nothing is reported.",
                    "True when the order went out; false when they cannot act, are riding, already carry an item or a body, the stack is not resting within their reach or somebody else is taking it, or they have no connection. Throws for an item with no carrying walk, or none for this player's body."));
            _class->prototype_function("putDownItem", &JS_PutDownItem,
                v8pp::metadata::docs("boolean", {v8pp::metadata::param("options", "{ immediate?: boolean }", true, "`immediate` lets go at once, without the put-down.")}, "Has this player put down the item in their arms, with the game's own put-down. It is an instruction to their client, so the carry ends when that put-down lands: `playerPutDownItem` follows then.",
                    "True when the order went out; false when they carry no item or have no connection."));
            _class->prototype_function("mount", &JS_Mount,
                v8pp::metadata::docs("boolean",
                    {v8pp::metadata::param("horse", "Horse", false, "The horse to get on."),
                        v8pp::metadata::param("options", "{ instant?: boolean }", true, "`instant` puts them straight into the seat, as the game's own forced mount does; by default they play the get-on a player plays at a horse.")},
                    "Puts this player in a horse's saddle. Everyone near enough to watch sees it the way it was asked for -- the get-on, or the instant seat -- while a client that only streams the rider in later finds them already seated. It is an instruction to their client, so the seat is reported back like any other mount: `horseMounting` can still refuse it, and `horseMount` and `player.horse` follow once it lands. The player has to be standing near the horse; teleport them beside it first.",
                    "True when the order went out; false when the horse is dead or somebody else is riding it, or the player has no connection."));
        }
        return *_class;
    }

    void Player::UnregisterIsolate(v8::Isolate *isolate) {
        _classes.erase(isolate);
    }

    void Player::Register(v8::Isolate *isolate, v8::Local<v8::Object> global) {
        if (isolate == nullptr || global.IsEmpty()) {
            return;
        }

        auto &events = ServerCatalog().data_type("EventMap");
        events.add_property("playerConnect", "[player: Player]", "Dispatched once a connecting player's body exists and can be resolved to a handle. The body has no pose yet -- `player.ready` is false until the owner reports one.");
        events.add_property("playerDisconnect", "[player: Player]", "Dispatched while a player is leaving, before their body is destroyed, so the handle still reads. Anything keyed on the player must be cleaned up here: a dropped connection raises no other event.");
        events.add_property("playerDied", std::string("[player: Player, killer: Player | Npc | null, reason: ") + DamageReasonType() + "]",
            "Dispatched once per accepted death report. Call player.revive() to request revival; there is no automatic respawn.\n\n`killer` is the player or server NPC whose blow took the last of the health, as the dying player's own client saw it -- null for a fall, a bleed-out, or anything without an identified attacker. `reason` is the game's own for that last write: `combat` for a blade or a bow, `fall`, `bleeding`, `poison` and so on.");
        events.add_property("playerDamage", std::string("[player: Player, attacker: Player | Npc | null, amount: number, bodyPart: ") + BodyPartType() + " | null, reason: " + DamageReasonType() + "]",
            "Dispatched when something takes health off a player: a weapon, an arrow, a fall, a collision, a scripted hit. Player combat and server NPC melee are raised by the server as it rules on them, after `playerHit` had its say, so `amount` is what the ruling takes; anything else is reported by the hit player's own client, which resolved it against its armour and its skills. It arrives ahead of the `playerDied` a killing blow causes.\n\n`attacker` is the player or server NPC who dealt it, or null. `bodyPart` is where it landed, or null for damage that lands nowhere in particular. A player attacker's weapon is available through `rightHandItem` or `leftHandItem`; an NPC's equipped items are in `wearing`.\n\nBleeding, poison and hunger wear health down a tick at a time without raising this -- `bleeding`, `poisoning` and `hunger` are live numbers already. The tick that kills does arrive, with its own reason, just before the `playerDied` it causes.");
        events.add_property("playerAttack", "[player: Player, opponent: Player]",
            "A melee swing aimed at another player was admitted by the server, including a miss. Raised once per attack generation, after presentation admission. Use it to track combat inactivity in your resource.");
        events.add_property("playerCombatCancelled", "[first: Player, second: Player]", "A resource called cancelCombat for this pair. Raised after the cancellation was sent to both players and their observers.");
        events.add_property("playerHit", "[player: Player, attacker: Player | Npc, hit: PlayerHit]",
            "Dispatched when another player's blade or arrow, or a server NPC's melee attack, lands on a player, before anything is taken -- the place for teams, safe zones, friendly fire and damage rules. The game resolved the blow against the guard the victim really held -- whether it was blocked, perfectly or not, and where it landed -- and the server works out what a swing takes from there; it has checked that the swing was one it relayed and could still land, or that the arrow was one whose impact it accepted, and that the two stood within reach. Only what is ruled here comes off the victim's health, on every client at once.\n\nReturn `false` from a handler to refuse the blow: no health is taken and no `playerDamage` follows. NPC melee uses the victim client's native damage with `priced: false`; set `hit.damage` to change it. For priced player swings, scale `hit.modifiers` to change how a swing is worked out -- a stronger attack, weaker armour -- or set `hit.damage` to say outright what it takes: `0` for a blow that lands harmlessly, more for a heavier one. Either way the blow is still seen and heard: the blades met. Handlers run synchronously, so the decision cannot wait on anything awaited.");
        events.add_property("playerInjured", std::string("[player: Player, bodyPart: ") + BodyPartType() + "]",
            "Dispatched when a limb becomes injured -- usually a blow landing there, sometimes a fall on both legs. `player.injuries` already says so. A limb hit again while injured stays injured and raises nothing new.");
        events.add_property("playerInjuryHealed", std::string("[player: Player, bodyPart: ") + BodyPartType() + "]", "Dispatched when a limb injury is gone: it healed by itself, a bandage took it, or `player.heal` did.");
        events.add_property("playerSpawning", "[player: Player]",
            "Dispatched when a player's body needs somewhere to stand, while their own client waits on the answer behind its loading screen. This is the one moment a spawn can be chosen without anybody seeing the body move -- call `player.spawn(position)` from the handler and that becomes where they arrive.\n\nHandlers run synchronously, so the choice has to be made in the handler itself rather than in something awaited from it. A handler that names no placement leaves the player at the level's own start point, which is the same tile for everybody.\n\n`reason` says whether they are arriving or coming back: `join` for a connection, `respawn` after a death.");
        events.add_property("playerAnimationEnd", std::string("[player: Player, fragment: string, reason: ") + kAnimationEndReasonType + "]",
            "Dispatched once when the animation a `playAnimation` request started is over. `fragment` is the request's. `reason` says how: `finished` when a one-shot played to its end, `interrupted` when it was playing and something cut it short (a hit, a weapon drawn, a teleport), `refused` when the game would not start it at all, `stopped` for `stopAnimation`, `replaced` for another `playAnimation`.\n\nThe first three come from the player's own client, sent from the moment the game's animation system ended it, so this is the place to chain the next animation. A `loop` never finishes by itself and only ends `stopped` or `replaced`. A request with no fragment -- one that only holds a prop or a tag -- raises nothing.");
        events.add_property("playerCarrying", "[carrier: Player, carried: Player]",
            "Dispatched when a player has picked up another player's body, before the server accepts it -- the place to decide who may carry whom. Return `false` from a handler and the carry is refused: the carrier's client is told to let go at once, and no `playerCarry` follows. Handlers run synchronously, so the decision cannot wait on anything awaited.\n\nThe pick-up is the game's own \"grab\" prompt, which it offers only over a body that is down -- dead or unconscious -- so a player a script revives at once is never there to carry. The carrier's game has already started the pick-up when this runs.");
        events.add_property("playerCarry", "[carrier: Player, carried: Player]", "Dispatched once a carry is accepted: the carried body is on the carrier's shoulder on every client, and `carrier.carrying` names it. It rides there until `playerPutDown`.");
        events.add_property("playerPutDown", "[carrier: Player, carried: Player]",
            "Dispatched once a carry is over, however it ended: the carrier dropped the body, `putDown` asked them to, the carried player was revived, or either of them left. A player who is leaving is still readable here; the event comes before their body is destroyed.");
        events.add_property("playerCarryingItem", "[player: Player, item: string, groundItem: GroundItem | null]",
            "Dispatched when a player starts a carry of their own, before the server accepts it: they chose the game's pick-up prompt over a carryable `groundItem`, or took an item from one of the world's own piles (`groundItem` null). Return `false` from a handler and the carry is refused -- the prop stays where it is, and a pile's item is let go at once. Handlers run synchronously, so the decision cannot wait on anything awaited. A carry a script ordered with `carryItem` is not asked. `item` is the item's name.");
        events.add_property("playerCarryItem", "[player: Player, item: string, groundItem: GroundItem | null]",
            "Dispatched once an item carry is accepted: everybody sees the player pick it up and carry it, and `player.carriedItem` names it. `groundItem` is the prop it was taken up from, still readable here and gone a moment later, or null. It is carried until `playerPutDownItem`.");
        events.add_property("playerPutDownItem", "[player: Player, item: string, groundItem: GroundItem | null]",
            "Dispatched once an item carry is over, however it ended: the player put it down, `putDownItem` asked them to, it went into a pile, or they left. `groundItem` is the carryable prop it became where it fell -- still falling, settling where the game's physics puts it -- or null when it went into a pile or they have no room left on the ground.");
        events.add_property("playerSpawned", "[player: Player]",
            "Dispatched once a joining player is really standing in the world: their level is up, their body is where `playerSpawning` put it, and the ground under it has loaded. Anything that acts on an arriving player -- kit, a welcome line, a marker -- belongs here rather than in `playerConnect`, which fires while they are still loading the level, or in `playerSpawning`, where the body is still mid-placement.\n\nThe player may still be looking at the game's loading screen; `playerReady` follows once they are not.");
        events.add_property("playerReady", "[player: Player]",
            "Dispatched once per connection, after `playerSpawned`, when the player can play: their own client reports that the game has taken its loading screen down. The game raises that moment itself -- it is its own gameplay-start signal, not a guess from timing.\n\nThat player's client resources were already running at `playerSpawned`, so this is not needed to make sure a `player.emit` is received. It is for what the player should actually see on arrival -- a welcome, a first page, a camera shot -- which shown earlier would play behind the loading screen. A client that never gets that far, or disconnects first, never raises it.");

        RegisterPlayerStatEnums(isolate, global, ServerCatalog());
        v8pp::class_<Player> &cls = GetClass(isolate);

        cls.static_function("outfitPresets", &JS_OutfitPresets, v8pp::metadata::docs("string[]", {v8pp::metadata::param("gender", "\"male\" | \"female\"", true, "Only the outfits authored for this gender. The game will not put a man's clothes on a woman or the reverse, so `player.setOutfit` takes only the presets of the player's own `appearance.gender`.")}, "The game's own outfits, from its clothing presets: what `player.setOutfit` and `Npc.setOutfit` dress a body in. Many belong to one named character.", "Preset names, men's first, each gender's in name order."));
        cls.static_function("all", &JS_All, v8pp::metadata::docs("Player[]", {}, "Lists every player currently connected, including those whose body has no pose yet.", "One handle per connected player, in no particular order."));
        cls.static_function("getById", &JS_GetById,
            v8pp::metadata::docs("Player | null", {v8pp::metadata::param("id", "number", false, "Network entity identifier.")}, "Looks a player up by their network entity ID.", "The player's handle, or null when no connected player has that ID."));

        cls.publish(global);
    }

    void Events::PlayerConnected(std::uint64_t playerNetworkId) {
        EmitPlayerEvent("playerConnect", playerNetworkId);
    }

    void Events::PlayerDisconnected(std::uint64_t playerNetworkId) {
        EmitPlayerEvent("playerDisconnect", playerNetworkId);
    }

    void Events::PlayerDied(std::uint64_t playerNetworkId, std::uint64_t killerNetworkId, Shared::Game::DamageReason reason) {
        EmitReservedEvent("playerDied", [playerNetworkId, killerNetworkId, reason](v8::Isolate *isolate) {
            std::vector<v8::Local<v8::Value>> args;
            args.push_back(WrapPlayer(isolate, playerNetworkId));
            args.push_back(WrapCombatant(isolate, killerNetworkId));
            args.push_back(v8pp::to_v8(isolate, std::string(Shared::Game::DamageReasonName(reason))));
            return args;
        });
    }

    void Events::PlayerAttack(std::uint64_t playerNetworkId, std::uint64_t opponentNetworkId) {
        EmitReservedEvent("playerAttack", [playerNetworkId, opponentNetworkId](v8::Isolate *isolate) {
            return std::vector<v8::Local<v8::Value>> {WrapPlayer(isolate, playerNetworkId), WrapPlayer(isolate, opponentNetworkId)};
        });
    }

    void Events::PlayerCombatCancelled(std::uint64_t first, std::uint64_t second) {
        EmitReservedEvent("playerCombatCancelled", [first, second](v8::Isolate *isolate) {
            return std::vector<v8::Local<v8::Value>> {WrapPlayer(isolate, first), WrapPlayer(isolate, second)};
        });
    }

    void Events::PlayerDamaged(std::uint64_t playerNetworkId, std::uint64_t attackerNetworkId, float amount, std::uint8_t bodyPart, Shared::Game::DamageReason reason) {
        EmitReservedEvent("playerDamage", [playerNetworkId, attackerNetworkId, amount, bodyPart, reason](v8::Isolate *isolate) {
            std::vector<v8::Local<v8::Value>> args;
            args.push_back(WrapPlayer(isolate, playerNetworkId));
            args.push_back(WrapCombatant(isolate, attackerNetworkId));
            args.push_back(v8pp::to_v8(isolate, amount));
            args.push_back(BodyPartToV8(isolate, bodyPart));
            args.push_back(v8pp::to_v8(isolate, std::string(Shared::Game::DamageReasonName(reason))));
            return args;
        });
    }

    bool Events::PlayerHit(std::uint64_t playerNetworkId, std::uint64_t attackerNetworkId, PlayerHitProposal &hit) {
        bool accepted = true;
        WithScriptContext([&](v8::Isolate *isolate, v8::Local<v8::Context> context, Framework::Scripting::ResourceManager *resources) {
            // Handlers write into `hit` itself through the object's
            // accessors, from whichever resource they run in.
            const PlayerHitHandles::Scope scope(hit);
            v8::Local<v8::Value> wrapped;
            {
                v8::TryCatch unbuilt(isolate);
                if (!PlayerHitHandles::Get().Wrap(isolate, context, scope.GetHandle()).ToLocal(&wrapped)) {
                    return;
                }
            }
            // Set to what it was, `damage` is not set: as before resources
            // had runtimes of their own, only the value the handlers leave
            // counts, and the modifiers still price the blow.
            const float proposed = hit.damage;
            const std::vector<v8::Local<v8::Value>> args {WrapPlayer(isolate, playerNetworkId), WrapCombatant(isolate, attackerNetworkId), wrapped};
            accepted      = resources->GetEvents().EmitReservedSync(isolate, context, "playerHit", args);
            hit.damageSet = hit.damage != proposed;
        });
        return accepted;
    }

    namespace {
        // The scales `hit.modifiers` carries, in the order its accessors are
        // numbered.
        constexpr std::array<const char *, 4> kHitModifierNames {"attack", "defense", "block", "health"};

        float &HitModifierAt(Shared::Game::CombatPricingScales &scales, std::uint32_t index) {
            switch (index) {
            case 0: return scales.attack;
            case 1: return scales.defense;
            case 2: return scales.block;
            default: return scales.health;
            }
        }

        // What a handler may set a damage or a scale to: a finite,
        // non-negative number. Anything else leaves it as it was.
        std::optional<float> HitNumber(v8::Local<v8::Value> value) {
            if (!value->IsNumber()) {
                return std::nullopt;
            }
            const double number = value.As<v8::Number>()->Value();
            if (!std::isfinite(number) || number < 0.0 || number > static_cast<double>(std::numeric_limits<float>::max())) {
                return std::nullopt;
            }
            return static_cast<float>(number);
        }

        // Where a `hit`, and the `modifiers` read off it, keep their handle:
        // a private key, which no script can read, write or enumerate.
        v8::Local<v8::Private> HitHandleKey(v8::Isolate *isolate) {
            return v8::Private::ForApi(isolate, v8pp::to_v8(isolate, "kcdc.PlayerHit.handle"));
        }

        // Where a `hit` keeps the `modifiers` object it handed out.
        v8::Local<v8::Private> HitModifiersKey(v8::Isolate *isolate) {
            return v8::Private::ForApi(isolate, v8pp::to_v8(isolate, "kcdc.PlayerHit.modifiers"));
        }

        std::optional<std::uint32_t> HandleOn(v8::Isolate *isolate, v8::Local<v8::Object> object) {
            v8::Local<v8::Value> handle;
            if (!object->GetPrivate(isolate->GetCurrentContext(), HitHandleKey(isolate)).ToLocal(&handle) || !handle->IsUint32()) {
                return std::nullopt;
            }
            return handle.As<v8::Uint32>()->Value();
        }
    } // namespace

    PlayerHitHandles &PlayerHitHandles::Get() {
        static PlayerHitHandles instance;
        return instance;
    }

    PlayerHitHandles::Scope::Scope(Events::PlayerHitProposal &hit) {
        PlayerHitHandles &handles = Get();
        // 0 is never a handle, and a dispatch still running around this one
        // keeps its own.
        do {
            ++handles._nextHandle;
        } while (handles._nextHandle == 0 || handles._live.contains(handles._nextHandle));
        _handle                = handles._nextHandle;
        handles._live[_handle] = &hit;
    }

    PlayerHitHandles::Scope::~Scope() {
        Get()._live.erase(_handle);
    }

    Events::PlayerHitProposal *PlayerHitHandles::Find(std::uint32_t handle) const {
        const auto found = _live.find(handle);
        return found != _live.end() ? found->second : nullptr;
    }

    PlayerHitHandles::Templates &PlayerHitHandles::TemplatesFor(v8::Isolate *isolate) {
        Templates &templates = _templates[isolate];
        if (templates.hit.IsEmpty()) {
            // The internal field is what has the value transfer ask
            // `HandleOf` about a `hit`; the handle rides under a private key.
            v8::Local<v8::ObjectTemplate> hit = v8::ObjectTemplate::New(isolate);
            hit->SetInternalFieldCount(1);
            hit->SetAccessorProperty(v8pp::to_v8(isolate, "damage"), v8::FunctionTemplate::New(isolate, &GetDamage), v8::FunctionTemplate::New(isolate, &SetDamage));
            hit->SetAccessorProperty(v8pp::to_v8(isolate, "modifiers"), v8::FunctionTemplate::New(isolate, &GetModifiers), v8::FunctionTemplate::New(isolate, &SetModifiers));
            templates.hit.Set(isolate, hit);

            // A plain object with no internal field, so a `modifiers` sent on
            // copies as its four values.
            v8::Local<v8::ObjectTemplate> modifiers = v8::ObjectTemplate::New(isolate);
            for (std::uint32_t index = 0; index < kHitModifierNames.size(); ++index) {
                const v8::Local<v8::Integer> which = v8::Integer::NewFromUnsigned(isolate, index);
                modifiers->SetAccessorProperty(v8pp::to_v8(isolate, kHitModifierNames[index]), v8::FunctionTemplate::New(isolate, &GetModifier, which), v8::FunctionTemplate::New(isolate, &SetModifier, which));
            }
            templates.modifiers.Set(isolate, modifiers);
        }
        return templates;
    }

    v8::MaybeLocal<v8::Value> PlayerHitHandles::Wrap(v8::Isolate *isolate, v8::Local<v8::Context> context, std::uint32_t handle) {
        const Events::PlayerHitProposal *hit = Find(handle);
        if (hit == nullptr) {
            return v8::Null(isolate);
        }
        v8::Local<v8::Object> object;
        if (!TemplatesFor(isolate).hit.Get(isolate)->NewInstance(context).ToLocal(&object) || !object->SetPrivate(context, HitHandleKey(isolate), v8::Integer::NewFromUnsigned(isolate, handle)).FromMaybe(false)) {
            return {};
        }

        // Defined, not assigned: this runs inside every resource's runtime,
        // and an assignment would run a setter a script put on
        // Object.prototype. Everything but `damage` and `modifiers`
        // describes what happened, and is read-only in effect.
        const auto define = [isolate, context](v8::Local<v8::Object> target, const char *key, v8::Local<v8::Value> value) {
            return target->CreateDataProperty(context, v8pp::to_v8(isolate, key), value).FromMaybe(false);
        };
        const auto passes = [&](const std::array<float, 3> &values) -> v8::Local<v8::Value> {
            v8::Local<v8::Object> vector = v8::Object::New(isolate);
            const bool built             = define(vector, "stab", v8pp::to_v8(isolate, values[0])) && define(vector, "slash", v8pp::to_v8(isolate, values[1])) && define(vector, "smash", v8pp::to_v8(isolate, values[2]));
            return built ? vector.As<v8::Value>() : v8::Local<v8::Value>();
        };
        const auto flag = [hit](std::uint8_t bit) {
            return (hit->flags & bit) != 0;
        };
        const v8::Local<v8::Value> attack = passes(hit->attack);
        const v8::Local<v8::Value> armor  = passes(hit->armor);
        if (attack.IsEmpty() || armor.IsEmpty()) {
            return {};
        }
        const bool built = define(object, "stamina", v8pp::to_v8(isolate, hit->stamina)) && define(object, "bodyPart", BodyPartToV8(isolate, hit->bodyPart)) && define(object, "reason", v8pp::to_v8(isolate, std::string(Shared::Game::DamageReasonName(hit->reason))))
                           && define(object, "source", v8pp::to_v8(isolate, hit->missile ? "missile" : "melee")) && define(object, "blocked", v8pp::to_v8(isolate, flag(Shared::RPC::CombatHitFlags::kBlocked)))
                           && define(object, "perfectBlock", v8pp::to_v8(isolate, flag(Shared::RPC::CombatHitFlags::kPerfectBlock))) && define(object, "blockBroken", v8pp::to_v8(isolate, flag(Shared::RPC::CombatHitFlags::kBlockBroken)))
                           && define(object, "engineDamage", v8pp::to_v8(isolate, hit->engineDamage)) && define(object, "priced", v8pp::to_v8(isolate, hit->priced))
                           && define(object, "weapon", hit->weapon.empty() ? v8::Null(isolate).As<v8::Value>() : v8pp::to_v8(isolate, hit->weapon).As<v8::Value>()) && define(object, "attack", attack) && define(object, "armor", armor)
                           && define(object, "blockDefense", v8pp::to_v8(isolate, hit->block)) && define(object, "bodyPartCoefficient", v8pp::to_v8(isolate, hit->bodyPartCoefficient));
        if (!built) {
            return {};
        }
        return object;
    }

    std::optional<std::uint32_t> PlayerHitHandles::HandleOf(v8::Isolate *isolate, v8::Local<v8::Object> object) {
        if (object->InternalFieldCount() != 1) {
            return std::nullopt;
        }
        return HandleOn(isolate, object);
    }

    Events::PlayerHitProposal *PlayerHitHandles::Receiver(const v8::FunctionCallbackInfo<v8::Value> &info) {
        const std::optional<std::uint32_t> handle = HandleOf(info.GetIsolate(), info.This());
        return handle ? Get().Find(*handle) : nullptr;
    }

    Events::PlayerHitProposal *PlayerHitHandles::ModifiersReceiver(const v8::FunctionCallbackInfo<v8::Value> &info) {
        const std::optional<std::uint32_t> handle = HandleOn(info.GetIsolate(), info.This());
        return handle ? Get().Find(*handle) : nullptr;
    }

    void PlayerHitHandles::GetDamage(const v8::FunctionCallbackInfo<v8::Value> &info) {
        if (const Events::PlayerHitProposal *hit = Receiver(info)) {
            info.GetReturnValue().Set(static_cast<double>(hit->damage));
        }
    }

    void PlayerHitHandles::SetDamage(const v8::FunctionCallbackInfo<v8::Value> &info) {
        Events::PlayerHitProposal *hit    = Receiver(info);
        const std::optional<float> damage = hit != nullptr && info.Length() > 0 ? HitNumber(info[0]) : std::nullopt;
        if (damage) {
            hit->damage = *damage;
        }
    }

    void PlayerHitHandles::GetModifiers(const v8::FunctionCallbackInfo<v8::Value> &info) {
        v8::Isolate *isolate                      = info.GetIsolate();
        v8::Local<v8::Context> context            = isolate->GetCurrentContext();
        const std::optional<std::uint32_t> handle = HandleOf(isolate, info.This());
        if (!handle || Get().Find(*handle) == nullptr) {
            return;
        }
        // One object per `hit`, so `hit.modifiers === hit.modifiers`.
        v8::Local<v8::Value> made;
        if (info.This()->GetPrivate(context, HitModifiersKey(isolate)).ToLocal(&made) && made->IsObject()) {
            info.GetReturnValue().Set(made);
            return;
        }
        v8::Local<v8::Object> modifiers;
        if (!Get().TemplatesFor(isolate).modifiers.Get(isolate)->NewInstance(context).ToLocal(&modifiers) || !modifiers->SetPrivate(context, HitHandleKey(isolate), v8::Integer::NewFromUnsigned(isolate, *handle)).FromMaybe(false)
            || !info.This()->SetPrivate(context, HitModifiersKey(isolate), modifiers).FromMaybe(false)) {
            return;
        }
        info.GetReturnValue().Set(modifiers);
    }

    void PlayerHitHandles::SetModifiers(const v8::FunctionCallbackInfo<v8::Value> &info) {
        Events::PlayerHitProposal *hit = Receiver(info);
        if (hit == nullptr || info.Length() < 1 || !info[0]->IsObject()) {
            return;
        }
        // Replaced whole: each scale the new object names is taken, and one
        // it leaves out stays as it was. It is read once, here; all of it or,
        // when reading it throws, none of it.
        v8::Isolate *isolate               = info.GetIsolate();
        v8::Local<v8::Context> context     = isolate->GetCurrentContext();
        const v8::Local<v8::Object> scales = info[0].As<v8::Object>();
        std::array<std::optional<float>, kHitModifierNames.size()> taken;
        for (std::uint32_t index = 0; index < kHitModifierNames.size(); ++index) {
            v8::Local<v8::Value> value;
            if (!scales->Get(context, v8pp::to_v8(isolate, kHitModifierNames[index])).ToLocal(&value)) {
                return;
            }
            taken[index] = HitNumber(value);
        }
        for (std::uint32_t index = 0; index < kHitModifierNames.size(); ++index) {
            if (taken[index]) {
                HitModifierAt(hit->modifiers, index) = *taken[index];
            }
        }
    }

    void PlayerHitHandles::GetModifier(const v8::FunctionCallbackInfo<v8::Value> &info) {
        if (Events::PlayerHitProposal *hit = ModifiersReceiver(info)) {
            info.GetReturnValue().Set(static_cast<double>(HitModifierAt(hit->modifiers, info.Data().As<v8::Uint32>()->Value())));
        }
    }

    void PlayerHitHandles::SetModifier(const v8::FunctionCallbackInfo<v8::Value> &info) {
        Events::PlayerHitProposal *hit   = ModifiersReceiver(info);
        const std::optional<float> scale = hit != nullptr && info.Length() > 0 ? HitNumber(info[0]) : std::nullopt;
        if (scale) {
            HitModifierAt(hit->modifiers, info.Data().As<v8::Uint32>()->Value()) = *scale;
        }
    }

    void PlayerHitHandles::UnregisterIsolate(v8::Isolate *isolate) {
        _templates.erase(isolate);
    }

    void PlayerHitHandles::Clear() {
        _templates.clear();
    }

    void PlayerHitHandles::RegisterTransfer() {
        Framework::Scripting::ValueTransfer::RegisterHostType(
            "kcdc.PlayerHit",
            [](v8::Isolate *isolate, v8::Local<v8::Object> object, std::vector<std::uint8_t> &bytes) {
                const std::optional<std::uint32_t> handle = HandleOf(isolate, object);
                if (!handle) {
                    return false;
                }
                bytes.resize(sizeof(*handle));
                std::memcpy(bytes.data(), &*handle, sizeof(*handle));
                return true;
            },
            [](v8::Isolate *isolate, v8::Local<v8::Context> context, const std::vector<std::uint8_t> &bytes) -> v8::MaybeLocal<v8::Value> {
                std::uint32_t handle = 0;
                std::memcpy(&handle, bytes.data(), std::min(bytes.size(), sizeof(handle)));
                return Get().Wrap(isolate, context, handle);
            });
    }

    void Events::PlayerInjured(std::uint64_t playerNetworkId, std::uint8_t bodyPart) {
        EmitReservedEvent("playerInjured", [playerNetworkId, bodyPart](v8::Isolate *isolate) {
            std::vector<v8::Local<v8::Value>> args;
            args.push_back(WrapPlayer(isolate, playerNetworkId));
            args.push_back(BodyPartToV8(isolate, bodyPart));
            return args;
        });
    }

    void Events::PlayerInjuryHealed(std::uint64_t playerNetworkId, std::uint8_t bodyPart) {
        EmitReservedEvent("playerInjuryHealed", [playerNetworkId, bodyPart](v8::Isolate *isolate) {
            std::vector<v8::Local<v8::Value>> args;
            args.push_back(WrapPlayer(isolate, playerNetworkId));
            args.push_back(BodyPartToV8(isolate, bodyPart));
            return args;
        });
    }

    void Events::PlayerSpawning(std::uint64_t playerNetworkId) {
        EmitPlayerEvent("playerSpawning", playerNetworkId);
    }

    void Events::PlayerSpawned(std::uint64_t playerNetworkId) {
        EmitPlayerEvent("playerSpawned", playerNetworkId);
    }

    void Events::PlayerReady(std::uint64_t playerNetworkId) {
        EmitPlayerEvent("playerReady", playerNetworkId);
    }

    bool Events::PlayerCarrying(std::uint64_t carrierNetworkId, std::uint64_t carriedNetworkId) {
        return EmitReservedEventSync("playerCarrying", [carrierNetworkId, carriedNetworkId](v8::Isolate *isolate) {
            return CarryArgs(isolate, carrierNetworkId, carriedNetworkId);
        });
    }

    void Events::PlayerCarried(std::uint64_t carrierNetworkId, std::uint64_t carriedNetworkId) {
        EmitReservedEvent("playerCarry", [carrierNetworkId, carriedNetworkId](v8::Isolate *isolate) {
            return CarryArgs(isolate, carrierNetworkId, carriedNetworkId);
        });
    }

    void Events::PlayerPutDown(std::uint64_t carrierNetworkId, std::uint64_t carriedNetworkId) {
        EmitReservedEvent("playerPutDown", [carrierNetworkId, carriedNetworkId](v8::Isolate *isolate) {
            return CarryArgs(isolate, carrierNetworkId, carriedNetworkId);
        });
    }

    bool Events::PlayerCarryingItem(std::uint64_t playerNetworkId, const Shared::RPC::ItemClassId &itemClass, std::uint64_t groundItemNetworkId) {
        return EmitReservedEventSync("playerCarryingItem", [playerNetworkId, itemClass, groundItemNetworkId](v8::Isolate *isolate) {
            return ItemCarryArgs(isolate, playerNetworkId, itemClass, groundItemNetworkId);
        });
    }

    void Events::PlayerCarriedItem(std::uint64_t playerNetworkId, const Shared::RPC::ItemClassId &itemClass, std::uint64_t groundItemNetworkId) {
        EmitReservedEvent("playerCarryItem", [playerNetworkId, itemClass, groundItemNetworkId](v8::Isolate *isolate) {
            return ItemCarryArgs(isolate, playerNetworkId, itemClass, groundItemNetworkId);
        });
    }

    void Events::PlayerPutDownItem(std::uint64_t playerNetworkId, const Shared::RPC::ItemClassId &itemClass, std::uint64_t groundItemNetworkId) {
        EmitReservedEvent("playerPutDownItem", [playerNetworkId, itemClass, groundItemNetworkId](v8::Isolate *isolate) {
            return ItemCarryArgs(isolate, playerNetworkId, itemClass, groundItemNetworkId);
        });
    }

    void Events::PlayerAnimationEnded(std::uint64_t playerNetworkId, const std::string &fragment, Shared::Game::AnimationEndReason reason) {
        EmitReservedEvent("playerAnimationEnd", [playerNetworkId, &fragment, reason](v8::Isolate *isolate) {
            std::vector<v8::Local<v8::Value>> args;
            args.push_back(WrapPlayer(isolate, playerNetworkId));
            args.push_back(v8pp::to_v8(isolate, fragment));
            args.push_back(v8pp::to_v8(isolate, std::string(Shared::Game::AnimationEndReasonName(reason))));
            return args;
        });
    }
} // namespace KCDC::Scripting
