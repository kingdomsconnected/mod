#include <utils/safe_win32.h>

#include <MinHook.h>
#include <utils/hooking/hook_function.h>

#include "sdk/patterns.h"

#include "core/application.h"
#include "features/world/world_service.h"

namespace KCDC::Features::World {
    namespace {
        void(__fastcall *gSetTime)(SDK::wh::rpgmodule::C_Calendar *, std::int64_t)             = nullptr;
        void(__fastcall *gSetScale)(SDK::wh::rpgmodule::C_Calendar *, float)                   = nullptr;
        void(__fastcall *gExecuteWeather)(SDK::wh::environmentmodule::BlendTimeOfDayCommand *) = nullptr;
        void(__fastcall *gUpdateWeather)(SDK::wh::environmentmodule::C_WeatherSystem *, float) = nullptr;
        void *(__fastcall *gMakeWeatherEntry)(SDK::wh::environmentmodule::TimeOfDayBlenderLayout *, SDK::wh::environmentmodule::RandomPresetEntry *, std::uint32_t) = nullptr;

        // These five fire before the application exists. The service owns
        // nothing at that point, so every edge below let the engine through
        // and did nothing else; guarding KCDC::Core::gApplication is what preserves that.

        void __fastcall SetTime(SDK::wh::rpgmodule::C_Calendar *calendar, std::int64_t time) {
            if (KCDC::Core::gApplication == nullptr || KCDC::Core::gApplication->GetWorldService().AllowCalendarWrite(calendar)) {
                gSetTime(calendar, time);
            }
        }

        void __fastcall SetScale(SDK::wh::rpgmodule::C_Calendar *calendar, float scale) {
            if (KCDC::Core::gApplication == nullptr || KCDC::Core::gApplication->GetWorldService().AllowCalendarWrite(calendar)) {
                gSetScale(calendar, scale);
            }
        }

        void __fastcall ExecuteWeather(SDK::wh::environmentmodule::BlendTimeOfDayCommand *command) {
            if (KCDC::Core::gApplication == nullptr || !KCDC::Core::gApplication->GetWorldService().ExecuteWeatherCommand(command)) {
                gExecuteWeather(command);
            }
        }

        void __fastcall UpdateWeather(SDK::wh::environmentmodule::C_WeatherSystem *weather, float seconds) {
            if (KCDC::Core::gApplication == nullptr) {
                gUpdateWeather(weather, seconds);
                return;
            }

            KCDC::Core::gApplication->GetWorldService().BeforeWeatherUpdate(weather);
            gUpdateWeather(weather, seconds);
            KCDC::Core::gApplication->GetWorldService().AfterWeatherUpdate(weather);
        }

        /**
         * The engine is about to roll its own next sky. Returns the entry it
         * was handed, as the native does; while the mod owns this weather
         * system that entry is the server's preset instead of a roll.
         */
        void *__fastcall MakeWeatherEntry(SDK::wh::environmentmodule::TimeOfDayBlenderLayout *blender, SDK::wh::environmentmodule::RandomPresetEntry *entry, std::uint32_t previousPreset) {
            if (KCDC::Core::gApplication != nullptr && KCDC::Core::gApplication->GetWorldService().HoldWeatherEntry(blender, entry, previousPreset)) {
                return entry;
            }

            return gMakeWeatherEntry(blender, entry, previousPreset);
        }
    } // namespace
} // namespace KCDC::Features::World

static InitFunction init(
    []() {
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_Calendar__SetWorldTime), reinterpret_cast<LPVOID>(&KCDC::Features::World::SetTime), reinterpret_cast<LPVOID *>(&KCDC::Features::World::gSetTime));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_Calendar__SetWorldTimeRatio), reinterpret_cast<LPVOID>(&KCDC::Features::World::SetScale), reinterpret_cast<LPVOID *>(&KCDC::Features::World::gSetScale));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.ExecuteBlendTimeOfDayCommand), reinterpret_cast<LPVOID>(&KCDC::Features::World::ExecuteWeather), reinterpret_cast<LPVOID *>(&KCDC::Features::World::gExecuteWeather));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_WeatherSystem__Update), reinterpret_cast<LPVOID>(&KCDC::Features::World::UpdateWeather), reinterpret_cast<LPVOID *>(&KCDC::Features::World::gUpdateWeather));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_TimeOfDayBlender__MakeRandomEntry), reinterpret_cast<LPVOID>(&KCDC::Features::World::MakeWeatherEntry), reinterpret_cast<LPVOID *>(&KCDC::Features::World::gMakeWeatherEntry));
    },
    "World");
