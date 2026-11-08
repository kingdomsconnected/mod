#pragma once

#include "features/player/player_scripting.h"

#include "shared/features/player/player_progression.h"

#include <v8.h>
#include <v8pp/class.hpp>

#include <cstdint>
#include <string>

namespace KCDC::Scripting {
    // The global `Progression` on the client: the game's skill, stat and perk
    // tables, read-only. A client script presents progression and never
    // grants it; every change is the server's.
    class Progression {
      public:
        static void Register(v8::Isolate *isolate, v8::Local<v8::Object> global);
    };

    // Puts `level`, `levels`, `perks`, `getTrack` and `hasPerk` on the client's
    // Player. The local player reads its own soul, everyone else the levels
    // and perks the server replicates.
    void RegisterProgressionPlayerMembers(v8pp::class_<Player> &cls);

    // What the local player's progression did, raised once the change is on
    // the soul -- after the server ordered it and the game applied it.
    namespace Events {
        void ProgressionXpGained(const std::string &track, double xp);
        void ProgressionLevelUp(const std::string &track, std::uint32_t level, std::uint32_t perkPoints);
        void ProgressionPerkAdded(const std::string &perk);
        void ProgressionPerkRemoved(const std::string &perk);
        void ProgressionPerkRefused(const std::string &perk, Shared::RPC::PerkRefusal reason);
    } // namespace Events
} // namespace KCDC::Scripting
