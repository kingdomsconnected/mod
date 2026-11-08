#include <utils/safe_win32.h>

#include "core/application.h"
#include "features/world/world_service.h"
#include "gathering_service.h"
#include "sdk/wh/environmentmodule/c_environment_module.h"
#include <MinHook.h>
#include <utils/hooking/hook_function.h>

namespace KCDC::Features::Gathering {
    namespace {
        using Start_t                                     = void(__fastcall *)(SDK::wh::playermodule::C_HerbGathering *, SDK::EntityId);
        using AwardGatheredHerbs_t                        = void(__fastcall *)(SDK::wh::playermodule::C_HerbGathering *);
        using HarvestSelected_t                           = std::uint32_t(__fastcall *)(SDK::wh::environmentmodule::C_RespawnManager *);
        Start_t g_startOriginal                           = nullptr;
        AwardGatheredHerbs_t g_awardGatheredHerbsOriginal = nullptr;
        HarvestSelected_t g_harvestSelectedOriginal       = nullptr;

        void __fastcall Start(SDK::wh::playermodule::C_HerbGathering *minigame, SDK::EntityId proxy) {
            if (KCDC::Core::gApplication == nullptr || !KCDC::Features::World::WorldService::IsSessionActive()) {
                g_startOriginal(minigame, proxy);
                return;
            }
            auto &events = KCDC::Core::gApplication->GetLocalEvents();
            // A refusal ends the minigame the script just created, the way
            // the game does for a missing area: Start with no area takes the
            // native failure path (vtable slot 33, 0x182E97270). One already
            // running is left alone.
            if (!events.CanStartGathering(minigame, proxy)) {
                if (minigame != nullptr && !minigame->IsActive()) {
                    g_startOriginal(minigame, 0);
                }
                return;
            }
            const auto source = SDK::wh::environmentmodule::C_EnvironmentModule::GetInstance()->GetRespawnManager()->GetInteractionState();
            g_startOriginal(minigame, proxy);
            events.GatheringStarted(minigame, source);
        }

        std::uint32_t __fastcall HarvestSelected(SDK::wh::environmentmodule::C_RespawnManager *manager) {
            if (KCDC::Core::gApplication == nullptr || !KCDC::Features::World::WorldService::IsSessionActive()) {
                return g_harvestSelectedOriginal(manager);
            }
            // C_HerbGathering stores this count and gates every native item,
            // survival/strength XP and Lucky Find effect on count != 0.
            return 0;
        }

        void __fastcall AwardGatheredHerbs(SDK::wh::playermodule::C_HerbGathering *minigame) {
            if (KCDC::Core::gApplication != nullptr && KCDC::Features::World::WorldService::IsSessionActive()) {
                // Both the action-exit and fade paths reach this success
                // routine. Finish/Cancel also clear the action's callbacks,
                // so observing action exit alone can miss a completed pick.
                auto *manager = SDK::wh::environmentmodule::C_EnvironmentModule::GetInstance()->GetRespawnManager();
                KCDC::Core::gApplication->GetLocalEvents().GatheringCompleted(manager);
            }
            // HarvestSelected still returns zero in a session, so the
            // original routine only finishes; the server grants the items.
            g_awardGatheredHerbsOriginal(minigame);
        }
    } // namespace
} // namespace KCDC::Features::Gathering

static InitFunction init(
    []() {
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_HerbGathering__Start), reinterpret_cast<LPVOID>(&KCDC::Features::Gathering::Start), reinterpret_cast<LPVOID *>(&KCDC::Features::Gathering::g_startOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_RespawnManager__HarvestSelected), reinterpret_cast<LPVOID>(&KCDC::Features::Gathering::HarvestSelected), reinterpret_cast<LPVOID *>(&KCDC::Features::Gathering::g_harvestSelectedOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_HerbGathering__AwardGatheredHerbs), reinterpret_cast<LPVOID>(&KCDC::Features::Gathering::AwardGatheredHerbs), reinterpret_cast<LPVOID *>(&KCDC::Features::Gathering::g_awardGatheredHerbsOriginal));
    },
    "Harvest");
