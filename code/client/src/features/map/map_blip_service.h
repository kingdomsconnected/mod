#pragma once

#include "sdk/types.h"
#include "sdk/wh/guimodule/c_compass_mark.h"
#include "sdk/wh/guimodule/c_ui_map.h"
#include "sdk/wh/guimodule/e_compass_mark_type.h"

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace KCDC::Features::Map {
    /**
     * What a blip should look like, and where. The compass has no text, so
     * the label only ever shows on the map screen, as the row's name.
     */
    struct BlipDescription {
        SDK::wh::guimodule::E_CompassMarkType type = SDK::wh::guimodule::E_CompassMarkType::GeneralPoi;
        SDK::Vec3 position {};
        std::int32_t state = 0;
        std::string label;

        bool compass = true; // a mark on the HUD compass
        bool map     = true; // a row on the map screen
        bool legend  = true; // hidden with the map legend's switch for its icon

        /**
         * Set for a blip a replicated server entity owns. The server decides
         * everything about it, so the client scripting layer neither lists it
         * nor lets a handle change it.
         */
        bool fromServer = false;
    };

    struct BlipRow {
        std::uint32_t id = 0;
        BlipDescription description;
        bool live = false; // the compass currently carries a mark for it
    };

    /**
     * The same blip spelled in plain types: the icon by the game's own name
     * for it, the position in metres. It exists so the scripting binding can
     * read and write blips without naming an engine type, which is what
     * separates a binding that translates from one that reaches past its
     * service.
     */
    struct BlipView {
        std::uint32_t id = 0;
        glm::vec3 position {};
        std::string type; // one of the game's own compass icon names
        std::int32_t state = 0;
        std::string label;
        bool compass    = true;
        bool map        = true;
        bool legend     = true;
        bool fromServer = false;
        bool live       = false;
    };

    enum class BlipResult : std::uint8_t {
        None,
        Applied,
        NoCompass,
        AllocationFailed,
        LimitReached,
    };

    const char *BlipResultToString(BlipResult result);

    /**
     * The mod's own compass and map blips.
     *
     * Two halves. The registry -- what the mod wants to exist -- is plain data
     * under a lock that any thread writes. The native side is game-thread
     * only, and Update() reconciles it to the registry.
     *
     * On the compass each blip is a native mark: created when missing, moved
     * and re-stated in place, rebuilt when its icon changes, and put back
     * whenever the game's own `RemoveAllMarks` drops it across a lifecycle
     * edge.
     *
     * On the map screen each blip is a flash row, built by the game's own
     * `MakeFlashMapPoiMarker` and pushed to the map movie the way the game
     * pushes its own -- never through the map's mark list, which only takes
     * marks backed by a registered game object. A row is only ever added or
     * removed, so any change replaces it, and the screen forgets every row
     * when it closes: `OnMapScreenRebuilt` puts them all back on each open,
     * right after the game's own.
     *
     * Nothing here reaches the network. Client scripts and replicated server
     * entities both write this registry, and the reconcile is the same for
     * either.
     */
    class MapBlipService final {
      public:
        /** A ceiling so a bad script or server cannot flood the HUD. Shared by both owners. */
        static constexpr std::size_t kMaxBlips = 128;

        /**
         * Where the map screen's row ids for blips start. The game draws its
         * own mark ids from a counter that starts at one (0x1803A480C), so
         * rows numbered from here can never collide with one of its rows.
         */
        static constexpr std::int32_t kMapRowIdBase = 0x7F000000;

        MapBlipService()                                  = default;
        MapBlipService(const MapBlipService &)            = delete;
        MapBlipService &operator=(const MapBlipService &) = delete;

        // --- Registry. Callable from any thread. -------------------------

        /**
         * Zero when the registry is full; otherwise the new blip's id.
         *
         * That id is the blip's identity as far as the mod is concerned, and is
         * deliberately a plain number rather than a `C_CompassMark *`: the native
         * object is replaced whenever the icon changes and dropped whenever the
         * game resets the compass.
         */
        std::uint32_t Create(const BlipDescription &description);

        bool SetPosition(std::uint32_t id, const SDK::Vec3 &position);
        bool SetState(std::uint32_t id, std::int32_t state);
        bool SetType(std::uint32_t id, SDK::wh::guimodule::E_CompassMarkType type);
        bool SetLabel(std::uint32_t id, const std::string &label);

        /** Replaces everything about a blip at once, its owner included. */
        bool SetDescription(std::uint32_t id, const BlipDescription &description);

        bool Remove(std::uint32_t id);
        void RemoveAll();

        /** Every blip a client script made, leaving the server's alone. */
        void RemoveAllLocal();

        bool Find(std::uint32_t id, BlipDescription &description) const;

        /**
         * Answers the localization key a map row is named with, from the
         * blip's current label. The map screen does not show a row's name
         * as text: it turns every word into an '@' key and resolves each one,
         * so a row carries one key and the text comes from here. False for a
         * label that is not one of these keys.
         */
        bool ResolveMapLabel(const char *label, SDK::CryStringT<char> &out) const;
        std::vector<BlipRow> Snapshot() const;
        std::size_t GetCount() const;

        // --- The same registry in plain types, for the binding -----------

        /** Whether the game knows an icon by this name. A caller that wants to refuse an unknown one asks first; Create only reports that it made nothing. */
        static bool IsKnownType(const std::string &typeName);

        /** Zero when the registry is full or `type` names an icon the game does not know. An empty `type` takes the default. */
        std::uint32_t Create(const BlipView &blip);

        bool Describe(std::uint32_t id, BlipView &view) const;
        std::vector<BlipView> List() const;

        bool SetPosition(std::uint32_t id, const glm::vec3 &position);
        bool SetType(std::uint32_t id, const std::string &typeName);

        BlipResult GetLastResult() const;
        std::string GetLastMessage() const;

        /**
         * Marks on the compass at the last tick, the game's own included.
         * Sampled rather than read live: the vector it comes from is
         * reallocated by the thread that adds to it.
         */
        std::size_t GetCompassMarkCount() const;

        // --- Native side. Game thread only. ------------------------------

        void Update();

        /**
         * The map screen has just rebuilt its POI rows from the game's own
         * list, which drops every row it carried before. Called from the
         * detour on `C_UIMap::RefreshPoiMarkers`, once per open, with the
         * movie loaded.
         */
        void OnMapScreenRebuilt();

        /**
         * Takes every mark the mod owns back off the compass and releases it.
         * The registry is left alone, so a later Update() rebuilds the lot;
         * pair it with RemoveAll() to forget them for good.
         */
        void Shutdown();

      private:
        struct DesiredBlip {
            std::uint32_t id = 0;
            BlipDescription description;
            bool live = false; // published by the game thread, read by the overlay
        };

        /** Game thread only; owns a native reference. */
        struct LiveBlip {
            std::uint32_t id = 0;
            SDK::wh::guimodule::CompassMarkRef mark;
            BlipDescription applied;
        };

        /** A row the open map screen is showing for a blip. Game thread only. */
        struct MapRow {
            std::uint32_t id = 0;
            BlipDescription applied;
        };

        /** Edits one registry entry and asks for a reconcile. */
        template <typename Fn>
        bool Mutate(std::uint32_t id, Fn &&apply) {
            std::scoped_lock lock(_mutex);
            for (auto &blip : _desired) {
                if (blip.id != id) {
                    continue;
                }
                apply(blip.description);
                ++_revision;
                return true;
            }
            return false;
        }

        void Reconcile(const std::vector<DesiredBlip> &desired);

        /** Brings the open map screen's rows in line with `desired`; forgets them all while it is closed. */
        void ReconcileMapScreen(const std::vector<DesiredBlip> &desired);
        void PushMapRows(SDK::wh::guimodule::C_UIMap &map, const std::vector<const DesiredBlip *> &blips);
        void RemoveMapRows(SDK::wh::guimodule::C_UIMap &map, const std::vector<std::uint32_t> &ids);

        bool Attach(LiveBlip &live, const BlipDescription &description);
        void Detach(LiveBlip &live);
        void PublishLiveState();
        void SetResult(BlipResult result, std::string message);

        mutable std::mutex _mutex;
        std::vector<DesiredBlip> _desired;

        // Bumped by every registry write the reconcile has to act on, so a
        // tick that changed nothing can skip it.
        std::uint64_t _revision = 0;
        std::uint32_t _nextId   = 1;

        BlipResult _lastResult = BlipResult::None;
        std::string _lastMessage;
        std::size_t _compassMarks = 0;

        // Game thread only.
        std::vector<LiveBlip> _live;
        std::vector<MapRow> _mapRows;
        std::uint64_t _appliedRevision = 0;
        bool _forceReconcile           = true;
    };
} // namespace KCDC::Features::Map
