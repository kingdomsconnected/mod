#pragma once

#include "prop_editor_document.h"
#include "prop_editor_selection.h"

#include "shared/features/level_edit/level_edit_definition.h"

#include "common/spawn/static_brush.h"
#include "sdk/cryengine/3dengine/i_render_node.h"
#include "sdk/types.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace KCDC::Features::Prop {
    /** What a ray into the level landed on. */
    enum class WorldPickKind : std::uint8_t {
        None,
        RenderNode, // a brush, vegetation or another 3D-engine node
        Entity,     // a door, a body, a placed item
        Terrain,
        Unknown, // solid, but owned by something the editor cannot hold
    };

    const char *WorldPickKindToString(WorldPickKind kind);

    /** How hard a pick may look; only the entity pass scales with the level. */
    enum class PickEffort : std::uint8_t {
        Quick,    // the ray and the octree
        Thorough, // adds the walk over every entity
    };

    struct WorldPick {
        WorldPickKind kind     = WorldPickKind::None;
        SDK::IRenderNode *node = nullptr; // RenderNode only
        SDK::EntityId entityId = 0;       // Entity only
        SDK::Vec3 hitPoint {};
        float distance  = 0.0F;
        bool boundsOnly = false; // approximate fallback, not a collider surface

        int foreignId = -1; // what the collider said it belonged to
        int inspected = 0;  // render nodes the octree pass weighed
    };

    /** Resolve overlapping candidates without letting decoration bounds steal a solid object. */
    inline bool PreferWorldPick(const WorldPick &candidate, const WorldPick &current, bool preferPhysics) {
        if (candidate.kind == WorldPickKind::None) {
            return false;
        }
        if (current.kind == WorldPickKind::None) {
            return true;
        }
        const bool candidateObject = candidate.kind == WorldPickKind::RenderNode || candidate.kind == WorldPickKind::Entity;
        const bool currentObject   = current.kind == WorldPickKind::RenderNode || current.kind == WorldPickKind::Entity;
        if (preferPhysics && candidateObject && currentObject && candidate.boundsOnly != current.boundsOnly) {
            return !candidate.boundsOnly;
        }
        return candidate.distance < current.distance;
    }

    /** Whether two picks name the same level object; terrain and misses name none. */
    inline bool IsSameLevelObject(const WorldPick &left, const WorldPick &right) {
        return left.kind == right.kind && ((left.kind == WorldPickKind::Entity && left.entityId == right.entityId) || (left.kind == WorldPickKind::RenderNode && left.node == right.node));
    }

    /** Names a pick without taking hold of it. Game thread. */
    void DescribeWorldPick(const WorldPick &pick, std::string &label, std::string &detail);

    /** One level object near the camera, as the level browser lists it. */
    struct LevelRow {
        WorldPickKind kind     = WorldPickKind::None;
        SDK::IRenderNode *node = nullptr; // RenderNode only; valid for this level
        SDK::EntityId entityId = 0;       // Entity only
        std::string label;
        std::string detail; // the entity class, or the node type and mesh path
        SDK::Vec3 position {};
        float distance = 0.0F;
        bool edited    = false; // already in the editor's list
        bool hidden    = false;
    };

    /** One level object a selection tool weighs, read without taking hold of it. */
    struct LevelCandidate {
        WorldPick pick;        // RenderNode or Entity, valid for this frame only
        std::uint8_t kind = 0; // SelectableKinds::Brush, Vegetation or Entity
        std::string name;
        std::string mesh;      // a node's mesh path, an entity's model; empty for an entity without one
        std::string className; // Entity only
        SDK::Vec3 position {};
        SDK::AABB bounds {};
        bool hasBounds = false;
        bool hidden    = false;
    };

    /** A level object the editor holds. `original*` makes every change reversible; `guid`/`path` key it in a saved map. */
    struct WorldEdit {
        std::uint64_t id   = 0;
        WorldPickKind kind = WorldPickKind::None;

        // Valid only straight after Resolve: HLOD streaming deletes and
        // re-creates level brushes as the camera moves, so a pointer kept
        // from an earlier frame may be freed memory.
        SDK::IRenderNode *node = nullptr;
        SDK::EntityId entityId = 0;
        /** Entity only, refreshed by the O(1) live lookup on every Refresh; identity, never dereferenced. */
        const SDK::IRenderNode *entityRenderNode = nullptr;
        std::string label;  // short enough for a list row
        std::string detail; // what it is, and the path that identifies it

        std::uint64_t guid = 0; // Entity only; 0 for one the level did not author
        std::string path;       // RenderNode: the mesh path. Entity: its name
        std::string className;  // Entity only

        SDK::Vec3 position {};
        SDK::Quat rotation {0.0F, 0.0F, 0.0F, 1.0F};
        SDK::Vec3 scale {1.0F, 1.0F, 1.0F};

        SDK::Vec3 originalPosition {};
        SDK::Quat originalRotation {0.0F, 0.0F, 0.0F, 1.0F};
        SDK::Vec3 originalScale {1.0F, 1.0F, 1.0F};

        /** False for an edit imported while its object was streamed out, until it is first seen. */
        bool originalKnown = true;

        // What the user asked for, re-applied whenever the object is found
        // again: a fresh HLOD instance or a layer switch undoes it.
        bool hidden = false;
        bool moved  = false;

        /**
         * The mesh standing in the object's place, or empty. Set, the object
         * is out of the world exactly as `hidden` takes it out, and this mesh
         * stands at the edit's pose with a collider of its own.
         */
        std::string replacement;

        /** The replacement as built; not built until the object's own pose has been seen. */
        KCDC::Common::Spawn::StaticBrush replacementBrush;

        /** What `replacementBrush` was built from and where, so a change rebuilds or moves it. */
        std::string replacementBuilt;
        SDK::Vec3 replacementPosition {};
        SDK::Quat replacementRotation {0.0F, 0.0F, 0.0F, 1.0F};
        SDK::Vec3 replacementScale {1.0F, 1.0F, 1.0F};

        /** The game would not load `replacement`; not tried again until it changes. */
        bool replacementRefused = false;

        /** Whether the object is out of the world: taken out, or swapped for `replacement`. */
        bool IsRemoved() const {
            return hidden || !replacement.empty();
        }

        /** Found in the world this tick. False while streamed out; the edit waits. */
        bool loaded = true;

        /** Brush only: on a layer the level switches (ERF_ACTIVE_LAYER seen set). */
        bool onSwitchedLayer = false;

        /** Brush only: that layer is switched off right now, so the level hides it. */
        bool layerOff = false;

        std::uint16_t layerId = 0;

        /**
         * Brush only: the mesh whose layer physics collider this edit keeps
         * out of the world, or null. While it is held the brush owns its
         * collision instead, so hiding and moving it take the collision
         * along; with the mesh and `originalPosition` it is the key
         * `LevelColliderModule` releases.
         */
        SDK::IStatObj *collisionHeldFor = nullptr;

        SDK::AABB worldBounds {};
        bool hasBounds = false;
    };

    /**
     * The level's own objects the editor has adopted, held by key rather than
     * by pointer so they survive HLOD streaming and layer switches. The level
     * edge drops them all. Game thread only.
     */
    class WorldObjects final {
      public:
        /** What is under a ray, across physics, octree and entities, which see different populations. */
        WorldPick Pick(const SDK::Vec3 &origin, const SDK::Vec3 &direction, float maxDistance, PickEffort effort, bool preferPhysics = true) const;

        /**
         * The nearest plant whose box the ray enters within `maxDistance`. A
         * tree's crown has no collider and Pick passes through it to the
         * ground, then leaves out a plant whose box holds that ground point,
         * so a click under a tree is not stolen; the selection brush, painting
         * over trees, wants that tree.
         */
        WorldPick PickPlant(const SDK::Vec3 &origin, const SDK::Vec3 &direction, float maxDistance) const;

        /** Starts tracking `pick`. Returns its id, or 0 if it cannot be held. */
        std::uint64_t Adopt(const WorldPick &pick);

        /** The same eligibility as Adopt; the caller must provide a live pick. */
        bool CanAdopt(const WorldPick &pick) const;

        bool SetTransform(std::uint64_t id, const SDK::Vec3 &position, const SDK::Quat &rotation, const SDK::Vec3 &scale);

        /**
         * Hides or shows an object, collision included. Entities use
         * IEntity::Hide, whose event disables physics; a layer brush's
         * collider is the layer physics', so it is held out of the world
         * (`SyncCollision`).
         */
        bool SetHidden(std::uint64_t id, bool hidden);

        /**
         * Swaps an object for another mesh, or with an empty `model` gives it
         * back. The object goes out of the world as `SetHidden` takes it, and
         * the mesh stands in its place, static and solid. False for an
         * unknown id, or a mesh that is neither a prop catalog path nor one
         * this client has streamed in.
         */
        bool SetReplacement(std::uint64_t id, const std::string &model);

        /** Whether an edit can swap an object for `model`: a prop catalog path, or a mesh streamed in. */
        static bool IsReplacementModel(const std::string &model);

        /** Puts one object back the way it was found and stops tracking it. */
        bool Restore(std::uint64_t id, bool keepIdentity = false);

        void RestoreAll();

        /** Drops every adoption without touching the world, and lets go of every collider held. */
        void Forget();

        /** Stops tracking the named objects that are neither hidden nor moved, in one pass. Returns the ones that went. */
        std::unordered_set<std::uint64_t> ReleaseUntouched(const std::unordered_set<std::uint64_t> &ids);

        /**
         * Re-finds each object and re-applies its edit: a script, a layer
         * switch or a fresh HLOD instance undoes one. Every entity each tick,
         * brushes a slice per tick. Drops entity edits that cannot come back.
         */
        void Refresh();

        /** One edit as a document carries it; false for one with nothing to carry or no identity another run would know. */
        static bool ToDefinition(const WorldEdit &edit, Shared::Game::LevelEditDefinition &entry, bool identityOnly = false);

        /** Every edit a document should carry: what is hidden or moved. */
        void ToDocument(std::vector<Shared::Game::LevelEditDefinition> &edits) const;

        /**
         * Records a document's edit and applies it now if the object is
         * loaded, or when it streams in. 0 only for an entry with no key.
         */
        std::uint64_t ApplyDocument(const Shared::Game::LevelEditDefinition &edit);

        /** Level objects within `radius`, nearest first. `truncated` says the radius was too large. */
        void Browse(const SDK::Vec3 &origin, float radius, std::size_t maxRows, std::vector<LevelRow> &rows, bool &truncated) const;

        /**
         * Every level object the editor could hold whose position lies in
         * `box`, hidden ones included: the level browser's population, read
         * for a selection tool. A box too crowded for one octree query is
         * split into eighths until each fits; `truncated` says one stayed too
         * crowded even then, so some brushes and vegetation were left out.
         */
        void Collect(const SDK::AABB &box, std::vector<LevelCandidate> &candidates, bool &truncated) const;

        /** The level object `pick` names, as Collect lists it. False for terrain, a miss, or an object Collect would not list. */
        bool Describe(const WorldPick &pick, LevelCandidate &candidate) const;

        /** Whether a listed node is still in the octree where it was. */
        bool IsNodeStanding(SDK::IRenderNode *node, const SDK::Vec3 &position) const;

        const std::vector<WorldEdit> &Edits() const {
            return _edits;
        }

        const WorldEdit *Find(std::uint64_t id) const;
        const SDK::IRenderNode *ResolveRenderNode(std::uint64_t id);
        /** Assignment-time check; resolves once, never called by the per-frame render policy. */
        bool SupportsLayerVisibility(std::uint64_t id);
        /** Resolves a live collider for ground probes that must skip the edited object. */
        SDK::IPhysicalEntity *Collider(std::uint64_t id);

        /** The edit already tracking `pick`, or 0. */
        std::uint64_t FindByPick(const WorldPick &pick) const;

        /**
         * Where the level put the object `pick` names, and how it turned and
         * sized it: an edit's originals while one of these edits has moved
         * it, the object's own pose otherwise -- the key a level edit finds
         * it again by. False for anything else: terrain, a miss, a brush or
         * swap the mod stands, an entity no GUID names.
         */
        bool LevelPose(const WorldPick &pick, SDK::Vec3 &at, SDK::Quat &rotation, SDK::Vec3 &scale) const;

        /** The level brush of `path` the level put within a key's slack of `position`, or null. */
        static SDK::IRenderNode *FindLevelBrushAt(const std::string &path, const SDK::Vec3 &position);

        /** The mesh a level object shows, as the engine spells it: a brush's or vegetation's own path, an entity's model. Empty when it has none, such as a character. */
        static std::string MeshPath(const WorldEdit &edit);

      private:
        /** Octree nodes near the ray. `avoid` drops boxes containing that point, `maxSpan` boxes larger than it, `plantsOnly` everything but vegetation. */
        WorldPick PickFromOctree(const SDK::Vec3 &origin, const SDK::Vec3 &direction, float maxDistance, const SDK::Vec3 *avoid, float maxSpan, bool plantsOnly = false) const;

        /** Every entity against its world box: the only pass that finds meshless-collider entities, and the costly one. */
        WorldPick PickFromEntities(const SDK::Vec3 &origin, const SDK::Vec3 &direction, float maxDistance, const SDK::Vec3 *avoid, float maxSpan) const;

        WorldEdit *FindMutable(std::uint64_t id);

        /**
         * Finds the edit's object again by its key and re-applies what the
         * edit asks for. False while it is not in the world.
         */
        bool Resolve(WorldEdit &edit);

        /** Writes an edit's stored transform onto the resolved object. */
        static bool ApplyTransform(const WorldEdit &edit);

        /** Hides or shows the resolved object, collision included. */
        static void ApplyHidden(const WorldEdit &edit, bool hidden);

        /**
         * Builds, moves or takes down the mesh standing in the object's
         * place, to match the edit. Waits for the object's own pose while it
         * has never been seen; keeps standing while it is streamed out.
         */
        static void SyncReplacement(WorldEdit &edit);

        /**
         * Hands a resolved layer brush's collision to the brush while the edit
         * hides or moves it, and back to the layer physics once it does
         * neither. Without it the level's own collider stays where the level
         * put it: an invisible wall, or a moved wall that is not solid.
         */
        static void SyncCollision(WorldEdit &edit);

        /** Takes the layer collider out and lets the brush physicalize itself. */
        static void HoldCollision(WorldEdit &edit);

        /** Gives the layer collider back; the brush's own goes when the last hold does. */
        static void ReleaseCollision(WorldEdit &edit);

        /** Takes bounds, and an entity's pose, off the resolved object. */
        static void ReadBack(WorldEdit &edit);

        /** Undoes an edit on the object, if it is loaded; the record stays. */
        void PutBack(WorldEdit &edit);

        /** Fills the scratch list with the nodes overlapping `box`, hidden ones included. */
        std::size_t QueryOctree(const SDK::AABB &box) const;

        /** Indexes edit `at` if the index is current; a fresh adoption keeps it current without a rebuild. */
        void IndexEdit(std::size_t at) const;

        /** Rebuilds the index after a removal shifted the edits. */
        void EnsureIndex() const;

        /** The octree half of Collect: one box, split into eighths while it is too crowded and `depth` allows. */
        void CollectNodes(const SDK::AABB &box, const SDK::AABB &outer, int depth, std::vector<LevelCandidate> &candidates, bool &truncated) const;

        std::vector<WorldEdit> _edits;
        std::uint64_t _nextId = 1;

        // Positions in `_edits`, so a selection of thousands of level objects
        // does not scan every edit per lookup. Brushes are keyed by their
        // lowercased mesh, entities by their level GUID; an entity the level
        // did not author has none and is found by a scan, as before.
        mutable std::unordered_map<std::uint64_t, std::size_t> _indexById;
        mutable std::unordered_multimap<std::string, std::size_t> _indexByMesh;
        mutable std::unordered_map<std::uint64_t, std::size_t> _indexByGuid;
        mutable std::unordered_map<SDK::EntityId, std::size_t> _indexByEntity;
        mutable bool _indexValid = false;

        /** Where the next tick's slice of brush edits starts. */
        std::size_t _refreshCursor = 0;

        /** Reused between queries so a pick does not allocate. */
        mutable std::vector<SDK::IRenderNode *> _octreeScratch;
    };
} // namespace KCDC::Features::Prop
