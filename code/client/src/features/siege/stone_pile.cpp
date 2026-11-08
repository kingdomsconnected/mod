#include <utils/safe_win32.h>

#include "stone_pile.h"

#include "core/application.h"
#include "common/module_support.h"
#include "shared/pose_math.h"
#include "sdk/cryengine/3dengine/i_3d_engine.h"
#include "sdk/cryengine/3dengine/i_material_manager.h"
#include "sdk/cryengine/entitysystem/i_entity_class.h"
#include "sdk/cryengine/physics/pe_params_part.h"
#include "sdk/cryengine/entitysystem/i_entity_system.h"
#include "sdk/cryengine/scriptsystem/i_script_table.h"

#include <logging/logger.h>

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cstdio>
#include <string>
#include <string_view>

namespace KCDC::Features::Siege {
    namespace {
        constexpr const char *kLogger = "StonePile";

        constexpr std::uint64_t kBuildRetryMs = 1000;

        /**
         * The trigger sphere's scale. The prefab's 0.6 sits against a
         * battlement where the eye meets it; on open ground it is a small
         * target at the thrower's feet. The interactor's own gates still
         * apply: 0.3-1.8 m from the pile, facing the throw within
         * `wh_pl_StoneThrowingAngleTolerance` (0.65).
         */
        constexpr float kTriggerScale = 1.2F;

        /**
         * The pile entity's yaw in the layout. Its rotation is read in one
         * place: `C_StoneThrowingPile::CanUse` (0x182EA8304) offers the grab
         * only to a user 0.3-1.8 m away inside a cone about its +Y (dot 0.65).
         * The grab itself aligns the thrower onto the entity, facing the
         * path's quarter turn (`GetInteractionTargetRotation`), whatever the
         * entity's yaw. The prefab's -50 degrees opens that cone beside the
         * heap; turned half round it opens behind the stand point, so the
         * prompt is there for whoever walks up to the pile facing the throw.
         */
        constexpr float kPileApproachYawDeg = 180.0F;

        /**
         * Where the path starts behind the stand point: off it, since the grab
         * faces the thrower from the first node towards the pile
         * (`GetInteractionTargetRotation`, 0x182EAB930), and close enough
         * that being kept on the path does not walk them off it.
         */
        constexpr float kPathStart = 0.1F;

        /** How long the path is: long enough to be a segment, short enough that carrying the stone along it goes nowhere. */
        constexpr float kPathLength = 0.5F;

        /**
         * The pile's `esThrowType`. `walls`, the battlement prefab's, sets
         * `C_StoneThrowingPile+0xA9`, which keeps the minigame on its
         * non-interactive action map (`RefreshInteractionAction`,
         * 0x182EB3D50): the stone is thrown only by the grab's own end. `hole`
         * leaves the `throw` action live, so the stone goes on the use key.
         */
        constexpr const char *kThrowType = "hole";


        /**
         * How long after a grab the heap is left as the game has it: the grab
         * and the throw run about six seconds, and a heap reset while a stone
         * is in the hands would put that stone back.
         */
        constexpr std::uint64_t kGrabBusyMs = 8000;

        /**
         * Keeps a part out of the rigid-body simulation. `GeomEntity.lua` and
         * `UsableItem.lua` both declare `Physics.bRigidBody = true`, and the
         * pile's trigger sphere and its stones would fall before the pile's
         * own reset makes the stones static. The `Physics` table is cloned
         * first: the one it would otherwise write is every instance's.
         */
        void MakePhysicsStatic(SDK::IEntity *entity) {
            SDK::IScriptTable *script = KCDC::Common::EntityScriptTable(entity);
            auto properties           = script != nullptr ? script->GetTable("Properties") : SDK::ScriptTablePtr {};
            auto physics              = properties ? properties->GetTable("Physics") : SDK::ScriptTablePtr {};
            auto owned                = physics ? physics->CloneTable() : SDK::ScriptTablePtr {};
            if (owned) {
                properties->SetValue("Physics", owned.get());
                owned->SetValue("bRigidBody", false);
            }
        }

        glm::quat Yaw(float degrees) {
            return glm::angleAxis(glm::radians(degrees), glm::vec3(0.0F, 0.0F, 1.0F));
        }

        void Link(SDK::IEntity *pile, const char *name, SDK::EntityId target) {
            SDK::IEntity *entity = KCDC::Common::ResolveEntity(target);
            if (pile == nullptr || entity == nullptr) {
                return;
            }
            SDK::ScopedCryString linkName(name);
            pile->AddEntityLink(linkName, target, entity->GetGuid());
        }
    } // namespace

    void StonePile::DeallocReplica(MafiaNet::Connection_RM3 *) {
        Despawn();
        delete this;
    }

    void StonePile::Despawn() {
        if (auto *app = KCDC::Core::gApplication.get()) {
            for (SDK::EntityId part : _parts) {
                app->GetPropService().QueueDespawn(part);
            }
        }
        _parts.clear();
        _pileEntityId = 0;
        _shown        = -1;
    }

    void StonePile::Unbind() {
        // Not removed: the entity system holding these ids is gone too.
        _parts.clear();
        _pileEntityId     = 0;
        _shown            = -1;
        _nextBuildAttempt = 0;
        _buildRefused     = false;
    }

    SDK::wh::playermodule::C_StoneThrowingPile *StonePile::ResolvePile() const {
        return _pileEntityId != 0 && KCDC::Common::ResolveEntity(_pileEntityId) != nullptr ? SDK::wh::playermodule::C_StoneThrowingPile::Find(_pileEntityId) : nullptr;
    }

    void StonePile::NoteGrab(std::uint64_t nowLocal) {
        if (_shown > 0) {
            --_shown;
        }
        _busyUntil = nowLocal + kGrabBusyMs;
    }

    SDK::EntityId StonePile::SpawnPart(const char *className, const char *name, const glm::vec3 &at, const glm::quat &rotation, float scale) {
        auto *entitySystem             = SDK::IEntitySystem::GetInstance();
        auto *registry                 = entitySystem != nullptr ? entitySystem->GetClassRegistry() : nullptr;
        SDK::IEntityClass *entityClass = registry != nullptr ? registry->FindClass(className) : nullptr;
        if (entityClass == nullptr) {
            return 0;
        }
        SDK::SEntitySpawnParams params {};
        params.entityClass = entityClass;
        params.name        = name;
        params.position    = KCDC::Common::ToNative(at);
        params.rotation    = KCDC::Common::ToNative(glm::normalize(rotation));
        params.scale       = SDK::Vec3 {scale, scale, scale};
        params.flags       = SDK::ENTITY_FLAG_CASTSHADOW;

        SDK::IEntity *entity = entitySystem->SpawnEntity(params, false);
        if (entity == nullptr) {
            return 0;
        }
        const SDK::EntityId entityId = entity->GetId();
        MakePhysicsStatic(entity);
        if (std::string_view(className) == "StoneThrowingPile") {
            SDK::IScriptTable *script = KCDC::Common::EntityScriptTable(entity);
            auto properties           = script != nullptr ? script->GetTable("Properties") : SDK::ScriptTablePtr {};
            if (properties) {
                KCDC::Common::AssignDeclaredProperty(*properties, "esThrowType", kThrowType);
                KCDC::Common::AssignDeclaredProperty(*properties, "object_Model", Shared::Game::g_StonePileLayout.pileModel);
            }
        }
        if (!entitySystem->InitEntity(entity, params) || KCDC::Common::ResolveEntity(entityId) == nullptr) {
            return 0;
        }
        _parts.push_back(entityId);
        return entityId;
    }

    bool StonePile::TryBuild() {
        // Without a world yet it is tried again; once there is one, a part it
        // refuses is refused for good -- a missing class or a refused spawn
        // does not come right a second later.
        auto *entitySystem = SDK::IEntitySystem::GetInstance();
        if (entitySystem == nullptr || entitySystem->GetClassRegistry() == nullptr || !Shared::Game::IsFinite(position)) {
            return false;
        }
        const Shared::Game::StonePileLayout &layout = Shared::Game::g_StonePileLayout;
        const unsigned long long id                 = static_cast<unsigned long long>(GetNetworkID());
        char name[64];

        // The stones first, each a `GeomEntity` whose mesh goes in by hand:
        // KCD2's GeomEntity takes its mesh from the level loader's `Geometry`
        // attribute, not a property.
        std::vector<SDK::EntityId> stones;
        for (std::size_t index = 0; index < layout.stones.size(); ++index) {
            const Shared::Game::StonePileStone &stone = layout.stones[index];
            std::snprintf(name, sizeof(name), "KCDC_StonePile_%llu_stone%zu", id, index);
            const glm::quat turn         = glm::quat(stone.rotation[0], stone.rotation[1], stone.rotation[2], stone.rotation[3]);
            const SDK::EntityId entityId = SpawnPart("GeomEntity", name, LayoutPoint(stone.at), rotation * turn, stone.scale);
            if (entityId == 0) {
                _buildRefused = true;
                Framework::Logging::GetLogger(kLogger)->error("Stone pile {}: the GeomEntity for stone {} was refused", GetNetworkID(), index);
                return false;
            }
            KCDC::Common::ResolveEntity(entityId)->LoadGeometry(0, stone.model);
            stones.push_back(entityId);
        }

        // The path: a short segment running straight back from the stand
        // point. A one-node path makes the grab throw at once
        // (`ShouldAutoThrowAfterGrab`, 0x182EABFD4). While a stone is held,
        // `C_StoneThrowing::OnMovementInput` (0x182EACCDC) keeps the thrower
        // on the path and turns them against its direction (a `left` pile
        // negates it), so a path running back has them hold the stone at the
        // stand point, facing the throw -- the way the stone then goes.
        std::snprintf(name, sizeof(name), "KCDC_StonePile_%llu_node", id);
        const SDK::EntityId node = SpawnPart("StoneThrowingNode", name, LayoutPoint(Shared::Game::SiegePoint {0.0F, -kPathStart, 0.0F}), rotation, 1.0F);
        std::snprintf(name, sizeof(name), "KCDC_StonePile_%llu_node_end", id);
        const SDK::EntityId nodeEnd = SpawnPart("StoneThrowingNode", name, LayoutPoint(Shared::Game::SiegePoint {0.0F, -(kPathStart + kPathLength), 0.0F}), rotation, 1.0F);
        // Linked both ways: `BuildPathFromNode` (0x180D7D948) finds the next
        // node through the node registry and skips one it has visited, so
        // either direction of the prefab's link is met and neither loops.
        if (node != 0 && nodeEnd != 0) {
            Link(KCDC::Common::ResolveEntity(node), "next", nodeEnd);
            Link(KCDC::Common::ResolveEntity(nodeEnd), "next", node);
        }
        // A pick point is only a transform the pile reads off whatever its
        // link names; a bodyless entity carries it.
        std::snprintf(name, sizeof(name), "KCDC_StonePile_%llu_pick", id);
        const SDK::EntityId pick = KCDC::Common::SpawnCarrierEntity("BasicEntity", name, LayoutPoint(layout.pickPoint), rotation);
        if (pick != 0) {
            _parts.push_back(pick);
        }
        std::snprintf(name, sizeof(name), "KCDC_StonePile_%llu", id);
        const SDK::EntityId pile = SpawnPart("StoneThrowingPile", name, LayoutPoint(layout.pile), rotation * Yaw(kPileApproachYawDeg), kTriggerScale);
        if (node == 0 || nodeEnd == 0 || pick == 0 || pile == 0) {
            _buildRefused = true;
            Framework::Logging::GetLogger(kLogger)->error("Stone pile {}: its {} was refused", GetNetworkID(), node == 0 ? "StoneThrowingNode" : pick == 0 ? "pick point" : "StoneThrowingPile");
            return false;
        }

        // The prefab's links, then the pile's own reset to read them.
        SDK::IEntity *pileEntity = KCDC::Common::ResolveEntity(pile);
        Link(pileEntity, "path", node);
        Link(pileEntity, "pickPoint", pick);
        for (SDK::EntityId stone : stones) {
            Link(pileEntity, "stone", stone);
        }
        SDK::wh::playermodule::C_StoneThrowingPile *native = SDK::wh::playermodule::C_StoneThrowingPile::Find(pile);
        if (native == nullptr) {
            _buildRefused = true;
            Framework::Logging::GetLogger(kLogger)->error("Stone pile {}: its StoneThrowingPile registered no pile extension", GetNetworkID());
            return false;
        }
        native->Reset();
        _pileEntityId = pile;
        HideTrigger();
        _shown        = kShownStones;
        Framework::Logging::GetLogger(kLogger)->info("Built stone pile {} as {} entities", GetNetworkID(), _parts.size());
        return true;
    }

    void StonePile::HideTrigger() {
        // The pile's sphere is a trigger the interactor's ray is aimed at,
        // never meant to be seen. `UsableItem:SetupTrigger` makes it one: its
        // parts collide with rays alone (PHYSICPARAM_PART_FLAGS, every part's
        // flags set to geom_colltype_ray, its collider flags cleared), then
        // the entity takes the nodraw material. The level's piles stand like
        // that; a runtime spawn is left white, so the same two calls are made
        // here, natively and in the same order.
        SDK::IEntity *entity              = KCDC::Common::ResolveEntity(_pileEntityId);
        SDK::IPhysicalEntity *physics     = entity != nullptr ? entity->GetPhysics() : nullptr;
        SDK::I3DEngine *engine            = SDK::I3DEngine::GetInstance();
        SDK::IMaterialManager *materials  = engine != nullptr ? engine->GetMaterialManager() : nullptr;
        SDK::IMaterial *nodraw            = materials != nullptr ? materials->LoadMaterial("materials/special/nodraw", true, false, 0, true) : nullptr;
        if (physics == nullptr || nodraw == nullptr) {
            Framework::Logging::GetLogger(kLogger)->warn("Stone pile {}: its trigger cannot be set up (physics {}, nodraw material {})", GetNetworkID(), physics != nullptr, nodraw != nullptr);
            return;
        }
        // The nodraw material carries `mat_rock`, pierceability 2, and the
        // interactor's ray has pierceability 0, so through it the sphere is
        // only a pierceable hit. `RayWorldIntersection` keeps slot 0 for the
        // solid hit, and with no solid one in reach it reports one hit, which
        // the interactor reads from slot 0 (0x18082B984) and finds empty. A
        // level pile stands against its parapet, which fills slot 0; ours
        // stands in the open. So the part keeps the surfaces its own model
        // gave it -- read before the material changes them, written back
        // after (CPhysicalEntity::GetParams/SetParams, pe_params_part) -- and
        // nodraw only decides how it renders.
        SDK::pe_params_part current;
        current.ipart = 0;
        if (physics->GetParams(&current) != 0 && current.pMatMapping != 0 && current.nMats > 0) {
            const auto *mapping = reinterpret_cast<const std::int32_t *>(current.pMatMapping);
            _triggerSurfaces.assign(mapping, mapping + current.nMats);
        }
        else {
            _triggerSurfaces.clear();
        }
        entity->SetMaterial(nodraw);

        SDK::pe_params_part part;
        part.flagsAND         = 0;
        part.flagsOR          = SDK::geom_colltype_ray;
        part.flagsColliderAND = 0;
        part.pMatMapping      = reinterpret_cast<std::intptr_t>(_triggerSurfaces.empty() ? nullptr : _triggerSurfaces.data());
        part.nMats            = static_cast<std::int32_t>(_triggerSurfaces.size());
        physics->SetParams(&part);
    }

    void StonePile::Frame(std::uint64_t nowLocal) {
        if (_pileEntityId != 0 && KCDC::Common::ResolveEntity(_pileEntityId) == nullptr) {
            // Gone with the level; built again below.
            Despawn();
        }
        if (_pileEntityId == 0) {
            if (_buildRefused || nowLocal < _nextBuildAttempt) {
                return;
            }
            _nextBuildAttempt = nowLocal + kBuildRetryMs;
            if (!TryBuild()) {
                Despawn();
                return;
            }
        }

        // The server's count, written back only when it disagrees and nothing
        // is under way: the next stone is prepared after, which is what the
        // game's prompt asks for.
        const int wanted = ShownStones();
        if (wanted == _shown || nowLocal < _busyUntil) {
            return;
        }
        if (SDK::wh::playermodule::C_StoneThrowingPile *native = ResolvePile()) {
            native->SetActiveStoneCount(wanted);
            native->PrepareStoneForGrab();
            _shown = wanted;
        }
    }
} // namespace KCDC::Features::Siege
