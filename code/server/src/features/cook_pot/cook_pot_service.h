#pragma once

#include "shared/features/cook_pot/cook_pot_entity.h"
#include "shared/features/cook_pot/cook_pot_message.h"
#include "shared/features/player/player_entity.h"
#include "shared/game/cook_pot_catalog.h"
#include <mafianet/types.h>
#include <map>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace KCDC::Features::CookPot {
    class CookPotService final {
      public:
        void Install(const std::string &level);
        void Update();
        void OnPlayerDisconnect(MafiaNet::PeerGuid peer);
        /** Snapshots of a world's pots; empty until a player or a write has given that world its pots. */
        nlohmann::json All(std::uint32_t world) const;
        nlohmann::json Snapshot(std::uint64_t guid, std::uint32_t world) const;
        bool Refill(std::uint64_t guid, std::uint32_t world, std::uint16_t portions);
        bool SetEnabled(std::uint64_t guid, std::uint32_t world, bool enabled);
        bool SetState(std::uint64_t guid, std::uint32_t world, KCDC::Shared::CookPot::State state);

        bool SetFood(std::uint64_t guid, std::uint32_t world, KCDC::Shared::Game::CookPotFood food);
        bool SetFire(std::uint64_t guid, std::uint32_t world, KCDC::Shared::CookPot::Fire fire);
        nlohmann::json RefillFromInventory(KCDC::Shared::Entities::HumanEntity *player, std::uint64_t guid, const nlohmann::json &recipe);
        nlohmann::json Poison(KCDC::Shared::Entities::HumanEntity *player, std::uint64_t guid, const std::string &rowId);

      private:
        const char *CheckPlayer(KCDC::Shared::Entities::HumanEntity *player, std::uint64_t guid, KCDC::Shared::Entities::CookPotEntity *&pot) const;
        KCDC::Shared::Entities::CookPotEntity *Find(std::uint64_t guid, std::uint32_t world) const;
        KCDC::Shared::Entities::CookPotEntity *Resolve(std::uint64_t guid, std::uint32_t world);
        static nlohmann::json Describe(const KCDC::Shared::Entities::CookPotEntity &pot);
        struct Meal {
            std::uint64_t sequence = 0;
            std::uint64_t guid     = 0;
            std::uint64_t expires  = 0;
            std::uint64_t revision = 0;
            std::uint64_t nextUse  = 0;
            std::uint32_t world    = 0;
            KCDC::Shared::RPC::BuffId poison;
            std::uint32_t incarnation      = 0;
            std::uint64_t contextRevision  = 0;
            std::uint64_t inventoryRestore = 0;
            bool granted                   = false;
            bool settled                   = false;
            bool nativeEffects             = true;
            nlohmann::json description;
        };
        void Receive(MafiaNet::PeerGuid peer, const KCDC::Shared::RPC::CookPotRequest &request);
        std::map<std::uint64_t, KCDC::Shared::Entities::CookPotEntity *> &EnsureWorld(std::uint32_t world);
        std::map<std::uint32_t, std::map<std::uint64_t, KCDC::Shared::Entities::CookPotEntity *>> _worlds;
        std::map<std::uint64_t, Meal> _meals;
        std::map<MafiaNet::PeerGuid, KCDC::Shared::RPC::CookPotContext> _contexts;
        std::uint64_t _nextContext = 0;
        std::vector<const KCDC::Shared::Game::CookPotPlacement *> _placements;
    };
} // namespace KCDC::Features::CookPot
