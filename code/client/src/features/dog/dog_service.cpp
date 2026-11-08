#include "dog_service.h"

#include "core/application.h"

#include <core_modules.h>
#include <networking/replication/replication_manager.h>


namespace KCDC::Features::Dog {
    DogService *DogService::Get() {
        // The detour reaches the register from inside the engine's own update,
        // where there is no replica to ask and no application argument to pass
        // -- and it can fire before the application is up.
        return KCDC::Core::gApplication != nullptr ? &KCDC::Core::gApplication->GetDogService() : nullptr;
    }

    void DogService::Update() {
        auto *replication = Framework::CoreModules::GetReplication();
        if (replication == nullptr) {
            return;
        }

        replication->ForEach<Dog>([](Dog *dog) {
            dog->Frame();
        });
    }

    void DogService::Reset() {
        // Every entry belonged to a body that went with the session, and a
        // stale pointer here would refuse to run a brain that is now somebody
        // else's memory.
        _suppressed.clear();
        _wantedWaitBehaviour.clear();
    }

    void DogService::SuppressPlanner(void *companion) {
        if (companion != nullptr) {
            _suppressed.insert(companion);
        }
    }

    void DogService::ResumePlanner(void *companion) {
        if (companion != nullptr) {
            _suppressed.erase(companion);
        }
    }

    void DogService::WantWaitBehaviour(void *objective, std::uint8_t type) {
        if (objective == nullptr) {
            return;
        }
        _wantedWaitBehaviour[objective] = type;
    }

    void DogService::ForgetWaitBehaviour(void *objective) {
        _wantedWaitBehaviour.erase(objective);
    }

    std::uint8_t DogService::WantedWaitBehaviour(void *objective) const {
        const auto found = _wantedWaitBehaviour.find(objective);
        return found != _wantedWaitBehaviour.end() ? found->second : 0;
    }
} // namespace KCDC::Features::Dog
