#pragma once

#include "prop_editor_objects.h"
#include "prop_editor_world_objects.h"

#include "common/spawn/prop_spawner.h"
#include "features/smart_object/smart_object.h"
#include "prop_editor_layers.h"
#include "sdk/cryengine/3dengine/i_render_node.h"
#include "sdk/types.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace KCDC::Features::Prop {
    /**
     * A mesh, door, smart object or item the editor placed. Physics changes
     * rebuild the entity, so the id outlives it.
     *
     * Only a prop is a mesh the export carries as one. The rest are previews
     * of what the server builds from the export: a door shows its leaf, an
     * item its mesh, and a smart object stands assembled as the game client
     * builds a placed one, so a bench can be tried. `entityId` is the
     * preview's entity -- a smart object's first part -- and `settings` the
     * rest of its kind's entry, which only the export reads.
     */
    struct PlacedObject {
        std::uint64_t id       = 0;
        SDK::EntityId entityId = 0;
        PlacedKind kind        = PlacedKind::Prop;
        std::string model; // a prop's `objects/....cgf`; a door's leaf, a blueprint, an item class
        std::string name;  // the mesh's file stem, or the door's, blueprint's or item's name
        nlohmann::json settings;

        SDK::Vec3 position {};
        SDK::Quat rotation {0.0F, 0.0F, 0.0F, 1.0F};
        SDK::Vec3 scale {1.0F, 1.0F, 1.0F};
        KCDC::Common::Spawn::PropPhysics physics = KCDC::Common::Spawn::PropPhysics::Static;
        std::string group;

        // Coalesce scale edits before rebuilding the native collision.
        float collisionScale        = 1.0F;
        bool collisionDirty         = false;
        bool collisionRebuildFailed = false;
        std::chrono::steady_clock::time_point collisionChanged {};

        /** The entity's render node as of the last refresh, for matching octree picks. An identity only: never dereferenced. */
        const SDK::IRenderNode *renderNode = nullptr;

        /** Refreshed from the entity each tick; empty when it has no mesh. */
        SDK::AABB worldBounds {};
        bool hasBounds = false;
    };

    /** The mesh an item class lies on the ground as, as the game's tables name it, or empty. Game thread. */
    std::string ItemPreviewMesh(const std::string &itemClass);

    /** Everything the editor has placed. Game thread only. */
    class EditorScene final {
      public:
        /** Spawns `model` and takes ownership. Returns 0 when it failed. */
        std::uint64_t Place(const std::string &model, const SDK::Vec3 &position, const SDK::Quat &rotation, const SDK::Vec3 &scale, KCDC::Common::Spawn::PropPhysics physics);

        /**
         * Places a door, smart object or item, its model and settings checked
         * the way the server will load them. None of them scales. Returns 0,
         * with `error` saying why, when it failed.
         */
        std::uint64_t PlaceWorldObject(PlacedKind kind, const std::string &model, const nlohmann::json &settings, const SDK::Vec3 &position, const SDK::Quat &rotation, std::string &error);

        /** Replaces a door's, smart object's or item's settings, checked as at placement. */
        bool SetSettings(std::uint64_t id, const nlohmann::json &settings, std::string &error);

        /** Moves an object. Scale changes schedule a collision rebuild after a short pause. */
        bool SetTransform(std::uint64_t id, const SDK::Vec3 &position, const SDK::Quat &rotation, const SDK::Vec3 &scale);

        /** Swaps an object's physics, which means building it again. */
        bool SetPhysics(std::uint64_t id, KCDC::Common::Spawn::PropPhysics physics);

        /** Respawns the entity so its collision matches the current scale. */
        bool Rebuild(std::uint64_t id);

        /** Places a copy beside the original. Returns 0 when it failed. */
        std::uint64_t Duplicate(std::uint64_t id);

        /** Rebuilds a removed object under its old id, for undo. */
        bool Restore(const PlacedObject &record);

        bool Remove(std::uint64_t id);
        void SetGroup(std::uint64_t id, const std::string &group);

        void Clear();

        /** Drops every record without touching the world, which is gone: its entities and brushes went with the level. */
        void Forget();

        /** Drops dead handles and refreshes bounds. */
        void Refresh();

        /** The nearest object a ray enters, or 0, with its distance. */
        std::uint64_t PickNearest(const SDK::Vec3 &origin, const SDK::Vec3 &direction, float maxDistance, float &distance, const EditorLayers *layers = nullptr) const;

        const std::vector<PlacedObject> &Objects() const {
            return _objects;
        }

        const PlacedObject *Find(std::uint64_t id) const;

        /** The object built on `entityId`, or 0. */
        std::uint64_t FindByEntityId(SDK::EntityId entityId) const;

        /** Resolves either the entity or its render node to an editor-owned object. */
        std::uint64_t FindByPick(const WorldPick &pick) const;

        std::size_t Count() const {
            return _objects.size();
        }

      private:
        PlacedObject *FindMutable(std::uint64_t id);

        /** Builds `object`'s entity from its own fields. */
        bool BuildEntity(PlacedObject &object);

        /** A smart object's preview, under GUIDs no replicated one uses. */
        bool BuildAssembly(PlacedObject &object);

        /** Removes `object`'s entity, leaving the record behind. */
        void DestroyEntity(PlacedObject &object);

        /** Writes the stored transform onto the live entity. */
        void ApplyTransform(const PlacedObject &object);

        std::vector<PlacedObject> _objects;
        std::uint64_t _nextId = 1;

        /** Each smart object's preview, by record id. */
        std::unordered_map<std::uint64_t, std::unique_ptr<SmartObject::SmartObjectAssembly>> _assemblies;

        /** Mints preview GUIDs; never reused, across scenes too. */
        static std::uint64_t _previewSerial;
    };
} // namespace KCDC::Features::Prop
