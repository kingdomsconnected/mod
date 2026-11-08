#pragma once

#include "core/feature_registry.h"

#include "sdk/types.h"
#include "sdk/wh/rpgmodule/c_rpg_location.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <optional>
#include <string>

namespace KCDC::Features::Map {
    /**
     * The map screen and the compass strip: what they tell the rest of the
     * mod, and what the mod tells them. The marks on them are
     * MapBlipService's. Game thread only.
     *
     * None of the edges below goes out on the wire: a marker is one player's
     * own note to himself and the screen coming up is nobody else's business,
     * so what the map raises ends at the client scripting layer and a
     * resource decides from there whether it is worth sharing.
     *
     * Every verb reports whether the native side was there to take it: before
     * the GUI module has built its pages it is not.
     *
     * A session participant for `Reset()` alone. The compass suppression it
     * holds is the one thing here that outlives a call, so the service is
     * registered in `core/feature_registry.cpp` to have that swept; it has no
     * `Update()` because nothing it does is per-frame work.
     */
    class MapService final: public KCDC::Core::IFeatureService {
      public:
        /** The local player's marker on one map, in world-space metres. */
        struct Waypoint {
            glm::vec3 position {};
            std::int32_t mapId = 0;
        };

        MapService()                              = default;
        MapService(const MapService &)            = delete;
        MapService &operator=(const MapService &) = delete;

        // --- Local engine edges, raised by this feature's own hooks --------

        // Reports the local map waypoint in world metres, with terrain
        // height. mapId is required because levels reuse coordinate ranges.
        void NotifyMarkerPlaced(const SDK::Vec3 &position, std::int32_t mapId, bool moved);

        /** The local player took their marker back off `mapId`. */
        void NotifyMarkerCleared(const SDK::Vec3 &position, std::int32_t mapId);

        /** The map screen came up or went away; the detour gives the edge. */
        void NotifyScreenChanged(bool open);

        // --- The screen and the strip as things to drive ------------------

        bool IsMapOpen() const;

        /** The mod's own view: a suppression the game pushed is invisible here. */
        bool IsCompassVisible() const {
            return !_compassBlocked;
        }

        bool SetCompassVisible(bool visible);

        /**
         * The marker the player has dropped on the map they are looking at.
         * The game keeps at most one per map.
         */
        bool GetWaypoint(Waypoint &waypoint) const;

        /**
         * Moves the marker already down. It cannot create one, so it reads
         * false when there is nothing on the map to move.
         */
        bool MoveWaypoint(const glm::vec3 &position);

        /** Runs the game's own removal, so the marker-cleared edge is raised as it would be for the player. */
        bool ClearWaypoint();

        // --- Mark categories, by the game's own name for the icon ---------

        /** Whether the game knows a compass mark type by this name. A caller that wants to refuse an unknown one asks first; the two below only report that nothing happened. */
        static bool IsKnownMarkType(const std::string &typeName);

        bool IsMarkTypeVisible(const std::string &typeName) const;
        bool SetMarkTypeVisible(const std::string &typeName, bool visible);

        // --- What the player has found, put back ---------------------------

        /**
         * Marks the point of interest or location this id names discovered,
         * silently -- no toast, trigger or XP. The four fields are the game's
         * 16-byte location id. False when it names neither, or before the RPG
         * module is up.
         */
        bool Discover(std::uint32_t data1, std::uint16_t data2, std::uint16_t data3, const std::uint8_t (&data4)[8]) const;

        /** Clears the fog of war around a world point by the game's own reveal radius, as walking there does. False before the player module is up. */
        bool RevealFog(const glm::vec3 &position) const;

        /**
         * Clears the fog of war over the whole level, as the game's own
         * `player_revealFow` does. False before the player module is up.
         *
         * The fog is also what hides the names of places the player has not
         * found: the map writes every label and lets the fog cover them. So
         * until the next level load a label shows only once its location is
         * known (IsLabelShown).
         */
        bool RevealAllFog();

        /** Whether the map may write the name of this location: always, unless the whole fog was cleared and the place is still undiscovered. */
        bool IsLabelShown(SDK::wh::rpgmodule::I_RPGLocationDiscovery *location) const;

        // --- Fast travel, which begins from this screen -------------------

        /** Whether a fast travel is running on this machine right now. */
        bool IsTraveling() const;

        /** How far along its route the travel has got, in metres. Zero while none is running. */
        glm::vec3 GetTravelPosition() const;

        /** The game's own answer to whether a travel could begin now -- 0 means yes -- or nothing before the player module is up. Starts nothing. */
        std::optional<std::uint8_t> ValidateTravelStart() const;

        /** Gives the compass back. The suppression is not reference-counted, so a leftover would outlive the session. */
        void Reset() override;

        /** A new level comes with its fog back. */
        void NativeContextChanged() override;

      private:
        bool _compassBlocked = false;
        bool _allFogRevealed = false;
    };
} // namespace KCDC::Features::Map
