#pragma once

#include "siege_catalog.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace KCDC::Shared::Game {
    /**
     * One of the game's own ladders a wall can be stormed with: the climbable
     * `Ladder` entity of a battle-ladder prefab, its broken mesh, and the
     * defenders' push that knocks it down.
     *
     * Every value is mined from `Prefabs/Ladder/*.xml` by
     * `tools/generate_siege_defence_catalog.py`. The Suchdol siege ladder is
     * `battleLadder_10000`; the shorter ones are the village ladders the game
     * builds `battleLadder_350` from, with the height the battle prefab gives
     * its climb (a quarter metre under the village ladder's, so the top rests
     * on the palisade and the step off it is the game's own).
     *
     * The frame is the prefab's: the ladder stands at its foot and leans
     * `leanDeg` about its own X towards -Y, which is the wall. A ladder is
     * never scaled -- a scaled `Ladder` hands its scale to whoever climbs it.
     */
    struct SiegeLadderAsset {
        /** `siege`, `tall`, `short` or `low`, the name the scripting API uses. */
        const char *name;

        const char *model;
        const char *brokenModel;

        /** The climb, `Properties.height`: how far up the rungs go, in metres. */
        float height;

        /** How far the standing ladder leans towards the wall, in degrees. */
        float leanDeg;

        /** The tag of `GuardHlbrdPushAway_Player` whose swing fits this height; the push fragment has one per ladder. */
        const char *pushTags;

        /** Where the defender who pushes it stands, in the ladder's frame: the battle prefab's push smart object. */
        SiegePoint pushFrom;
    };

    /** One stone of a pile, in the pile's frame: `GeomEntity` placements of `Prefabs/hurlingStone/hurlingStonePile_a.xml`. */
    struct StonePileStone {
        const char *model;
        SiegePoint at;

        /** w, x, y, z, as the prefab writes it. */
        std::array<float, 4> rotation;

        float scale;
    };

    /**
     * The game's one-sided pile of hurling stones -- the battlement piles at
     * Nebakov and Suchdol -- as `hurlingStonePile_onesided_walls_left_a`
     * lays it out: the `StoneThrowingPile`, the one `StoneThrowingNode` its
     * path is, the point its `pickPoint` link names, and the nine stones of
     * the nested heap.
     *
     * Turned and moved so that the frame's origin is where the thrower stands
     * on the walk and +Y is the way they throw, over the wall. The grab
     * aligns the thrower onto the pile entity facing the path node's
     * direction to it turned a quarter right (`GetInteractionTargetRotation`,
     * 0x182EAB930), so that is the way the stone goes.
     */
    struct StonePileLayout {
        /** The pile entity: its place, its yaw in degrees, its uniform scale and its trigger mesh. */
        SiegePoint pile;
        float pileYawDeg;
        float pileScale;
        const char *pileModel;

        /** `esThrowType`: `walls` throws as soon as the stone is in the hands. */
        const char *throwType;

        SiegePoint node;
        SiegePoint pickPoint;

        std::array<StonePileStone, 9> stones;
    };

    extern const std::vector<SiegeLadderAsset> g_SiegeLadderAssets;
    extern const StonePileLayout g_StonePileLayout;

    inline constexpr std::size_t kNoSiegeLadder = static_cast<std::size_t>(-1);

    inline const SiegeLadderAsset *SiegeLadderAssetAt(std::size_t index) {
        return index < g_SiegeLadderAssets.size() ? &g_SiegeLadderAssets[index] : nullptr;
    }

    inline std::size_t FindSiegeLadderIndex(std::string_view name) {
        for (std::size_t index = 0; index < g_SiegeLadderAssets.size(); ++index) {
            if (name == g_SiegeLadderAssets[index].name) {
                return index;
            }
        }
        return kNoSiegeLadder;
    }
} // namespace KCDC::Shared::Game
