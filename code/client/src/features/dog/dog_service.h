#pragma once

#include "core/feature_registry.h"

#include "dog.h"

#include <unordered_map>
#include <unordered_set>

namespace KCDC::Features::Dog {
    /**
     * Everything about dogs that is not about one dog.
     *
     * Which brains this client is not allowed to run is single-instance state:
     * the replicas write into it from their own frame, and a raw engine detour
     * reads it back. It lives here, on one object with a lifetime, so the
     * replicas stay replicas.
     */
    class DogService final: public KCDC::Core::IFeatureService {
      public:
        /** Game thread: drives every live dog. */
        void Update() override;

        /** Session ended: every suppressed brain went with its replica. */
        void Reset() override;

        // --- The suppression register ---
        //
        // A dog this client does not own must not decide anything: the pose
        // comes off the wire, and two of the game's own objectives write the
        // transform directly rather than through movement. Suppression is one
        // detour on C_DogCompanion::UpdateObjectivePlanner and this set of the
        // companions it must refuse.

        void SuppressPlanner(void *companion);

        /**
         * Which wait behaviour a replicated dog's objective should choose.
         *
         * `C_WaitDogObjective` picks at random, so two clients running the
         * same objective land on different postures. Keyed by the objective,
         * because that is all the selection hook is given. 0 means "no
         * opinion", which is every dog this client owns.
         */
        void WantWaitBehaviour(void *objective, std::uint8_t type);
        void ForgetWaitBehaviour(void *objective);
        std::uint8_t WantedWaitBehaviour(void *objective) const;
        void ResumePlanner(void *companion);

        /**
         * Whether this companion's planner must not run.
         *
         * Called from inside the engine's own update, so it does nothing but
         * look up: no allocation, no logging, no re-entry.
         */
        bool IsPlannerSuppressed(void *companion) const {
            return !_suppressed.empty() && _suppressed.find(companion) != _suppressed.end();
        }

        static DogService *Get();

      private:
        std::unordered_set<void *> _suppressed;
        std::unordered_map<void *, std::uint8_t> _wantedWaitBehaviour;
    };
} // namespace KCDC::Features::Dog
