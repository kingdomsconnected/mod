#pragma once

#include "npc_inventory.h"
#include "shared/features/npc/npc_entity.h"
#include "shared/game/animal_loot.h"

#include <mafianet/types.h>

#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace KCDC::Features::Npc {
    class NpcInventoryModule final {
      public:
        static NpcInventoryModule &Get();
        static void Install();
        void Update();
        void Reset();
        void Create(KCDC::Shared::Entities::NpcEntity &npc);
        void Destroy(std::uint64_t npc);
        void Forget(std::uint64_t player);
        void Revived(KCDC::Shared::Entities::NpcEntity &npc);
        NpcInventory *Find(std::uint64_t npc);
        bool Dress(KCDC::Shared::Entities::NpcEntity &npc, const std::vector<std::string> &classes);
        nlohmann::json ScriptGet(std::uint64_t npc);
        nlohmann::json AnimalLoot(std::uint64_t npc) const;
        nlohmann::json ScriptSet(std::uint64_t npc, const nlohmann::json &request);
        void Changed(std::uint64_t npc);

      private:
        struct Record {
            explicit Record(KCDC::Features::Inventory::InventoryIds &ids): inventory(ids) {}
            NpcInventory inventory;
            const KCDC::Shared::Game::AnimalLootDefinition *animal = nullptr;
            bool nativeKnown                                       = false;
            KCDC::Shared::NpcInventory::InteractionState publishedInteraction;
            std::uint64_t publishedInventoryRevision = 0;
            std::map<std::string, std::uint32_t> publishedEquipment;
            bool publishedReady         = false;
            bool publishedNativeKnown   = false;
            bool announced              = false;
            std::uint64_t stateRevision = 0;
        };
        struct Incoming {
            MafiaNet::PeerGuid peer;
            std::string body;
        };
        void Receive(MafiaNet::PeerGuid peer, const KCDC::Shared::RPC::NpcInventoryRequest &message);
        void Process(const Incoming &incoming);
        static bool CanAccess(std::uint64_t player, const KCDC::Shared::Entities::NpcEntity &npc);
        void Publish(KCDC::Shared::Entities::NpcEntity &npc, Record &record);
        std::unordered_map<std::uint64_t, std::unique_ptr<Record>> _records;
        std::mutex _incomingMutex;
        std::vector<Incoming> _incoming;
        std::vector<std::pair<std::uint64_t, std::uint64_t>> _harvests;
        std::uint64_t _nextToken = 1;
    };
} // namespace KCDC::Features::Npc
