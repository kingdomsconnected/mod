#include "cook_pot_service.h"

#include "core/server.h"
#include "features/inventory/consumption_scripting.h"
#include "features/player/player_roster.h"
#include "shared/features/player/player_entity.h"
#include "shared/game/buff_catalog.h"
#include "shared/game/cry_guid_text.h"
#include "shared/scripting/level_guid.h"
#include <algorithm>
#include <cmath>
#include <core_modules.h>
#include <glm/geometric.hpp>
#include <mafianet/get_time.h>
#include <networking/network_peer.h>
#include <networking/replication/replication_manager.h>
#include <set>

namespace KCDC::Features::CookPot {
    namespace {
        constexpr std::uint64_t kReservationMilliseconds  = 10000;
        constexpr std::uint64_t kMealIntervalMilliseconds = 1000;
        constexpr float kReachMetres                      = 4.0F;

        std::uint32_t StockWorld(std::uint32_t world) {
            // New players use global visibility. Their shared stock belongs to
            // the default world; the visibility sentinel is not a stock world.
            return world == MafiaNet::VIRTUAL_WORLD_GLOBAL ? MafiaNet::VIRTUAL_WORLD_DEFAULT : world;
        }
    } // namespace

    void CookPotService::Install(const std::string &level) {
        for (const auto &placement : KCDC::Shared::Game::g_CookPotPlacements) {
            if (level == placement.level) {
                _placements.push_back(&placement);
            }
        }
        EnsureWorld(MafiaNet::VIRTUAL_WORLD_DEFAULT);
        Framework::CoreModules::GetNetworkPeer()->RegisterRPC<KCDC::Shared::RPC::CookPotRequest>([this](const KCDC::Shared::RPC::CookPotRequest &request, MafiaNet::Packet *packet) {
            if (packet != nullptr && request.valid && request.sequence != 0) {
                Receive(MafiaNet::ToPeerGuid(packet->guid), request);
            }
        });
    }

    std::map<std::uint64_t, KCDC::Shared::Entities::CookPotEntity *> &CookPotService::EnsureWorld(std::uint32_t world) {
        if (const auto found = _worlds.find(world); found != _worlds.end()) {
            return found->second;
        }
        auto &pots = _worlds[world];
        for (const auto *placement : _placements) {
            auto *pot = Framework::CoreModules::GetReplication()->CreateEntity<KCDC::Shared::Entities::CookPotEntity>();
            if (pot == nullptr) {
                continue;
            }
            pot->guid            = placement->guid;
            pot->stockWorld      = world;
            pot->stock.food      = placement->food;
            pot->position        = placement->position;
            pot->stock.portions  = placement->fullOnInit ? KCDC::Shared::CookPot::Stock::kDefaultPortions : 0;
            pot->streaming.range = 150.0F;
            pot->SetVirtualWorld(world);
            pots.emplace(pot->guid, pot);
        }
        return pots;
    }

    void CookPotService::Update() {
        std::set<std::uint32_t> worlds;
        Framework::CoreModules::GetReplication()->ForEach<KCDC::Shared::Entities::HumanEntity>([&](auto *player) {
            if (player->ownerGUID == MafiaNet::UNASSIGNED_PEER_GUID || Framework::CoreModules::GetReplication()->GetViewer(player->ownerGUID) != player) {
                return;
            }
            const auto world = StockWorld(player->GetVirtualWorld());
            worlds.insert(world);
            auto &context = _contexts[player->ownerGUID];
            if (context.revision == 0 || context.world != world || context.incarnation != player->soul.incarnation) {
                context.world       = world;
                context.incarnation = player->soul.incarnation;
                context.revision    = ++_nextContext;
                Framework::CoreModules::GetNetworkPeer()->SendRPC(context, MafiaNet::ToGuid(player->ownerGUID), MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
            }
        });
        for (const auto world : worlds) {
            EnsureWorld(world);
        }
    }

    KCDC::Shared::Entities::CookPotEntity *CookPotService::Find(std::uint64_t guid, std::uint32_t world) const {
        const auto pots = _worlds.find(world);
        if (pots == _worlds.end()) {
            return nullptr;
        }
        const auto found = pots->second.find(guid);
        return found == pots->second.end() ? nullptr : found->second;
    }

    KCDC::Shared::Entities::CookPotEntity *CookPotService::Resolve(std::uint64_t guid, std::uint32_t world) {
        // Only a write naming a catalogued pot creates a world's pots: neither
        // a read nor an unknown GUID may replicate every pot into a new world.
        const auto known = std::ranges::any_of(_placements, [guid](const auto *placement) {
            return placement->guid == guid;
        });
        if (!known) {
            return nullptr;
        }
        auto &pots       = EnsureWorld(world);
        const auto found = pots.find(guid);
        return found == pots.end() ? nullptr : found->second;
    }

    nlohmann::json CookPotService::All(std::uint32_t world) const {
        auto result = nlohmann::json::array();
        if (const auto pots = _worlds.find(world); pots != _worlds.end()) {
            for (const auto &[guid, pot] : pots->second) {
                result.push_back(Describe(*pot));
            }
        }
        return result;
    }

    nlohmann::json CookPotService::Snapshot(std::uint64_t guid, std::uint32_t world) const {
        const auto *pot = Find(guid, world);
        return pot != nullptr ? Describe(*pot) : nlohmann::json(nullptr);
    }

    nlohmann::json CookPotService::Describe(const KCDC::Shared::Entities::CookPotEntity &pot) {
        const auto state   = pot.stock.Appearance();
        const auto *poison = KCDC::Shared::Game::FindBuffById(pot.stock.poison);
        return {{"food", KCDC::Shared::Game::CookPotFoodName(pot.stock.food)}, {"nutrition", KCDC::Shared::Game::g_CookPotNutrition[static_cast<int>(pot.stock.food)]}, {"poison", poison != nullptr ? nlohmann::json(poison->guid) : nlohmann::json(nullptr)},
            {"guid", KCDC::Scripting::FormatLevelGuid(pot.guid)}, {"portions", pot.stock.portions}, {"capacity", pot.stock.capacity}, {"enabled", pot.stock.enabled},
            {"state", state == KCDC::Shared::CookPot::State::Full   ? "full"
                      : state == KCDC::Shared::CookPot::State::Half ? "half"
                                                                    : "empty"},
            {"fireLit", pot.stock.fire == KCDC::Shared::CookPot::Fire::Native ? nlohmann::json(nullptr) : nlohmann::json(pot.stock.fire == KCDC::Shared::CookPot::Fire::Burning)}, {"virtualWorld", pot.GetVirtualWorld()},
            {"position", {{"x", pot.position.x}, {"y", pot.position.y}, {"z", pot.position.z}}}};
    }

    bool CookPotService::Refill(std::uint64_t guid, std::uint32_t world, std::uint16_t portions) {
        auto *pot = Resolve(guid, world);
        if (pot == nullptr || !pot->stock.Refill(portions)) {
            return false;
        }
        ++pot->revision;
        return true;
    }

    bool CookPotService::SetEnabled(std::uint64_t guid, std::uint32_t world, bool enabled) {
        // Not a stock change: a reservation made before it still refunds.
        auto *pot = Resolve(guid, world);
        if (pot == nullptr) {
            return false;
        }
        pot->stock.enabled = enabled;
        return true;
    }

    bool CookPotService::SetState(std::uint64_t guid, std::uint32_t world, KCDC::Shared::CookPot::State state) {
        // The halfway point needs the default capacity, so refill to it and
        // then draw the stock down to the requested level.
        auto *pot = Resolve(guid, world);
        if (pot == nullptr) {
            return false;
        }
        ++pot->revision;
        pot->stock.capacity = KCDC::Shared::CookPot::Stock::kDefaultPortions;
        pot->stock.portions = state == KCDC::Shared::CookPot::State::Full ? KCDC::Shared::CookPot::Stock::kDefaultPortions : state == KCDC::Shared::CookPot::State::Half ? KCDC::Shared::CookPot::Stock::kDefaultPortions / 2 : 0;
        return true;
    }

    bool CookPotService::SetFood(std::uint64_t guid, std::uint32_t world, KCDC::Shared::Game::CookPotFood food) {
        if (KCDC::Shared::Game::CookPotFoodName(food).empty()) {
            return false;
        }
        auto *pot = Resolve(guid, world);
        if (pot == nullptr) {
            return false;
        }
        // The stock is unchanged: a refused reservation still returns its
        // portion even when future meals use a different recipe.
        pot->stock.food = food;
        return true;
    }

    bool CookPotService::SetFire(std::uint64_t guid, std::uint32_t world, KCDC::Shared::CookPot::Fire fire) {
        auto *pot = Resolve(guid, world);
        if (pot == nullptr) {
            return false;
        }
        pot->stock.fire = fire;
        return true;
    }

    const char *CookPotService::CheckPlayer(KCDC::Shared::Entities::HumanEntity *player, std::uint64_t guid, KCDC::Shared::Entities::CookPotEntity *&pot) const {
        if (player == nullptr || player->ownerGUID == MafiaNet::UNASSIGNED_PEER_GUID || Framework::CoreModules::GetReplication()->GetViewer(player->ownerGUID) != player) {
            return "invalid_player";
        }
        pot = Find(guid, StockWorld(player->GetVirtualWorld()));
        if (pot == nullptr) {
            return "unknown_pot";
        }
        if (!pot->stock.enabled) {
            return "disabled";
        }
        if (!(glm::distance(player->position, pot->position) <= kReachMetres)) {
            return "too_far";
        }
        if (!player->soul.CanAct()) {
            return "cannot_act";
        }
        if (!Server::Get()->GetInventory().IsReady(player->GetNetworkID())) {
            return "inventory_not_ready";
        }
        return nullptr;
    }

    nlohmann::json CookPotService::RefillFromInventory(KCDC::Shared::Entities::HumanEntity *player, std::uint64_t guid, const nlohmann::json &recipe) {
        const auto refuse = [](const char *code) {
            return nlohmann::json {{"ok", false}, {"code", code}};
        };
        KCDC::Shared::Entities::CookPotEntity *pot = nullptr;
        if (const char *code = CheckPlayer(player, guid, pot)) {
            return refuse(code);
        }
        if (pot->stock.portions != 0) {
            return refuse("not_empty");
        }
        if (!recipe.is_object() || !recipe.contains("food") || !recipe["food"].is_string() || !recipe.contains("portions") || !recipe["portions"].is_number() || !recipe.contains("ingredients") || !recipe["ingredients"].is_array()) {
            return refuse("invalid_recipe");
        }
        const auto foodName = recipe["food"].get<std::string>();
        auto food           = KCDC::Shared::Game::CookPotFood::Goulash;
        if (foodName == "lentil") {
            food = KCDC::Shared::Game::CookPotFood::Lentil;
        }
        else if (foodName == "soup") {
            food = KCDC::Shared::Game::CookPotFood::Soup;
        }
        else if (foodName != "goulash") {
            return refuse("invalid_recipe");
        }
        const double portions   = recipe["portions"].get<double>();
        const auto &ingredients = recipe["ingredients"];
        if (!std::isfinite(portions) || portions < 1 || portions > KCDC::Shared::CookPot::Stock::kMaximumPortions || portions != std::floor(portions) || ingredients.empty() || ingredients.size() > 16) {
            return refuse("invalid_recipe");
        }
        std::map<std::string, std::uint32_t> required;
        for (const auto &ingredient : ingredients) {
            if (!ingredient.is_object() || !ingredient.contains("item") || !ingredient["item"].is_string() || !ingredient.contains("amount") || !ingredient["amount"].is_number()) {
                return refuse("invalid_recipe");
            }
            const auto *item    = KCDC::Features::Player::Players::ResolveItemClass(ingredient["item"].get<std::string>());
            const double amount = ingredient["amount"].get<double>();
            if (item == nullptr || item->category != "Food" || !std::isfinite(amount) || amount < 1 || amount > KCDC::Features::Inventory::Inventory::kMaximumAmount || amount != std::floor(amount)) {
                return refuse("invalid_recipe");
            }
            auto &total = required[std::string(item->guid)];
            if (amount > KCDC::Features::Inventory::Inventory::kMaximumAmount - total) {
                return refuse("invalid_recipe");
            }
            total += static_cast<std::uint32_t>(amount);
        }
        auto *inventory = Server::Get()->GetInventory().Find(player->GetNetworkID());
        std::vector<KCDC::Features::Inventory::InventoryUnit> units;
        for (const auto &[id, row] : inventory->Rows()) {
            auto found = required.find(row.itemClass);
            if (found == required.end() || found->second == 0 || row.metadata.value("health", 1.0) <= 0.0) {
                continue;
            }
            const auto amount = std::min(found->second, row.amount);
            units.push_back({id, amount});
            found->second -= amount;
        }
        if (std::ranges::any_of(required, [](const auto &entry) {
                return entry.second != 0;
            })) {
            return refuse("missing_ingredients");
        }
        const auto result = inventory->Remove(units, inventory->Revision(), "cook_pot_refill", true);
        if (!result.ok) {
            return {{"ok", false}, {"code", result.code}};
        }
        pot->stock.Refill(static_cast<std::uint16_t>(portions));
        pot->stock.food = food;
        ++pot->revision;
        return {{"ok", true}, {"code", "ok"}};
    }

    nlohmann::json CookPotService::Poison(KCDC::Shared::Entities::HumanEntity *player, std::uint64_t guid, const std::string &rowId) {
        const auto refuse = [](const char *code) {
            return nlohmann::json {{"ok", false}, {"code", code}};
        };
        KCDC::Shared::Entities::CookPotEntity *pot = nullptr;
        if (const char *code = CheckPlayer(player, guid, pot)) {
            return refuse(code);
        }
        if (pot->stock.portions == 0) {
            return refuse("empty");
        }
        auto *inventory = Server::Get()->GetInventory().Find(player->GetNetworkID());
        const auto row  = inventory->Rows().find(rowId);
        if (row == inventory->Rows().end()) {
            return refuse("unknown_item");
        }
        const auto poison = std::ranges::find_if(KCDC::Shared::Game::g_CookPotPoisons, [&](const auto &entry) {
            return entry.item == row->second.itemClass;
        });
        if (poison == KCDC::Shared::Game::g_CookPotPoisons.end() || KCDC::Shared::Game::FindBuffById(poison->buff) == nullptr) {
            return refuse("not_poison");
        }
        // An invalid row must not reveal whether this pot is contaminated.
        if (pot->stock.poison.IsValid()) {
            return refuse("already_poisoned");
        }
        const auto result = inventory->Remove({{rowId, 1}}, inventory->Revision(), "cook_pot_poison", true);
        if (!result.ok) {
            return {{"ok", false}, {"code", result.code}};
        }
        pot->stock.poison = poison->buff;
        // Poison changes future meals, but a refused earlier meal must still
        // return its portion. Only replacing the stock changes the revision.
        return {{"ok", true}, {"code", "ok"}};
    }

    void CookPotService::Receive(MafiaNet::PeerGuid peer, const KCDC::Shared::RPC::CookPotRequest &request) {
        auto *player       = Framework::CoreModules::GetReplication()->GetViewerAs<KCDC::Shared::Entities::HumanEntity>(peer);
        const auto context = _contexts.find(peer);
        if (player == nullptr || request.world != StockWorld(player->GetVirtualWorld()) || request.incarnation != player->soul.incarnation || context == _contexts.end() || request.contextRevision != context->second.revision) {
            return;
        }
        auto &meal     = _meals[player->GetNetworkID()];
        const auto now = MafiaNet::GetTimeUS() / 1000;
        if (request.completion) {
            if (meal.sequence != request.sequence || meal.guid != request.guid || meal.world != request.world || meal.incarnation != request.incarnation || meal.contextRevision != request.contextRevision || !meal.granted || meal.settled || now > meal.expires) {
                return;
            }
            meal.settled = true;
            if (request.eaten) {
                const auto playerId    = player->GetNetworkID();
                if (Server::Get()->GetInventory().RestoreGeneration(playerId) != meal.inventoryRestore) {
                    return;
                }
                const auto description = meal.description;
                const auto incarnation = meal.incarnation;
                const auto restore     = meal.inventoryRestore;
                if (const auto *buff = KCDC::Shared::Game::FindBuffById(meal.poison); buff != nullptr && meal.nativeEffects) {
                    if (KCDC::Scripting::PlayerPoisonAbsorbing(playerId, description, std::string(buff->name))) {
                        player = KCDC::Features::Player::Players::Find(playerId);
                        if (player != nullptr && player->soul.incarnation == incarnation && StockWorld(player->GetVirtualWorld()) == request.world && Server::Get()->GetInventory().RestoreGeneration(playerId) == restore) {
                            Server::Get()->GetBuffs().Apply(player, *buff, incarnation);
                        }
                    }
                }
                player = KCDC::Features::Player::Players::Find(playerId);
                if (player != nullptr && player->soul.incarnation == incarnation && StockWorld(player->GetVirtualWorld()) == request.world && Server::Get()->GetInventory().RestoreGeneration(playerId) == restore) {
                    KCDC::Scripting::PlayerConsumed(playerId, description);
                }
                return;
            }
            // A native refusal returns its reserved portion once. A disconnect
            // or missing receipt spends it: a lost acknowledgement must not
            // make an already eaten meal available again.
            auto *pot = request.eaten ? nullptr : Find(meal.guid, meal.world);
            if (pot != nullptr && pot->revision == meal.revision && pot->stock.portions < pot->stock.capacity) {
                ++pot->stock.portions;
            }
            return;
        }
        if (request.sequence <= meal.sequence || (meal.granted && !meal.settled && now <= meal.expires)) {
            return;
        }
        const auto nextUse   = meal.nextUse;
        meal                 = {};
        meal.sequence        = request.sequence;
        meal.guid            = request.guid;
        meal.expires         = now + kReservationMilliseconds;
        meal.nextUse         = nextUse;
        meal.world           = request.world;
        meal.incarnation     = request.incarnation;
        meal.contextRevision = request.contextRevision;
        auto *pot            = Find(request.guid, meal.world);
        if (now >= nextUse && pot != nullptr && glm::distance(player->position, pot->position) <= kReachMetres && player->soul.CanAct() && Server::Get()->GetInventory().IsReady(player->GetNetworkID())) {
            const auto playerId = player->GetNetworkID();
            const auto restore  = Server::Get()->GetInventory().RestoreGeneration(playerId);
            const auto revision = pot->revision;
            const auto stock    = pot->stock;
            const auto &item    = KCDC::Shared::Game::g_CookPotFoodItemClasses[static_cast<int>(stock.food)];
            nlohmann::json description {{"source", "cookPot"}, {"item", KCDC::Shared::Game::CryGuidToString(item.hipart, item.lopart)}, {"rowId", nullptr}, {"potGuid", KCDC::Scripting::FormatLevelGuid(pot->guid)}, {"metadata", nlohmann::json::object()},
                {"poison", stock.poison.IsValid() ? nlohmann::json(KCDC::Shared::Game::CryGuidToString(stock.poison.low, stock.poison.high)) : nlohmann::json(nullptr)}, {"nativeEffects", true}};
            KCDC::Scripting::DescribeConsumable(description);
            const bool accepted = stock.enabled && stock.portions != 0 && KCDC::Scripting::PlayerConsuming(playerId, description);
            const bool effects  = accepted && KCDC::Scripting::PlayerConsumptionEffects(playerId, description);
            player              = KCDC::Features::Player::Players::Find(playerId);
            if (player == nullptr || !_meals.contains(playerId)) {
                return;
            }
            const bool unchanged = player->soul.incarnation == request.incarnation && StockWorld(player->GetVirtualWorld()) == request.world && player->soul.CanAct() && glm::distance(player->position, pot->position) <= kReachMetres && pot->revision == revision
                                   && pot->stock == stock && Server::Get()->GetInventory().IsReady(playerId) && Server::Get()->GetInventory().RestoreGeneration(playerId) == restore;
            meal.inventoryRestore        = *restore;
            meal.nativeEffects           = effects;
            description["nativeEffects"] = effects;
            meal.description             = std::move(description);
            meal.granted                 = accepted && unchanged && pot->stock.Take();
            meal.revision = pot->revision;
            meal.poison   = pot->stock.poison;
            if (meal.granted) {
                meal.nextUse = now + kMealIntervalMilliseconds;
            }
        }
        KCDC::Shared::RPC::CookPotReply reply;
        reply.sequence = request.sequence;
        reply.guid     = request.guid;
        reply.granted  = meal.granted;
        reply.nativeEffects = meal.nativeEffects;
        if (pot != nullptr) {
            reply.food = pot->stock.food;
        }
        Framework::CoreModules::GetNetworkPeer()->SendRPC(reply, MafiaNet::ToGuid(peer), MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
    }

    void CookPotService::OnPlayerDisconnect(MafiaNet::PeerGuid peer) {
        _contexts.erase(peer);
        if (auto *player = Framework::CoreModules::GetReplication()->GetViewerAs<KCDC::Shared::Entities::HumanEntity>(peer)) {
            _meals.erase(player->GetNetworkID());
        }
    }
} // namespace KCDC::Features::CookPot
