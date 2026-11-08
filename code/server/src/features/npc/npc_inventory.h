#pragma once

#include "features/inventory/inventory.h"
#include "shared/features/npc/npc_inventory_message.h"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace KCDC::Features::Npc {
    /** Stock outlives native bodies and simulators. Only its initial census is delegated. */
    class NpcInventory final {
      public:
        explicit NpcInventory(KCDC::Features::Inventory::InventoryIds &ids): _inventory(ids) {}
        KCDC::Features::Inventory::Inventory &Stock() {
            return _inventory;
        }
        const KCDC::Features::Inventory::Inventory &Stock() const {
            return _inventory;
        }
        bool Ready() const {
            return _ready;
        }
        bool Harvested() const {
            return _harvested;
        }
        std::uint64_t Holder() const {
            return _holder;
        }
        std::uint64_t Token() const {
            return _token;
        }
        KCDC::Shared::NpcInventory::InteractionState Interaction() const {
            return {_holder, _token, _harvesting, _harvested};
        }
        const std::map<std::string, std::uint32_t> &Equipped() const {
            return _equipped;
        }

        /** One atomic baseline, with server-issued ids; subsequent censuses are refused. */
        bool Seed(const std::vector<KCDC::Features::Inventory::InventoryRow> &rows, const std::vector<std::uint32_t> &equipped);
        /** A script restore explicitly replaces the native baseline. */
        void Restored(std::map<std::string, std::uint32_t> equipped, std::optional<bool> harvested = std::nullopt);
        /** Adds missing worn units atomically. Taking clothes off keeps them as loose stock. */
        bool Dress(const std::vector<std::string> &classes);
        void ClampEquipment();

        bool Open(std::uint64_t player, std::uint64_t token, std::uint32_t epoch, std::uint64_t now, bool harvest, bool harvestable);
        bool Holds(std::uint64_t player, std::uint64_t token, std::uint32_t epoch, std::uint64_t now) const;
        bool Renew(std::uint64_t player, std::uint64_t token, std::uint32_t epoch, std::uint64_t now);
        bool Complete(std::uint64_t player, std::uint64_t token, std::uint32_t epoch, std::uint64_t now);
        KCDC::Features::Inventory::InventoryResult Transfer(KCDC::Features::Inventory::Inventory &playerInventory, std::uint64_t player, std::uint64_t token, std::uint32_t epoch, std::uint64_t now, const std::vector<KCDC::Features::Inventory::InventoryUnit> &units, bool deposit,
            std::uint64_t npcRevision, std::uint64_t playerRevision);
        static bool CanAccess(bool initialized, bool canAct, bool alive, bool lootable, bool visible, float distance);
        bool Close(std::uint64_t player, std::uint64_t token);
        void Release();
        /** Reviving retains stock; it starts another harvestable life. */
        void Revived();
        nlohmann::json Snapshot() const;

      private:
        KCDC::Features::Inventory::Inventory _inventory;
        std::map<std::string, std::uint32_t> _equipped;
        bool _ready            = false;
        bool _harvested        = false;
        bool _harvesting       = false;
        std::uint64_t _holder  = 0;
        std::uint64_t _token   = 0;
        std::uint64_t _expires = 0;
        std::uint32_t _epoch   = 0;
    };
} // namespace KCDC::Features::Npc
