#pragma once

#include "features/player/player_scripting.h"

#include <v8.h>
#include <v8pp/class.hpp>

#include <cstdint>
#include <string>

namespace KCDC::Scripting {
    // The global `Progression`: the game's skill, stat and perk tables, and
    // the rules this server puts on how players move along them.
    //
    // Nothing is decided by default beyond what the game decides: every rate
    // is 1, budgets are unlimited, caps are the game's own, no perk is
    // blocked. What a server wants on top of that is a resource's to set.
    class Progression {
      public:
        static void Register(v8::Isolate *isolate, v8::Local<v8::Object> global);
    };

    // Puts the progression reads and verbs on the server's Player.
    void RegisterProgressionPlayerMembers(v8pp::class_<Player> &cls);

    // What the progression authority tells loaded resources. Event arguments
    // are network IDs; the handles a resource sees are created inside the
    // scripting isolate.
    namespace Events {
        // A player's own game produced XP and asks for it. False when a
        // handler refused it.
        bool PlayerXpGaining(std::uint64_t playerNetworkId, const std::string &track, double xp, const std::string &source);

        // XP landed on a player's soul, after the owner's own multipliers.
        void PlayerXpGained(std::uint64_t playerNetworkId, const std::string &track, double xp);

        void PlayerLevelUp(std::uint64_t playerNetworkId, const std::string &track, std::uint32_t level, std::uint32_t perkPoints);

        // A player confirmed a perk on the perk screen and every rule of the
        // game's allowed it. False when a handler refused it.
        bool PlayerPerkLearning(std::uint64_t playerNetworkId, const std::string &perk);

        void PlayerPerkAdded(std::uint64_t playerNetworkId, const std::string &perk, const char *source);
        void PlayerPerkRemoved(std::uint64_t playerNetworkId, const std::string &perk, const char *source);

        // A digest rose past what this server ordered.
        void PlayerProgressionRejected(std::uint64_t playerNetworkId, const std::string &reason);
    } // namespace Events
} // namespace KCDC::Scripting
