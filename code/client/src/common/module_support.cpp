#include <utils/safe_win32.h>

#include "module_support.h"

#include "sdk/cryengine/action/i_game_framework.h"
#include "sdk/cryengine/entitysystem/i_entity.h"
#include "sdk/cryengine/entitysystem/i_entity_class_registry.h"
#include "sdk/cryengine/entitysystem/i_entity_proxy.h"
#include "sdk/cryengine/entitysystem/i_entity_system.h"
#include "sdk/cryengine/scriptsystem/i_script_table.h"
#include "sdk/cryengine/system/i_localization_manager.h"
#include "sdk/cryengine/system/i_system.h"
#include "sdk/cryengine/system/xml_node_ref.h"
#include "sdk/wh/entitymodule/c_actor.h"
#include "sdk/wh/entitymodule/c_human.h"

#include <networking/network_peer.h>

#include <mafianet/get_time.h>

namespace KCDC::Common {
    SDK::IEntity *LocalPlayer() {
        auto *framework = SDK::IGameFramework::GetInstance();
        return framework != nullptr ? framework->GetClientEntity() : nullptr;
    }

    SDK::wh::entitymodule::C_Human *LocalHumanActor() {
        auto *framework = SDK::IGameFramework::GetInstance();
        return static_cast<SDK::wh::entitymodule::C_Human *>(framework != nullptr ? framework->GetClientActor() : nullptr);
    }

    SDK::wh::rpgmodule::C_Soul *LocalSoul() {
        auto *actor = LocalHumanActor();
        return actor != nullptr ? actor->GetSoul() : nullptr;
    }

    SDK::EntityId LocalActorEntityId() {
        auto *framework  = SDK::IGameFramework::GetInstance();
        auto *localActor = framework != nullptr ? framework->GetClientActor() : nullptr;
        return localActor != nullptr ? localActor->GetEntityId() : 0;
    }

    bool ServerClockDifferential(std::int64_t &differential) {
        auto *networkPeer = Framework::CoreModules::GetNetworkPeer();
        auto *peer        = networkPeer != nullptr ? networkPeer->GetPeer() : nullptr;
        if (peer == nullptr) {
            return false;
        }
        const auto server = peer->GetGUIDFromIndex(0);
        if (server == MafiaNet::UNASSIGNED_RAKNET_GUID || peer->GetAveragePing(server) < 0) {
            return false;
        }
        differential = static_cast<std::int64_t>(peer->GetClockDifferential(server));
        return true;
    }

    bool ServerRoundTripMs(std::int32_t &roundTripMs) {
        auto *networkPeer = Framework::CoreModules::GetNetworkPeer();
        auto *peer        = networkPeer != nullptr ? networkPeer->GetPeer() : nullptr;
        if (peer == nullptr) {
            return false;
        }
        const auto server = peer->GetGUIDFromIndex(0);
        const int ping    = server != MafiaNet::UNASSIGNED_RAKNET_GUID ? peer->GetAveragePing(server) : -1;
        if (ping < 0) {
            return false;
        }
        roundTripMs = ping;
        return true;
    }

    std::uint64_t NowMs() {
        return MafiaNet::GetTimeUS() / 1000;
    }

    std::string GameText(const char *key, const char *fallback) {
        SDK::ILocalizationManager *strings = SDK::ILocalizationManager::GetInstance();
        SDK::ScopedCryString localized("");
        return strings != nullptr && strings->LocalizeLabel(key, *localized.Out()) && localized.c_str()[0] != '\0' ? std::string(localized.c_str()) : std::string(fallback);
    }

    namespace {
        /**
         * Empties the properties that would give a carrier a body of its own.
         * Clearing the model is enough -- SetFromProperties returns on an empty
         * one before SetupModel -- but the physics flags follow so the intent
         * survives an edit to that early return. They must be booleans: Lua
         * treats numeric zero as truthy.
         */
        bool ClearCarrierProperties(SDK::IEntity *entity) {
            auto *proxy                    = static_cast<SDK::IEntityProxy *>(entity->GetProxy(static_cast<int>(SDK::EEntityProxy::Script)));
            SDK::IScriptTable *scriptTable = proxy != nullptr ? proxy->GetScriptTable() : nullptr;
            if (scriptTable == nullptr) {
                // A class with no script has no properties to empty, which is
                // the good case rather than a failure.
                return true;
            }

            auto properties = scriptTable->GetTable("Properties");
            if (!properties) {
                return false;
            }
            properties->SetValue("object_Model", "");

            auto physics = properties->GetTable("Physics");
            if (!physics) {
                return false;
            }
            physics->SetValue("bPhysicalize", false);
            physics->SetValue("bRigidBody", false);
            return true;
        }
    } // namespace

    SDK::IEntity *ResolveEntity(SDK::EntityId entityId) {
        SDK::IEntitySystem *entities = SDK::IEntitySystem::GetInstance();
        SDK::IEntity *entity         = entities != nullptr && entityId != 0 ? entities->GetEntity(entityId) : nullptr;
        return entity != nullptr && !entity->IsGarbage() ? entity : nullptr;
    }

    SDK::IScriptTable *EntityScriptTable(SDK::IEntity *entity) {
        auto *proxy = entity != nullptr ? entity->GetProxy(static_cast<int>(SDK::EEntityProxy::Script)) : nullptr;
        return proxy != nullptr ? proxy->GetScriptTable() : nullptr;
    }

    SDK::IScriptTable *ClassScriptTable(const char *className) {
        auto *entities = SDK::IEntitySystem::GetInstance();
        auto *classes  = entities != nullptr ? entities->GetClassRegistry() : nullptr;
        auto *type     = classes != nullptr ? classes->FindClass(className) : nullptr;
        return type != nullptr ? type->GetScriptTable() : nullptr;
    }

    void AssignDeclaredProperty(SDK::IScriptTable &properties, const char *key, const char *value) {
        const char *existing = nullptr;
        if (properties.GetValue(key, existing)) {
            properties.SetValue(key, value);
        }
    }

    void AssignDeclaredProperty(SDK::IScriptTable &properties, const char *key, float value) {
        float existing = 0.0F;
        if (properties.GetValue(key, existing)) {
            properties.SetValue(key, value);
        }
    }

    void AssignDeclaredProperty(SDK::IScriptTable &properties, const char *key, bool value) {
        bool existing = false;
        if (properties.GetValue(key, existing)) {
            properties.SetValue(key, value);
        }
    }

    SDK::XmlNodeRef LoadPrefabProperties(SDK::IEntity *entity, std::span<const Shared::Game::PrefabProperty> properties) {
        SDK::ISystem *system = SDK::ISystem::GetInstance();
        if (entity == nullptr || system == nullptr || properties.empty()) {
            return {};
        }

        // Parsed rather than created, for a node of its own document that takes writes.
        static constexpr char kEntityXml[] = "<Entity><Properties/></Entity>";
        SDK::XmlNodeRef entityNode         = system->LoadXmlFromBuffer(kEntityXml, sizeof(kEntityXml) - 1, false, false);
        const SDK::XmlNodeRef root         = entityNode.IsValid() ? entityNode->findChild("Properties") : SDK::XmlNodeRef {};
        if (!root.IsValid()) {
            return {};
        }
        for (const Shared::Game::PrefabProperty &property : properties) {
            // Each `Table.` a child node, as the level's own XML nests a table's values.
            SDK::XmlNodeRef table = root;
            std::string_view key  = property.key;
            for (std::size_t dot = key.find('.'); dot != std::string_view::npos; dot = key.find('.')) {
                const std::string name(key.substr(0, dot));
                SDK::XmlNodeRef child = table->findChild(name.c_str());
                table                 = child.IsValid() ? std::move(child) : table->newChild(name.c_str());
                key.remove_prefix(dot + 1);
            }
            table->setAttr(std::string(key).c_str(), std::string(property.value).c_str());
        }
        // An item slot has none: it reads the node itself, from its spawn params.
        if (SDK::IEntityProxy *script = entity->GetProxy(static_cast<int>(SDK::EEntityProxy::Script))) {
            script->SerializeXML(entityNode, true);
        }
        return entityNode;
    }

    SDK::EntityId SpawnCarrierEntity(const char *className, const char *name, const glm::vec3 &position, const glm::quat &rotation) {
        SDK::IEntitySystem *entities = SDK::IEntitySystem::GetInstance();
        if (entities == nullptr) {
            return 0;
        }

        SDK::IEntityClassRegistry *registry = entities->GetClassRegistry();
        SDK::IEntityClass *entityClass      = registry != nullptr ? registry->FindClass(className) : nullptr;
        if (entityClass == nullptr) {
            return 0;
        }

        SDK::SEntitySpawnParams params {};
        params.entityClass = entityClass;
        params.name        = name;
        params.position    = ToNative(position);
        params.rotation    = ToNative(glm::normalize(rotation));

        // Uninitialised, so the properties can be emptied before OnSpawn reads them.
        SDK::IEntity *entity = entities->SpawnEntity(params, false);
        if (entity == nullptr) {
            return 0;
        }

        const SDK::EntityId entityId = entity->GetId();
        if (!ClearCarrierProperties(entity)) {
            entities->RemoveEntity(entityId, false);
            return 0;
        }

        // InitEntity queues the entity for deletion on failure.
        if (!entities->InitEntity(entity, params) || ResolveEntity(entityId) == nullptr) {
            return 0;
        }
        return entityId;
    }

    void PlaceEntity(SDK::IEntity *entity, const glm::vec3 &position, const glm::quat &rotation) {
        entity->SetPosRotScale(ToNative(position), ToNative(glm::normalize(rotation)), SDK::Vec3 {1.0F, 1.0F, 1.0F}, SDK::ENTITY_XFORM_IGNORE_PHYSICS);
    }

    void ClearSteering(SDK::IMovementController *movement) {
        SDK::CMovementRequest request {};
        request.ClearMoveTarget();
        request.ClearBodyTarget();
        request.ClearDesiredSpeed();
        request.ClearLookTarget();
        movement->RequestMovement(request);
    }
} // namespace KCDC::Common
