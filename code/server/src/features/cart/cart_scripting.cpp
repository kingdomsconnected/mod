#include "cart_scripting.h"

#include "core/scripting/script_context.h"
#include "core/scripting/wrap.h"
#include "shared/scripting/rotation_arg.h"

#include "features/cart/cart_verbs.h"

#include "shared/game/cart_blueprints.h"
#include "shared/scripting/catalog.h"

#include <core_modules.h>
#include <networking/replication/replication_manager.h>
#include <scripting/v8pp_types.h>

#include <v8pp/convert.hpp>

#include <cstdint>
#include <cstring>
#include <optional>
#include <sstream>
#include <vector>

namespace KCDC::Scripting {

    namespace {
        // The one shape of both lifetime events: the cart, nothing else.
        void EmitCartEvent(const char *eventName, std::uint64_t cartNetworkId) {
            EmitReservedEvent(eventName, [cartNetworkId](v8::Isolate *isolate) {
                std::vector<v8::Local<v8::Value>> args;
                args.push_back(WrapCart(isolate, cartNetworkId));
                return args;
            });
        }

        // The seat events: the cart, the player, and which seat.
        std::vector<v8::Local<v8::Value>> SeatEventArgs(v8::Isolate *isolate, std::uint64_t cartNetworkId, std::uint64_t playerNetworkId, std::uint8_t seat) {
            std::vector<v8::Local<v8::Value>> args;
            args.push_back(WrapCart(isolate, cartNetworkId));
            args.push_back(WrapPlayer(isolate, playerNetworkId));
            args.push_back(v8pp::to_v8(isolate, CartSeatName(cartNetworkId, seat)));
            return args;
        }

        /** The seat a script names on `cart`, or kSeatCount for a name that is not one of its. */
        std::uint8_t ParseSeat(const Shared::Entities::CartEntity *cart, const std::string &name) {
            const std::optional<std::uint8_t> seat = cart != nullptr ? cart->Seats().Find(name) : std::nullopt;
            return seat.value_or(static_cast<std::uint8_t>(Shared::Entities::CartEntity::kSeatCount));
        }

        /** What the seat argument accepts on `cart`, for the errors that name it. */
        std::string SeatNames(const Shared::Entities::CartEntity *cart) {
            std::string names;
            const Shared::Game::CartSeatLayout layout = cart != nullptr ? cart->Seats() : Shared::Game::CartSeatLayout {};
            for (std::size_t seat = 0; seat < layout.count; ++seat) {
                names += (seat == 0 ? "'" : seat + 1 == layout.count ? " or '" : ", '") + std::string(layout.seats[seat].name) + "'";
            }
            return names;
        }

        std::optional<MafiaNet::VirtualWorldId> WorldArg(const v8::FunctionCallbackInfo<v8::Value> &info, int index) {
            if (info.Length() <= index || !info[index]->IsNumber()) {
                return std::nullopt;
            }
            return static_cast<MafiaNet::VirtualWorldId>(v8pp::from_v8<double>(info.GetIsolate(), info[index]));
        }

        Cart *Self(const v8::FunctionCallbackInfo<v8::Value> &info) {
            return v8pp::class_<Cart>::unwrap_object(info.GetIsolate(), info.This());
        }

        Framework::Scripting::Builtins::Entity *PlayerArg(const v8::FunctionCallbackInfo<v8::Value> &info, int index) {
            return info.Length() > index ? v8pp::class_<Framework::Scripting::Builtins::Entity>::unwrap_object(info.GetIsolate(), info[index]) : nullptr;
        }

        // JS: cart.destroy()
        void JS_Destroy(const v8::FunctionCallbackInfo<v8::Value> &info) {
            if (auto *self = Self(info)) {
                KCDC::Features::Cart::Carts::Destroy(self->ResolveCart());
            }
        }

        // JS: cart.putPlayer(player, seat) -> boolean
        void JS_PutPlayer(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);

            auto *self   = Self(info);
            auto *player = PlayerArg(info, 0);
            auto *cart   = self != nullptr ? self->ResolveCart() : nullptr;
            if (cart == nullptr || player == nullptr || info.Length() < 2 || !info[1]->IsString()) {
                isolate->ThrowError(v8pp::to_v8(isolate, std::string("Cart.putPlayer: expected (player, seat), the seat one of ") + SeatNames(cart)));
                return;
            }

            const std::uint8_t seat = ParseSeat(cart, v8pp::from_v8<std::string>(isolate, info[1]));
            if (seat >= Shared::Entities::CartEntity::kSeatCount) {
                isolate->ThrowError(v8pp::to_v8(isolate, std::string("Cart.putPlayer: this cart's seats are ") + SeatNames(cart)));
                return;
            }
            info.GetReturnValue().Set(KCDC::Features::Cart::Carts::Seat(cart, seat, player->GetId()));
        }

        // JS: cart.removePlayer(player) -> boolean
        void JS_RemovePlayer(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *self   = Self(info);
            auto *player = PlayerArg(info, 0);
            auto *cart   = self != nullptr ? self->ResolveCart() : nullptr;
            if (cart == nullptr || player == nullptr || cart->SeatOf(player->GetId()) >= Shared::Entities::CartEntity::kSeatCount) {
                info.GetReturnValue().Set(false);
                return;
            }
            info.GetReturnValue().Set(KCDC::Features::Cart::Carts::Unseat(player->GetId()));
        }

        // JS: cart.getOccupant(seat) -> Player | null
        void JS_GetOccupant(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);

            auto *self = Self(info);
            auto *cart = self != nullptr ? self->ResolveCart() : nullptr;
            if (cart == nullptr || info.Length() < 1 || !info[0]->IsString()) {
                info.GetReturnValue().SetNull();
                return;
            }
            const std::uint8_t seat = ParseSeat(cart, v8pp::from_v8<std::string>(isolate, info[0]));
            if (seat >= Shared::Entities::CartEntity::kSeatCount || cart->seats[seat] == 0) {
                info.GetReturnValue().SetNull();
                return;
            }
            info.GetReturnValue().Set(WrapPlayer(isolate, cart->seats[seat]));
        }

        // JS: cart.seatOf(player) -> string | null
        void JS_SeatOf(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            auto *self           = Self(info);
            auto *cart           = self != nullptr ? self->ResolveCart() : nullptr;
            auto *player         = PlayerArg(info, 0);
            const std::size_t at = cart != nullptr && player != nullptr ? cart->SeatOf(player->GetId()) : Shared::Entities::CartEntity::kSeatCount;
            if (at >= Shared::Entities::CartEntity::kSeatCount) {
                info.GetReturnValue().SetNull();
                return;
            }
            info.GetReturnValue().Set(v8pp::to_v8(isolate, CartSeatName(cart->GetNetworkID(), static_cast<std::uint8_t>(at))));
        }

        // JS: cart.teleport(position, rotation?) -> boolean
        void JS_Teleport(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);

            auto *self = Self(info);
            auto *cart = self != nullptr ? self->ResolveCart() : nullptr;
            if (cart == nullptr || info.Length() < 1 || !info[0]->IsObject()) {
                info.GetReturnValue().Set(false);
                return;
            }
            const glm::vec3 position = v8pp::convert<glm::vec3>::from_v8(isolate, info[0]);
            const glm::quat rotation = info.Length() >= 2 ? ParseRotationArg(isolate, info[1], cart->rotation) : cart->rotation;
            info.GetReturnValue().Set(KCDC::Features::Cart::Carts::Teleport(cart, position, rotation));
        }

        // JS: Cart.spawn(blueprint?, position?, rotation?, virtualWorld?) -> Cart
        void JS_Spawn(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);

            std::string name = std::string(Shared::Game::kDefaultCartBlueprint);
            if (info.Length() >= 1 && info[0]->IsString()) {
                name = v8pp::from_v8<std::string>(isolate, info[0]);
            }
            const Shared::Game::CartBlueprint *blueprint = Shared::Game::FindCartBlueprint(name);
            if (blueprint == nullptr) {
                isolate->ThrowError(v8pp::to_v8(isolate, "Cart.spawn: " + name + " is not a cart; Cart.blueprints() lists them"));
                return;
            }

            glm::vec3 position {0.0F};
            glm::quat rotation {1.0F, 0.0F, 0.0F, 0.0F};
            std::uint32_t virtualWorld = MafiaNet::VIRTUAL_WORLD_GLOBAL;
            if (info.Length() >= 2 && info[1]->IsObject()) {
                position = v8pp::convert<glm::vec3>::from_v8(isolate, info[1]);
            }
            if (info.Length() >= 3) {
                rotation = ParseRotationArg(isolate, info[2], rotation);
            }
            if (info.Length() >= 4 && info[3]->IsNumber()) {
                virtualWorld = static_cast<std::uint32_t>(v8pp::from_v8<double>(isolate, info[3]));
            }

            auto *cart = KCDC::Features::Cart::Carts::Spawn(*blueprint, position, rotation, virtualWorld);
            if (cart == nullptr) {
                isolate->ThrowError(v8pp::to_v8(isolate, "Cart.spawn: the cart could not be created, or a cartSpawn handler destroyed it"));
                return;
            }
            info.GetReturnValue().Set(WrapCart(isolate, cart->GetNetworkID()));
        }

        // JS: Cart.all(virtualWorld?) -> Cart[]
        void JS_All(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);
            v8::Local<v8::Context> context = isolate->GetCurrentContext();
            v8::Local<v8::Array> carts     = v8::Array::New(isolate);

            std::uint32_t index = 0;
            for (auto *cart : KCDC::Features::Cart::Carts::Collect(WorldArg(info, 0))) {
                carts->Set(context, index++, WrapCart(isolate, cart->GetNetworkID())).Check();
            }
            info.GetReturnValue().Set(carts);
        }

        // JS: Cart.getById(id) -> Cart | null
        void JS_GetById(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);

            if (info.Length() < 1 || !info[0]->IsNumber()) {
                isolate->ThrowError(v8pp::to_v8(isolate, "Cart.getById: expected (id)"));
                return;
            }
            const std::uint64_t networkId = static_cast<std::uint64_t>(info[0]->IntegerValue(isolate->GetCurrentContext()).FromMaybe(0));
            info.GetReturnValue().Set(WrapCart(isolate, networkId));
        }

        // JS: Cart.destroyAll(virtualWorld?) -> number
        void JS_DestroyAll(const v8::FunctionCallbackInfo<v8::Value> &info) {
            info.GetReturnValue().Set(static_cast<std::uint32_t>(KCDC::Features::Cart::Carts::DestroyAll(WorldArg(info, 0))));
        }

        // JS: Cart.blueprints() -> string[]
        void JS_Blueprints(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);
            v8::Local<v8::Context> context = isolate->GetCurrentContext();
            v8::Local<v8::Array> names     = v8::Array::New(isolate);

            std::uint32_t index = 0;
            for (const Shared::Game::CartBlueprint &blueprint : Shared::Game::g_CartBlueprints) {
                names->Set(context, index++, v8pp::to_v8(isolate, std::string(blueprint.name))).Check();
            }
            info.GetReturnValue().Set(names);
        }
    } // namespace

    std::string CartSeatName(std::uint64_t cartNetworkId, std::uint8_t seat) {
        const Shared::Entities::CartEntity *cart = KCDC::Features::Cart::Carts::Find(cartNetworkId);
        const Shared::Game::CartSeat *found      = cart != nullptr ? cart->Seats().At(seat) : nullptr;
        return found != nullptr ? std::string(found->name) : std::string();
    }

    Shared::Entities::CartEntity *Cart::ResolveCart() const {
        return dynamic_cast<Shared::Entities::CartEntity *>(Resolve());
    }

    std::string Cart::GetBlueprint() const {
        auto *cart = ResolveCart();
        return cart != nullptr ? cart->blueprint : std::string();
    }

    int Cart::GetHorseCount() const {
        auto *cart                                   = ResolveCart();
        const Shared::Game::CartBlueprint *blueprint = cart != nullptr ? Shared::Game::FindCartBlueprint(cart->blueprint) : nullptr;
        return blueprint != nullptr ? Shared::Game::CartHarnessSlots(*blueprint) : 0;
    }

    v8::Local<v8::Value> Cart::GetDriverHandle(v8::Isolate *isolate) const {
        v8::EscapableHandleScope handleScope(isolate);
        auto *cart = ResolveCart();
        if (cart == nullptr || cart->Driver() == 0) {
            return handleScope.Escape(v8::Null(isolate).As<v8::Value>());
        }
        return handleScope.Escape(WrapPlayer(isolate, cart->Driver()));
    }

    std::string Cart::GetPace() const {
        auto *cart = ResolveCart();
        if (cart == nullptr || !Shared::Entities::IsCartPace(cart->pace)) {
            return "stand";
        }
        switch (static_cast<Shared::Entities::CartPace>(cart->pace)) {
        case Shared::Entities::CartPace::Walk: return "walk";
        case Shared::Entities::CartPace::Trot: return "trot";
        case Shared::Entities::CartPace::Reverse: return "reverse";
        case Shared::Entities::CartPace::Stand: break;
        }
        return "stand";
    }

    float Cart::GetMaxSpeed() const {
        auto *cart = ResolveCart();
        return cart != nullptr ? cart->maxSpeed : Shared::Entities::CartEntity::kDefaultMaxSpeed;
    }

    void Cart::SetMaxSpeed(float maxSpeed) {
        KCDC::Features::Cart::Carts::SetMaxSpeed(ResolveCart(), maxSpeed);
    }

    std::vector<std::string> Cart::GetSeatNames() const {
        std::vector<std::string> names;
        auto *cart                                = ResolveCart();
        const Shared::Game::CartSeatLayout layout = cart != nullptr ? cart->Seats() : Shared::Game::CartSeatLayout {};
        for (std::size_t seat = 0; seat < layout.count; ++seat) {
            names.emplace_back(layout.seats[seat].name);
        }
        return names;
    }

    std::string Cart::ToString() const {
        std::ostringstream ss;
        auto *cart = ResolveCart();
        ss << "Cart{ id: " << GetId() << ", blueprint: '" << GetBlueprint() << "', driver: " << (cart != nullptr ? cart->Driver() : 0) << ", pace: " << GetPace() << " }";
        return ss.str();
    }

    v8pp::class_<Cart> &Cart::GetClass(v8::Isolate *isolate) {
        auto &_class = _classes[isolate];
        if (!_class) {
            // v8pp's inherit<Entity> needs the framework Entity registered first.
            Framework::Scripting::Builtins::Entity::GetClass(isolate);

            _class = std::make_unique<v8pp::class_<Cart>>(isolate, ServerCatalog(), "Cart", "Replicated cart or wagon handle.");
            _class->auto_wrap_objects(true);
            _class->inherit<Framework::Scripting::Builtins::Entity>()
                .ctor<std::uint64_t>(v8pp::metadata::docs("void", {v8pp::metadata::param("id", "number", false, "Network entity identifier.")}, "Creates a script wrapper for an existing cart with this ID; use Cart.spawn() to spawn one."))
                .function("toString", &Cart::ToString, v8pp::metadata::docs("string", {}, "Formats this cart handle for logging and debugging.", "The cart ID, its blueprint, its driver and its pace."));

            _class->prototype_function("destroy", &JS_Destroy, v8pp::metadata::docs("void", {}, "Despawns this cart on every client. Everyone in it is let out first, each with a `cartExit`, then `cartDestroy` is raised."));
            _class->prototype_function("putPlayer", &JS_PutPlayer,
                v8pp::metadata::docs("boolean",
                    {
                        v8pp::metadata::param("player", "Player", false, "The player to seat."),
                        v8pp::metadata::param("seat", "string", false,
                            "One of the cart's `seats`. A wagon has `driver` (the reins, on the bench's left), `bench` (beside them), `rightBack`, `leftBack`, `rightFront` and `leftFront` (on the rails); the two-wheeler `driver`, `rightBack` and `leftBack`."),
                    },
                    "Puts a player in a seat. They are taken out of any cart they were in, their own game walks them to the seat and climbs in, and `cartEnter` follows; the driver's client runs the cart from then on. `cartEntering` handlers are asked first.",
                    "Whether the player was seated: false for a taken seat, a player that is not connected, or a handler's refusal."));
            _class->prototype_function("removePlayer", &JS_RemovePlayer,
                v8pp::metadata::docs("boolean", {v8pp::metadata::param("player", "Player", false, "The player to let out.")}, "Lets a player out of this cart: their own game climbs down, and `cartExit` is raised.", "False when the player is not in this cart."));
            _class->prototype_function("getOccupant", &JS_GetOccupant, v8pp::metadata::docs("Player | null", {v8pp::metadata::param("seat", "string", false, "One of the cart's `seats`.")}, "Who sits in a seat.", "The player in it, or null when it is empty."));
            _class->prototype_function("seatOf", &JS_SeatOf,
                v8pp::metadata::docs("string | null", {v8pp::metadata::param("player", "Player", false, "The player to look for.")}, "Where a player sits in this cart.", "The seat's name, one of `seats`, or null when they are not in it."));
            _class->prototype_function("teleport", &JS_Teleport,
                v8pp::metadata::docs("boolean",
                    {
                        v8pp::metadata::param("position", "Vector3", false, "Where the cart's front axle stands."),
                        v8pp::metadata::param("rotation", "Vector3 | Quaternion", true, "Which way it faces; omitted keeps its heading."),
                    },
                    "Moves the cart outright, standing, with everyone in it; a driver carries on from there.", "False for a pose that is not finite."));

            _class->property("blueprint", &Cart::GetBlueprint, v8pp::metadata::property_docs("string", "The game prefab this cart was built from, e.g. `wagon_b_covered`. `Cart.blueprints()` lists them all."));
            _class->property("horseCount", &Cart::GetHorseCount,
                v8pp::metadata::property_docs("number", "How many horses the cart is harnessed for: 2 for a wagon, 1 for the two-wheeled cart, 0 for `wagon_b_covered_empty`, whose chassis has nowhere to hitch one and so can be sat in but never pulled."));
            _class->property(
                "driver",
                [](const Cart &self, v8::Isolate *isolate) {
                    return self.GetDriverHandle(isolate);
                },
                v8pp::metadata::property_docs("Player | null", "The player at the reins, or null when nobody drives it."));
            _class->property("pace", &Cart::GetPace, v8pp::metadata::property_docs("string", "How the driver last asked the cart to go: `stand`, `walk`, `trot` or `reverse`."));
            _class->property("seats", &Cart::GetSeatNames, v8pp::metadata::property_docs("string[]", "The names of this cart's seats, the reins first: six on a wagon, three on the two-wheeler."));
            _class->property("maxSpeed", &Cart::GetMaxSpeed, &Cart::SetMaxSpeed,
                v8pp::metadata::property_docs("number", "The fastest the driver may take it urged on (the trot key held), in metres a second, 0 to 15; 7 by default. Unurged the team trots at 3.6 or this, whichever is less. A value outside the range is ignored."));
        }
        return *_class;
    }

    void Cart::UnregisterIsolate(v8::Isolate *isolate) {
        _classes.erase(isolate);
    }

    void Cart::Register(v8::Isolate *isolate, v8::Local<v8::Object> global) {
        if (isolate == nullptr || global.IsEmpty()) {
            return;
        }

        auto &events = ServerCatalog().data_type("EventMap");
        events.add_property("cartSpawn", "[cart: Cart]", "Dispatched immediately after a cart is created and replicated, whether by `Cart.spawn`, the `/cart` command, or anything else.");
        events.add_property("cartDestroy", "[cart: Cart]", "Dispatched while a cart is being despawned, after everyone in it has been let out. The handle still resolves, so its blueprint and its pose can be read one last time.");
        events.add_property("cartEntering", "[cart: Cart, player: Player, seat: string]",
            "Dispatched before a player is put in a seat -- by their own use of the cart's prompt, or `putPlayer`. Return `false` from a handler and the seat is refused: nothing changes, and the player's game never climbs in. Handlers run synchronously.");
        events.add_property("cartEnter", "[cart: Cart, player: Player | null, seat: string]", "Dispatched after a player is given a seat, named as in `cart.seats`; the `driver` seat's client runs the cart from here on.");
        events.add_property("cartExit", "[cart: Cart, player: Player | null, seat: string]", "Dispatched after a player leaves a seat: their own climb down, `removePlayer`, a disconnect, or the cart being destroyed.");

        v8pp::class_<Cart> &cls = GetClass(isolate);

        cls.static_function("spawn", &JS_Spawn,
            v8pp::metadata::docs("Cart",
                {
                    v8pp::metadata::param("blueprint", "string", true, "Which of the game's cart prefabs to build; omitted builds `wagon_b_covered`. `Cart.blueprints()` lists them."),
                    v8pp::metadata::param("position", "Vector3 | Partial<Vector3>", true, "Where the cart's front axle stands; omitted components default to zero."),
                    v8pp::metadata::param("rotation", "Vector3 | Quaternion", true, "Which way it faces: a Quaternion, or a Vector3 of Euler angles in degrees."),
                    v8pp::metadata::param("virtualWorld", "number", true, "Optional virtual world the cart belongs to; omitted puts it in the global one."),
                },
                "Spawns and replicates a cart or wagon from the game's own prefabs, with its horses already in the shafts. It stands where it was put until somebody takes the reins.", "The newly spawned cart handle."));
        cls.static_function("all", &JS_All,
            v8pp::metadata::docs("Cart[]", {v8pp::metadata::param("virtualWorld", "number", true, "Optional virtual world to list; omitted lists every one.")}, "Lists the carts the server has.", "One handle per live cart, in no particular order."));
        cls.static_function("getById", &JS_GetById, v8pp::metadata::docs("Cart | null", {v8pp::metadata::param("id", "number", false, "Network entity identifier.")}, "Looks a cart up by its network entity ID.", "The cart's handle, or null when no live cart has that ID."));
        cls.static_function("destroyAll", &JS_DestroyAll,
            v8pp::metadata::docs("number", {v8pp::metadata::param("virtualWorld", "number", true, "Optional virtual world to clear; omitted clears every one of them.")}, "Despawns carts, emitting cartDestroy for each one.", "How many carts were removed."));
        cls.static_function("blueprints", &JS_Blueprints, v8pp::metadata::docs("string[]", {}, "Names every cart prefab `Cart.spawn` can build.", "The blueprint names, in catalog order."));
        cls.publish(global);
    }

    void Events::CartSpawned(std::uint64_t cartNetworkId) {
        EmitCartEvent("cartSpawn", cartNetworkId);
    }

    void Events::CartDestroyed(std::uint64_t cartNetworkId) {
        EmitCartEvent("cartDestroy", cartNetworkId);
    }

    bool Events::CartEntering(std::uint64_t cartNetworkId, std::uint64_t playerNetworkId, std::uint8_t seat) {
        return EmitReservedEventSync("cartEntering", [cartNetworkId, playerNetworkId, seat](v8::Isolate *isolate) {
            return SeatEventArgs(isolate, cartNetworkId, playerNetworkId, seat);
        });
    }

    void Events::CartEntered(std::uint64_t cartNetworkId, std::uint64_t playerNetworkId, std::uint8_t seat) {
        EmitReservedEvent("cartEnter", [cartNetworkId, playerNetworkId, seat](v8::Isolate *isolate) {
            return SeatEventArgs(isolate, cartNetworkId, playerNetworkId, seat);
        });
    }

    void Events::CartExited(std::uint64_t cartNetworkId, std::uint64_t playerNetworkId, std::uint8_t seat) {
        EmitReservedEvent("cartExit", [cartNetworkId, playerNetworkId, seat](v8::Isolate *isolate) {
            return SeatEventArgs(isolate, cartNetworkId, playerNetworkId, seat);
        });
    }
} // namespace KCDC::Scripting
