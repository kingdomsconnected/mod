#include "discord_presence.h"

#include <discord.h>

#include <chrono>

#include "shared/game/levels.h"
#include "shared/network_config.h"
#include "shared/version.h"

namespace KCDC::Core {
    namespace {
        // Rich Presence art key on the Discord application.
        constexpr char kLargeImage[] = "logo";

        std::string RegionName(const std::string &levelName) {
            const KCDC::Shared::Game::PlayableLevel *level = KCDC::Shared::Game::FindPlayableLevel(levelName);
            return level != nullptr ? std::string(level->displayName) : std::string {};
        }
    } // namespace

    void DiscordPresence::Install(Framework::External::Discord::Wrapper &presence) {
        presence.SetScriptFields(Framework::External::Discord::PresenceField::State | Framework::External::Discord::PresenceField::SmallImage);
    }

    void DiscordPresence::Update(Framework::External::Discord::Wrapper &presence, Phase phase, const std::string &serverName, const std::string &levelName) {
        // The timer counts the menu, a whole session, or the builder.
        if (_since == 0 || (phase != _phase && phase != Phase::Playing && phase != Phase::Leaving)) {
            _since = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        }
        _phase = phase;

        const std::string server = serverName.empty() ? std::string("On a server") : serverName;
        const std::string region = RegionName(levelName);

        std::string details;
        std::string state;
        switch (phase) {
        case Phase::Starting: details = "Starting up"; break;
        case Phase::MainMenu:
            details = "In the main menu";
            state   = "Choosing a server";
            break;
        case Phase::Joining:
            details = server;
            state   = "Joining";
            break;
        case Phase::Playing:
            details = server;
            state   = region.empty() ? std::string("In Bohemia") : "In " + region;
            break;
        case Phase::Leaving:
            details = server;
            state   = "Leaving";
            break;
        case Phase::WorldBuilder:
            details = "In the World Builder";
            state   = region;
            break;
        }

        const std::string largeText = std::string(KCDC::Shared::Network::ModDisplayName) + " " + KCDC::Shared::Version::rel;

        discord::Activity activity {};
        activity.SetType(discord::ActivityType::Playing);
        activity.SetDetails(details.c_str());
        activity.SetState(state.c_str());
        activity.GetTimestamps().SetStart(_since);
        activity.GetAssets().SetLargeImage(kLargeImage);
        activity.GetAssets().SetLargeText(largeText.c_str());
        presence.SetBaseActivity(activity);
    }
} // namespace KCDC::Core
