#pragma once

#include "common/spawn/actor_spawner.h"
#include "sdk/cryengine/action/i_actor_system.h"
#include "sdk/cryengine/entitysystem/i_entity_system_sink.h"
#include "shared/game/animal_catalog.h"

#include <string_view>

namespace KCDC::Features::World {
    /**
     * Refuses native human and animal NPC spawns, including directly placed
     * horses, dogs and chicken flock owners, which the two population hooks
     * leave alone. Replica bodies created by ActorSpawner retain their
     * existing name exemption.
     *
     * A false from OnBeforeSpawn is the engine's own refusal: SpawnEntity
     * returns null and the load manager skips the record.
     *
     * The sink holds nothing -- constructing one publishes its vtable and
     * that is all -- but the entity system keeps the pointer it is handed, so
     * the instance has to outlive the call that registers it. `WorldService`
     * owns it for that reason, rather than the translation unit that hooks.
     */
    class PopulationSink final: public SDK::IEntitySystemSink {
      public:
        bool OnBeforeSpawn(SDK::SEntitySpawnParams &params) override;

        /** Population policy shared by the spawn sink and its regression tests. */
        static bool AllowsSpawn(const char *className, const char *entityName) {
            if (className == nullptr) {
                return true;
            }

            // The scheduler hooks cannot stop directly placed or scripted
            // animals. Chicken owners create their Boids in OnInit; refusing
            // the owner prevents the flock without affecting other Boids.
            constexpr const char *refusedClasses[] = {SDK::ActorClassNames::NPC, SDK::ActorClassNames::NPCFemale, SDK::ActorClassNames::NPCNonAI, SDK::ActorClassNames::Horse, SDK::ActorClassNames::Dog, "Chickens", "ChickensBrownLight", "ChickensWhite"};
            bool nativePopulation                  = KCDC::Shared::Game::IsAnimalClass(className);
            for (const char *refused : refusedClasses) {
                if (std::string_view(className) == refused) {
                    nativePopulation = true;
                    break;
                }
            }

            return !nativePopulation || (entityName != nullptr && std::string_view(entityName).starts_with(KCDC::Common::Spawn::kSpawnedActorNamePrefix));
        }

        void OnSpawn(SDK::IEntity *entity, SDK::SEntitySpawnParams &params) override {}
        bool OnRemove(SDK::IEntity *entity) override {
            return true;
        }
        void OnReused(SDK::IEntity *entity, SDK::SEntitySpawnParams &params) override {}
        void OnEvent(SDK::IEntity *entity, SDK::SEntityEvent &event) override {}
        void GetMemoryUsage(SDK::ICrySizer *sizer) const override {}
        const char *GetName() const override {
            return "KCDCPopulationSink";
        }
    };

    /** Refuses the level's own actors. Call after the entity system exists and before any level load. */
    void InstallPopulationSink();
} // namespace KCDC::Features::World
