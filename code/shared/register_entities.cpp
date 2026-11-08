#include "register_entities.h"
#include "shared/features/area/area_collision_entity.h"

#include "shared/features/blip/blip_entity.h"
#include "shared/features/cart/cart_entity.h"
#include "shared/features/cook_pot/cook_pot_entity.h"
#include "shared/features/dog/dog_entity.h"
#include "shared/features/door/door_entity.h"
#include "shared/features/door/gate_entity.h"
#include "shared/features/ground_item/ground_item_entity.h"
#include "shared/features/horse/horse_entity.h"
#include "shared/features/level_edit/level_edit_entity.h"
#include "shared/features/marker/marker_entity.h"
#include "shared/features/npc/npc_entity.h"
#include "shared/features/player/player_entity.h"
#include "shared/features/prop/prop_entity.h"
#include "shared/features/quest/quest_entity.h"
#include "shared/features/siege/siege_entity.h"
#include "shared/features/siege/siege_ladder_entity.h"
#include "shared/features/siege/stone_pile_entity.h"
#include "shared/features/smart_object/smart_object_entity.h"
#include "shared/features/stash/stash_entity.h"
#include "shared/features/vfx/vfx_entity.h"
#include "shared/features/light/light_entity.h"
#include "shared/features/world/world_entity.h"

#include <networking/replication/entity_registry.h>

namespace KCDC::Shared::Entities {
    void RegisterEntities() {
        Framework::Networking::Replication::EntityRegistry::Get().Register<KCDC::Shared::Entities::AreaCollisionEntity>(KCDC::Shared::Entities::AreaCollisionEntity::kTypeName);
        Framework::Networking::Replication::EntityRegistry::Get().Register<KCDC::Shared::Entities::HumanEntity>(KCDC::Shared::Entities::HumanEntity::kTypeName);
        Framework::Networking::Replication::EntityRegistry::Get().Register<KCDC::Shared::Entities::HorseEntity>(KCDC::Shared::Entities::HorseEntity::kTypeName);
        Framework::Networking::Replication::EntityRegistry::Get().Register<KCDC::Shared::Entities::DogEntity>(KCDC::Shared::Entities::DogEntity::kTypeName);
        Framework::Networking::Replication::EntityRegistry::Get().Register<KCDC::Shared::Entities::NpcEntity>(KCDC::Shared::Entities::NpcEntity::kTypeName);
        Framework::Networking::Replication::EntityRegistry::Get().Register<KCDC::Shared::Entities::GroundItemEntity>(KCDC::Shared::Entities::GroundItemEntity::kTypeName);
        Framework::Networking::Replication::EntityRegistry::Get().Register<KCDC::Shared::Entities::StashEntity>(KCDC::Shared::Entities::StashEntity::kTypeName);
        Framework::Networking::Replication::EntityRegistry::Get().Register<KCDC::Shared::Entities::PropEntity>(KCDC::Shared::Entities::PropEntity::kTypeName);
        Framework::Networking::Replication::EntityRegistry::Get().Register<KCDC::Shared::Entities::CartEntity>(KCDC::Shared::Entities::CartEntity::kTypeName);
        Framework::Networking::Replication::EntityRegistry::Get().Register<KCDC::Shared::Entities::DoorEntity>(KCDC::Shared::Entities::DoorEntity::kTypeName);
        Framework::Networking::Replication::EntityRegistry::Get().Register<KCDC::Shared::Entities::GateEntity>(KCDC::Shared::Entities::GateEntity::kTypeName);
        Framework::Networking::Replication::EntityRegistry::Get().Register<KCDC::Shared::Entities::CookPotEntity>(KCDC::Shared::Entities::CookPotEntity::kTypeName);
        Framework::Networking::Replication::EntityRegistry::Get().Register<KCDC::Shared::Entities::MarkerEntity>(KCDC::Shared::Entities::MarkerEntity::kTypeName);
        Framework::Networking::Replication::EntityRegistry::Get().Register<KCDC::Shared::Entities::BlipEntity>(KCDC::Shared::Entities::BlipEntity::kTypeName);
        Framework::Networking::Replication::EntityRegistry::Get().Register<KCDC::Shared::Entities::LevelEditEntity>(KCDC::Shared::Entities::LevelEditEntity::kTypeName);
        Framework::Networking::Replication::EntityRegistry::Get().Register<KCDC::Shared::Entities::VfxEntity>(KCDC::Shared::Entities::VfxEntity::kTypeName);
        Framework::Networking::Replication::EntityRegistry::Get().Register<KCDC::Shared::Entities::LightEntity>(KCDC::Shared::Entities::LightEntity::kTypeName);
        Framework::Networking::Replication::EntityRegistry::Get().Register<KCDC::Shared::Entities::QuestEntity>(KCDC::Shared::Entities::QuestEntity::kTypeName);
        Framework::Networking::Replication::EntityRegistry::Get().Register<KCDC::Shared::Entities::WorldEntity>(KCDC::Shared::Entities::WorldEntity::kTypeName);
        Framework::Networking::Replication::EntityRegistry::Get().Register<KCDC::Shared::Entities::SiegeEngineEntity>(KCDC::Shared::Entities::SiegeEngineEntity::kTypeName);
        Framework::Networking::Replication::EntityRegistry::Get().Register<KCDC::Shared::Entities::SiegeLadderEntity>(KCDC::Shared::Entities::SiegeLadderEntity::kTypeName);
        Framework::Networking::Replication::EntityRegistry::Get().Register<KCDC::Shared::Entities::StonePileEntity>(KCDC::Shared::Entities::StonePileEntity::kTypeName);
        Framework::Networking::Replication::EntityRegistry::Get().Register<KCDC::Shared::Entities::SmartObjectEntity>(KCDC::Shared::Entities::SmartObjectEntity::kTypeName);
    }
} // namespace KCDC::Shared::Entities
