#pragma once

#include "sdk/cryengine/action/i_movement_controller.h"
#include "sdk/cryengine/entitysystem/i_entity.h"
#include "sdk/cryengine/scriptsystem/i_script_table.h"
#include "sdk/cryengine/system/xml_node_ref.h"
#include "sdk/types.h"
#include "sdk/wh/entitymodule/c_human.h"
#include "sdk/wh/rpgmodule/c_soul.h"

#include "shared/game/prefab_property.h"

#include <core_modules.h>
#include <networking/replication/replication_manager.h>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

// Primitives every replicated module needs and none of them owns: who the
// local player is, whether a pose is worth writing, and how to state one.
// Each of these had grown a private copy in two or three modules.
namespace KCDC::Common {
    // --- The local player -------------------------------------------------

    SDK::IEntity *LocalPlayer();
    SDK::wh::entitymodule::C_Human *LocalHumanActor();

    /** The entity id of the local player's body, or 0 while there is none. */
    SDK::EntityId LocalActorEntityId();

    /** The local player's soul, or null while there is no body. */
    SDK::wh::rpgmodule::C_Soul *LocalSoul();

    // --- The server clock -------------------------------------------------

    /**
     * The peer's estimate of how far this machine's clock sits from the
     * server's, in milliseconds, or false while there is no ping to base it
     * on. A server-stamped time becomes a local one by subtracting it.
     */
    bool ServerClockDifferential(std::int64_t &differential);

    /** The peer's average round trip to the server, in milliseconds, or false while it has none. */
    bool ServerRoundTripMs(std::int32_t &roundTripMs);

    /** This machine's clock in milliseconds, at the microsecond clock's resolution: the one every module's tick reads. */
    std::uint64_t NowMs();

    // --- The game's text --------------------------------------------------

    /** A game string by its localization key, or `fallback` when it has none. */
    std::string GameText(const char *key, const char *fallback);

    // --- Conversion between the engine's vectors and the wire's ------------

    inline glm::vec3 ToGlm(const SDK::Vec3 &value) {
        return glm::vec3(value.x, value.y, value.z);
    }

    /** Note the reordering: the engine stores xyzw, glm constructs wxyz. */
    inline glm::quat ToGlm(const SDK::Quat &value) {
        return glm::quat(value.w, value.x, value.y, value.z);
    }

    inline SDK::Vec3 ToNative(const glm::vec3 &value) {
        return SDK::Vec3 {value.x, value.y, value.z};
    }

    inline SDK::Quat ToNative(const glm::quat &value) {
        return SDK::Quat {value.x, value.y, value.z, value.w};
    }

    // --- Conversion between the engine's GUIDs and the wire's --------------

    /**
     * A wire GUID -- `ItemClassId`, `BuffId`, `PerkGuid`, any `{low, high}` --
     * as the engine's. The wire's `low` is `CryGUID::hipart`: the order
     * `cry_guid_text.h` and every generated catalog spell GUIDs in.
     */
    template <typename WireGuid>
    SDK::CryGUID ToCryGuid(const WireGuid &id) {
        return SDK::CryGUID {id.low, id.high};
    }

    template <typename WireGuid>
    WireGuid FromCryGuid(const SDK::CryGUID &guid) {
        return WireGuid {guid.hipart, guid.lopart};
    }

    // --- Stating a pose ---------------------------------------------------

    /**
     * Places `entity` outright, leaving its scale and physics alone.
     *
     * Uncontested rather than pushed: a replicated body's transform has been
     * taken away from whatever the engine would otherwise move it with, so the
     * pose stated here is the pose it keeps. The rotation is normalized on the
     * way in, because a wire quaternion has been through quantization.
     */
    void PlaceEntity(SDK::IEntity *entity, const glm::vec3 &position, const glm::quat &rotation);

    /**
     * Takes back everything a module steered `movement` with: the move and
     * body targets, the desired speed and the look target. The controller
     * merges what it is sent and keeps a target until told otherwise, so a
     * body a module stops driving goes on walking to the last place asked
     * unless this is sent once.
     */
    void ClearSteering(SDK::IMovementController *movement);

    // --- Carrier entities -------------------------------------------------

    /**
     * Spawns an initialised, bodyless `BasicEntity` at a pose, for a module that
     * needs somewhere to hang a render slot. The caller owns the result and
     * removes it again.
     *
     * A scripted `className` has its properties emptied between SpawnEntity
     * and InitEntity -- `BasicEntity.lua` defaults `object_Model` to a pyramid
     * and physicalizes it, and `OnSpawn` reads that once. A class with no
     * script has nothing to empty. Returns 0 on refusal, having cleaned up.
     */
    SDK::EntityId SpawnCarrierEntity(const char *className, const char *name, const glm::vec3 &position, const glm::quat &rotation);

    /** The live entity behind an id, or null once it has gone or been queued for deletion. */
    SDK::IEntity *ResolveEntity(SDK::EntityId entityId);

    /** The script table of an entity's script proxy, or null for an entity without one. */
    SDK::IScriptTable *EntityScriptTable(SDK::IEntity *entity);

    /** The class script table every instance of `className` delegates to, or null for an unregistered class. */
    SDK::IScriptTable *ClassScriptTable(const char *className);

    /**
     * Writes a value into a `Properties` table by the type of the script's
     * own default, the way the level loader assigns one; a key the script
     * does not declare is left alone.
     */
    void AssignDeclaredProperty(SDK::IScriptTable &properties, const char *key, const char *value);
    void AssignDeclaredProperty(SDK::IScriptTable &properties, const char *key, float value);
    void AssignDeclaredProperty(SDK::IScriptTable &properties, const char *key, bool value);

    /**
     * A prefab part's `Properties`, given to an entity spawned but not yet
     * initialised, the way the level loader gives them: as the `Properties`
     * node of its entity XML, through the script proxy's `SerializeXML`
     * (0x180ECEF78). The game's own assignment (0x1807197F4) converts each
     * value by the type of the script's default, leaves a key the script does
     * not declare alone, and writes a `Table.key` into the entity's own copy
     * of that table.
     *
     * Answers the entity node, which the caller hands an item slot as
     * `SEntitySpawnParams::entityNode` for `InitEntity`, as the loader does:
     * a slot reads its own properties -- which item it holds -- only from
     * there (`C_ItemSlot::Init`, 0x1804578E0), and spawns nothing without.
     * An item slot has no script proxy, so it gets only the node. Invalid
     * when there is nothing to give.
     */
    SDK::XmlNodeRef LoadPrefabProperties(SDK::IEntity *entity, std::span<const Shared::Game::PrefabProperty> properties);

    // --- Walking replication ----------------------------------------------

    /**
     * The first replica of `T` satisfying `predicate`, or nullptr.
     *
     * Replication offers iteration but no search, and three modules had each
     * grown their own ForEach-with-a-flag around that. The walk is not cut
     * short: the callback is a `fu2::function` the manager drives to the end.
     * Before the spawn barrier completes, pass the network peer's manager:
     * CoreModules publishes its gameplay view only after finalization.
     */
    template <typename T, typename Predicate>
    T *FindReplica(Predicate &&predicate, Framework::Networking::Replication::ReplicationManager *replication = Framework::CoreModules::GetReplication()) {
        if (replication == nullptr) {
            return nullptr;
        }

        T *found = nullptr;
        replication->template ForEach<T>([&](T *candidate) {
            if (found == nullptr && candidate != nullptr && predicate(candidate)) {
                found = candidate;
            }
        });
        return found;
    }

    /** The replica of `T` with `networkId`, or nullptr for an unknown id. */
    template <typename T>
    T *FindReplicaById(std::uint64_t networkId) {
        auto *replication = Framework::CoreModules::GetReplication();
        return replication != nullptr && networkId != 0 ? replication->template GetEntity<T>(networkId) : nullptr;
    }

    // --- Token issue ------------------------------------------------------

    /**
     * Hands out the next value of `counter`, never 0.
     *
     * Zero is the wire's "no token", so it is skipped rather than issued.
     */
    inline std::uint64_t NextToken(std::uint64_t &counter) {
        const std::uint64_t token = counter++;
        return token != 0 ? token : counter++;
    }
} // namespace KCDC::Common
