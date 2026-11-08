#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace KCDC::Shared::Game {
    /** The siege engines the game ships a model for. It has no catapult and no ballista. */
    enum class SiegeEngineKind : std::uint8_t {
        Trebuchet,
        Cannon
    };

    /** One point in an engine's model space, in metres. */
    struct SiegePoint {
        float x;
        float y;
        float z;
    };

    /** One place a crewman stands to work an engine: a trebuchet's winch. */
    struct SiegeCrewPlace {
        /** Where he stands, in the engine's frame. */
        SiegePoint at;

        /** Which way he faces, in degrees off the engine's own forward. */
        float yawDeg;

        /** The tags of `crewFragment` he plays while he works it; empty for no place. */
        const char *tags;
    };

    /**
     * One siege engine the game ships, and the facts a shot is built from.
     *
     * KCD2 has no siege gameplay: the trebuchet is a skinned character only
     * cutscenes animate, its stones are separate objects a TrackView sequence
     * flies, and the cannon is a static mesh. So nothing in the running game
     * can be asked any of this. `tools/generate_siege_catalog.py` mines it out
     * of the paks and declares the few values that have no source there.
     *
     * Forward is +Y, the engine's own forward: the trebuchet's cutscene
     * stone flies out along it. A mesh that faces another way is turned by
     * `modelYawDeg` when it is placed, so every offset here is in the
     * engine's frame, not the mesh's.
     */
    struct SiegeEngineAsset {
        SiegeEngineKind kind;

        /** `trebuchet` or `cannon`, the name the scripting API uses. */
        const char *name;

        /** The `.chr` or `.cgf` the engine is built from. */
        const char *model;

        /** How far the mesh is turned about Z from the engine's forward, in degrees: 180 for a cannon whose barrel lies on the mesh's -Y. */
        float modelYawDeg;

        /**
         * The clips for idle, drawing, loading and firing, in that order. All
         * empty for a static engine, which only times its cycle.
         */
        std::array<const char *, 4> clips;

        /**
         * How long each step of the cycle takes at normal speed, in the same
         * order: the clips' key ranges for the trebuchet, declared lengths for
         * the cannon. A step that takes 0 is skipped.
         */
        std::array<std::uint32_t, 4> durationsMs;

        /** How far into the fire step the projectile is let go. */
        std::uint32_t releaseMs;

        /** Where it is let go from, in the engine's frame. */
        SiegePoint releaseOffset;

        /**
         * The fixed elevation a shot leaves at, in degrees above forward, with
         * its speed solved for the target; 0 when the speed is fixed instead.
         */
        float launchElevationDeg;

        /** The fixed launch speed, with the elevation solved for the target -- about `pivot` -- 0 when the elevation is fixed instead. */
        float muzzleSpeed;

        /** The closest and the furthest horizontal distance a shot may be aimed at. */
        float minRange;
        float maxRange;

        /** How far it throws when fired without a target: a player's native prompt fires along their facing this far. */
        float defaultRange;

        /** How far into the load step the crew lays the projectile in the sling; it shows from then on. */
        std::uint32_t projectileLaidMs;

        /**
         * Where the projectile is in model space from the start of the fire
         * step until its release, a point every 30th of a second. Empty for an
         * engine that holds it out of sight.
         */
        std::vector<SiegePoint> sling;

        const char *projectileModel;
        float projectileRadius;

        /** ATL triggers and vfx catalog names; empty for none. */
        const char *releaseSound;
        const char *impactSound;
        const char *releaseEffect;
        const char *impactEffect;

        /**
         * Metres each sound carries, 0 for the trigger's own setting. The
         * client culls the trigger past it and fades it out toward it.
         */
        float releaseSoundRange;
        float impactSoundRange;

        /** Defaults a script can change per engine: health taken at the point of impact, and how far from it anything is hurt. */
        float damage;
        float damageRadius;

        /**
         * What a cannon's barrel is raised about, in the engine's frame: the
         * back end of its bed, which stays on the ground. The release point
         * turns about it with the elevation. Unused by an engine that throws
         * at a fixed angle.
         */
        SiegePoint pivot;

        /** The mesh a wrecked engine is shown as; empty when the game has none and the wreck leans and sinks instead. */
        const char *brokenModel;

        /** How far a wreck without a broken mesh leans about its own X and Y, in degrees, and sinks into the ground, in metres. */
        float wreckTiltXDeg;
        float wreckTiltYDeg;
        float wreckSink;

        /** How far a shot kicks the engine back along its axis, in metres, before it eases home; 0 for none. */
        float recoil;

        /** Where the fuse burns as a shot starts, in the engine's frame, and its spark and fizz; empty for an engine with no fuse. */
        SiegePoint touchHole;
        const char *fuseEffect;
        const char *fuseSound;

        /** Played where a projectile flies past the listener; empty for none. */
        const char *whizSound;

        /** The dust and debris an engine throws up as it is wrecked, or as a stone strikes it. */
        const char *wreckEffect;
        const char *wreckSound;

        /** Metres the fuse, the fly-by and the wreck carry, culled and faded like the release and impact. */
        float fuseSoundRange;
        float whizSoundRange;
        float wreckSoundRange;

        /** The local camera's shake at an impact and at the release: its strength at the point, and the distance it fades out over. 0 for none. */
        float impactShake;
        float impactShakeRange;
        float releaseShake;
        float releaseShakeRange;

        /** The crew places, an empty `tags` for none. */
        std::array<SiegeCrewPlace, 2> crew;

        /** The Mannequin fragment the crew plays; empty for none. */
        const char *crewFragment;
    };

    /** The steps of an engine's cycle, indexing `clips` and `durationsMs`. */
    enum class SiegeStep : std::uint8_t {
        Idle,
        Draw,
        Load,
        Fire
    };

    extern const std::vector<SiegeEngineAsset> g_SiegeEngineAssets;

    inline const SiegeEngineAsset *SiegeEngineAssetAt(std::size_t index) {
        return index < g_SiegeEngineAssets.size() ? &g_SiegeEngineAssets[index] : nullptr;
    }

    inline constexpr std::size_t kNoSiegeEngine = static_cast<std::size_t>(-1);

    inline std::size_t FindSiegeEngineIndex(std::string_view name) {
        for (std::size_t index = 0; index < g_SiegeEngineAssets.size(); ++index) {
            if (name == g_SiegeEngineAssets[index].name) {
                return index;
            }
        }
        return kNoSiegeEngine;
    }
} // namespace KCDC::Shared::Game
