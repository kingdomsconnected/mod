#pragma once

#include <external/discord/wrapper.h>

#include <cstdint>
#include <string>

namespace KCDC::Core {
    /**
     * The client's Discord rich presence. Server scripts may only override
     * the state line and the small image.
     */
    class DiscordPresence final {
      public:
        enum class Phase {
            Starting,
            MainMenu,
            Joining,
            Playing,
            Leaving,
            WorldBuilder
        };

        static void Install(Framework::External::Discord::Wrapper &presence);

        void Update(Framework::External::Discord::Wrapper &presence, Phase phase, const std::string &serverName, const std::string &levelName);

      private:
        Phase _phase        = Phase::Starting;
        std::int64_t _since = 0;
    };
} // namespace KCDC::Core
