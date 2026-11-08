#include "smart_object.h"
#include "smart_object_module.h"

#include "common/module_support.h"

#include "sdk/cryengine/action/i_game_framework.h"
#include "sdk/cryengine/entitysystem/i_entity_class_registry.h"
#include "sdk/cryengine/entitysystem/i_entity_system.h"
#include "sdk/cryengine/scriptsystem/i_script_table.h"
#include "sdk/wh/entitymodule/c_item_slot.h"

#include <logging/logger.h>

#include <algorithm>
#include <cstdio>
#include <string>
#include <utility>

namespace KCDC::Features::SmartObject {
    namespace {
        constexpr const char *kLogger = "SmartObject";

        /** A build the game refused is tried again this often, this many times. */
        constexpr MafiaNet::Time kRetryIntervalMs = 1000;
        constexpr std::uint32_t kMaxTries         = 5;

        /** A part's or brush's pose in the world, from the whole's and its own in the prefab's frame. */
        void Place(const glm::vec3 &position, const glm::quat &rotation, float x, float y, float z, float rx, float ry, float rz, float rw, SDK::Vec3 &worldPosition, SDK::Quat &worldRotation) {
            worldPosition = KCDC::Common::ToNative(position + rotation * glm::vec3(x, y, z));
            worldRotation = KCDC::Common::ToNative(glm::normalize(rotation * glm::quat(rw, rx, ry, rz)));
        }

        /**
         * What the level loader loads a placed object's `Geometry` with
         * (0x1803D02F0): physicalized unless its `Physics.bPhysicalize` is
         * "0", kept loaded when its `bAlwaysLoaded` is anything else.
         */
        int GeometryLoadFlags(const Shared::Game::SmartObjectPart &part) {
            int flags = 1;
            for (std::size_t index = 0; index < part.propertyCount; ++index) {
                const Shared::Game::PrefabProperty &property = part.properties[index];
                if (property.key == "Physics.bPhysicalize" && property.value == "0") {
                    flags &= ~1;
                }
                if (property.key == "bAlwaysLoaded" && property.value != "0") {
                    flags |= 2;
                }
            }
            return flags;
        }

        bool LevelLoaded() {
            auto *framework   = SDK::IGameFramework::GetInstance();
            auto *levelSystem = framework != nullptr ? framework->GetILevelSystem() : nullptr;
            return levelSystem != nullptr && levelSystem->IsLevelLoaded();
        }
    } // namespace

    SmartObjectAssembly::BuildResult SmartObjectAssembly::Build(const Shared::Game::SmartObjectBlueprint &blueprint, std::uint64_t guidBase, const glm::vec3 &position, const glm::quat &rotation) {
        Destroy();
        auto *entitySystem = SDK::IEntitySystem::GetInstance();
        auto *registry     = entitySystem != nullptr ? entitySystem->GetClassRegistry() : nullptr;
        if (registry == nullptr) {
            return BuildResult::Retry;
        }
        const glm::quat facing = glm::normalize(rotation);
        _guidBase              = guidBase;
        _blueprint             = &blueprint;

        // Every part spawned, but not initialised: its properties and its
        // links go on first, as the level loader puts them on before any of
        // a prefab's entities runs its script.
        std::vector<SDK::SEntitySpawnParams> params(blueprint.partCount);
        std::vector<std::string> names(blueprint.partCount);
        _parts.assign(blueprint.partCount, 0);
        for (std::size_t index = 0; index < blueprint.partCount; ++index) {
            const Shared::Game::SmartObjectPart &part = blueprint.parts[index];
            SDK::IEntityClass *entityClass            = registry->FindClass(std::string(part.entityClass).c_str());
            if (entityClass == nullptr) {
                Framework::Logging::GetLogger(kLogger)->error("Cannot build '{}': the {} class is not registered", blueprint.name, part.entityClass);
                Destroy();
                return BuildResult::Refused;
            }

            char name[64];
            std::snprintf(name, sizeof(name), "KCDC_SmartObject_%012llx_%zu", static_cast<unsigned long long>((guidBase >> 8) & 0xFFFFFFFFFFULL), index);
            names[index] = name;

            SDK::SEntitySpawnParams &spawn = params[index];
            spawn.entityClass              = entityClass;
            spawn.guid                     = guidBase | index;
            spawn.name                     = names[index].c_str();
            spawn.scale                    = SDK::Vec3 {part.scaleX, part.scaleY, part.scaleZ};
            Place(position, facing, part.posX, part.posY, part.posZ, part.rotX, part.rotY, part.rotZ, part.rotW, spawn.position, spawn.rotation);

            SDK::IEntity *entity = entitySystem->SpawnEntity(spawn, false);
            if (entity == nullptr) {
                Framework::Logging::GetLogger(kLogger)->warn("SpawnEntity refused part {} ('{}') of '{}'", index, part.name, blueprint.name);
                Destroy();
                return BuildResult::Retry;
            }
            _parts[index] = entity->GetId();

            // Before it initialises, which is when its own `OnInit` and `OnReset` read them.
            SDK::XmlNodeRef entityNode = KCDC::Common::LoadPrefabProperties(entity, {part.properties, part.propertyCount});
            // Listed before it initialises, which is when the item slot hook asks; and the slot
            // reads which item it holds from the node its spawn params carry into `InitEntity`.
            if (part.fixture) {
                SmartObjectModule::Get().AddFixture(spawn.guid);
                spawn.entityNode = std::move(entityNode);
            }
        }

        for (std::size_t index = 0; index < blueprint.partCount; ++index) {
            const Shared::Game::SmartObjectPart &part = blueprint.parts[index];
            SDK::IEntity *entity                      = entitySystem->GetEntity(_parts[index]);
            for (std::size_t link = 0; link < part.linkCount && entity != nullptr; ++link) {
                const std::uint8_t target = part.links[link].target;
                if (target < _parts.size() && _parts[target] != 0) {
                    SDK::ScopedCryString linkName(std::string(part.links[link].name).c_str());
                    entity->AddEntityLink(linkName, _parts[target], guidBase | target);
                }
            }
        }

        for (std::size_t index = 0; index < blueprint.partCount; ++index) {
            SDK::IEntity *entity = entitySystem->GetEntity(_parts[index]);
            // InitEntity queues the entity for deletion on failure.
            if (entity == nullptr || !entitySystem->InitEntity(entity, params[index])) {
                Framework::Logging::GetLogger(kLogger)->warn("InitEntity refused part {} ('{}') of '{}'", index, blueprint.parts[index].name, blueprint.name);
                _parts[index] = 0;
                Destroy();
                return BuildResult::Retry;
            }
        }

        // A part the editor placed as a `GeomEntity` gets its model once it is initialised, as the level loader gives it one.
        for (std::size_t index = 0; index < blueprint.partCount; ++index) {
            const Shared::Game::SmartObjectPart &part = blueprint.parts[index];
            SDK::IEntity *entity                      = part.geometry.empty() ? nullptr : entitySystem->GetEntity(_parts[index]);
            if (entity != nullptr && entity->LoadObject(0, std::string(part.geometry).c_str(), GeometryLoadFlags(part)) < 0) {
                Framework::Logging::GetLogger(kLogger)->warn("'{}' part {} ('{}') stands without '{}': the game would not load it", blueprint.name, index, part.name, part.geometry);
            }
        }

        // A station's book or tool slot that comes up empty leaves its minigame without it.
        for (std::size_t index = 0; index < blueprint.partCount; ++index) {
            const Shared::Game::SmartObjectPart &part = blueprint.parts[index];
            SDK::IEntity *entity                      = part.fixture ? entitySystem->GetEntity(_parts[index]) : nullptr;
            SDK::IEntityProxy *proxy                  = entity != nullptr ? entity->GetProxy(static_cast<int>(SDK::EEntityProxy::GameObjectLight)) : nullptr;
            if (proxy == nullptr) {
                continue;
            }
            // One the prefab leaves empty (`bSpawnOnStart="0"`, a dice table's own dice) is filled by its minigame.
            const SDK::wh::entitymodule::C_ItemSlot *slot = SDK::wh::entitymodule::C_ItemSlot::FromProxy(proxy);
            if (slot->SpawnsOnStart() && slot->GetMultiSlotItem() == nullptr) {
                const SDK::wh::entitymodule::S_ItemClassGUID &item = slot->GetItemClassGuid();
                Framework::Logging::GetLogger(kLogger)->warn("'{}' part {} ('{}') holds no item: class {:016x}{:016x}", blueprint.name, index, part.name, item.m_high, item.m_low);
            }
        }

        // DiceInteractor.lua offers the game's own "play dice" prompt until SetDiceAvailable(false)
        // turns it off, which the dice table's AI tree (AI/world/so_diceTable_new.xml) does for the
        // level's tables. A placed table's matches belong to the server, so its interactor starts off.
        for (std::size_t index = 0; index < blueprint.partCount; ++index) {
            if (blueprint.parts[index].entityClass != "DiceInteractor") {
                continue;
            }
            if (SDK::IScriptTable *script = KCDC::Common::EntityScriptTable(entitySystem->GetEntity(_parts[index]))) {
                script->SetValue("diceAvailable", false);
            }
        }

        // The furniture: solid, as the level's own brushes are.
        _brushes.resize(blueprint.brushCount);
        for (std::size_t index = 0; index < blueprint.brushCount; ++index) {
            const Shared::Game::SmartObjectBrush &brush = blueprint.brushes[index];
            SDK::Vec3 brushPosition {};
            SDK::Quat brushRotation {};
            Place(position, facing, brush.posX, brush.posY, brush.posZ, brush.rotX, brush.rotY, brush.rotZ, brush.rotW, brushPosition, brushRotation);
            const SDK::Matrix34 matrix = SDK::Matrix34FromTransform(brushPosition, brushRotation, SDK::Vec3 {brush.scaleX, brush.scaleY, brush.scaleZ});
            if (_brushes[index].Build(std::string(brush.mesh).c_str(), matrix, true) == KCDC::Common::Spawn::StaticBrush::BuildResult::MeshRefused) {
                Framework::Logging::GetLogger(kLogger)->warn("'{}' stands without '{}': the game would not load the mesh", blueprint.name, brush.mesh);
            }
        }
        return BuildResult::Built;
    }

    void SmartObjectAssembly::Move(const glm::vec3 &position, const glm::quat &rotation) {
        if (_blueprint == nullptr) {
            return;
        }
        const glm::quat facing = glm::normalize(rotation);
        auto *entitySystem     = SDK::IEntitySystem::GetInstance();
        for (std::size_t index = 0; index < _parts.size() && entitySystem != nullptr; ++index) {
            const Shared::Game::SmartObjectPart &part = _blueprint->parts[index];
            if (SDK::IEntity *entity = _parts[index] != 0 ? entitySystem->GetEntity(_parts[index]) : nullptr) {
                SDK::Vec3 partPosition {};
                SDK::Quat partRotation {};
                Place(position, facing, part.posX, part.posY, part.posZ, part.rotX, part.rotY, part.rotZ, part.rotW, partPosition, partRotation);
                entity->SetPosRotScale(partPosition, partRotation, SDK::Vec3 {part.scaleX, part.scaleY, part.scaleZ});
            }
        }
        for (std::size_t index = 0; index < _brushes.size(); ++index) {
            const Shared::Game::SmartObjectBrush &brush = _blueprint->brushes[index];
            SDK::Vec3 brushPosition {};
            SDK::Quat brushRotation {};
            Place(position, facing, brush.posX, brush.posY, brush.posZ, brush.rotX, brush.rotY, brush.rotZ, brush.rotW, brushPosition, brushRotation);
            _brushes[index].SetMatrix(SDK::Matrix34FromTransform(brushPosition, brushRotation, SDK::Vec3 {brush.scaleX, brush.scaleY, brush.scaleZ}));
        }
    }

    void SmartObjectAssembly::ForgetFixtures() {
        if (_blueprint == nullptr) {
            return;
        }
        for (std::size_t index = 0; index < _blueprint->partCount; ++index) {
            if (_blueprint->parts[index].fixture) {
                SmartObjectModule::Get().RemoveFixture(_guidBase | index);
            }
        }
    }

    void SmartObjectAssembly::Destroy() {
        ForgetFixtures();
        if (auto *entitySystem = SDK::IEntitySystem::GetInstance()) {
            for (const SDK::EntityId part : _parts) {
                if (part != 0) {
                    entitySystem->RemoveEntity(part, false);
                }
            }
        }
        _parts.clear();
        for (KCDC::Common::Spawn::StaticBrush &brush : _brushes) {
            brush.Destroy();
        }
        _brushes.clear();
        _blueprint = nullptr;
    }

    void SmartObjectAssembly::Forget() {
        ForgetFixtures();
        _parts.clear();
        for (KCDC::Common::Spawn::StaticBrush &brush : _brushes) {
            brush.Forget();
        }
        _brushes.clear();
        _blueprint = nullptr;
    }

    void SmartObjectAssembly::Release(std::vector<SDK::EntityId> &parts, std::vector<std::pair<SDK::IRenderNode *, SDK::IPhysicalEntity *>> &brushes) {
        ForgetFixtures();
        parts = std::exchange(_parts, {});
        for (KCDC::Common::Spawn::StaticBrush &brush : _brushes) {
            SDK::IRenderNode *node         = nullptr;
            SDK::IPhysicalEntity *collider = nullptr;
            brush.Release(node, collider);
            brushes.emplace_back(node, collider);
        }
        _brushes.clear();
        _blueprint = nullptr;
    }

    bool SmartObjectAssembly::OwnsEntity(SDK::EntityId entityId) const {
        return entityId != 0 && std::find(_parts.begin(), _parts.end(), entityId) != _parts.end();
    }

    bool SmartObjectAssembly::Bounds(SDK::AABB &bounds) const {
        bool any           = false;
        const auto include = [&](const SDK::AABB &box) {
            if (box.min.x > box.max.x) {
                return;
            }
            if (!any) {
                bounds = box;
                any    = true;
                return;
            }
            bounds.min = SDK::Vec3 {std::min(bounds.min.x, box.min.x), std::min(bounds.min.y, box.min.y), std::min(bounds.min.z, box.min.z)};
            bounds.max = SDK::Vec3 {std::max(bounds.max.x, box.max.x), std::max(bounds.max.y, box.max.y), std::max(bounds.max.z, box.max.z)};
        };
        auto *entitySystem = SDK::IEntitySystem::GetInstance();
        for (const SDK::EntityId part : _parts) {
            SDK::IEntity *entity = entitySystem != nullptr && part != 0 ? entitySystem->GetEntity(part) : nullptr;
            if (entity != nullptr) {
                SDK::AABB box {};
                entity->GetWorldBounds(box);
                include(box);
            }
        }
        for (const KCDC::Common::Spawn::StaticBrush &brush : _brushes) {
            if (brush.IsBuilt()) {
                SDK::AABB box {};
                brush.GetNode()->FillBBox(box);
                include(box);
            }
        }
        return any;
    }

    void SmartObject::DeallocReplica(MafiaNet::Connection_RM3 *) {
        std::vector<SDK::EntityId> parts;
        std::vector<std::pair<SDK::IRenderNode *, SDK::IPhysicalEntity *>> brushes;
        _assembly.Release(parts, brushes);
        SmartObjectModule::Get().QueueRelease(std::move(parts), std::move(brushes));
        delete this;
    }

    void SmartObject::Unbind() {
        _assembly.Forget();
        _refused = false;
        _tries   = 0;
        _nextTry = 0;
    }

    void SmartObject::Frame() {
        if (_assembly.IsBuilt() || _refused || !LevelLoaded()) {
            return;
        }
        const MafiaNet::Time now = MafiaNet::GetTime();
        if (now < _nextTry) {
            return;
        }
        // The same object streamed out and back in: until the entity system
        // collects its old parts, it keeps their GUIDs mapped to them, and new
        // parts under those GUIDs could never be found by GUID.
        if (auto *entitySystem = SDK::IEntitySystem::GetInstance(); entitySystem != nullptr && entitySystem->GetEntityByGuid(PartGuid(0)) != nullptr) {
            return;
        }
        _nextTry = now + kRetryIntervalMs;
        if (++_tries > kMaxTries) {
            _refused = true;
            Framework::Logging::GetLogger(kLogger)->error("Gave up building smart object {} after {} attempts", GetNetworkID(), kMaxTries);
            return;
        }
        const Shared::Game::SmartObjectBlueprint *layout = Blueprint();
        if (layout == nullptr) {
            _refused = true;
            Framework::Logging::GetLogger(kLogger)->error("Smart object {} names blueprint {}, which this build's catalog does not have", GetNetworkID(), blueprint);
            return;
        }
        switch (_assembly.Build(*layout, PartGuid(0), position, rotation)) {
        case SmartObjectAssembly::BuildResult::Built: Framework::Logging::GetLogger(kLogger)->info("Built smart object {} ('{}', serial {}) from {} part(s) and {} brush(es)", GetNetworkID(), layout->name, serial, layout->partCount, layout->brushCount); break;
        case SmartObjectAssembly::BuildResult::Refused: _refused = true; break;
        case SmartObjectAssembly::BuildResult::Retry: break;
        }
    }
} // namespace KCDC::Features::SmartObject
