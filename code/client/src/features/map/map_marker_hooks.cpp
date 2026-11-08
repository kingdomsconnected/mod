#include <utils/safe_win32.h>

#include <MinHook.h>

#include <utils/hooking/hook_function.h>

#include "core/application.h"

#include "sdk/patterns.h"
#include "sdk/wh/guimodule/c_compass_mark.h"
#include "sdk/wh/guimodule/c_ui_map.h"

#include <cstdint>

namespace KCDC::Features::Map {
    namespace {
        using CheckpointAction_t = void(__fastcall *)(SDK::wh::guimodule::C_UIMap *);
        CheckpointAction_t g_placeCheckpointOriginal  = nullptr;
        CheckpointAction_t g_removeCheckpointOriginal = nullptr;

        /**
         * Read after the original: the action is gated on the cursor being
         * over the map, so a press that misses changes nothing, and the
         * position only exists once the original has asked flash for it.
         *
         * The same native places and moves -- moving overwrites the mark's
         * Vec3 in place and calls nothing else -- so asking beforehand is the
         * only way to tell the two apart.
         */
        void __fastcall PlaceCheckpointAtCursor_Hook(SDK::wh::guimodule::C_UIMap *map) {
            const bool hadMarker = map->HasCheckpoint();
            g_placeCheckpointOriginal(map);

            const auto mark        = map->FindCheckpointForCurrentMap();
            const auto *checkpoint = mark.Get();
            if (checkpoint == nullptr || KCDC::Core::gApplication == nullptr) {
                return;
            }

            KCDC::Core::gApplication->GetMapService().NotifyMarkerPlaced(checkpoint->GetWorldPosition(), checkpoint->GetMapId(), hadMarker);
        }

        /**
         * Read before the original: it unregisters the mark and erases it, so
         * afterwards nothing says where it was. The handle keeps the mark
         * alive across the call and releasing it is what destroys it.
         */
        void __fastcall RemoveCheckpoint_Hook(SDK::wh::guimodule::C_UIMap *map) {
            const auto mark        = map->FindCheckpointForCurrentMap();
            const auto *checkpoint = mark.Get();

            SDK::Vec3 position {};
            std::int32_t mapId = -1;
            if (checkpoint != nullptr) {
                position = checkpoint->GetWorldPosition();
                mapId    = checkpoint->GetMapId();
            }

            g_removeCheckpointOriginal(map);

            // The native is a no-op without a marker, and so is the event.
            if (checkpoint != nullptr && KCDC::Core::gApplication != nullptr) {
                KCDC::Core::gApplication->GetMapService().NotifyMarkerCleared(position, mapId);
            }
        }
    } // namespace
} // namespace KCDC::Features::Map

static InitFunction init(
    []() {
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_UIMap__PlaceCheckpointAtCursor), reinterpret_cast<LPVOID>(&KCDC::Features::Map::PlaceCheckpointAtCursor_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Map::g_placeCheckpointOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_UIMap__RemoveCheckpoint), reinterpret_cast<LPVOID>(&KCDC::Features::Map::RemoveCheckpoint_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Map::g_removeCheckpointOriginal));
    },
    "MapMarker");
