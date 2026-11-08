#include "wrap.h"

#include "features/blip/blip_scripting.h"
#include "features/cart/cart_scripting.h"
#include "features/dog/dog_scripting.h"
#include "features/door/door_scripting.h"
#include "features/door/gate_scripting.h"
#include "features/ground_item/ground_item_scripting.h"
#include "features/horse/horse_scripting.h"
#include "features/player/player_scripting.h"
#include "features/prop/prop_scripting.h"
#include "features/stash/stash_scripting.h"
#include "features/marker/marker_scripting.h"
#include "features/npc/npc_scripting.h"
#include "features/quest/quest_scripting.h"
#include "features/siege/siege_scripting.h"
#include "features/siege/siege_ladder_scripting.h"
#include "features/siege/stone_pile_scripting.h"
#include "features/vfx/vfx_scripting.h"
#include "features/light/light_scripting.h"

#include "shared/features/cart/cart_entity.h"
#include "shared/features/dog/dog_entity.h"
#include "shared/features/door/door_entity.h"
#include "shared/features/door/gate_entity.h"
#include "shared/features/ground_item/ground_item_entity.h"
#include "shared/features/horse/horse_entity.h"
#include "shared/features/npc/npc_entity.h"
#include "shared/features/player/player_entity.h"
#include "shared/features/prop/prop_entity.h"
#include "shared/features/quest/quest_entity.h"
#include "shared/features/siege/siege_entity.h"
#include "shared/features/siege/siege_ladder_entity.h"
#include "shared/features/siege/stone_pile_entity.h"
#include "shared/features/stash/stash_entity.h"
#include "shared/features/vfx/vfx_entity.h"

#include <core_modules.h>
#include <networking/replication/replication_manager.h>

namespace KCDC::Scripting {
    namespace {
        // Whether `networkId` still names a live entity of this type.
        template <typename Entity>
        bool Alive(std::uint64_t networkId) {
            auto *replication = Framework::CoreModules::GetReplication();
            return replication != nullptr && networkId != 0 && replication->GetEntity<Entity>(networkId) != nullptr;
        }
    } // namespace

    v8::Local<v8::Value> WrapPlayer(v8::Isolate *isolate, std::uint64_t playerNetworkId) {
        if (!Alive<Shared::Entities::HumanEntity>(playerNetworkId)) {
            return v8::Null(isolate);
        }

        Player::GetClass(isolate);
        return v8pp::class_<Player>::create_object(isolate, playerNetworkId);
    }

    v8::Local<v8::Value> WrapHorse(v8::Isolate *isolate, std::uint64_t horseNetworkId) {
        if (!Alive<Shared::Entities::HorseEntity>(horseNetworkId)) {
            return v8::Null(isolate);
        }

        Horse::GetClass(isolate);
        return v8pp::class_<Horse>::create_object(isolate, horseNetworkId);
    }

    v8::Local<v8::Value> WrapNpc(v8::Isolate *isolate, std::uint64_t npcNetworkId) {
        if (!Alive<Shared::Entities::NpcEntity>(npcNetworkId)) {
            return v8::Null(isolate);
        }

        Npc::GetClass(isolate);
        return v8pp::class_<Npc>::create_object(isolate, npcNetworkId);
    }

    v8::Local<v8::Value> WrapCombatant(v8::Isolate *isolate, std::uint64_t networkId) {
        return Alive<Shared::Entities::NpcEntity>(networkId) ? WrapNpc(isolate, networkId) : WrapPlayer(isolate, networkId);
    }

    v8::Local<v8::Value> WrapDog(v8::Isolate *isolate, std::uint64_t dogNetworkId) {
        if (!Alive<Shared::Entities::DogEntity>(dogNetworkId)) {
            return v8::Null(isolate);
        }

        Dog::GetClass(isolate);
        return v8pp::class_<Dog>::create_object(isolate, dogNetworkId);
    }

    v8::Local<v8::Value> WrapGroundItem(v8::Isolate *isolate, std::uint64_t groundItemNetworkId) {
        if (!Alive<Shared::Entities::GroundItemEntity>(groundItemNetworkId)) {
            return v8::Null(isolate);
        }

        GroundItem::GetClass(isolate);
        return v8pp::class_<GroundItem>::create_object(isolate, groundItemNetworkId);
    }

    v8::Local<v8::Value> WrapProp(v8::Isolate *isolate, std::uint64_t propNetworkId) {
        if (!Alive<Shared::Entities::PropEntity>(propNetworkId)) {
            return v8::Null(isolate);
        }

        Prop::GetClass(isolate);
        return v8pp::class_<Prop>::create_object(isolate, propNetworkId);
    }

    v8::Local<v8::Value> WrapCart(v8::Isolate *isolate, std::uint64_t cartNetworkId) {
        if (!Alive<Shared::Entities::CartEntity>(cartNetworkId)) {
            return v8::Null(isolate);
        }

        Cart::GetClass(isolate);
        return v8pp::class_<Cart>::create_object(isolate, cartNetworkId);
    }

    v8::Local<v8::Value> WrapDoor(v8::Isolate *isolate, std::uint64_t doorNetworkId) {
        if (!Alive<Shared::Entities::DoorEntity>(doorNetworkId)) {
            return v8::Null(isolate);
        }

        Door::GetClass(isolate);
        return v8pp::class_<Door>::create_object(isolate, doorNetworkId);
    }

    v8::Local<v8::Value> WrapGate(v8::Isolate *isolate, std::uint64_t gateNetworkId) {
        if (!Alive<Shared::Entities::GateEntity>(gateNetworkId)) {
            return v8::Null(isolate);
        }

        Gate::GetClass(isolate);
        return v8pp::class_<Gate>::create_object(isolate, gateNetworkId);
    }

    v8::Local<v8::Value> WrapStash(v8::Isolate *isolate, std::uint64_t stashNetworkId) {
        if (!Alive<Shared::Entities::StashEntity>(stashNetworkId)) {
            return v8::Null(isolate);
        }

        Stash::GetClass(isolate);
        return v8pp::class_<Stash>::create_object(isolate, stashNetworkId);
    }

    v8::Local<v8::Value> WrapBlip(v8::Isolate *isolate, std::uint64_t blipNetworkId) {
        if (!Alive<Shared::Entities::BlipEntity>(blipNetworkId)) {
            return v8::Null(isolate);
        }

        Blip::GetClass(isolate);
        return v8pp::class_<Blip>::create_object(isolate, blipNetworkId);
    }

    v8::Local<v8::Value> WrapMarker(v8::Isolate *isolate, std::uint64_t markerNetworkId) {
        if (!Alive<Shared::Entities::MarkerEntity>(markerNetworkId)) {
            return v8::Null(isolate);
        }

        Marker::GetClass(isolate);
        return v8pp::class_<Marker>::create_object(isolate, markerNetworkId);
    }

    v8::Local<v8::Value> WrapQuest(v8::Isolate *isolate, std::uint64_t questNetworkId) {
        if (!Alive<Shared::Entities::QuestEntity>(questNetworkId)) {
            return v8::Null(isolate);
        }

        Quest::GetClass(isolate);
        return v8pp::class_<Quest>::create_object(isolate, questNetworkId);
    }

    v8::Local<v8::Value> WrapLight(v8::Isolate *isolate, std::uint64_t lightNetworkId) {
        if (!Alive<Shared::Entities::LightEntity>(lightNetworkId)) {
            return v8::Null(isolate);
        }
        Light::GetClass(isolate);
        return v8pp::class_<Light>::create_object(isolate, lightNetworkId);
    }

    v8::Local<v8::Value> WrapVfx(v8::Isolate *isolate, std::uint64_t vfxNetworkId) {
        if (!Alive<Shared::Entities::VfxEntity>(vfxNetworkId)) {
            return v8::Null(isolate);
        }

        Vfx::GetClass(isolate);
        return v8pp::class_<Vfx>::create_object(isolate, vfxNetworkId);
    }

    v8::Local<v8::Value> WrapSiegeEngine(v8::Isolate *isolate, std::uint64_t engineNetworkId) {
        if (!Alive<Shared::Entities::SiegeEngineEntity>(engineNetworkId)) {
            return v8::Null(isolate);
        }

        SiegeEngine::GetClass(isolate);
        return v8pp::class_<SiegeEngine>::create_object(isolate, engineNetworkId);
    }

    v8::Local<v8::Value> WrapSiegeLadder(v8::Isolate *isolate, std::uint64_t ladderNetworkId) {
        if (!Alive<Shared::Entities::SiegeLadderEntity>(ladderNetworkId)) {
            return v8::Null(isolate);
        }

        SiegeLadder::GetClass(isolate);
        return v8pp::class_<SiegeLadder>::create_object(isolate, ladderNetworkId);
    }

    v8::Local<v8::Value> WrapStonePile(v8::Isolate *isolate, std::uint64_t pileNetworkId) {
        if (!Alive<Shared::Entities::StonePileEntity>(pileNetworkId)) {
            return v8::Null(isolate);
        }

        StonePile::GetClass(isolate);
        return v8pp::class_<StonePile>::create_object(isolate, pileNetworkId);
    }
} // namespace KCDC::Scripting
