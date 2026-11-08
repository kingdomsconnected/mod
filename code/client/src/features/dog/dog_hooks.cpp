#include <utils/safe_win32.h>

#include <MinHook.h>

#include <utils/hooking/hook_function.h>

#include "features/dog/dog_service.h"

#include "sdk/patterns.h"
#include "sdk/wh/entitymodule/c_actor.h"
#include "sdk/wh/xgenaimodule/activitysystem/c_dog_objective.h"

#include <cstdint>

namespace KCDC::Features::Dog {
    namespace {
        using UpdateObjectivePlanner_t                            = void(__fastcall *)(void *);
        UpdateObjectivePlanner_t g_updateObjectivePlannerOriginal = nullptr;

        using SelectNextWaitBehavior_t                            = void(__fastcall *)(void *);
        SelectNextWaitBehavior_t g_selectNextWaitBehaviorOriginal = nullptr;

        /**
         * Holds a replicated dog to the wait behaviour its owner chose.
         *
         * `C_WaitDogObjective` picks from its configured set at random, so two
         * clients running the same objective land on different postures -- one
         * sits, the other lies down. Re-rolled rather than written: the set is
         * the objective's own and its layout is unproven, but the selection is
         * a pure pick over it, so asking again touches no memory the mod does
         * not understand. Bounded, because a dog whose set holds no such entry
         * would otherwise never return.
         *
         * A dog this client owns has no wanted behaviour, and rolls once.
         */
        void __fastcall SelectNextWaitBehavior_Hook(void *self) {
            auto *service     = DogService::Get();
            const auto wanted = service != nullptr ? service->WantedWaitBehaviour(self) : 0;

            const auto chosen = [self]() -> std::uint8_t {
                const auto *behaviour = static_cast<SDK::wh::xgenaimodule::activitysystem::C_DogObjective *>(self)->GetWaitBehaviour();
                return behaviour != nullptr ? behaviour->GetType() : 0;
            };

            for (int attempt = 0; attempt < 8; ++attempt) {
                g_selectNextWaitBehaviorOriginal(self);
                if (wanted == 0 || chosen() == wanted) {
                    break;
                }
            }
        }

        /**
         * Stops a replicated dog from deciding anything.
         *
         * `C_DogCompanion::UpdateObjectivePlanner` is the sole entry point
         * that scores every objective and switches the running one. Skipping
         * it leaves the animal fully alive -- possessed, animated, physical,
         * audible and perceived by NPCs -- and takes away only the deciding.
         *
         * Not merely for tidiness: `C_TeleportDogObjective` writes the dog's
         * transform through actor virtuals 85 and 5, and the runaway
         * objective's return does the same. Neither can be corrected against,
         * only prevented.
         *
         * Suppressed means "do not choose", not "do not run". This function is
         * also what ticks the objective it settled on -- its own else-branch
         * at `0x1809F6D9B` -- so a dog driven to an objective from the wire
         * would otherwise hold whatever pose that objective struck on its
         * first frame and never advance past it.
         *
         * And under the same condition the planner ticks it: not while the
         * objective is still queued behind an activity slot, because its
         * `OnActivate` has not run and its update would read state nothing
         * has written.
         *
         * The owner's own dog is never in the register, so its brain runs
         * exactly as the shipped game's does.
         */
        void __fastcall UpdateObjectivePlanner_Hook(void *planner) {
            auto *service = DogService::Get();
            if (service != nullptr && service->IsPlannerSuppressed(planner)) {
                auto *running = static_cast<SDK::wh::xgenaimodule::activitysystem::C_DogObjectivePlanner *>(planner)->GetRunningObjective();
                if (running != nullptr && !running->IsAwaitingActivitySlot()) {
                    running->Update();
                }
                return;
            }

            g_updateObjectivePlannerOriginal(planner);
        }

    } // namespace
} // namespace KCDC::Features::Dog

static InitFunction init(
    []() {
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_DogCompanion__UpdateObjectivePlanner), reinterpret_cast<LPVOID>(&KCDC::Features::Dog::UpdateObjectivePlanner_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Dog::g_updateObjectivePlannerOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_WaitDogObjective__SelectNextWaitBehavior), reinterpret_cast<LPVOID>(&KCDC::Features::Dog::SelectNextWaitBehavior_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Dog::g_selectNextWaitBehaviorOriginal));
    },
    "Dog");
