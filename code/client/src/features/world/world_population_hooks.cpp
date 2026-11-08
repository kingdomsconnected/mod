#include <utils/safe_win32.h>

#include <MinHook.h>

#include <utils/hooking/hook_function.h>

#include "world_population.h"

#include "core/application.h"

#include "sdk/cryengine/entitysystem/i_entity_class.h"
#include "sdk/cryengine/entitysystem/i_entity_system.h"
#include "sdk/cryengine/entitysystem/i_entity_system_sink.h"
#include "sdk/patterns.h"
#include "sdk/wh/rpgmodule/c_soul_pool_base.h"

#include <cstdint>

namespace KCDC::Features::World {
    namespace {
        using SetSpawningEnabled_t            = void(__fastcall *)(void *, bool, void *);
        using FinalizeAndSubmitSpawnRequest_t = std::int32_t(__fastcall *)(SDK::wh::rpgmodule::C_SoulPoolBase *, void *, const void *);

        SetSpawningEnabled_t g_setSpawningEnabledOriginal                       = nullptr;
        FinalizeAndSubmitSpawnRequest_t g_finalizeAndSubmitSpawnRequestOriginal = nullptr;

        /**
         * Keeps every animal spawner switched off.
         *
         * Each level-authored `AnimalSpawner` decides on its own machine
         * which animals appear and where, so no two clients agree.
         *
         * The wanted state is forced rather than the call skipped: the pass
         * this runs reads the flag both ways, and the disabled branch retires
         * every entry the spawner owns. A spawner holding animals when the mod
         * loads gives them up the next time it is toggled.
         */
        void __fastcall SetSpawningEnabled_Hook(void *self, bool /*enable*/, void *queryParams) {
            g_setSpawningEnabledOriginal(self, false, queryParams);
        }

        /**
         * Refuses every soul a pool offers, where the request reaches the
         * spawner queue.
         *
         * `SpawnSouls` backs both `C_StaticSoulPool` and `C_SingleSoulPool` --
         * every guard post, bandit camp and random-event group -- and picks a
         * random count of random souls, so no two clients agree. Each one it
         * picks passes through here.
         *
         * Not by answering `SpawnSouls` itself: that takes its completion
         * callback by value, the MSVC ABI has the callee destroy it, and a
         * stand-in returning early leaks it. Refusing a step lower leaves the
         * native to build and destroy everything itself, and to retire the
         * pool on the way -- a soul is marked used and a spawn slot consumed
         * before the submit, so one refused pass drains both.
         */
        std::int32_t __fastcall FinalizeAndSubmitSpawnRequest_Hook(SDK::wh::rpgmodule::C_SoulPoolBase * /*pool*/, void * /*request*/, const void * /*spawnSlot*/) {
            return SDK::wh::rpgmodule::C_SoulPoolBase::kSpawnRequestRefused;
        }

    } // namespace

    bool PopulationSink::OnBeforeSpawn(SDK::SEntitySpawnParams &params) {
        const char *className = params.entityClass != nullptr ? params.entityClass->GetName() : nullptr;
        return AllowsSpawn(className, params.name);
    }

    void InstallPopulationSink() {
        auto *entitySystem = SDK::IEntitySystem::GetInstance();
        if (entitySystem != nullptr && KCDC::Core::gApplication != nullptr) {
            entitySystem->AddSink(&KCDC::Core::gApplication->GetWorldService().GetPopulationSink(), SDK::EntitySystemSinkSubscriptions::OnBeforeSpawn, 0);
        }
    }
} // namespace KCDC::Features::World

static InitFunction init(
    []() {
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_AnimalSpawner__SetSpawningEnabled), reinterpret_cast<LPVOID>(&KCDC::Features::World::SetSpawningEnabled_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::World::g_setSpawningEnabledOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_SoulPoolBase__FinalizeAndSubmitSpawnRequest), reinterpret_cast<LPVOID>(&KCDC::Features::World::FinalizeAndSubmitSpawnRequest_Hook),
            reinterpret_cast<LPVOID *>(&KCDC::Features::World::g_finalizeAndSubmitSpawnRequestOriginal));
    },
    "Population");
