#include "ground_item_scripting.h"

#include "shared/scripting/rotation_arg.h"
#include "core/scripting/script_context.h"
#include "core/scripting/wrap.h"

#include "features/ground_item/ground_item_verbs.h"
#include "features/player/player_roster.h"

#include "shared/features/ground_item/ground_item_definition.h"
#include "shared/game/animation_catalog.h"
#include "shared/game/item_classes.h"
#include "shared/item_stack.h"
#include "shared/scripting/catalog.h"
#include "shared/features/player/player_bindings.h"

#include <core_modules.h>
#include <networking/replication/replication_manager.h>
#include <scripting/v8pp_types.h>

#include <v8pp/convert.hpp>

#include <cstdint>
#include <optional>
#include <sstream>
#include <vector>

namespace KCDC::Scripting {

    namespace {
        // The shape the spawn and destroy events have: one subject, nothing else.
        void EmitGroundItemEvent(const char *eventName, std::uint64_t groundItemNetworkId) {
            EmitReservedEvent(eventName, [groundItemNetworkId](v8::Isolate *isolate) {
                std::vector<v8::Local<v8::Value>> args;
                args.push_back(WrapGroundItem(isolate, groundItemNetworkId));
                return args;
            });
        }

        // The virtual world an argument names, or none for every one of them.
        std::optional<MafiaNet::VirtualWorldId> WorldArg(const v8::FunctionCallbackInfo<v8::Value> &info, int index) {
            if (info.Length() <= index || !info[index]->IsNumber()) {
                return std::nullopt;
            }
            return static_cast<MafiaNet::VirtualWorldId>(v8pp::from_v8<double>(info.GetIsolate(), info[index]));
        }

        // The handle a prototype function was called on, or nullptr.
        GroundItem *Self(const v8::FunctionCallbackInfo<v8::Value> &info) {
            return v8pp::class_<GroundItem>::unwrap_object(info.GetIsolate(), info.This());
        }

        // One number out of the trailing properties bag, left alone when it names nothing.
        void ReadProperty(v8::Isolate *isolate, v8::Local<v8::Object> properties, const char *name, double &value) {
            v8::Local<v8::Context> context = isolate->GetCurrentContext();
            v8::Local<v8::Value> found;
            if (properties->Get(context, v8pp::to_v8(isolate, name)).ToLocal(&found) && found->IsNumber()) {
                value = v8pp::from_v8<double>(isolate, found);
            }
        }

        // JS: groundItem.destroy()
        void JS_Destroy(const v8::FunctionCallbackInfo<v8::Value> &info) {
            if (auto *self = Self(info)) {
                KCDC::Features::GroundItem::GroundItems::Destroy(self->ResolveGroundItem());
            }
        }

        // JS: GroundItem.spawn(item, position, rotation, amount, virtualWorld, properties) -> GroundItem
        void JS_Spawn(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);

            if (info.Length() < 1 || !info[0]->IsString()) {
                isolate->ThrowError(v8pp::to_v8(isolate, "GroundItem.spawn: expected (item, position?, rotation?, amount?, virtualWorld?, properties?)"));
                return;
            }

            // The same resolver `/give` and Player.giveItem take, so one spelling of an item works everywhere.
            const std::string query                       = v8pp::from_v8<std::string>(isolate, info[0]);
            const Shared::Game::ItemClassEntry *itemClass = KCDC::Features::Player::Players::ResolveItemClass(query);
            if (itemClass == nullptr) {
                isolate->ThrowError(v8pp::to_v8(isolate, "GroundItem.spawn: " + query + " is not an item class in the game's tables"));
                return;
            }

            glm::vec3 position {0.0F};
            glm::quat rotation {1.0F, 0.0F, 0.0F, 0.0F};
            std::uint32_t amount       = 1;
            std::uint32_t virtualWorld = MafiaNet::VIRTUAL_WORLD_GLOBAL;

            // Called directly for the reason ParseRotationArg gives.
            if (info.Length() >= 2 && info[1]->IsObject()) {
                position = v8pp::convert<glm::vec3>::from_v8(isolate, info[1]);
            }
            if (info.Length() >= 3) {
                rotation = ParseRotationArg(isolate, info[2], rotation);
            }
            if (info.Length() >= 4 && info[3]->IsNumber()) {
                amount = static_cast<std::uint32_t>(info[3]->IntegerValue(isolate->GetCurrentContext()).FromMaybe(1));
            }
            if (info.Length() >= 5 && info[4]->IsNumber()) {
                virtualWorld = static_cast<std::uint32_t>(v8pp::from_v8<double>(isolate, info[4]));
            }

            Shared::RPC::InventoryItem item;
            item.itemClass = Shared::Game::ItemClassIdFromGuid(itemClass->guid);
            item.amount    = amount;

            // -1 is the wire's "whatever the class says", which is what an unset quality, health or condition means here too.
            double quality   = 0.0;
            double health    = -1.0;
            double condition = -1.0;
            bool carryable   = false;
            std::string name;
            if (info.Length() >= 6 && info[5]->IsObject()) {
                v8::Local<v8::Object> properties = info[5].As<v8::Object>();
                ReadProperty(isolate, properties, "quality", quality);
                ReadProperty(isolate, properties, "health", health);
                ReadProperty(isolate, properties, "condition", condition);
                v8::Local<v8::Value> value;
                if (properties->Get(isolate->GetCurrentContext(), v8pp::to_v8(isolate, "carryable")).ToLocal(&value) && !value->IsUndefined()) {
                    carryable = value->BooleanValue(isolate);
                }
                if (properties->Get(isolate->GetCurrentContext(), v8pp::to_v8(isolate, "name")).ToLocal(&value) && value->IsString()) {
                    name = v8pp::from_v8<std::string>(isolate, value);
                    if (name.size() > Shared::Game::ItemPlacementDefinition::kMaxNameLength) {
                        isolate->ThrowError(v8pp::to_v8(isolate, "GroundItem.spawn: name is longer than 64 bytes"));
                        return;
                    }
                }
            }
            // A prop is carried one at a time, and only one the game has a
            // carrying walk for is worth offering to a pair of arms.
            if (carryable && (amount != 1 || Shared::Game::CarryWalkBodies(Shared::Game::ItemClassIdFromGuid(itemClass->guid)) == 0)) {
                isolate->ThrowError(v8pp::to_v8(isolate, "GroundItem.spawn: " + query + " is not carried in the arms, or not as one; see Animations.props() and player.carryItem"));
                return;
            }
            item.quality   = static_cast<std::uint32_t>(quality);
            item.health    = static_cast<float>(health);
            item.condition = static_cast<float>(condition);

            auto *groundItem = KCDC::Features::GroundItem::GroundItems::Spawn(item, position, rotation, virtualWorld, carryable, name);
            if (groundItem == nullptr) {
                isolate->ThrowError(v8pp::to_v8(isolate, "GroundItem.spawn: no client could build " + query + " as described (check the amount, health and condition), or a groundItemSpawn handler took it away"));
                return;
            }

            info.GetReturnValue().Set(WrapGroundItem(isolate, groundItem->GetNetworkID()));
        }

        // JS: GroundItem.all() -> GroundItem[]
        void JS_All(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);
            v8::Local<v8::Context> context   = isolate->GetCurrentContext();
            v8::Local<v8::Array> groundItems = v8::Array::New(isolate);

            std::uint32_t index = 0;
            for (auto *groundItem : KCDC::Features::GroundItem::GroundItems::Collect(std::nullopt)) {
                groundItems->Set(context, index++, WrapGroundItem(isolate, groundItem->GetNetworkID())).Check();
            }

            info.GetReturnValue().Set(groundItems);
        }

        // JS: GroundItem.getById(id) -> GroundItem | null
        void JS_GetById(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);

            if (info.Length() < 1 || !info[0]->IsNumber()) {
                isolate->ThrowError(v8pp::to_v8(isolate, "GroundItem.getById: expected (id)"));
                return;
            }

            const std::uint64_t networkId = static_cast<std::uint64_t>(info[0]->IntegerValue(isolate->GetCurrentContext()).FromMaybe(0));
            info.GetReturnValue().Set(WrapGroundItem(isolate, networkId));
        }

        // JS: GroundItem.destroyAll(virtualWorld) -> number
        void JS_DestroyAll(const v8::FunctionCallbackInfo<v8::Value> &info) {
            info.GetReturnValue().Set(static_cast<std::uint32_t>(KCDC::Features::GroundItem::GroundItems::DestroyAll(WorldArg(info, 0))));
        }
    } // namespace

    Shared::Entities::GroundItemEntity *GroundItem::ResolveGroundItem() const {
        return dynamic_cast<Shared::Entities::GroundItemEntity *>(Resolve());
    }

    std::string GroundItem::GetItemClass() const {
        auto *groundItem = ResolveGroundItem();
        return groundItem != nullptr ? Detail::FormatItemClass(groundItem->item.itemClass) : std::string();
    }

    std::uint32_t GroundItem::GetAmount() const {
        auto *groundItem = ResolveGroundItem();
        return groundItem != nullptr ? groundItem->item.amount : 0;
    }

    std::uint32_t GroundItem::GetQuality() const {
        auto *groundItem = ResolveGroundItem();
        return groundItem != nullptr ? groundItem->item.quality : 0;
    }

    float GroundItem::GetHealth() const {
        auto *groundItem = ResolveGroundItem();
        return groundItem != nullptr ? groundItem->item.health : -1.0F;
    }

    float GroundItem::GetCondition() const {
        auto *groundItem = ResolveGroundItem();
        return groundItem != nullptr ? groundItem->item.condition : -1.0F;
    }

    bool GroundItem::GetResting() const {
        auto *groundItem = ResolveGroundItem();
        return groundItem != nullptr && groundItem->resting;
    }

    bool GroundItem::GetCarryable() const {
        auto *groundItem = ResolveGroundItem();
        return groundItem != nullptr && groundItem->carryable;
    }

    std::string GroundItem::GetName() const {
        auto *groundItem = ResolveGroundItem();
        return groundItem != nullptr ? groundItem->name : std::string();
    }

    std::uint64_t GroundItem::GetDroppedById() const {
        auto *groundItem = ResolveGroundItem();
        return groundItem != nullptr ? groundItem->sourcePlayerNetworkId : 0;
    }

    v8::Local<v8::Value> GroundItem::GetDroppedByHandle(v8::Isolate *isolate) const {
        v8::EscapableHandleScope handleScope(isolate);
        return handleScope.Escape(WrapPlayer(isolate, GetDroppedById()));
    }

    std::string GroundItem::ToString() const {
        std::ostringstream ss;
        ss << "GroundItem{ id: " << GetId() << ", itemClass: " << GetItemClass() << ", amount: " << GetAmount() << ", resting: " << (GetResting() ? "true" : "false") << " }";
        return ss.str();
    }

    v8pp::class_<GroundItem> &GroundItem::GetClass(v8::Isolate *isolate) {
        auto &_class = _classes[isolate];
        if (!_class) {
            // v8pp's inherit<Entity> needs the framework Entity registered first.
            Framework::Scripting::Builtins::Entity::GetClass(isolate);

            _class = std::make_unique<v8pp::class_<GroundItem>>(isolate, ServerCatalog(), "GroundItem", "Replicated world stack handle.");
            _class->auto_wrap_objects(true);
            _class->inherit<Framework::Scripting::Builtins::Entity>()
                .ctor<std::uint64_t>(v8pp::metadata::docs("void", {v8pp::metadata::param("id", "number", false, "Network entity identifier.")}, "Creates a script wrapper for an existing ground item with this ID; use GroundItem.spawn() to spawn one."))
                .function("toString", &GroundItem::ToString, v8pp::metadata::docs("string", {}, "Formats this ground item handle for logging and debugging.", "The stack ID, its item class, how many of it there are and whether it has settled."));

            _class->prototype_function("destroy", &JS_Destroy, v8pp::metadata::docs("void", {}, "Takes this stack back out of the world on every client after emitting groundItemDestroy."));

            _class->property("itemClass", &GroundItem::GetItemClass,
                v8pp::metadata::property_docs("string",
                    "The class of item lying here, as the same 32 hex digits `player.rightHandItem` and `player.equipment` use, so the three can be compared directly. The server holds no name for a class: naming one is an item database's job."));
            _class->property("amount", &GroundItem::GetAmount, v8pp::metadata::property_docs("number", "How many units are in the stack. One entity however many that is: a stack is picked up whole or not at all."));
            _class->property("quality", &GroundItem::GetQuality, v8pp::metadata::property_docs("number", "Quality the stack was made with, or 0 when it was left to the class's own."));
            _class->property("health", &GroundItem::GetHealth, v8pp::metadata::property_docs("number", "Absolute item health from 0 to 1, or -1 when it was left to the class's own."));
            _class->property("condition", &GroundItem::GetCondition, v8pp::metadata::property_docs("number", "Displayed condition from 0 to 1, or -1 when it was left to the class's own."));
            _class->property("resting", &GroundItem::GetResting,
                v8pp::metadata::property_docs("boolean",
                    "Whether the stack has settled where it will stay. A stack a script spawned is resting from the start; one a player dropped is false while the nearest client simulates its fall, and its position moves until then. A resting stack's pose is the server's and does not move again."));
            _class->property("carryable", &GroundItem::GetCarryable,
                v8pp::metadata::property_docs("boolean", "Whether this is a prop people carry in their arms -- one a carry put down, or one spawned `carryable` -- rather than stock. Taking it up is a carry: the game's pick-up prompt or `player.carryItem(groundItem)`, never an inventory."));
            _class->property("name", &GroundItem::GetName, v8pp::metadata::property_docs("string", "The name `GroundItem.spawn` or a World Builder export gave the stack, so a script finds it again; empty for none."));
            _class->property("droppedById", &GroundItem::GetDroppedById, v8pp::metadata::property_docs("number", "Network ID of the player who dropped this stack, or 0 when the server spawned it."));
            _class->property(
                "droppedBy",
                [](const GroundItem &self, v8::Isolate *isolate) {
                    return self.GetDroppedByHandle(isolate);
                },
                v8pp::metadata::property_docs("Player | null", "The player who dropped this stack, or null when the server spawned it or that player has since left."));
        }
        return *_class;
    }

    void GroundItem::UnregisterIsolate(v8::Isolate *isolate) {
        _classes.erase(isolate);
    }

    void GroundItem::Register(v8::Isolate *isolate, v8::Local<v8::Object> global) {
        if (isolate == nullptr || global.IsEmpty()) {
            return;
        }

        auto &events = ServerCatalog().data_type("EventMap");
        events.add_property("groundItemSpawn", "[groundItem: GroundItem]", "Dispatched immediately after a stack is laid in the world and replicated, whether by `GroundItem.spawn`, the `/drop` command, or a player dropping something from their own inventory.");
        events.add_property("groundItemDestroy", "[groundItem: GroundItem]", "Dispatched while a stack is being taken out of the world, including the removal a completed pickup performs. The handle still resolves, so its item and its pose can be read one last time.");
        events.add_property("groundItemPickup", "[groundItem: GroundItem, player: Player | null]",
            "Dispatched when a player's pickup has been granted and the stack is about to go, so what was taken and by whom can both still be read. It reports a pickup rather than deciding one -- the server has already told that client the stack is theirs by the time this is raised -- and `groundItemDestroy` follows it.");

        v8pp::class_<GroundItem> &cls = GetClass(isolate);

        cls.static_function("spawn", &JS_Spawn,
            v8pp::metadata::docs("GroundItem",
                {
                    v8pp::metadata::param("item", "string", false, "Item class to lay down, as either its GUID or its name (`arrow_crude`). The same spelling `player.giveItem` takes."),
                    v8pp::metadata::param("position", "Vector3 | Partial<Vector3>", true, "Optional world-space spawn position; omitted components default to zero. The stack is placed there rather than dropped, so put it where the ground is."),
                    v8pp::metadata::param("rotation", "Vector3 | Quaternion", true, "Optional resting orientation: a Quaternion, or a Vector3 of Euler angles in degrees."),
                    v8pp::metadata::param("amount", "number", true, "Optional number of units in the stack, from 1 to 10000; omitted lays down one. A stack is picked up whole."),
                    v8pp::metadata::param("virtualWorld", "number", true, "Optional virtual world the stack belongs to; omitted puts it in the global one."),
                    v8pp::metadata::param("properties", "{ quality?: number; health?: number; condition?: number; carryable?: boolean; name?: string }", true,
                        "Optional condition of the item itself: `quality` as the game grades it, `health` and `condition` from 0 to 1. Each defaults to the class's own. Arrows and other missile classes are held to tighter bounds by the receiving client and are given a quality of 1 and full health when these are left out, since a stack outside those bounds is one no client would build. `carryable` makes it a prop people carry in their arms rather than stock: the game's pick-up prompt over it starts a carry (raising `playerCarryingItem`) and never puts it in an inventory. Only for a single item `player.carryItem` takes by name. `name`, up to 64 bytes of UTF-8, is what `groundItem.name` reads back, so a script finds the stack again."),
                },
                "Lays a pickable stack of an item on the ground and replicates it, spawning it already at rest.", "The newly spawned ground item handle. Throws when the item is not a class in the game's tables, or when no client could build the stack as described."));
        cls.static_function("all", &JS_All, v8pp::metadata::docs("GroundItem[]", {}, "Lists every stack lying in the world, however it got there.", "One handle per live stack, in no particular order."));
        cls.static_function("getById", &JS_GetById,
            v8pp::metadata::docs("GroundItem | null", {v8pp::metadata::param("id", "number", false, "Network entity identifier.")}, "Looks a stack up by its network entity ID.", "The stack's handle, or null when no live stack has that ID."));
        cls.static_function("destroyAll", &JS_DestroyAll,
            v8pp::metadata::docs("number", {v8pp::metadata::param("virtualWorld", "number", true, "Optional virtual world to clear; omitted clears every one of them.")}, "Removes stacks from the world, emitting groundItemDestroy for each one.", "How many stacks were removed."));
        cls.publish(global);
    }

    void Events::GroundItemSpawned(std::uint64_t groundItemNetworkId) {
        EmitGroundItemEvent("groundItemSpawn", groundItemNetworkId);
    }

    void Events::GroundItemDestroyed(std::uint64_t groundItemNetworkId) {
        EmitGroundItemEvent("groundItemDestroy", groundItemNetworkId);
    }

    void Events::GroundItemPickedUp(std::uint64_t groundItemNetworkId, std::uint64_t playerNetworkId) {
        EmitReservedEvent("groundItemPickup", [groundItemNetworkId, playerNetworkId](v8::Isolate *isolate) {
            std::vector<v8::Local<v8::Value>> args;
            args.push_back(WrapGroundItem(isolate, groundItemNetworkId));
            args.push_back(WrapPlayer(isolate, playerNetworkId));
            return args;
        });
    }
} // namespace KCDC::Scripting
