#include "smithing_module.h"
#include "features/smart_object/smart_object_service.h"

#include "shared/features/crafting/smithing_native_ids.h"
#include "shared/game/crafting_catalog.h"
#include "shared/game/custom_recipe_registry.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace KCDC::Features::Crafting {
    namespace {
        constexpr const char *kReason = "smithing";

        bool Accessible(const SmithingActor &actor, const KCDC::Shared::Game::CraftStation &station) {
            const double x        = static_cast<double>(actor.position.x) - station.x;
            const double y        = static_cast<double>(actor.position.y) - station.y;
            const double z        = static_cast<double>(actor.position.z) - station.z;
            const double distance = x * x + y * y + z * z;
            return actor.canAct && actor.level == station.level && station.kind == KCDC::Shared::Game::CraftKind::Smithing && std::isfinite(distance) && distance <= SmithingModule::kUseDistance * SmithingModule::kUseDistance;
        }

        nlohmann::json Units(const std::vector<KCDC::Features::Inventory::InventoryUnit> &units) {
            nlohmann::json result = nlohmann::json::array();
            for (const auto &unit : units) {
                result.push_back({{"id", unit.id}, {"amount", unit.amount}});
            }
            return result;
        }

        /** The rows of one class, oldest first: row ids are decimal and only grow. */
        std::vector<const KCDC::Features::Inventory::InventoryRow *> RowsOf(const KCDC::Features::Inventory::Inventory &inventory, std::string_view itemClass) {
            std::vector<const KCDC::Features::Inventory::InventoryRow *> rows;
            for (const auto &[id, row] : inventory.Rows()) {
                if (row.itemClass == itemClass) {
                    rows.push_back(&row);
                }
            }
            std::sort(rows.begin(), rows.end(), [](const KCDC::Features::Inventory::InventoryRow *a, const KCDC::Features::Inventory::InventoryRow *b) {
                return a->id.size() != b->id.size() ? a->id.size() < b->id.size() : a->id < b->id;
            });
            return rows;
        }

        /** Units of a row the body does not wear. */
        std::uint32_t Spare(const KCDC::Features::Inventory::InventoryRow &row, const std::map<std::string, std::uint32_t> *equipped) {
            if (equipped == nullptr) {
                return row.amount;
            }
            const auto worn = equipped->find(row.id);
            return worn == equipped->end() ? row.amount : row.amount - std::min(row.amount, worn->second);
        }
    } // namespace

    std::string SmithingModule::Name(std::uint32_t session) {
        return "smithing-" + std::to_string(session);
    }

    std::optional<KCDC::Shared::RPC::SmithingCode> SmithingModule::Admissible(const SmithingActor &actor, const std::string &station, const KCDC::Shared::Game::CraftRecipe *recipe) const {
        if (!actor.character) {
            return KCDC::Shared::RPC::SmithingCode::CharacterUnavailable;
        }
        const auto *placement = KCDC::Features::SmartObject::SmartObjectService::Get().FindCraftStation(station);
        if (placement == nullptr || placement->kind != KCDC::Shared::Game::CraftKind::Smithing) {
            return KCDC::Shared::RPC::SmithingCode::UnknownStation;
        }
        if (!Accessible(actor, *placement)) {
            return KCDC::Shared::RPC::SmithingCode::NotAuthorized;
        }
        if (_settling.contains(actor.playerId) || _sessions.contains(actor.playerId) || (_hooks.busy && _hooks.busy(actor.playerId))) {
            return KCDC::Shared::RPC::SmithingCode::PlayerBusy;
        }
        if (_stations.contains({station, actor.virtualWorld})) {
            return KCDC::Shared::RPC::SmithingCode::StationBusy;
        }
        if (recipe == nullptr || !KCDC::Shared::Game::CraftDLCEnabled(recipe->dlc, actor.requiredDLC)) {
            return KCDC::Shared::RPC::SmithingCode::UnknownRecipe;
        }
        if (const auto refused = SmithingEvaluator::Admits(*recipe, *actor.character)) {
            return *refused == "recipeNotKnown" ? KCDC::Shared::RPC::SmithingCode::RecipeNotKnown : KCDC::Shared::RPC::SmithingCode::SkillTooLow;
        }
        if (actor.nowMs > std::numeric_limits<std::uint64_t>::max() - kSessionLifetimeMs) {
            return KCDC::Shared::RPC::SmithingCode::NotAuthorized;
        }
        return std::nullopt;
    }

    std::optional<std::vector<KCDC::Features::Inventory::InventoryUnit>> SmithingModule::Materials(std::uint64_t playerId, const KCDC::Features::Inventory::Inventory &inventory, const KCDC::Shared::Game::CraftRecipe &recipe) const {
        // A worn unit is never spent: the player takes off the sword they
        // mean to reforge.
        const auto *equipped = _hooks.equipped ? _hooks.equipped(playerId) : nullptr;
        std::vector<KCDC::Features::Inventory::InventoryUnit> units;
        for (const auto &material : recipe.ingredients) {
            const auto rule = KCDC::Shared::Game::CustomRecipeRegistry::Get().MaterialRule(material.item);
            if (!rule) {
                return std::nullopt;
            }
            std::uint32_t remaining = material.amount;
            for (const auto *row : RowsOf(inventory, material.item)) {
                const auto amount = std::min(remaining, Spare(*row, equipped));
                // A quest material has to be there, but the native debit skips it.
                if (amount != 0 && !rule->questItem) {
                    units.push_back({row->id, amount});
                }
                remaining -= amount;
                if (remaining == 0) {
                    break;
                }
            }
            if (remaining != 0) {
                return std::nullopt;
            }
        }
        return units;
    }

    std::uint32_t SmithingModule::Draw(std::uint32_t bound) {
        if (bound <= 1) {
            return 0;
        }
        if (_hooks.random) {
            return std::min(_hooks.random(bound), bound - 1);
        }
        // splitmix64: the server's own coin, independent of any game state.
        _generator += 0x9E3779B97F4A7C15ULL;
        std::uint64_t value = _generator;
        value               = (value ^ (value >> 30)) * 0xBF58476D1CE4E5B9ULL;
        value               = (value ^ (value >> 27)) * 0x94D049BB133111EBULL;
        value ^= value >> 31;
        return static_cast<std::uint32_t>(value % bound);
    }

    SmithingReply SmithingModule::Begin(const SmithingActor &actor, const std::string &station, const std::string &recipe) {
        const auto *entry = KCDC::Shared::Game::CustomRecipeRegistry::Get().Find(KCDC::Shared::Game::CraftKind::Smithing, recipe);
        if (const auto code = Admissible(actor, station, entry)) {
            return {*code, 0};
        }
        auto *inventory = _hooks.inventory ? _hooks.inventory(actor.playerId) : nullptr;
        if (inventory == nullptr) {
            return {KCDC::Shared::RPC::SmithingCode::InventoryUnavailable, 0};
        }
        if (!Materials(actor.playerId, *inventory, *entry)) {
            return {KCDC::Shared::RPC::SmithingCode::MissingMaterials, 0};
        }
        if (_hooks.starting) {
            const nlohmann::json proposal = {{"kind", "smithing"}, {"station", station}, {"virtualWorld", actor.virtualWorld}, {"recipe", recipe}, {"continuationOf", ""}};
            bool accepted                 = false;
            try {
                accepted = _hooks.starting(actor.playerId, proposal);
            }
            catch (...) {
            }
            if (!accepted) {
                return {KCDC::Shared::RPC::SmithingCode::StartingRejected, 0};
            }
        }
        // A handler may have taken the station, the materials or the player's
        // other craft; the actor itself was read before it ran.
        if (const auto code = Admissible(actor, station, entry)) {
            return {*code, 0};
        }
        inventory = _hooks.inventory ? _hooks.inventory(actor.playerId) : nullptr;
        if (inventory == nullptr) {
            return {KCDC::Shared::RPC::SmithingCode::InventoryUnavailable, 0};
        }
        const auto units = Materials(actor.playerId, *inventory, *entry);
        if (!units) {
            return {KCDC::Shared::RPC::SmithingCode::MissingMaterials, 0};
        }
        Session session;
        for (const auto &unit : *units) {
            const auto &row = inventory->Rows().at(unit.id);
            session.taken.push_back({row.itemClass, row.metadata, unit.amount});
        }
        if (!units->empty() && !inventory->Remove(*units, std::nullopt, kReason).ok) {
            return {KCDC::Shared::RPC::SmithingCode::MissingMaterials, 0};
        }
        session.id                               = ++_nextSession;
        session.station                          = station;
        session.virtualWorld                     = actor.virtualWorld;
        session.level                            = actor.level;
        session.requiredDLC                      = actor.requiredDLC;
        session.recipe                           = recipe;
        session.definition                       = entry;
        session.startedMs                        = actor.nowMs;
        session.deadlineMs                       = actor.nowMs + kSessionLifetimeMs;
        session.character                        = *actor.character;
        const auto id                            = session.id;
        _stations[{station, actor.virtualWorld}] = actor.playerId;
        _sessions[actor.playerId]                = std::move(session);
        _events.push_back({"craftingStarted", actor.playerId, {{"kind", "smithing"}, {"session", Name(id)}, {"station", station}, {"virtualWorld", actor.virtualWorld}, {"recipe", recipe}, {"continuationOf", ""}}});
        return {KCDC::Shared::RPC::SmithingCode::Accepted, id};
    }

    bool SmithingModule::Grant(std::uint64_t playerId, const SmithingProduct &product, std::uint32_t &lockpicks, nlohmann::json &outputs) {
        auto *inventory = _hooks.inventory ? _hooks.inventory(playerId) : nullptr;
        if (inventory == nullptr) {
            return false;
        }
        std::vector<KCDC::Features::Inventory::InventoryUnit> received;
        if (!product.questItem) {
            const auto added = inventory->Add(product.itemClass, 1, product.metadata, std::nullopt, kReason, true);
            if (!added.ok) {
                return false;
            }
            received.insert(received.end(), added.outputs.begin(), added.outputs.end());
        }
        if (lockpicks != 0) {
            const auto added = inventory->Add(KCDC::Shared::Crafting::SmithingNative::kLockpick, lockpicks, nlohmann::json::object(), std::nullopt, kReason, true);
            if (added.ok) {
                received.insert(received.end(), added.outputs.begin(), added.outputs.end());
            }
            else {
                lockpicks = 0;
            }
        }
        outputs = Units(received);
        return true;
    }

    SmithingReply SmithingModule::Finish(const SmithingActor &actor, std::uint32_t session, KCDC::Shared::RPC::SmithingOutcome outcome, float quality) {
        const auto found = _sessions.find(actor.playerId);
        if (found == _sessions.end() || found->second.id != session) {
            return {KCDC::Shared::RPC::SmithingCode::UnknownSession, session};
        }
        const auto &held = found->second;
        if (actor.nowMs >= held.deadlineMs) {
            Retire(actor.playerId, "cancelled", "timeout", Settlement::Failed);
            return {KCDC::Shared::RPC::SmithingCode::SessionExpired, session};
        }
        // The report can overtake Expire in the same tick: a player who has
        // since walked off, died or changed world gets what leaving gives.
        const auto *station = KCDC::Features::SmartObject::SmartObjectService::Get().FindCraftStation(held.station);
        if (station == nullptr || !Accessible(actor, *station) || actor.virtualWorld != held.virtualWorld || actor.requiredDLC != held.requiredDLC) {
            Retire(actor.playerId, "cancelled", "contextInvalidated", Settlement::Failed);
            return {KCDC::Shared::RPC::SmithingCode::NotAuthorized, session};
        }
        if (outcome == KCDC::Shared::RPC::SmithingOutcome::Abandoned) {
            if (actor.nowMs > held.startedMs + kAbandonWindowMs) {
                Retire(actor.playerId, "failed", "abandonTooLate", Settlement::Failed);
                return {KCDC::Shared::RPC::SmithingCode::AbandonTooLate, session};
            }
            Retire(actor.playerId, "cancelled", "abandoned", Settlement::Returned);
            return {KCDC::Shared::RPC::SmithingCode::Accepted, session};
        }
        if (outcome == KCDC::Shared::RPC::SmithingOutcome::Cancelled || outcome == KCDC::Shared::RPC::SmithingOutcome::Interrupted) {
            Retire(actor.playerId, "cancelled", outcome == KCDC::Shared::RPC::SmithingOutcome::Interrupted ? "interrupted" : "cancelled", Settlement::Failed);
            return {KCDC::Shared::RPC::SmithingCode::Accepted, session};
        }
        if (outcome == KCDC::Shared::RPC::SmithingOutcome::Failure) {
            Retire(actor.playerId, "failed", "craftingFailed", Settlement::Failed);
            return {KCDC::Shared::RPC::SmithingCode::Accepted, session};
        }
        const auto product = held.definition != nullptr ? SmithingEvaluator::Product(*held.definition, quality, held.character) : std::nullopt;
        if (!product) {
            Retire(actor.playerId, "failed", "invalidQuality", Settlement::Failed);
            return {KCDC::Shared::RPC::SmithingCode::InvalidQuality, session};
        }
        // RPG slot 73: 1..n lockpicks when the character's limit is positive.
        std::uint32_t lockpicks       = held.character.maximumLockpicks != 0 ? 1 + Draw(held.character.maximumLockpicks) : 0;
        const auto name               = Name(session);
        const nlohmann::json proposal = {{"kind", "smithing"}, {"session", name}, {"station", held.station}, {"outcome", "success"}, {"recipe", held.recipe}, {"grade", product->metadata.value("quality", 1U)}, {"product", product->questItem ? std::string() : product->itemClass},
            {"amount", product->questItem ? 0U : 1U}, {"quality", quality}, {"xp", product->xp}, {"lockpicks", lockpicks}};
        bool accepted                 = true;
        if (_hooks.completing) {
            try {
                accepted = _hooks.completing(actor.playerId, proposal);
            }
            catch (...) {
                accepted = false;
            }
        }
        // The handler may have ended this workpiece.
        const auto current = _sessions.find(actor.playerId);
        if (current == _sessions.end() || current->second.id != session) {
            return {KCDC::Shared::RPC::SmithingCode::UnknownSession, session};
        }
        if (!accepted) {
            Retire(actor.playerId, "failed", "craftingCompletingRejected", Settlement::Failed);
            return {KCDC::Shared::RPC::SmithingCode::CompletingRejected, session};
        }
        nlohmann::json outputs;
        if (!Grant(actor.playerId, *product, lockpicks, outputs)) {
            // The inventory had no room: the player keeps the materials.
            Retire(actor.playerId, "cancelled", "inventoryUnavailable", Settlement::Returned);
            return {KCDC::Shared::RPC::SmithingCode::InventoryUnavailable, session};
        }
        if (product->xp > 0.0F && _hooks.experience) {
            _hooks.experience(actor.playerId, static_cast<double>(product->xp));
        }
        _events.push_back({"craftingCompleted", actor.playerId,
            {{"kind", "smithing"}, {"session", name}, {"station", current->second.station}, {"virtualWorld", current->second.virtualWorld}, {"outcome", "success"}, {"recipe", current->second.recipe}, {"grade", proposal.at("grade")}, {"quality", quality}, {"xp", product->xp},
                {"lockpicks", lockpicks}, {"outputs", std::move(outputs)}}});
        Retire(actor.playerId, "success", "", Settlement::Spent);
        return {KCDC::Shared::RPC::SmithingCode::Accepted, session};
    }

    void SmithingModule::Retire(std::uint64_t playerId, const std::string &outcome, const std::string &reason, Settlement settlement) {
        const auto found = _sessions.find(playerId);
        if (found == _sessions.end()) {
            return;
        }
        // Out of the maps before anything reaches the inventory.
        auto session = std::move(found->second);
        _sessions.erase(found);
        _stations.erase({session.station, session.virtualWorld});
        // Held until the terminal event, also when an inventory write throws:
        // a leaked entry would refuse every later workpiece as PlayerBusy.
        struct SettlingScope {
            std::set<std::uint64_t> &players;
            std::uint64_t playerId;
            ~SettlingScope() {
                players.erase(playerId);
            }
        } settling {_settling, playerId};
        _settling.insert(playerId);
        if (settlement != Settlement::Spent && _hooks.refunding) {
            nlohmann::json materials = nlohmann::json::array();
            for (const auto &taken : session.taken) {
                materials.push_back({{"item", taken.itemClass}, {"amount", taken.amount}, {"metadata", taken.metadata}});
            }
            const nlohmann::json proposal = {{"kind", "smithing"}, {"session", Name(session.id)}, {"station", session.station}, {"virtualWorld", session.virtualWorld}, {"outcome", outcome}, {"reason", reason}, {"materials", std::move(materials)}};
            try {
                if (_hooks.refunding(playerId, proposal)) {
                    settlement = Settlement::Returned;
                }
            }
            catch (...) {
                // A failed policy leaves the native settlement in force.
            }
        }
        if (settlement == Settlement::Spent) {
            session.taken.clear();
        }
        const auto *recipe = session.definition;
        if (settlement == Settlement::Failed && recipe != nullptr) {
            // Per material, the native share is spent out of the units taken
            // for it, first rows first.
            for (const auto &material : recipe->ingredients) {
                const auto rule = KCDC::Shared::Game::CustomRecipeRegistry::Get().MaterialRule(material.item);
                if (!rule) {
                    continue;
                }
                std::uint32_t lost = SmithingEvaluator::FailureLoss(*rule, material.amount, [this]() {
                    return Draw(2) != 0;
                });
                for (auto &taken : session.taken) {
                    if (lost == 0) {
                        break;
                    }
                    if (taken.itemClass != material.item) {
                        continue;
                    }
                    const auto spent = std::min(lost, taken.amount);
                    taken.amount -= spent;
                    lost -= spent;
                }
            }
        }
        std::vector<KCDC::Features::Inventory::InventoryUnit> refunded;
        auto *inventory = _hooks.inventory ? _hooks.inventory(playerId) : nullptr;
        for (const auto &taken : session.taken) {
            if (taken.amount == 0 || inventory == nullptr) {
                continue;
            }
            const auto added = inventory->Add(taken.itemClass, taken.amount, taken.metadata, std::nullopt, kReason);
            if (added.ok) {
                refunded.insert(refunded.end(), added.outputs.begin(), added.outputs.end());
            }
            else {
                _unreturned.push_back({playerId, taken.itemClass, taken.amount, added.code});
            }
        }
        _events.push_back({"craftingEnded", playerId, {{"kind", "smithing"}, {"session", Name(session.id)}, {"station", session.station}, {"virtualWorld", session.virtualWorld}, {"outcome", outcome}, {"reason", reason}, {"refunded", Units(refunded)}}});
    }

    bool SmithingModule::End(std::uint64_t playerId, const std::string &reason) {
        if (!_sessions.contains(playerId)) {
            return false;
        }
        Retire(playerId, "cancelled", reason, Settlement::Failed);
        return true;
    }

    std::vector<SmithingClosure> SmithingModule::Expire(std::uint64_t nowMs, const fu2::function<std::optional<SmithingActor>(std::uint64_t playerId) const> &locate) {
        std::vector<SmithingClosure> closing;
        for (const auto &[playerId, session] : _sessions) {
            const auto actor    = locate(playerId);
            const auto *station = KCDC::Features::SmartObject::SmartObjectService::Get().FindCraftStation(session.station);
            if (nowMs >= session.deadlineMs) {
                closing.push_back({playerId, session.id, KCDC::Shared::RPC::SmithingCloseReason::Timeout});
            }
            else if (!actor || station == nullptr || !Accessible(*actor, *station) || actor->virtualWorld != session.virtualWorld || actor->requiredDLC != session.requiredDLC) {
                closing.push_back({playerId, session.id, KCDC::Shared::RPC::SmithingCloseReason::ContextInvalidated});
            }
        }
        for (const auto &closure : closing) {
            End(closure.playerId, closure.reason == KCDC::Shared::RPC::SmithingCloseReason::Timeout ? "timeout" : "contextInvalidated");
        }
        return closing;
    }

    std::optional<std::uint32_t> SmithingModule::SessionOf(std::uint64_t playerId) const {
        const auto found = _sessions.find(playerId);
        return found == _sessions.end() ? std::nullopt : std::optional<std::uint32_t>(found->second.id);
    }

    nlohmann::json SmithingModule::Inspect(std::uint64_t playerId, std::uint64_t nowMs) const {
        const auto found = _sessions.find(playerId);
        if (found == _sessions.end()) {
            return nullptr;
        }
        const auto &session  = found->second;
        nlohmann::json taken = nlohmann::json::array();
        for (const auto &unit : session.taken) {
            taken.push_back({{"item", unit.itemClass}, {"amount", unit.amount}});
        }
        return {{"kind", "smithing"}, {"id", Name(session.id)}, {"station", session.station}, {"virtualWorld", session.virtualWorld}, {"phase", "working"}, {"recipe", session.recipe}, {"materials", std::move(taken)},
            {"expiresInMs", session.deadlineMs > nowMs ? session.deadlineMs - nowMs : 0}};
    }

    nlohmann::json SmithingModule::Occupancy(const std::string &station, std::uint32_t virtualWorld, std::uint64_t nowMs) const {
        const auto *placement = KCDC::Features::SmartObject::SmartObjectService::Get().FindCraftStation(station);
        if (placement == nullptr || placement->kind != KCDC::Shared::Game::CraftKind::Smithing) {
            return nullptr;
        }
        nlohmann::json result = {{"station", station}, {"virtualWorld", virtualWorld}, {"occupied", false}, {"occupant", nullptr}};
        const auto occupant   = _stations.find({station, virtualWorld});
        if (occupant == _stations.end()) {
            return result;
        }
        const auto &session = _sessions.at(occupant->second);
        result["occupied"]  = true;
        result["occupant"]  = {{"playerId", occupant->second}, {"entryId", Name(session.id)}, {"sessionId", Name(session.id)}, {"phase", "working"}, {"expiresInMs", session.deadlineMs > nowMs ? session.deadlineMs - nowMs : 0}};
        return result;
    }

    std::vector<SmithingModule::Unreturned> SmithingModule::TakeUnreturned() {
        std::vector<Unreturned> unreturned;
        unreturned.swap(_unreturned);
        return unreturned;
    }

    std::vector<SmithingEvent> SmithingModule::TakeEvents() {
        std::vector<SmithingEvent> events;
        events.swap(_events);
        return events;
    }
} // namespace KCDC::Features::Crafting
