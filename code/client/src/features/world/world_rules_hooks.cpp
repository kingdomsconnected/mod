#include <utils/safe_win32.h>

#include <MinHook.h>
#include <logging/logger.h>

#include <utils/hooking/hook_function.h>

#include "sdk/patterns.h"
#include "sdk/types.h"
#include "sdk/wh/playermodule/i_fog_of_war.h"
#include "sdk/wh/rpgmodule/c_poi.h"
#include "sdk/wh/rpgmodule/c_rpg_location.h"
#include "sdk/wh/rpgmodule/c_rpg_location_manager.h"

#include "core/application.h"

#include <cstdint>

namespace KCDC::Features::World {
    namespace {
        /** Overwrites one entry of a vtable in the game's read-only data. */
        void *OverrideSlot(std::uint64_t vtable, std::size_t slot, void *replacement) {
            auto **entry = reinterpret_cast<void **>(vtable) + slot;

            DWORD previous = 0;
            if (VirtualProtect(entry, sizeof(void *), PAGE_READWRITE, &previous) == 0) {
                return nullptr;
            }

            void *original = *entry;
            *entry         = replacement;
            VirtualProtect(entry, sizeof(void *), previous, &previous);
            return original;
        }

        // The server's `reveal_map`. The overrides are installed during CRT
        // startup, before the application exists, and keep the reveal there.
        bool IsMapRevealed() {
            return KCDC::Core::gApplication == nullptr || KCDC::Core::gApplication->IsSessionMapRevealed();
        }

        using GetDiscoveryStatus_t                             = std::uint32_t(__fastcall *)(void *);
        GetDiscoveryStatus_t g_poiDiscoveryStatusOriginal      = nullptr;
        GetDiscoveryStatus_t g_locationDiscoveryStatusOriginal = nullptr;

        std::uint32_t __fastcall PoiDiscoveryStatus_Override(void *poi) {
            return IsMapRevealed() ? static_cast<std::uint32_t>(SDK::wh::rpgmodule::E_DiscoveryStatus::Discovered) : g_poiDiscoveryStatusOriginal(poi);
        }

        std::uint32_t __fastcall LocationDiscoveryStatus_Override(void *location) {
            return IsMapRevealed() ? static_cast<std::uint32_t>(SDK::wh::rpgmodule::E_DiscoveryStatus::Discovered) : g_locationDiscoveryStatusOriginal(location);
        }

        using SetDiscoveryStatusAndNotify_t = bool(__fastcall *)(SDK::wh::rpgmodule::I_POI *, std::uint32_t, bool);
        SetDiscoveryStatusAndNotify_t g_setDiscoveryStatusAndNotifyOriginal = nullptr;

        bool __fastcall SetDiscoveryStatusAndNotify_Hook(SDK::wh::rpgmodule::I_POI *poi, std::uint32_t status, bool notify) {
            // Revealed, native discovery bypasses the getter; suppress its
            // notifications. Otherwise the discovery is the game's own: the
            // status it asked for, and the toast, trigger and XP with it.
            const bool revealed         = IsMapRevealed();
            const std::uint32_t written = revealed ? static_cast<std::uint32_t>(SDK::wh::rpgmodule::E_DiscoveryStatus::Discovered) : status;
            const bool changed          = g_setDiscoveryStatusAndNotifyOriginal(poi, written, revealed ? false : notify);
            // Only a finished discovery is news; "position known" is not.
            if (changed && written == static_cast<std::uint32_t>(SDK::wh::rpgmodule::E_DiscoveryStatus::Discovered) && KCDC::Core::gApplication != nullptr) {
                KCDC::Core::gApplication->GetWorldService().NotifyPOIDiscovered(poi->GetId());
            }
            return changed;
        }

        using DiscoverLocation_t                      = void(__fastcall *)(SDK::wh::rpgmodule::C_RPGLocationManager *, const SDK::wh::rpgmodule::S_LocationId *, bool, bool);
        DiscoverLocation_t g_discoverLocationOriginal = nullptr;

        void __fastcall DiscoverLocation_Override(SDK::wh::rpgmodule::C_RPGLocationManager *manager, const SDK::wh::rpgmodule::S_LocationId *locationId, bool unused, bool positionOnly) {
            // A location -- a town, a district -- writes its status raw, so the
            // edge is read across the call. Revealed, it already reads
            // Discovered and nothing is raised.
            SDK::wh::rpgmodule::I_RPGLocationDiscovery *location = manager->FindLocationById(*locationId);
            const bool wasDiscovered                             = location == nullptr || location->GetDiscoveryStatus() == SDK::wh::rpgmodule::E_DiscoveryStatus::Discovered;
            g_discoverLocationOriginal(manager, locationId, unused, positionOnly);
            if (!wasDiscovered && location->GetDiscoveryStatus() == SDK::wh::rpgmodule::E_DiscoveryStatus::Discovered && KCDC::Core::gApplication != nullptr) {
                KCDC::Core::gApplication->GetWorldService().NotifyLocationDiscovered(*locationId);
            }
        }

        using GetOrCreateFogOfWar_t = SDK::wh::playermodule::I_FogOfWar *(__fastcall *)(void *, const SDK::CryStringT<char> *, unsigned int);
        GetOrCreateFogOfWar_t g_getOrCreateFogOfWarOriginal = nullptr;

        using FogLoad_t = bool(__fastcall *)(SDK::wh::playermodule::I_FogOfWar *, void *);
        FogLoad_t g_fogLoadOriginal = nullptr;

        using FogManagerLoad_t = bool(__fastcall *)(void *, void *);
        FogManagerLoad_t g_fogManagerLoadOriginal = nullptr;

        using FogManagerReset_t = SDK::wh::playermodule::I_FogOfWar *(__fastcall *)(void *);
        FogManagerReset_t g_fogManagerResetOriginal = nullptr;

        bool __fastcall FogManagerLoad_Hook(void *manager, void *reader) {
            const bool loaded = g_fogManagerLoadOriginal(manager, reader);
            // The saved FOW is phase 504, after CryEngine restores entities
            // from phase 503. Same-level loads must invalidate handles too.
            if (KCDC::Core::gApplication != nullptr) {
                KCDC::Core::gApplication->NotifyNativeContextChanged();
            }
            return loaded;
        }

        SDK::wh::playermodule::I_FogOfWar *__fastcall FogManagerReset_Hook(void *manager) {
            auto *fog = g_fogManagerResetOriginal(manager);
            // This reset comes from C_Game's completed-level listener,
            // through module message 24, after the new entities are loaded.
            if (KCDC::Core::gApplication != nullptr) {
                KCDC::Core::gApplication->NotifyNativeContextChanged();
            }
            return fog;
        }

        bool __fastcall FogLoad_Override(SDK::wh::playermodule::I_FogOfWar *fog, void *reader) {
            // Consume the saved chunk before revealing the loaded fog.
            const bool loaded = g_fogLoadOriginal(fog, reader);
            if (IsMapRevealed()) {
                fog->RevealAll();
            }
            return loaded;
        }

        SDK::wh::playermodule::I_FogOfWar *__fastcall GetOrCreateFogOfWar_Hook(void *manager, const SDK::CryStringT<char> *levelName, unsigned int terrainSize) {
            if (levelName != nullptr && KCDC::Core::gApplication != nullptr) {
                KCDC::Core::gApplication->SetCurrentLevelName(levelName->m_pData);
            }

            auto *fog = g_getOrCreateFogOfWarOriginal(manager, levelName, terrainSize);
            if (fog == nullptr) {
                return fog;
            }

            // Fog instances share this vtable.
            if (g_fogLoadOriginal == nullptr) {
                const auto vtable = *reinterpret_cast<std::uint64_t *>(fog);
                g_fogLoadOriginal = reinterpret_cast<FogLoad_t>(OverrideSlot(vtable, SDK::wh::playermodule::I_FogOfWar::kLoadSlot, reinterpret_cast<void *>(&FogLoad_Override)));
            }

            if (IsMapRevealed()) {
                fog->RevealAll();
            }
            return fog;
        }
    } // namespace
} // namespace KCDC::Features::World

static InitFunction init(
    []() {
        KCDC::Features::World::g_poiDiscoveryStatusOriginal = reinterpret_cast<KCDC::Features::World::GetDiscoveryStatus_t>(
            KCDC::Features::World::OverrideSlot(SDK::gPatterns.C_POI__DiscoveryVTable, SDK::wh::rpgmodule::I_POI::kGetDiscoveryStatusSlot, reinterpret_cast<void *>(&KCDC::Features::World::PoiDiscoveryStatus_Override)));
        KCDC::Features::World::g_locationDiscoveryStatusOriginal = reinterpret_cast<KCDC::Features::World::GetDiscoveryStatus_t>(
            KCDC::Features::World::OverrideSlot(SDK::gPatterns.C_RPGLocation__DiscoveryVTable, SDK::wh::rpgmodule::I_RPGLocationDiscovery::kGetDiscoveryStatusSlot, reinterpret_cast<void *>(&KCDC::Features::World::LocationDiscoveryStatus_Override)));
        KCDC::Features::World::g_discoverLocationOriginal = reinterpret_cast<KCDC::Features::World::DiscoverLocation_t>(
            KCDC::Features::World::OverrideSlot(SDK::gPatterns.C_RPGLocationManager__VTable, SDK::wh::rpgmodule::C_RPGLocationManager::kDiscoverLocationSlot, reinterpret_cast<void *>(&KCDC::Features::World::DiscoverLocation_Override)));
        const MH_STATUS discoveryStatus =
            MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_POI__SetDiscoveryStatusAndNotify), reinterpret_cast<LPVOID>(&KCDC::Features::World::SetDiscoveryStatusAndNotify_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::World::g_setDiscoveryStatusAndNotifyOriginal));
        Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("POI silent discovery hook: {}", MH_StatusToString(discoveryStatus));

        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_FOWManager__GetOrCreateFogOfWar), reinterpret_cast<LPVOID>(&KCDC::Features::World::GetOrCreateFogOfWar_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::World::g_getOrCreateFogOfWarOriginal));
        // Ordinary cached fog lookups (including player_dumpFow) do not
        // replace the world. Only the native reset/save reader invalidates it.
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_FOWManager__Load), reinterpret_cast<LPVOID>(&KCDC::Features::World::FogManagerLoad_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::World::g_fogManagerLoadOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_FOWManager__Reset), reinterpret_cast<LPVOID>(&KCDC::Features::World::FogManagerReset_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::World::g_fogManagerResetOriginal));
    },
    "WorldRules");
