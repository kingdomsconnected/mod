#include "scripting_registry.h"
#include "features/inventory/inventory_scripting.h"

#include "features/animation/animation_scripting.h"
#include "features/area/area_scripting.h"
#include "features/blip/blip_scripting.h"
#include "features/camera/camera_scripting.h"
#include "features/cart/cart_scripting.h"
#include "features/cook_pot/cook_pot_scripting.h"
#include "features/crafting/crafting_scripting.h"
#include "features/dialogue/dialogue_scripting.h"
#include "features/dice/dice_scripting.h"
#include "features/dog/dog_scripting.h"
#include "features/door/door_scripting.h"
#include "features/door/gate_scripting.h"
#include "features/gathering/gathering_scripting.h"
#include "features/ground_item/ground_item_scripting.h"
#include "features/horse/horse_scripting.h"
#include "features/level_edit/level_edit_scripting.h"
#include "features/marker/marker_scripting.h"
#include "features/navigation/navigation_scripting.h"
#include "features/npc/npc_scripting.h"
#include "features/patrol/patrol_scripting.h"
#include "features/pickpocket/pickpocket_scripting.h"
#include "features/player/player_appearance_scripting.h"
#include "features/player/player_buff_scripting.h"
#include "features/player/player_progression_scripting.h"
#include "features/player/player_scripting.h"
#include "features/prop/prop_scripting.h"
#include "features/quest/quest_scripting.h"
#include "features/siege/siege_scripting.h"
#include "features/siege/siege_ladder_scripting.h"
#include "features/siege/stone_pile_scripting.h"
#include "features/smart_object/smart_object_scripting.h"
#include "features/social/social_chat_scripting.h"
#include "features/stash/stash_scripting.h"
#include "features/vendor/vendor_scripting.h"
#include "features/vfx/vfx_scripting.h"
#include "features/light/light_scripting.h"
#include "features/world/world_scripting.h"
#include "features/world_builder/world_resource_scripting.h"

#include "shared/scripting/catalog.h"

#include <scripting/builtins/builtins.h>
#include <logging/logger.h>
#include <scripting/builtins/entity.h>
#include <scripting/value_transfer.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

namespace KCDC::Core {
    void RegisterScripting(v8::Isolate *isolate, v8::Local<v8::Object> global) {
        if (isolate == nullptr || global.IsEmpty()) {
            return;
        }

        // Creates EventMap before any binding below appends to it.
        Scripting::EnsureEventMap(Scripting::ServerCatalog());

        Framework::Scripting::Builtins::Entity::Register(isolate, global);
        Scripting::Player::Register(isolate, global);
        Scripting::RegisterInventoryBindings(isolate, global);
        Scripting::Horse::Register(isolate, global);
        Scripting::RegisterDialogueBindings(isolate, global);
        Scripting::RegisterVendorBindings(isolate, global);
        Scripting::RegisterPickpocketBindings(isolate, global);
        Scripting::RegisterDiceBindings(isolate, global);
        Scripting::RegisterCraftingBindings(isolate, global);
        Scripting::Dog::Register(isolate, global);
        Scripting::Prop::Register(isolate, global);
        Scripting::Cart::Register(isolate, global);
        Scripting::Vfx::Register(isolate, global);
        Scripting::Light::Register(isolate, global);
        Scripting::Camera::Register(isolate, global);
        Scripting::Marker::Register(isolate, global);
        Scripting::Blip::Register(isolate, global);
        Scripting::LevelEdit::Register(isolate, global);
        Scripting::Npc::Register(isolate, global);
        Scripting::PatrolRoute::Register(isolate, global);
        Scripting::Quest::Register(isolate, global);
        Scripting::GroundItem::Register(isolate, global);
        Scripting::RegisterGatheringBindings(isolate, global);
        Scripting::RegisterCookPotBindings(isolate, global);
        Scripting::Door::Register(isolate, global);
        Scripting::SmartObject::Register(isolate, global);
        Scripting::Gate::Register(isolate, global);
        Scripting::SiegeEngine::Register(isolate, global);
        Scripting::SiegeLadder::Register(isolate, global);
        Scripting::StonePile::Register(isolate, global);
        Scripting::Area::Register(isolate, global);
        Scripting::WorldResource::Register(isolate, global);
        Scripting::Stash::Register(isolate, global);
        Scripting::Chat::Register(isolate, global);
        Scripting::World::Register(isolate, global);
        Scripting::Navigation::Register(isolate, global);
        Scripting::Buffs::Register(isolate, global);
        Scripting::Progression::Register(isolate, global);
        Scripting::Appearance::Register(isolate, global);
        Scripting::Animations::Register(isolate, global);

#if !defined(NDEBUG) && defined(KCDC_SCRIPTING_API_METADATA_FILE)
        // Every runtime needs fresh classes, but the API is process-wide. Export
        // once after the complete root registration, before resource registrations
        // append inherited-class metadata to the same catalog again.
        static bool exported = false;
        if (!exported) {
            exported = Scripting::ExportCatalog(Scripting::ServerCatalog(), "framework-server", KCDC_SCRIPTING_API_METADATA_FILE);
            if (!exported) {
                Framework::Logging::GetLogger("Scripting")->error("Failed to export the scripting API metadata to {}", KCDC_SCRIPTING_API_METADATA_FILE);
            }
            else {
                Framework::Logging::GetLogger("Scripting")->debug("Exported the scripting API metadata to {}", KCDC_SCRIPTING_API_METADATA_FILE);
            }
        }
#endif
    }

    void RegisterTransfers() {
        // Named apart from the framework's own Entity and Player, which stay registered.
        Framework::Scripting::Builtins::RegisterHandleTransfer<Scripting::Player>("kcdc.Player");
        Framework::Scripting::Builtins::RegisterHandleTransfer<Scripting::Horse>("kcdc.Horse");
        Framework::Scripting::Builtins::RegisterHandleTransfer<Scripting::Dog>("kcdc.Dog");
        Framework::Scripting::Builtins::RegisterHandleTransfer<Scripting::Prop>("kcdc.Prop");
        Framework::Scripting::Builtins::RegisterHandleTransfer<Scripting::Cart>("kcdc.Cart");
        Framework::Scripting::Builtins::RegisterHandleTransfer<Scripting::Vfx>("kcdc.Vfx");
        Framework::Scripting::Builtins::RegisterHandleTransfer<Scripting::Light>("kcdc.Light");
        Framework::Scripting::Builtins::RegisterHandleTransfer<Scripting::Marker>("kcdc.Marker");
        Framework::Scripting::Builtins::RegisterHandleTransfer<Scripting::Blip>("kcdc.Blip");
        Framework::Scripting::Builtins::RegisterHandleTransfer<Scripting::Npc>("kcdc.Npc");
        Framework::Scripting::Builtins::RegisterHandleTransfer<Scripting::GroundItem>("kcdc.GroundItem");
        Framework::Scripting::Builtins::RegisterHandleTransfer<Scripting::Door>("kcdc.Door");
        Framework::Scripting::Builtins::RegisterHandleTransfer<Scripting::SmartObject>("kcdc.SmartObject");
        Framework::Scripting::Builtins::RegisterHandleTransfer<Scripting::Gate>("kcdc.Gate");
        Framework::Scripting::Builtins::RegisterHandleTransfer<Scripting::SiegeEngine>("kcdc.SiegeEngine");
        Framework::Scripting::Builtins::RegisterHandleTransfer<Scripting::SiegeLadder>("kcdc.SiegeLadder");
        Framework::Scripting::Builtins::RegisterHandleTransfer<Scripting::StonePile>("kcdc.StonePile");
        Framework::Scripting::Builtins::RegisterHandleTransfer<Scripting::Quest>("kcdc.Quest");
        Framework::Scripting::Builtins::RegisterHandleTransfer<Scripting::Stash>("kcdc.Stash");

        // Every areaEnter and areaExit crosses into the resources that handle it. An area is no entity, so it travels as
        // the area service's handle; that is never reused, and an area destroyed on the way arrives as a gone Area.
        Framework::Scripting::ValueTransfer::RegisterHostType(
            "kcdc.Area",
            [](v8::Isolate *isolate, v8::Local<v8::Object> object, std::vector<std::uint8_t> &bytes) {
                Scripting::Area::GetClass(isolate);
                const Scripting::Area *area = v8pp::class_<Scripting::Area>::unwrap_object(isolate, object);
                if (area == nullptr) {
                    return false;
                }
                const std::uint32_t handle = area->GetHandle();
                bytes.resize(sizeof(handle));
                std::memcpy(bytes.data(), &handle, sizeof(handle));
                return true;
            },
            [](v8::Isolate *isolate, v8::Local<v8::Context>, const std::vector<std::uint8_t> &bytes) -> v8::MaybeLocal<v8::Value> {
                std::uint32_t handle = 0;
                std::memcpy(&handle, bytes.data(), std::min(bytes.size(), sizeof(handle)));
                Scripting::Area::GetClass(isolate);
                return v8pp::class_<Scripting::Area>::create_object(isolate, handle);
            });

        // A playerHit's `hit` crosses as the proposal it edits, so what a
        // handler sets reaches the blow.
        Scripting::PlayerHitHandles::RegisterTransfer();
    }

    void UnregisterGlobals(v8::Isolate *isolate) {
        Scripting::Player::UnregisterIsolate(isolate);
        Scripting::Horse::UnregisterIsolate(isolate);
        Scripting::Dog::UnregisterIsolate(isolate);
        Scripting::Prop::UnregisterIsolate(isolate);
        Scripting::Cart::UnregisterIsolate(isolate);
        Scripting::Vfx::UnregisterIsolate(isolate);
        Scripting::Light::UnregisterIsolate(isolate);
        Scripting::Marker::UnregisterIsolate(isolate);
        Scripting::Blip::UnregisterIsolate(isolate);
        Scripting::Npc::UnregisterIsolate(isolate);
        Scripting::GroundItem::UnregisterIsolate(isolate);
        Scripting::SmartObject::UnregisterIsolate(isolate);
        Scripting::Door::UnregisterIsolate(isolate);
        Scripting::Gate::UnregisterIsolate(isolate);
        Scripting::SiegeEngine::UnregisterIsolate(isolate);
        Scripting::SiegeLadder::UnregisterIsolate(isolate);
        Scripting::StonePile::UnregisterIsolate(isolate);
        Scripting::Quest::UnregisterIsolate(isolate);
        Scripting::Stash::UnregisterIsolate(isolate);
        Scripting::LevelEdit::UnregisterIsolate(isolate);
        Scripting::Area::UnregisterIsolate(isolate);
        Scripting::PlayerHitHandles::Get().UnregisterIsolate(isolate);
        Scripting::WorldResource::UnregisterIsolate(isolate);
    }
} // namespace KCDC::Core
