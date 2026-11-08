#include "stash_scripting.h"

#include "core/scripting/script_context.h"
#include "core/scripting/wrap.h"
#include "core/server.h"
#include "features/inventory/inventory_scripting.h"
#include "features/inventory/inventory_service.h"
#include "features/player/player_scripting.h"
#include "shared/scripting/rotation_arg.h"

#include "shared/game/cry_guid_text.h"
#include "shared/game/item_classes.h"
#include "shared/game/stash_catalog.h"
#include "shared/scripting/catalog.h"
#include "shared/scripting/level_guid.h"
#include "shared/scripting/virtual_world_arg.h"

#include <logging/logger.h>
#include <scripting/v8pp_types.h>

#include <v8pp/convert.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <sstream>
#include <vector>

namespace KCDC::Scripting {

    namespace {
        // How far `Stash.nearest` looks when the caller names no distance.
        constexpr float kDefaultNearestDistance = 5.0F;

        // Unguarded on purpose: PostInit installs the stash service and publishes the server pointer before the scripting engine exists, so no script call can run ahead of either.
        KCDC::Features::Stash::StashService &ResolveStashes() {
            return Server::Get()->GetStashes();
        }

        // The handle a prototype function was called on, or nullptr.
        Stash *Self(const v8::FunctionCallbackInfo<v8::Value> &info) {
            return v8pp::class_<Stash>::unwrap_object(info.GetIsolate(), info.This());
        }

        // The virtual world an argument names, or none for every one of them.
        std::optional<MafiaNet::VirtualWorldId> WorldArg(const v8::FunctionCallbackInfo<v8::Value> &info, int index) {
            if (info.Length() <= index || !info[index]->IsNumber()) {
                return std::nullopt;
            }
            return static_cast<MafiaNet::VirtualWorldId>(v8pp::from_v8<double>(info.GetIsolate(), info[index]));
        }

        // JS: stash.destroy()
        void JS_Destroy(const v8::FunctionCallbackInfo<v8::Value> &info) {
            if (auto *self = Self(info)) {
                ResolveStashes().Despawn(self->ResolveStash());
            }
        }

        // JS: stash.getInventory() -> InventoryContents | null
        void JS_GetInventory(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *self  = Self(info);
            auto *stash = self != nullptr ? self->ResolveStash() : nullptr;
            auto *stock = stash != nullptr ? ResolveStashes().ContentsOf(*stash) : nullptr;
            info.GetReturnValue().Set(stock != nullptr ? InventoryValue(info.GetIsolate(), KCDC::Features::Inventory::InventoryService::EncodeInventory(*stock)) : v8::Local<v8::Value>(v8::Null(info.GetIsolate())));
        }

        // The stock a call names, changed through `change`, then told to its holder and the scripts.
        template <typename Change>
        void ChangeInventory(const v8::FunctionCallbackInfo<v8::Value> &info, Change &&change) {
            const auto request = InventoryRequestArgument(info, 0);
            auto *self         = Self(info);
            auto *stash        = self != nullptr ? self->ResolveStash() : nullptr;
            auto *stock        = stash != nullptr ? ResolveStashes().ContentsOf(*stash) : nullptr;
            const auto result  = change(stock, request);
            if (stash != nullptr && result.value("ok", false)) {
                ResolveStashes().ContentsChanged(*stash);
            }
            info.GetReturnValue().Set(InventoryValue(info.GetIsolate(), result));
        }

        // JS: stash.addItem({ item, amount, metadata?, revision? }) -> InventoryResult
        void JS_AddItem(const v8::FunctionCallbackInfo<v8::Value> &info) {
            ChangeInventory(info, [](KCDC::Features::Inventory::Inventory *stock, const nlohmann::json &request) {
                return KCDC::Features::Inventory::InventoryService::ScriptAdd(stock, request);
            });
        }

        // JS: stash.removeItem({ units, revision? }) -> InventoryResult
        void JS_RemoveItem(const v8::FunctionCallbackInfo<v8::Value> &info) {
            ChangeInventory(info, [](KCDC::Features::Inventory::Inventory *stock, const nlohmann::json &request) {
                return KCDC::Features::Inventory::InventoryService::ScriptRemove(stock, request);
            });
        }

        // JS: stash.setInventory({ items, revision? }) -> InventoryResult
        void JS_SetInventory(const v8::FunctionCallbackInfo<v8::Value> &info) {
            ChangeInventory(info, [](KCDC::Features::Inventory::Inventory *stock, const nlohmann::json &request) {
                return KCDC::Features::Inventory::InventoryService::ScriptSet(stock, request);
            });
        }

        // JS: Stash.spawn(position, rotation, virtualWorld, { model }) -> Stash
        void JS_Spawn(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);

            glm::vec3 position {0.0F};
            glm::quat rotation {1.0F, 0.0F, 0.0F, 0.0F};
            std::uint32_t virtualWorld = MafiaNet::VIRTUAL_WORLD_GLOBAL;

            // Called directly for the reason ParseRotationArg gives.
            if (info.Length() >= 1 && info[0]->IsObject()) {
                position = v8pp::convert<glm::vec3>::from_v8(isolate, info[0]);
            }
            if (info.Length() >= 2) {
                rotation = ParseRotationArg(isolate, info[1], rotation);
            }
            if (info.Length() >= 3 && info[2]->IsNumber()) {
                virtualWorld = static_cast<std::uint32_t>(v8pp::from_v8<double>(isolate, info[2]));
            }
            std::string model;
            if (info.Length() >= 4 && info[3]->IsObject()) {
                v8::Local<v8::Context> context = isolate->GetCurrentContext();
                v8::Local<v8::Value> value;
                if (info[3].As<v8::Object>()->Get(context, v8pp::to_v8(isolate, "model")).ToLocal(&value) && !value->IsUndefined()) {
                    model = value->IsString() ? v8pp::from_v8<std::string>(isolate, value) : std::string {};
                    if (!KCDC::Features::Stash::StashService::IsSpawnableModel(model)) {
                        isolate->ThrowError(v8pp::to_v8(isolate, "Stash.spawn: model must be a .cgf, .cga or .cdf path"));
                        return;
                    }
                }
            }

            auto *stash = ResolveStashes().Spawn(position, rotation, virtualWorld, model);
            if (stash == nullptr) {
                isolate->ThrowError(v8pp::to_v8(isolate, "Stash.spawn: failed to create entity"));
                return;
            }

            info.GetReturnValue().Set(WrapStash(isolate, stash->GetNetworkID()));
        }

        void JS_CreateVirtual(const v8::FunctionCallbackInfo<v8::Value> &info) {
            const bool supplied = info.Length() > 0 && !info[0]->IsUndefined();
            const auto world    = KCDC::Scripting::VirtualWorldArg(info, 0);
            if (supplied && !world.has_value()) {
                info.GetIsolate()->ThrowError(v8pp::to_v8(info.GetIsolate(), "Stash.createVirtual: virtualWorld must be a uint32 number"));
                return;
            }
            auto *stash = ResolveStashes().CreateVirtual(world.value_or(MafiaNet::VIRTUAL_WORLD_GLOBAL));
            if (stash == nullptr) {
                info.GetIsolate()->ThrowError(v8pp::to_v8(info.GetIsolate(), "Stash.createVirtual: failed to create stock"));
                return;
            }
            info.GetReturnValue().Set(WrapStash(info.GetIsolate(), stash->GetNetworkID()));
        }

        void JS_Open(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *self   = Self(info);
            auto *stash  = self != nullptr ? self->ResolveStash() : nullptr;
            auto *player = info.Length() > 0 && info[0]->IsObject() ? v8pp::class_<Player>::unwrap_object(info.GetIsolate(), info[0]) : nullptr;
            info.GetReturnValue().Set(stash != nullptr && player != nullptr && ResolveStashes().OpenVirtual(*stash, player->GetId()));
        }

        // JS: Stash.all() -> Stash[]
        void JS_All(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);
            v8::Local<v8::Context> context = isolate->GetCurrentContext();
            v8::Local<v8::Array> stashes   = v8::Array::New(isolate);

            std::uint32_t index = 0;
            for (auto *stash : ResolveStashes().Collect(std::nullopt)) {
                stashes->Set(context, index++, WrapStash(isolate, stash->GetNetworkID())).Check();
            }

            info.GetReturnValue().Set(stashes);
        }

        // JS: Stash.getById(id) -> Stash | null
        void JS_GetById(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);

            if (info.Length() < 1 || !info[0]->IsNumber()) {
                isolate->ThrowError(v8pp::to_v8(isolate, "Stash.getById: expected (id)"));
                return;
            }

            info.GetReturnValue().Set(WrapStash(isolate, static_cast<std::uint64_t>(info[0]->IntegerValue(isolate->GetCurrentContext()).FromMaybe(0))));
        }

        // JS: Stash.find(guid, virtualWorld) -> Stash | null
        void JS_Find(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);

            if (info.Length() < 1 || !info[0]->IsString()) {
                isolate->ThrowError(v8pp::to_v8(isolate, "Stash.find: expected (guid, virtualWorld?)"));
                return;
            }

            const std::uint64_t guid = ParseLevelGuid(v8pp::from_v8<std::string>(isolate, info[0]));
            if (guid == 0) {
                info.GetReturnValue().SetNull();
                return;
            }

            Shared::Entities::StashEntity *stash = ResolveStashes().Resolve(guid, WorldArg(info, 1).value_or(MafiaNet::VIRTUAL_WORLD_GLOBAL));
            info.GetReturnValue().Set(stash != nullptr ? WrapStash(isolate, stash->GetNetworkID()) : v8::Local<v8::Value>(v8::Null(isolate)));
        }

        // JS: Stash.nearest(position, maxDistance, virtualWorld) -> Stash | null
        void JS_Nearest(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);

            if (info.Length() < 1 || !info[0]->IsObject()) {
                isolate->ThrowError(v8pp::to_v8(isolate, "Stash.nearest: expected (position, maxDistance?, virtualWorld?)"));
                return;
            }

            const glm::vec3 position = v8pp::convert<glm::vec3>::from_v8(isolate, info[0]);
            const float maxDistance  = info.Length() >= 2 && info[1]->IsNumber() ? static_cast<float>(v8pp::from_v8<double>(isolate, info[1])) : kDefaultNearestDistance;
            if (!(maxDistance >= 0.0F)) {
                isolate->ThrowError(v8pp::to_v8(isolate, "Stash.nearest: maxDistance must be a non-negative number"));
                return;
            }

            Shared::Entities::StashEntity *stash = ResolveStashes().Nearest(position, WorldArg(info, 2).value_or(MafiaNet::VIRTUAL_WORLD_GLOBAL), maxDistance);
            info.GetReturnValue().Set(stash != nullptr ? WrapStash(isolate, stash->GetNetworkID()) : v8::Local<v8::Value>(v8::Null(isolate)));
        }

        // JS: Stash.destroyAll(virtualWorld) -> number
        void JS_DestroyAll(const v8::FunctionCallbackInfo<v8::Value> &info) {
            info.GetReturnValue().Set(static_cast<std::uint32_t>(ResolveStashes().DespawnAll(WorldArg(info, 0))));
        }
    } // namespace

    bool Events::StashInteract(std::uint64_t playerNetworkId, std::uint64_t stashNetworkId, const char *action) {
        return EmitReservedEventSync("stashInteract", [playerNetworkId, stashNetworkId, action](v8::Isolate *isolate) {
            std::vector<v8::Local<v8::Value>> args;
            args.push_back(WrapPlayer(isolate, playerNetworkId));
            args.push_back(WrapStash(isolate, stashNetworkId));
            args.push_back(v8pp::to_v8(isolate, action));
            return args;
        });
    }

    void Events::StashSpawned(std::uint64_t stashNetworkId) {
        EmitReservedEvent("stashSpawn", [stashNetworkId](v8::Isolate *isolate) {
            return std::vector<v8::Local<v8::Value>> {WrapStash(isolate, stashNetworkId)};
        });
    }

    void Events::StashChanged(const KCDC::Features::Stash::StashEvent &event) {
        EmitReservedEvent(event.name.c_str(), [&event](v8::Isolate *isolate) {
            std::vector<v8::Local<v8::Value>> args {WrapStash(isolate, event.stash.networkId)};
            const auto player = event.playerId != 0 ? WrapPlayer(isolate, event.playerId) : v8::Local<v8::Value>(v8::Null(isolate));
            if (event.name == "stashOpen") {
                args.push_back(player);
            }
            else if (event.name == "stashClose") {
                args.push_back(player);
                args.push_back(v8pp::to_v8(isolate, event.reason));
            }
            else if (event.name == "stashInventoryChanged" && event.change) {
                args.push_back(player);
                args.push_back(InventoryValue(isolate, KCDC::Features::Inventory::Inventory::EncodeChange(*event.change)));
            }
            return args;
        });
    }

    Shared::Entities::StashEntity *Stash::ResolveStash() const {
        return dynamic_cast<Shared::Entities::StashEntity *>(Resolve());
    }

    std::uint32_t Stash::GetStashId() const {
        auto *stash = ResolveStash();
        return stash != nullptr ? static_cast<std::uint32_t>(stash->stashId) : 0;
    }

    std::string Stash::GetGuid() const {
        auto *stash = ResolveStash();
        return stash != nullptr && !stash->isVirtual ? FormatLevelGuid(stash->GetEntityGuid()) : std::string {};
    }

    std::string Stash::GetLevelGuid() const {
        auto *stash = ResolveStash();
        return stash != nullptr && stash->levelGuid != 0 ? FormatLevelGuid(stash->levelGuid) : std::string {};
    }

    std::string Stash::GetName() const {
        auto *stash                                         = ResolveStash();
        const KCDC::Shared::Game::StashPlacement *placement = stash != nullptr ? ResolveStashes().PlacementOf(*stash) : nullptr;
        return placement != nullptr ? placement->name : std::string {};
    }

    std::uint32_t Stash::GetItemCount() const {
        auto *stash = ResolveStash();
        return stash != nullptr ? static_cast<std::uint32_t>(ResolveStashes().ContentCount(stash->GetNetworkID())) : 0;
    }

    bool Stash::IsOpen() const {
        auto *stash = ResolveStash();
        return stash != nullptr && stash->isOpen;
    }

    bool Stash::IsVirtual() const {
        auto *stash = ResolveStash();
        return stash != nullptr && stash->isVirtual;
    }

    std::uint64_t Stash::GetHolderId() const {
        auto *stash = ResolveStash();
        return stash != nullptr ? ResolveStashes().HolderOf(stash->GetNetworkID()) : 0;
    }

    bool Stash::IsLocked() const {
        auto *stash = ResolveStash();
        return stash != nullptr && stash->locked;
    }

    void Stash::SetLocked(bool locked) {
        if (auto *stash = ResolveStash()) {
            ResolveStashes().SetLock(*stash, locked);
        }
    }

    bool Stash::IsLockedByServer() const {
        auto *stash = ResolveStash();
        return stash != nullptr && stash->lockedByServer;
    }

    bool Stash::IsLockpickable() const {
        auto *stash = ResolveStash();
        return stash != nullptr && stash->lockpickable;
    }

    void Stash::SetLockpickable(bool lockpickable) {
        if (auto *stash = ResolveStash()) {
            ResolveStashes().SetLockpickable(*stash, lockpickable);
        }
    }

    v8::Local<v8::Value> Stash::GetLockDifficulty(v8::Isolate *isolate) const {
        auto *stash = ResolveStash();
        if (stash == nullptr || stash->lockDifficulty < 0.0F) {
            return v8::Null(isolate);
        }
        return v8pp::to_v8(isolate, static_cast<double>(stash->lockDifficulty));
    }

    void Stash::SetLockDifficulty(v8::Isolate *isolate, v8::Local<v8::Value> value) {
        auto *stash = ResolveStash();
        if (stash == nullptr) {
            return;
        }
        if (value->IsNullOrUndefined()) {
            ResolveStashes().SetLockDifficulty(*stash, KCDC::Shared::Entities::StashEntity::kNativeLockDifficulty);
            return;
        }
        const double difficulty = value->IsNumber() ? v8pp::from_v8<double>(isolate, value) : -1.0;
        if (!(difficulty >= 0.0 && difficulty <= 1.0)) {
            isolate->ThrowError(v8pp::to_v8(isolate, "stash.lockDifficulty must be null or a number from 0 to 1"));
            return;
        }
        ResolveStashes().SetLockDifficulty(*stash, static_cast<float>(difficulty));
    }

    v8::Local<v8::Value> Stash::GetLockFanciness(v8::Isolate *isolate) const {
        auto *stash         = ResolveStash();
        using LockFanciness = KCDC::Shared::Entities::StashEntity::LockFanciness;
        if (stash == nullptr || stash->lockFanciness == LockFanciness::Native) {
            return v8::Null(isolate);
        }
        return v8pp::to_v8(isolate, stash->lockFanciness == LockFanciness::Fancy ? "fancy" : "common");
    }

    void Stash::SetLockFanciness(v8::Isolate *isolate, v8::Local<v8::Value> value) {
        auto *stash = ResolveStash();
        if (stash == nullptr) {
            return;
        }
        using LockFanciness = KCDC::Shared::Entities::StashEntity::LockFanciness;
        if (value->IsNullOrUndefined()) {
            ResolveStashes().SetLockFanciness(*stash, LockFanciness::Native);
            return;
        }
        const std::string fanciness = value->IsString() ? v8pp::from_v8<std::string>(isolate, value) : std::string {};
        if (fanciness != "common" && fanciness != "fancy") {
            isolate->ThrowError(v8pp::to_v8(isolate, "stash.lockFanciness must be null, 'common' or 'fancy'"));
            return;
        }
        ResolveStashes().SetLockFanciness(*stash, fanciness == "fancy" ? LockFanciness::Fancy : LockFanciness::Common);
    }

    std::string Stash::GetModel() const {
        auto *stash = ResolveStash();
        return stash != nullptr ? stash->model : std::string {};
    }

    std::string Stash::GetKeyItem() const {
        auto *stash = ResolveStash();
        if (stash == nullptr) {
            return {};
        }
        if (stash->keyItemClass.IsValid()) {
            const auto text = KCDC::Shared::Game::CryGuidToText(stash->keyItemClass.low, stash->keyItemClass.high);
            return std::string(text.data(), text.size());
        }
        const KCDC::Shared::Game::StashPlacement *placement = ResolveStashes().PlacementOf(*stash);
        return placement != nullptr ? placement->keyItemClass : std::string {};
    }

    void Stash::SetKeyItem(std::string keyItem) {
        auto *stash = ResolveStash();
        if (stash == nullptr) {
            return;
        }
        if (keyItem.empty()) {
            ResolveStashes().SetKeyItem(*stash, {});
            return;
        }

        // Refused rather than stored: a class no client can hold would leave
        // a key nobody could ever turn, and nothing would say why.
        const KCDC::Shared::RPC::ItemClassId keyItemClass = KCDC::Shared::Game::ItemClassIdFromGuid(keyItem);
        if (!keyItemClass.IsValid() || KCDC::Shared::Game::FindItemClass(keyItem) == nullptr) {
            Framework::Logging::GetLogger("Stash")->warn("stash.keyItem: '{}' is not an item class this build knows; the key is unchanged", keyItem);
            return;
        }
        ResolveStashes().SetKeyItem(*stash, keyItemClass);
    }

    bool Stash::StartsLocked() const {
        auto *stash                                         = ResolveStash();
        const KCDC::Shared::Game::StashPlacement *placement = stash != nullptr ? ResolveStashes().PlacementOf(*stash) : nullptr;
        return placement != nullptr && placement->startsLocked;
    }

    glm::vec3 Stash::GetUseDirection() const {
        auto *stash                                         = ResolveStash();
        const KCDC::Shared::Game::StashPlacement *placement = stash != nullptr ? ResolveStashes().PlacementOf(*stash) : nullptr;
        return placement != nullptr ? glm::vec3(placement->forwardX, placement->forwardY, 0.0F) : glm::vec3(0.0F, 1.0F, 0.0F);
    }

    std::string Stash::ToString() const {
        std::ostringstream ss;
        ss << "Stash{ id: " << GetId() << ", guid: " << GetGuid() << ", stashId: " << GetStashId() << ", levelGuid: \"" << GetLevelGuid() << "\", items: " << GetItemCount() << ", holder: " << GetHolderId() << ", locked: " << (IsLocked() ? "true" : "false") << " }";
        return ss.str();
    }

    v8pp::class_<Stash> &Stash::GetClass(v8::Isolate *isolate) {
        auto &_class = _classes[isolate];
        if (!_class) {
            // v8pp's inherit<Entity> needs the framework Entity registered first.
            Framework::Scripting::Builtins::Entity::GetClass(isolate);

            _class = std::make_unique<v8pp::class_<Stash>>(isolate, ServerCatalog(), "Stash", "Container handle for a spawned chest, a level chest, or virtual stock without a world entity.");
            _class->auto_wrap_objects(true);
            _class->inherit<Framework::Scripting::Builtins::Entity>()
                .ctor<std::uint64_t>(v8pp::metadata::docs("void", {v8pp::metadata::param("id", "number", false, "Network entity identifier.")},
                    "Creates a script wrapper for an existing container with this ID; use Stash.spawn() for a chest, Stash.createVirtual() for stock without a chest, or Stash.find() for one the level places."))
                .function("toString", &Stash::ToString, v8pp::metadata::docs("string", {}, "Formats this container handle for logging and debugging.", "The stash ID, its level GUID, how much is in it, who has it open and whether it is locked."));

            _class->prototype_function("destroy", &JS_Destroy, v8pp::metadata::docs("void", {}, "Destroys this scripted chest or virtual stash and forgets what was in it. Anything inside goes with it. Does nothing to a container the level places."));
            _class->prototype_function("open", &JS_Open,
                v8pp::metadata::docs("boolean", {v8pp::metadata::param("player", "Player", false, "Player whose native transfer screen should open.")},
                    "Opens a virtual stash from anywhere in its virtual world. Calls stashInteract before granting exclusive access. Returns false for a physical stash, a locked or busy stash, an unavailable player, or a script refusal. A true result requests the screen; the client releases access if it cannot open."));
            _class->prototype_function("getInventory", &JS_GetInventory,
                v8pp::metadata::docs("{ revision: number; items: InventoryRow[] } | null", {}, "Reads what the container holds. A linked child reads its master's.", "A copy of its rows, or null once the container is gone."));
            _class->prototype_function("addItem", &JS_AddItem,
                v8pp::metadata::docs("InventoryResult", {v8pp::metadata::param("request", "{ item: string; amount: number; metadata?: Record<string, unknown>; revision?: number }")}, "Puts items in the container, as `Inventory.add` gives them to a player."));
            _class->prototype_function("removeItem", &JS_RemoveItem, v8pp::metadata::docs("InventoryResult", {v8pp::metadata::param("request", "{ units: InventoryUnit[]; revision?: number }")}, "Takes exact units out of named rows, every one of them or none."));
            _class->prototype_function("setInventory", &JS_SetInventory,
                v8pp::metadata::docs("InventoryResult", {v8pp::metadata::param("request", "{ items: { id?: string; item: string; amount: number; metadata?: Record<string, unknown> }[]; revision?: number }")},
                    "Replaces everything in the container, which is how saved contents come back. At most 128 rows. An empty list empties it."));

            _class->property("stashId", &Stash::GetStashId,
                v8pp::metadata::property_docs("number", "The server-minted identity of a scripted chest or virtual stash, from 1. Distinct from the network entity id. Virtual stashes use it for an inventory GUID without creating a chest. 0 for a level chest."));
            _class->property("guid", &Stash::GetGuid,
                v8pp::metadata::property_docs("string",
                    "The EntityGuid every client finds this container under, as sixteen lowercase hex digits: the level's own for a container the level places, the one minted when a script spawned it otherwise. Empty for virtual stock; `Stash.find` takes physical container GUIDs back."));
            _class->property("levelGuid", &Stash::GetLevelGuid,
                v8pp::metadata::property_docs("string",
                    "The level's own EntityGuid for a container the level places, as sixteen lowercase hex digits; empty for a scripted chest or virtual stock. The same on every machine, so it is the identity to store a container under; `Stash.find` takes it back."));
            _class->property("name", &Stash::GetName, v8pp::metadata::property_docs("string", "The entity name the level gives the container, or `spawned_stash_<stashId>` for a scripted chest; empty for virtual stock."));
            _class->property("itemCount", &Stash::GetItemCount, v8pp::metadata::property_docs("number", "How many rows the server is holding. A container linked to another in the level opens that one's contents, and reads them here too."));
            _class->property("isOpen", &Stash::IsOpen, v8pp::metadata::property_docs("boolean", "Whether a player holds its view. Physical chests animate their lid by it; virtual stashes have no lid."));
            _class->property("isVirtual", &Stash::IsVirtual, v8pp::metadata::property_docs("boolean", "Whether this stash has stock without a chest entity. Open it with open(player); its position and rotation do not govern access, and guid is empty."));
            _class->property("holderId", &Stash::GetHolderId, v8pp::metadata::property_docs("number", "The network ID of the player who has its contents open, or 0. While it is held, every other player's open is refused before their transfer screen appears."));
            _class->property("locked", &Stash::IsLocked, &Stash::SetLocked,
                v8pp::metadata::property_docs("boolean",
                    "Whether the container is locked. Assignment reaches every client that has it streamed in. A locked container offers only its key and its lockpick; a script locking one marks it `lockedByServer`, which the generated home or shop key does not open. Unlocking it by any route the server accepted clears both."));
            _class->property("lockedByServer", &Stash::IsLockedByServer, v8pp::metadata::property_docs("boolean", "Whether the lock was set by a script rather than by the level. The generated home or shop key does not open a container locked from here."));
            _class->property("lockpickable", &Stash::IsLockpickable, &Stash::SetLockpickable,
                v8pp::metadata::property_docs("boolean", "Whether the container offers the lockpick while locked. Starts as the level authored it; assignment reaches every client, and a pick on one that cannot be is refused."));
            _class->property("lockDifficulty", &Stash::GetLockDifficulty, &Stash::SetLockDifficulty,
                v8pp::metadata::property_docs("number | null",
                    "How hard the lockpicking minigame is, from 0 to 1, or null for what the level gave it. The game's prompt names it in fifths: below 0.2 very easy, then easy, average, hard, and from 0.8 very hard. Assignment reaches every client and applies from the next pick; null gives the level's back."));
            _class->property("lockFanciness", &Stash::GetLockFanciness, &Stash::SetLockFanciness, v8pp::metadata::property_docs("'common' | 'fancy' | null", "Which lock the lockpicking minigame shows, or null for the level's. Assignment reaches every client."));
            _class->property("model", &Stash::GetModel, v8pp::metadata::property_docs("string", "The model a scripted container was spawned with, or empty for the default chest and for one the level places."));
            _class->property("keyItem", &Stash::GetKeyItem, &Stash::SetKeyItem,
                v8pp::metadata::property_docs("string",
                    "The item class GUID of the key that unlocks the container, as `player.giveItem` takes it. Reads the level's own key until a script assigns one; empty when it has none. Assigning a class makes every client ask for that class instead, so a player holding it is offered \"unlock and open\"; assigning an empty string restores the level's. An unknown class is refused and logged. The server does not see inventories: which key a player holds is their client's to say, and `stashInteract` is where a script checks its own record."));
            _class->property("useDirection", &Stash::GetUseDirection,
                v8pp::metadata::property_docs("Vector3",
                    "The horizontal unit direction a player has to face the container along to be offered it: the game's own use check only offers a chest to a body standing behind this axis and looking along it. Stand at `position - useDirection * distance` to face it."));
            _class->property("startsLocked", &Stash::StartsLocked, v8pp::metadata::property_docs("boolean", "Whether the level builds this container locked, which is the state it has at boot. False for one a script spawned."));
        }
        return *_class;
    }

    void Stash::UnregisterIsolate(v8::Isolate *isolate) {
        _classes.erase(isolate);
    }

    void Stash::Register(v8::Isolate *isolate, v8::Local<v8::Object> global) {
        if (isolate == nullptr || global.IsEmpty()) {
            return;
        }

        auto &events = ServerCatalog().data_type("EventMap");
        events.add_property("stashSpawn", "[stash: Stash]",
            "A container now exists: a scripted chest, a virtual stash, or one the level places, built in a virtual world the first time anything there asked for it. Every container starts empty; restore saved contents here with `setInventory`. The level's containers in the global world are built before any resource runs, so restore those from `resourceStart` by walking `Stash.all()`.");
        events.add_property("stashOpen", "[stash: Stash, player: Player]", "A player holds a container view. Physical chests report the native opening; virtual stashes report the server grant before requesting the native screen.");
        events.add_property("stashClose", "[stash: Stash, player: Player, reason: \"closed\" | \"lostAccess\" | \"timeout\" | \"disconnected\" | \"destroyed\"]",
            "A player no longer has a container open: they closed it, walked away, died or changed world (`lostAccess`), sent nothing for two minutes, left, or the container went.");
        events.add_property("stashInventoryChanged", "[stash: Stash, player: Player | null, change: InventoryChange]",
            "What a container holds changed. `player` moved the units -- `change.reason` is `deposit` or `withdraw`, and their inventory raises `playerInventoryChanged` too -- or is null for a script's `addItem`, `removeItem` or `setInventory`, or `destroy` when the container went with something in it. Save from here to keep contents across restarts: the server keeps nothing itself. A linked child's `stash` is the one the player used; its stock is its master's.");
        events.add_property("stashDestroy", "[stash: Stash]", "A container is about to go. Raised after its close and its contents' removal, while it can still be read.");
        events.add_property("stashInteract", "[player: Player, stash: Stash, action: \"open\" | \"unlock\" | \"lockpick\"]",
            "Dispatched for a physical chest prompt or virtual stash open(player), before access is granted. For a prompt: the player's client holds the game's own handler until the server answers. `action` is `open`, `unlock` -- a key turned, which opens it in the same use -- or `lockpick`, the minigame starting; a successful pick then opens it without asking again.\n\n"
            "Return false to refuse it: the prompt does nothing, and no transfer screen, key or minigame ever appears. Every handler runs whatever an earlier one returned, and an async handler cannot refuse. The server has already refused what the game's own rules forbid -- a player out of reach, a container another player has open, opening a locked one, picking one that cannot be -- so this only sees what the game would allow. Virtual opening has no distance check; world, player readiness, lock and exclusive access checks still apply. Closing is never asked.");

        v8pp::class_<Stash> &cls = GetClass(isolate);

        cls.static_function("createVirtual", &JS_CreateVirtual,
            v8pp::metadata::docs("Stash", {v8pp::metadata::param("virtualWorld", "number", true, "World whose players may open it, as a uint32 number; defaults to the global world. Invalid values throw before creating stock.")},
                "Creates empty server-owned stock without spawning a chest on any client. Supports the same inventory methods and events as physical stashes. Use open(player) to show the native transfer screen, and destroy() to discard it. Contents last until destroyed or the server restarts."));

        cls.static_function("spawn", &JS_Spawn,
            v8pp::metadata::docs("Stash",
                {
                    v8pp::metadata::param("position", "Vector3 | Partial<Vector3>", true, "Optional world-space spawn position; omitted components default to zero."),
                    v8pp::metadata::param("rotation", "Vector3 | Quaternion", true, "Optional initial orientation: a Quaternion, or a Vector3 of Euler angles in degrees."),
                    v8pp::metadata::param("virtualWorld", "number", true, "Optional virtual world the container belongs to; omitted puts it in the global one."),
                    v8pp::metadata::param("options", "{ model?: string }", true,
                        "`model` builds it as any .cgf, .cga or .cdf -- a cabinet such as `objects/manmade/common_furniture/cabinets/cabinet_fancy_d.cgf` -- instead of the default chest. Throws on any other path. A mesh without the game's open and close clips stays shut while it is looted."),
                },
                "Spawns and replicates an empty container. Fill it with `addItem` or `setInventory`, or let players put things in it.", "The newly spawned container handle."));
        cls.static_function("all", &JS_All,
            v8pp::metadata::docs("Stash[]", {},
                "Lists every container the server currently has: every one the level places, which exist from boot in the global world, and every scripted chest or virtual stash. In another virtual world a level container is built the first time anything there reaches it, so this lists only those.",
                "One handle per live container, in no particular order."));
        cls.static_function("getById", &JS_GetById,
            v8pp::metadata::docs("Stash | null", {v8pp::metadata::param("id", "number", false, "Network entity identifier.")}, "Looks a container up by its network entity ID.", "The container's handle, or null when no live container has that ID."));
        cls.static_function("find", &JS_Find,
            v8pp::metadata::docs("Stash | null",
                {
                    v8pp::metadata::param("guid", "string", false, "The container's EntityGuid as hex, with or without an `0x` prefix, as `stash.guid` prints it."),
                    v8pp::metadata::param("virtualWorld", "number", true, "Optional virtual world to look in; omitted looks in the global one, where every body starts."),
                },
                "Looks a container up by its EntityGuid: one the level places, by the level's own identity for it, which survives a restart and is the same on every machine, and is found in any virtual world; or one a script spawned, in the virtual world it was spawned in, for as long as it exists.",
                "The container's handle, or null when no container has that GUID there."));
        cls.static_function("nearest", &JS_Nearest,
            v8pp::metadata::docs("Stash | null",
                {
                    v8pp::metadata::param("position", "Vector3 | Partial<Vector3>", false, "The point to measure from, usually a player's position."),
                    v8pp::metadata::param("maxDistance", "number", true, "How far to look, in metres; omitted looks 5 m, about the reach the game gives a chest."),
                    v8pp::metadata::param("virtualWorld", "number", true, "Optional virtual world to look in; omitted looks in the global one."),
                },
                "Finds the physical container nearest a point, level or spawned. Virtual stock is excluded. The server knows where every container the level places stands, so this answers at once, without asking a client.",
                "The nearest container's handle, or null when none is that close."));
        cls.static_function("destroyAll", &JS_DestroyAll,
            v8pp::metadata::docs("number", {v8pp::metadata::param("virtualWorld", "number", true, "Optional virtual world to clear; omitted clears every one of them.")}, "Destroys scripted chests and virtual stashes, including everything inside. The level's own stay.",
                "How many containers were removed."));
        cls.publish(global);
    }
} // namespace KCDC::Scripting
