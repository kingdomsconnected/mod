#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace KCDC::Shared::Game {
    /** What the piece of architecture is, for wording and for the operator. */
    enum class GateKind : std::uint8_t {
        Drawbridge,
        Portcullis,
        Doors
    };

    inline const char *GateKindName(GateKind kind) {
        switch (kind) {
        case GateKind::Portcullis: return "portcullis";
        case GateKind::Doors: return "gate";
        default: return "drawbridge";
        }
    }

    /** How many evenly spaced points of a motion's openness the catalog carries. */
    constexpr std::size_t kGateCurveSamples = 65;

    /**
     * One animated gate the game ships.
     *
     * The game has no gate entity class -- each is a `GeomEntity` carrying a
     * `.cdf`, animated by a quest's TrackView sequence -- so nothing in the
     * running game can be asked what the clips are. They are mined out of the
     * paks by `tools/generate_gate_catalog.py`.
     *
     * Open always means passable: a portcullis raised, a drawbridge lowered.
     *
     * An empty clip name is not a mining defect. The Malesov drawbridge ships
     * only a lifting clip; the client plays the opposite direction by running
     * that one backwards, which costs nothing when playback is driven by
     * normalized time.
     *
     * The one row of kind `Doors` has no character and no clips at all: it
     * stands for every gate the level swaps between two sets of layers
     * rather than animates. Its motions take no time and its curves are a
     * straight line, so a cycle settles on the server's next tick.
     */
    struct GateAsset {
        const char *character;      // `objects/...cdf`, the asset this describes

        const char *openClip;       // ends Open, empty when the asset ships none
        const char *closeClip;      // ends Closed, empty when the asset ships none
        const char *openedIdleClip; // held while Open, empty to hold the cycle's end pose
        const char *closedIdleClip; // held while Closed, empty to hold the cycle's end pose

        /**
         * How long each clip runs, from the key range its DBA stores -- the
         * same number `IAnimationSet::GetDuration_sec` returns at runtime, so
         * the server times a cycle without a client measuring it. A direction
         * with no clip of its own takes the length of the one it runs
         * backwards. An idle with no clip is 0.
         */
        std::uint32_t openDurationMs;
        std::uint32_t closeDurationMs;
        std::uint32_t openedIdleDurationMs;
        std::uint32_t closedIdleDurationMs;

        /**
         * How open the gate is at `kGateCurveSamples` evenly spaced points of
         * each motion, 0 shut to 1 open, read out of the DBA.
         *
         * Time is not openness: the portcullis slams shut in the first 1.3s
         * of its 7s close and winches up over 12.8s of its 17s open, holding
         * still for the rest of each. So a motion reversed partway has to
         * start the other clip where the gate is, not where the clock is,
         * and this is what says where that is. A motion that runs the other
         * one's clip backwards is its mirror.
         */
        std::array<float, kGateCurveSamples> openCurve;
        std::array<float, kGateCurveSamples> closeCurve;

        GateKind kind;
    };

    /**
     * Every animated gate asset in the shipped game.
     *
     * The index into this vector is what `GateEntity::assetIndex` carries, so
     * appending is safe and reordering is a wire break.
     */
    extern const std::vector<GateAsset> g_GateAssets;

    /**
     * One precomputed collider: the record of a layer's `physics/layer<id>.dat`
     * that physicalizes a brush.
     *
     * The engine builds a layer's colliders one per record, in file order, so
     * the record index names the collider. `recordCount` is what the file
     * holds; a layer file of any other length is not the one this was mined
     * from and is left alone.
     */
    struct GateCollider {
        const char *layer; // as `IEntitySystem::GetLayerName` reports it
        std::uint32_t recordIndex;
        std::uint32_t recordCount;
    };

    /**
     * A level brush a gate displaces.
     *
     * Its collision is never built: the layers in `colliders` would build it,
     * and the client drops those records, since a gate stands in its place
     * whenever anyone is near enough to touch it. Its mesh is hidden only
     * while the gate is bound, so one gateway never shows two gates. A brush
     * has no engine identity that survives streaming, so the mesh is found
     * again by what it shows and where: `mesh` is the path as the level's
     * static object table spells it, `%level%` alias included, and the
     * position is the centre of its world bounds.
     */
    struct GateBrush {
        const char *mesh;
        float positionX;
        float positionY;
        float positionZ;
        std::vector<GateCollider> colliders;
    };

    /**
     * One placed gate, keyed the way the game's own TrackView sequences bind
     * these entities: by `EntityGUID`, not by name or class.
     *
     * `guid` is what `IEntity::GetGuid()` returns, which is NOT the level
     * file's `EntityGuid` string with the dashes removed --
     * `ParseEntityGuidString` (WHGame.dll 0x18071A868) reads `"%8x-%4hx-%4hx"`
     * and recombines them as `g0 | ((g1 | (g2 << 16)) << 32)`. The generator
     * applies that shuffle; the obvious reading matches nothing.
     */
    struct GatePlacement {
        const char *level; // level folder name, e.g. `kutnohorsko`

        /**
         * The layer this gate lives in, as `EnableLayer` takes it. Both
         * animated gates sit alone in a deferred layer, so the entity does not
         * exist until that layer is streamed in. Empty for a swapped gate.
         */
        const char *layer;

        /**
         * What IEntity::GetGuid() returns. A swapped gate has no entity, so
         * its key is its first closed layer's GUID read through the same
         * shuffle, which is as stable and as unique.
         */
        std::uint64_t guid;

        /** Where the level puts it, so the server never takes it from a client. */
        float positionX;
        float positionY;
        float positionZ;

        std::uint16_t assetIndex; // into g_GateAssets

        /**
         * The level brushes this gate displaces.
         *
         * Its static stand-in, `<asset>_opened.cgf` at the same pivot, which
         * the world shows whenever no quest has swapped the animated gate in;
         * and any NPC-only collision `layer` brings in with the gate. The
         * Suchdol portcullis layer carries a `collision_proxy_npc_only` wall
         * on the bars line, which its battle quest pens NPCs in with, and
         * horses are NPCs: while it stands no rider gets through the open
         * gate, though people on foot do.
         */
        std::vector<GateBrush> displacedBrushes;

        /**
         * For a gate the level swaps rather than animates, the brush layers
         * that draw it open and drawn shut. Empty for an animated gate.
         *
         * The Ratborsch and Nebakov fortress gates and the Ruthard palace
         * gate are static meshes. Each state is a layer of its own -- the
         * quests switch them through GameProfiles such as
         * `ratborFortress_gate_open` and `ratborFortress_gate_closed` -- and
         * each layer holds brushes and their colliders only, so switching one
         * changes what is drawn and what collides, and nothing else.
         */
        std::vector<const char *> openLayers;
        std::vector<const char *> closedLayers;

        /**
         * Brush layers kept out of the world for as long as a swapped gate is:
         * the same gateway barricaded, damaged or broken for a battle, and
         * quest barriers on the gate line. Any of them would stand in a
         * gateway drawn open, or draw a second gate over the first.
         */
        std::vector<const char *> clearedLayers;

        /** Whether the level swaps this gate between layers rather than animating it. */
        bool IsSwapped() const {
            return !openLayers.empty();
        }
    };

    /** Every gate placement across every shipped level. */
    extern const std::vector<GatePlacement> g_GatePlacements;

    /** The placement for one level GUID, or null when it is not a gate. */
    inline const GatePlacement *FindGatePlacement(const char *level, std::uint64_t guid) {
        for (const GatePlacement &placement : g_GatePlacements) {
            if (placement.guid == guid && std::strcmp(placement.level, level) == 0) {
                return &placement;
            }
        }
        return nullptr;
    }

    /** Whether any displaced brush's collider is built by `layer`; the cheap test before a record-by-record one. */
    inline bool IsGateLayerWithDisplacedColliders(const char *layer) {
        for (const GatePlacement &placement : g_GatePlacements) {
            for (const GateBrush &brush : placement.displacedBrushes) {
                for (const GateCollider &collider : brush.colliders) {
                    if (std::strcmp(collider.layer, layer) == 0) {
                        return true;
                    }
                }
            }
        }
        return false;
    }

    /** Whether record `recordIndex` of a `recordCount`-record `layer` file is a displaced brush's collider. */
    inline bool IsGateDisplacedCollider(const char *layer, std::uint32_t recordIndex, std::uint32_t recordCount) {
        for (const GatePlacement &placement : g_GatePlacements) {
            for (const GateBrush &brush : placement.displacedBrushes) {
                for (const GateCollider &collider : brush.colliders) {
                    if (collider.recordIndex == recordIndex && collider.recordCount == recordCount && std::strcmp(collider.layer, layer) == 0) {
                        return true;
                    }
                }
            }
        }
        return false;
    }

    /** A quest layer a session keeps out of the world, wherever its level is loaded. */
    struct ClearedLevelLayer {
        const char *level; // level folder name, as GatePlacement::level
        const char *layer; // as `EnableLayer` takes it
    };

    /**
     * Quest props that stand in for something a session never gets to see
     * finished. The story hides each through its quest; nothing in a session
     * does, so they are switched off like a gate's cleared layers.
     *
     * Malesov fortress, Ptacek's room: M35 (zachranaPtacka) puts a
     * `Lockpickable` there with a door's model and a quest item for a key.
     * The class has no open, only a Hide the quest fires; every phase before
     * that, the default world included, has the layer on.
     */
    inline constexpr ClearedLevelLayer kClearedLevelLayers[] = {
        {"kutnohorsko", "_common_d959d196-66c3-4e10-94e4-51131e44d942"},
    };

    /** Whether `layer` is one of `level`'s cleared quest layers: an entity layer, unlike a gate's brush layers. */
    inline bool IsClearedLevelLayer(const char *level, const char *layer) {
        for (const ClearedLevelLayer &cleared : kClearedLevelLayers) {
            if (std::strcmp(cleared.level, level) == 0 && std::strcmp(cleared.layer, layer) == 0) {
                return true;
            }
        }
        return false;
    }
} // namespace KCDC::Shared::Game
