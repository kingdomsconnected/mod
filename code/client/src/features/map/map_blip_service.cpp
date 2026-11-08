#include "map_blip_service.h"

#include "sdk/cryengine/scaleform/s_ui_arguments.h"
#include "sdk/wh/guimodule/c_ui_compass.h"
#include "sdk/wh/guimodule/c_ui_flash_map_poi_marker.h"
#include "sdk/wh/guimodule/s_entity_map_mark.h"

#include "common/vector_math.h"

#include "shared/game/blip_icon_catalog.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>

namespace KCDC::Features::Map {
    namespace {
        bool SamePosition(const SDK::Vec3 &lhs, const SDK::Vec3 &rhs) {
            return lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z;
        }

        BlipView ToView(std::uint32_t id, const BlipDescription &description, bool live) {
            BlipView view;
            view.id          = id;
            view.position    = glm::vec3 {description.position.x, description.position.y, description.position.z};
            const char *icon = SDK::wh::guimodule::CompassMarkTypeToIconName(description.type);
            view.type        = icon != nullptr ? std::string(icon) : std::string();
            view.state       = description.state;
            view.label       = description.label;
            view.compass     = description.compass;
            view.map         = description.map;
            view.legend      = description.legend;
            view.fromServer  = description.fromServer;
            view.live        = live;
            return view;
        }

        /**
         * What a map row is built from in place of the game object a real map
         * mark follows. `MakeFlashMapPoiMarker` makes exactly one call on its
         * source -- slot 2, `C_LinkableObject::GetWorldPosition`, for a
         * `const Vec3 *` -- and this answers that and nothing else. It only
         * has to live through the call: the row copies x and y out.
         */
        class MapRowSource final {
          public:
            explicit MapRowSource(const SDK::Vec3 &position): _position(position) {}

            virtual ~MapRowSource() = default; // slot 0
            virtual void Unused() {}           // slot 1

            virtual const SDK::Vec3 *GetWorldPosition() const { // slot 2
                return &_position;
            }

          private:
            SDK::Vec3 _position;
        };

        /** The key a map row is named with; the leading '@' is added by the screen itself. */
        constexpr char kMapLabelPrefix[]         = "kcdc_blip_";
        constexpr std::size_t kMapLabelPrefixLen = sizeof(kMapLabelPrefix) - 1;

        std::int32_t MapRowId(std::uint32_t blipId) {
            return MapBlipService::kMapRowIdBase + static_cast<std::int32_t>(blipId);
        }

        /**
         * Whether a blip belongs on the open map, the legend's own switch for
         * its icon included unless the blip opts out of it. The map loads each row's art from
         * `Libs/UI/Textures/Icons/Map/` and draws a broken-image placeholder
         * for a texture that is not shipped, so an icon it has no art for --
         * the quest icons, the checkpoint -- stays on the compass alone.
         */
        bool ShowsOnMap(const SDK::wh::guimodule::C_UIMap &map, const BlipDescription &description) {
            const KCDC::Shared::Game::BlipIcon *icon = KCDC::Shared::Game::BlipIconOf(static_cast<std::uint8_t>(description.type));
            return description.map && icon != nullptr && icon->onMap && KCDC::Common::IsFinite(description.position) && (!description.legend || map.IsMarkTypeVisible(description.type));
        }

        /** The state a map row is drawn with: the undiscovered art only exists for some icons, and a missing one is a placeholder. */
        std::int32_t MapRowState(const BlipDescription &description) {
            const KCDC::Shared::Game::BlipIcon *icon = KCDC::Shared::Game::BlipIconOf(static_cast<std::uint8_t>(description.type));
            const bool undiscovered                  = description.state == KCDC::Shared::Game::kMapUndiscoveredState;
            return undiscovered && (icon == nullptr || !icon->undiscoveredOnMap) ? 0 : description.state;
        }

        /** Whether a row already on the screen still draws `description`. Height is not on the map. */
        bool SameRow(const BlipDescription &applied, const BlipDescription &description) {
            return applied.type == description.type && applied.state == description.state && applied.label == description.label && applied.position.x == description.position.x && applied.position.y == description.position.y;
        }
    } // namespace

    const char *BlipResultToString(BlipResult result) {
        switch (result) {
        case BlipResult::None: return "nothing yet";
        case BlipResult::Applied: return "applied";
        case BlipResult::NoCompass: return "the compass is not up yet";
        case BlipResult::AllocationFailed: return "the game refused to build the mark";
        case BlipResult::LimitReached: return "the blip limit is reached";
        }
        return "unknown";
    }

    std::uint32_t MapBlipService::Create(const BlipDescription &description) {
        std::scoped_lock lock(_mutex);

        if (_desired.size() >= kMaxBlips) {
            _lastResult  = BlipResult::LimitReached;
            _lastMessage = BlipResultToString(BlipResult::LimitReached);
            return 0;
        }

        DesiredBlip blip;
        blip.id          = _nextId++;
        blip.description = description;
        _desired.push_back(std::move(blip));
        ++_revision;

        return _desired.back().id;
    }

    bool MapBlipService::SetPosition(std::uint32_t id, const SDK::Vec3 &position) {
        return Mutate(id, [&position](BlipDescription &description) { description.position = position; });
    }

    bool MapBlipService::SetState(std::uint32_t id, std::int32_t state) {
        return Mutate(id, [state](BlipDescription &description) { description.state = state; });
    }

    bool MapBlipService::SetType(std::uint32_t id, SDK::wh::guimodule::E_CompassMarkType type) {
        return Mutate(id, [type](BlipDescription &description) { description.type = type; });
    }

    bool MapBlipService::SetLabel(std::uint32_t id, const std::string &label) {
        return Mutate(id, [&label](BlipDescription &description) { description.label = label; });
    }

    bool MapBlipService::SetDescription(std::uint32_t id, const BlipDescription &replacement) {
        return Mutate(id, [&replacement](BlipDescription &description) { description = replacement; });
    }

    bool MapBlipService::Remove(std::uint32_t id) {
        std::scoped_lock lock(_mutex);
        const auto found = std::find_if(_desired.begin(), _desired.end(), [id](const DesiredBlip &blip) { return blip.id == id; });
        if (found == _desired.end()) {
            return false;
        }

        _desired.erase(found);
        ++_revision;
        return true;
    }

    void MapBlipService::RemoveAll() {
        std::scoped_lock lock(_mutex);
        if (_desired.empty()) {
            return;
        }

        _desired.clear();
        ++_revision;
    }

    void MapBlipService::RemoveAllLocal() {
        std::scoped_lock lock(_mutex);
        const auto erased = std::erase_if(_desired, [](const DesiredBlip &blip) { return !blip.description.fromServer; });
        if (erased > 0) {
            ++_revision;
        }
    }

    bool MapBlipService::IsKnownType(const std::string &typeName) {
        SDK::wh::guimodule::E_CompassMarkType parsed = SDK::wh::guimodule::E_CompassMarkType::GeneralPoi;
        return SDK::wh::guimodule::CompassMarkTypeFromIconName(typeName.c_str(), parsed);
    }

    std::uint32_t MapBlipService::Create(const BlipView &blip) {
        BlipDescription description;
        if (!blip.type.empty() && !SDK::wh::guimodule::CompassMarkTypeFromIconName(blip.type.c_str(), description.type)) {
            return 0;
        }

        description.position = SDK::Vec3 {blip.position.x, blip.position.y, blip.position.z};
        description.state    = blip.state;
        description.label    = blip.label;
        description.compass  = blip.compass;
        description.map      = blip.map;
        description.legend   = blip.legend;
        return Create(description);
    }

    bool MapBlipService::Describe(std::uint32_t id, BlipView &view) const {
        std::scoped_lock lock(_mutex);
        for (const auto &blip : _desired) {
            if (blip.id == id) {
                view = ToView(blip.id, blip.description, blip.live);
                return true;
            }
        }
        return false;
    }

    std::vector<BlipView> MapBlipService::List() const {
        std::scoped_lock lock(_mutex);

        std::vector<BlipView> views;
        views.reserve(_desired.size());
        for (const auto &blip : _desired) {
            views.push_back(ToView(blip.id, blip.description, blip.live));
        }
        return views;
    }

    bool MapBlipService::SetPosition(std::uint32_t id, const glm::vec3 &position) {
        return SetPosition(id, SDK::Vec3 {position.x, position.y, position.z});
    }

    bool MapBlipService::SetType(std::uint32_t id, const std::string &typeName) {
        SDK::wh::guimodule::E_CompassMarkType parsed = SDK::wh::guimodule::E_CompassMarkType::GeneralPoi;
        if (!SDK::wh::guimodule::CompassMarkTypeFromIconName(typeName.c_str(), parsed)) {
            return false;
        }
        return SetType(id, parsed);
    }

    bool MapBlipService::Find(std::uint32_t id, BlipDescription &description) const {
        std::scoped_lock lock(_mutex);
        for (const auto &blip : _desired) {
            if (blip.id == id) {
                description = blip.description;
                return true;
            }
        }
        return false;
    }

    bool MapBlipService::ResolveMapLabel(const char *label, SDK::CryStringT<char> &out) const {
        if (label == nullptr) {
            return false;
        }
        if (*label == '@') {
            ++label;
        }
        if (std::strncmp(label, kMapLabelPrefix, kMapLabelPrefixLen) != 0) {
            return false;
        }

        std::uint32_t id   = 0;
        const char *digits = label + kMapLabelPrefixLen;
        if (*digits == '\0') {
            return false;
        }
        for (const char *character = digits; *character != '\0'; ++character) {
            if (*character < '0' || *character > '9') {
                return false;
            }
            id = id * 10U + static_cast<std::uint32_t>(*character - '0');
        }

        // A blip that has gone while the screen still shows its row: a blank
        // name beats showing the key.
        BlipDescription description;
        out.Assign(Find(id, description) ? description.label.c_str() : "");
        return true;
    }

    std::vector<BlipRow> MapBlipService::Snapshot() const {
        std::scoped_lock lock(_mutex);

        std::vector<BlipRow> rows;
        rows.reserve(_desired.size());
        for (const auto &blip : _desired) {
            rows.push_back(BlipRow {blip.id, blip.description, blip.live});
        }
        return rows;
    }

    std::size_t MapBlipService::GetCount() const {
        std::scoped_lock lock(_mutex);
        return _desired.size();
    }

    BlipResult MapBlipService::GetLastResult() const {
        std::scoped_lock lock(_mutex);
        return _lastResult;
    }

    std::string MapBlipService::GetLastMessage() const {
        std::scoped_lock lock(_mutex);
        return _lastMessage;
    }

    std::size_t MapBlipService::GetCompassMarkCount() const {
        std::scoped_lock lock(_mutex);
        return _compassMarks;
    }

    void MapBlipService::SetResult(BlipResult result, std::string message) {
        std::scoped_lock lock(_mutex);
        _lastResult  = result;
        _lastMessage = std::move(message);
    }

    void MapBlipService::Update() {
        // The open map screen is reconciled every tick rather than on a
        // registry change alone: its legend can hide or show an icon under us.
        const SDK::wh::guimodule::C_UIMap *map = SDK::wh::guimodule::C_UIMap::GetInstance();
        const bool mapOpen                     = map != nullptr && map->IsOpen();

        std::vector<DesiredBlip> desired;
        std::uint64_t revision = 0;
        bool dirty             = false;
        {
            std::scoped_lock lock(_mutex);
            revision = _revision;
            dirty    = _forceReconcile || revision != _appliedRevision;
            if (dirty || mapOpen) {
                desired = _desired;
            }
        }

        ReconcileMapScreen(desired);

        if (dirty) {
            Reconcile(desired);
            _appliedRevision = revision;
            _forceReconcile  = false;
        }
        else if (auto *compass = _live.empty() ? nullptr : SDK::wh::guimodule::C_UICompass::GetInstance()) {
            // Nothing the mod asked for changed, but the game may still have
            // emptied the compass under us.
            for (auto &live : _live) {
                if (auto *mark = live.mark.Get(); mark != nullptr && !mark->IsRegistered()) {
                    compass->AddMark(live.mark.Clone());
                }
            }
        }

        // Every tick: the count also covers the game's own marks, and those
        // come and go with the quest log.
        PublishLiveState();
    }

    void MapBlipService::Reconcile(const std::vector<DesiredBlip> &desired) {
        auto *compass = SDK::wh::guimodule::C_UICompass::GetInstance();
        if (compass == nullptr) {
            // Everything stays wanted; the next tick with a compass builds it.
            _forceReconcile = true;
            SetResult(BlipResult::NoCompass, BlipResultToString(BlipResult::NoCompass));
            return;
        }

        // Drop what is no longer wanted first, so a create that follows a
        // remove in the same tick cannot trip the limit. An entry left empty
        // by a failed rebuild is collected here too.
        for (auto live = _live.begin(); live != _live.end();) {
            const bool wanted = std::any_of(desired.begin(), desired.end(), [&live](const DesiredBlip &blip) { return blip.id == live->id && blip.description.compass; });
            if (wanted && live->mark) {
                ++live;
                continue;
            }

            Detach(*live);
            live = _live.erase(live);
        }

        for (const auto &blip : desired) {
            if (!blip.description.compass) {
                continue;
            }

            const auto live = std::find_if(_live.begin(), _live.end(), [&blip](const LiveBlip &existing) { return existing.id == blip.id; });

            if (live == _live.end()) {
                LiveBlip created;
                created.id = blip.id;
                if (Attach(created, blip.description)) {
                    _live.push_back(std::move(created));
                }
                continue;
            }

            // Flash is told the icon name once, at registration, and there is
            // no ChangeCompassMarkerType event. So a new icon is a new mark.
            if (blip.description.type != live->applied.type) {
                Detach(*live);
                Attach(*live, blip.description);
                continue;
            }

            auto *mark = live->mark.Get();
            if (!mark->IsRegistered()) {
                compass->AddMark(live->mark.Clone());
            }

            if (!SamePosition(blip.description.position, live->applied.position) && KCDC::Common::IsFinite(blip.description.position)) {
                mark->SetWorldPosition(blip.description.position);
                live->applied.position = blip.description.position;
            }

            if (blip.description.state != live->applied.state && mark->IsRegistered()) {
                mark->SetState(blip.description.state);
                live->applied.state = blip.description.state;
            }

            live->applied.label = blip.description.label;
        }

        SetResult(BlipResult::Applied, BlipResultToString(BlipResult::Applied));
    }

    bool MapBlipService::Attach(LiveBlip &live, const BlipDescription &description) {
        auto *compass = SDK::wh::guimodule::C_UICompass::GetInstance();
        if (compass == nullptr) {
            _forceReconcile = true;
            SetResult(BlipResult::NoCompass, BlipResultToString(BlipResult::NoCompass));
            return false;
        }

        // NaN and infinity feed the compass's distance and atan2 every frame.
        const SDK::Vec3 position = KCDC::Common::IsFinite(description.position) ? description.position : SDK::Vec3 {};

        auto mark = SDK::wh::guimodule::MakeCompassMark(description.type, position, description.state);
        if (!mark) {
            _forceReconcile = true;
            SetResult(BlipResult::AllocationFailed, BlipResultToString(BlipResult::AllocationFailed));
            return false;
        }

        // One reference stays here so the mark can be moved, re-stated and
        // removed later; the clone is the compass's.
        compass->AddMark(mark.Clone());

        live.mark             = std::move(mark);
        live.applied          = description;
        live.applied.position = position;
        return true;
    }

    void MapBlipService::Detach(LiveBlip &live) {
        if (!live.mark) {
            return;
        }

        // Only ask the compass to drop what it is actually carrying. The
        // reference kept here is what holds the mark up across the call: the
        // native releases the compass's own before it reads the mark again to
        // unregister it from flash.
        auto *compass = SDK::wh::guimodule::C_UICompass::GetInstance();
        if (compass != nullptr && live.mark.Get()->IsRegistered()) {
            compass->RemoveMark(live.mark.Clone());
        }

        live.mark.Release();
    }

    void MapBlipService::PublishLiveState() {
        auto *compass           = SDK::wh::guimodule::C_UICompass::GetInstance();
        const std::size_t onHud = compass != nullptr ? compass->GetMarkCount() : 0;

        std::scoped_lock lock(_mutex);
        _compassMarks = onHud;

        for (auto &blip : _desired) {
            const auto live = std::find_if(_live.begin(), _live.end(), [&blip](const LiveBlip &existing) { return existing.id == blip.id; });
            const auto *mark = live != _live.end() ? live->mark.Get() : nullptr;
            blip.live        = mark != nullptr && mark->IsRegistered();
        }
    }

    void MapBlipService::OnMapScreenRebuilt() {
        // The rebuild drops every row the screen carried, ours included.
        _mapRows.clear();

        std::vector<DesiredBlip> desired;
        {
            std::scoped_lock lock(_mutex);
            desired = _desired;
        }
        ReconcileMapScreen(desired);
    }

    void MapBlipService::ReconcileMapScreen(const std::vector<DesiredBlip> &desired) {
        SDK::wh::guimodule::C_UIMap *map = SDK::wh::guimodule::C_UIMap::GetInstance();
        if (map == nullptr || !map->IsOpen()) {
            // A closed screen keeps no rows; the next open rebuilds them.
            _mapRows.clear();
            return;
        }

        // A row can only be added or removed, so a changed blip is both.
        std::vector<std::uint32_t> stale;
        for (const MapRow &row : _mapRows) {
            const auto blip = std::find_if(desired.begin(), desired.end(), [&row](const DesiredBlip &wanted) { return wanted.id == row.id; });
            if (blip == desired.end() || !ShowsOnMap(*map, blip->description) || !SameRow(row.applied, blip->description)) {
                stale.push_back(row.id);
            }
        }
        if (!stale.empty()) {
            RemoveMapRows(*map, stale);
            std::erase_if(_mapRows, [&stale](const MapRow &row) { return std::find(stale.begin(), stale.end(), row.id) != stale.end(); });
        }

        std::vector<const DesiredBlip *> fresh;
        for (const DesiredBlip &blip : desired) {
            const bool shown = std::any_of(_mapRows.begin(), _mapRows.end(), [&blip](const MapRow &row) { return row.id == blip.id; });
            if (!shown && ShowsOnMap(*map, blip.description)) {
                fresh.push_back(&blip);
            }
        }
        if (!fresh.empty()) {
            PushMapRows(*map, fresh);
        }
    }

    void MapBlipService::PushMapRows(SDK::wh::guimodule::C_UIMap &map, const std::vector<const DesiredBlip *> &blips) {
        std::vector<SDK::wh::guimodule::FlashObjectRef> rows;
        rows.reserve(blips.size());

        for (const DesiredBlip *blip : blips) {
            const MapRowSource source(blip->description.position);

            SDK::wh::guimodule::S_EntityMapMark mark;
            mark.id     = MapRowId(blip->id);
            mark.type   = blip->description.type;
            mark.state  = MapRowState(blip->description);
            mark.source = &source;

            SDK::wh::guimodule::FlashMapPoiMarkerRef row = SDK::wh::guimodule::MakeFlashMapPoiMarker(mark);
            if (!row) {
                continue;
            }

            // The constructor named the row after its icon type. A label
            // replaces that with a key rather than the text: the screen splits
            // a name into words and resolves each as a localization key, so
            // plain text comes out as "@word @word". One key survives that
            // intact, and ResolveMapLabel answers it with the label.
            if (!blip->description.label.empty()) {
                row.Get()->SetName((std::string(kMapLabelPrefix) + std::to_string(blip->id)).c_str());
            }

            rows.emplace_back(std::move(row));
            _mapRows.push_back(MapRow {blip->id, blip->description});
        }

        if (rows.empty()) {
            return;
        }

        // A view in the game's own vector layout over the buffer above; the
        // native only reads it, and takes references of its own to keep.
        SDK::wh::guimodule::FlashObjectRef *first = rows.data();
        const SDK::StdVector<SDK::wh::guimodule::FlashObjectRef> view {first, first + rows.size(), first + rows.size()};

        // AddPoiMarkers appends what PoiMarkers holds to what the screen
        // already shows, which is how the legend puts a category back.
        const SDK::ScopedCryString array("PoiMarkers");
        const SDK::ScopedCryString add("AddPoiMarkers");
        map.GetContent()->SetFlashArray(array, view);
        map.GetContent()->CallFlashFunction(add);
    }

    void MapBlipService::RemoveMapRows(SDK::wh::guimodule::C_UIMap &map, const std::vector<std::uint32_t> &ids) {
        // The same shape the legend's hide sends: each row's id, as the
        // decimal string the row was named with.
        SDK::ScopedUIArguments arguments;
        for (const std::uint32_t id : ids) {
            arguments.AppendString(std::to_string(MapRowId(id)).c_str());
        }

        const SDK::ScopedCryString array("PoiMarkers");
        const SDK::ScopedCryString remove("RemovePoiMarkers");
        map.GetContent()->SetFlashArguments(array, arguments.Get());
        map.GetContent()->CallFlashFunction(remove);
    }

    void MapBlipService::Shutdown() {
        for (auto &live : _live) {
            Detach(live);
        }
        _live.clear();

        if (SDK::wh::guimodule::C_UIMap *map = SDK::wh::guimodule::C_UIMap::GetInstance(); map != nullptr && map->IsOpen() && !_mapRows.empty()) {
            std::vector<std::uint32_t> ids;
            for (const MapRow &row : _mapRows) {
                ids.push_back(row.id);
            }
            RemoveMapRows(*map, ids);
        }
        _mapRows.clear();

        _forceReconcile = true;
        PublishLiveState();
    }
} // namespace KCDC::Features::Map
