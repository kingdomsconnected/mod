#include <utils/safe_win32.h>

#include <MinHook.h>

#include <utils/hooking/hook_function.h>

#include "core/application.h"

#include "sdk/patterns.h"
#include "sdk/wh/guimodule/c_ui_map.h"
#include "sdk/wh/rpgmodule/c_rpg_location.h"

#include <cstdint>

namespace KCDC::Features::Map {
    namespace {
        using UIStateChanged_t                    = void(__fastcall *)(SDK::wh::guimodule::C_UIMap *, std::uint8_t, std::uint8_t);
        UIStateChanged_t g_uiStateChangedOriginal = nullptr;

        /**
         * The map screen coming up and going away, taken as an edge rather
         * than watched for.
         *
         * This is the only routine that writes the map-up flag at +0x748: it
         * clears it itself when the state leaves the map, and the show path
         * that sets it has no other code caller. So reading the flag either
         * side of the original turns every UI state change into exactly the
         * transitions the screen actually made -- nothing to poll, and no way
         * to miss one or report it twice.
         *
         * The state argument is a UI state rather than an apse view, and most
         * values are nothing to do with the map; the comparison is what
         * filters them, not the argument.
         */
        void __fastcall OnUIStateChanged_Hook(SDK::wh::guimodule::C_UIMap *map, std::uint8_t previous, std::uint8_t current) {
            const bool wasOpen = map->IsOpen();
            g_uiStateChangedOriginal(map, previous, current);
            const bool isOpen = map->IsOpen();

            if (wasOpen != isOpen && KCDC::Core::gApplication != nullptr) {
                KCDC::Core::gApplication->GetMapService().NotifyScreenChanged(isOpen);
            }
        }

        using RefreshPoiMarkers_t                       = void(__fastcall *)(SDK::wh::guimodule::C_UIMap *);
        RefreshPoiMarkers_t g_refreshPoiMarkersOriginal = nullptr;

        /**
         * The screen's POI rows being rebuilt from the game's own list, which
         * is the only moment the mod's rows have to be put back. Its one
         * caller runs it on every open, after the map-up flag is set and
         * before the movie is told to open, so the mod's rows arrive in the
         * same frame as the game's.
         */
        void __fastcall RefreshPoiMarkers_Hook(SDK::wh::guimodule::C_UIMap *map) {
            g_refreshPoiMarkersOriginal(map);

            if (KCDC::Core::gApplication != nullptr) {
                KCDC::Core::gApplication->GetMapBlipService().OnMapScreenRebuilt();
            }
        }

        using FindMapLabelLocation_t                          = SDK::wh::rpgmodule::I_RPGLocationDiscovery *(__fastcall *)(const void *);
        FindMapLabelLocation_t g_findMapLabelLocationOriginal = nullptr;

        /**
         * A map label's location, which the map's label pass and the label it
         * then makes both ask for. The pass writes every label it finds a
         * location for, discovered or not, and leaves the fog to cover them;
         * with the fog cleared by a script, a label whose place the player
         * has not found answers no location, and the pass skips it as one
         * with none.
         */
        SDK::wh::rpgmodule::I_RPGLocationDiscovery *__fastcall FindMapLabelLocation_Hook(const void *label) {
            SDK::wh::rpgmodule::I_RPGLocationDiscovery *location = g_findMapLabelLocationOriginal(label);
            if (location != nullptr && KCDC::Core::gApplication != nullptr && !KCDC::Core::gApplication->GetMapService().IsLabelShown(location)) {
                return nullptr;
            }
            return location;
        }
    } // namespace
} // namespace KCDC::Features::Map

static InitFunction init(
    []() {
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_UIMap__OnUIStateChanged), reinterpret_cast<LPVOID>(&KCDC::Features::Map::OnUIStateChanged_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Map::g_uiStateChangedOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_UIMap__RefreshPoiMarkers), reinterpret_cast<LPVOID>(&KCDC::Features::Map::RefreshPoiMarkers_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Map::g_refreshPoiMarkersOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_UIMap__FindMapLabelLocation), reinterpret_cast<LPVOID>(&KCDC::Features::Map::FindMapLabelLocation_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Map::g_findMapLabelLocationOriginal));
    },
    "MapScreen");
