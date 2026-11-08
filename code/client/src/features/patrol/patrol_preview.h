#pragma once
#include "shared/features/patrol/patrol_route.h"
#include <memory>

namespace KCDC::Features::Patrol {
    /** Game-thread owner of a temporary, unreplicated native NPC and its order. */
    class PatrolPreview final {
      public:
        PatrolPreview();
        ~PatrolPreview();
        void Start(const Shared::Game::PatrolRouteDefinition &route, const std::string &role, const std::string &level);
        void Stop(bool worldGone = false);
        void Update();
        bool Active() const;
        nlohmann::json Progress() const;
        const std::string &Status() const;
        const std::vector<glm::vec3> &Path() const;

      private:
        struct State;
        std::unique_ptr<State> _state;
    };
} // namespace KCDC::Features::Patrol
