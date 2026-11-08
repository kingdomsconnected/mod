#include "register_entities.h"
#include "features/area/area_collision.h"

#include "core/obfuscation.h"

#include "features/blip/blip.h"
#include "features/cart/cart.h"
#include "features/cook_pot/cook_pot.h"
#include "features/dog/dog.h"
#include "features/door/door.h"
#include "features/door/gate.h"
#include "features/ground_item/ground_item.h"
#include "features/horse/horse.h"
#include "features/level_edit/level_edit.h"
#include "features/marker/marker.h"
#include "features/npc/npc.h"
#include "features/player/player.h"
#include "features/prop/prop.h"
#include "features/quest/quest.h"
#include "features/siege/siege.h"
#include "features/siege/siege_ladder.h"
#include "features/siege/stone_pile.h"
#include "features/smart_object/smart_object.h"
#include "features/stash/stash.h"
#include "features/vfx/vfx.h"
#include "features/light/light.h"
#include "features/world/world.h"

#include "shared/features/blip/blip_entity.h"
#include "shared/features/cart/cart_entity.h"
#include "shared/features/dog/dog_entity.h"
#include "shared/features/door/door_entity.h"
#include "shared/features/door/gate_entity.h"
#include "shared/features/ground_item/ground_item_entity.h"
#include "shared/features/horse/horse_entity.h"
#include "shared/features/marker/marker_entity.h"
#include "shared/features/npc/npc_entity.h"
#include "shared/features/player/player_entity.h"
#include "shared/features/prop/prop_entity.h"
#include "shared/features/quest/quest_entity.h"
#include "shared/features/siege/siege_entity.h"
#include "shared/features/siege/siege_ladder_entity.h"
#include "shared/features/siege/stone_pile_entity.h"
#include "shared/features/stash/stash_entity.h"
#include "shared/features/vfx/vfx_entity.h"
#include "shared/features/world/world_entity.h"

#include <networking/replication/entity_registry.h>

namespace KCDC::Core {
    void RegisterEntities() {
        Framework::Networking::Replication::EntityRegistry::Get().Register<KCDC::Features::Area::AreaCollision>(KCDC::Shared::Entities::AreaCollisionEntity::kTypeName);
        KCDC_JUNK;
        Framework::Networking::Replication::EntityRegistry::Get().Register<KCDC::Features::Player::Human>(KCDC::Shared::Entities::HumanEntity::kTypeName);
        Framework::Networking::Replication::EntityRegistry::Get().Register<KCDC::Features::Horse::Horse>(KCDC::Shared::Entities::HorseEntity::kTypeName);
        Framework::Networking::Replication::EntityRegistry::Get().Register<KCDC::Features::Dog::Dog>(KCDC::Shared::Entities::DogEntity::kTypeName);
        Framework::Networking::Replication::EntityRegistry::Get().Register<KCDC::Features::Npc::Npc>(KCDC::Shared::Entities::NpcEntity::kTypeName);
        Framework::Networking::Replication::EntityRegistry::Get().Register<KCDC::Features::GroundItem::GroundItem>(KCDC::Shared::Entities::GroundItemEntity::kTypeName);
        Framework::Networking::Replication::EntityRegistry::Get().Register<KCDC::Features::Stash::Stash>(KCDC::Shared::Entities::StashEntity::kTypeName);
        Framework::Networking::Replication::EntityRegistry::Get().Register<KCDC::Features::Prop::Prop>(KCDC::Shared::Entities::PropEntity::kTypeName);
        Framework::Networking::Replication::EntityRegistry::Get().Register<KCDC::Features::Cart::Cart>(KCDC::Shared::Entities::CartEntity::kTypeName);
        Framework::Networking::Replication::EntityRegistry::Get().Register<KCDC::Features::Door::Door>(KCDC::Shared::Entities::DoorEntity::kTypeName);
        Framework::Networking::Replication::EntityRegistry::Get().Register<KCDC::Features::Door::Gate>(KCDC::Shared::Entities::GateEntity::kTypeName);
        Framework::Networking::Replication::EntityRegistry::Get().Register<KCDC::Features::CookPot::CookPot>(KCDC::Shared::Entities::CookPotEntity::kTypeName);
        Framework::Networking::Replication::EntityRegistry::Get().Register<KCDC::Features::Marker::Marker>(KCDC::Shared::Entities::MarkerEntity::kTypeName);
        Framework::Networking::Replication::EntityRegistry::Get().Register<KCDC::Features::Blip::Blip>(KCDC::Shared::Entities::BlipEntity::kTypeName);
        Framework::Networking::Replication::EntityRegistry::Get().Register<KCDC::Features::LevelEdit::LevelEdit>(KCDC::Shared::Entities::LevelEditEntity::kTypeName);
        Framework::Networking::Replication::EntityRegistry::Get().Register<KCDC::Features::Vfx::Vfx>(KCDC::Shared::Entities::VfxEntity::kTypeName);
        Framework::Networking::Replication::EntityRegistry::Get().Register<KCDC::Features::Light::Light>(KCDC::Shared::Entities::LightEntity::kTypeName);
        Framework::Networking::Replication::EntityRegistry::Get().Register<KCDC::Features::Quest::Quest>(KCDC::Shared::Entities::QuestEntity::kTypeName);
        Framework::Networking::Replication::EntityRegistry::Get().Register<KCDC::Features::World::World>(KCDC::Shared::Entities::WorldEntity::kTypeName);
        Framework::Networking::Replication::EntityRegistry::Get().Register<KCDC::Features::Siege::SiegeEngine>(KCDC::Shared::Entities::SiegeEngineEntity::kTypeName);
        Framework::Networking::Replication::EntityRegistry::Get().Register<KCDC::Features::Siege::SiegeLadder>(KCDC::Shared::Entities::SiegeLadderEntity::kTypeName);
        Framework::Networking::Replication::EntityRegistry::Get().Register<KCDC::Features::Siege::StonePile>(KCDC::Shared::Entities::StonePileEntity::kTypeName);
        Framework::Networking::Replication::EntityRegistry::Get().Register<KCDC::Features::SmartObject::SmartObject>(KCDC::Shared::Entities::SmartObjectEntity::kTypeName);
    }
} // namespace KCDC::Core
