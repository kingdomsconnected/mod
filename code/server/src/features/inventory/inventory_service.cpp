#include "inventory_service.h"
#include "consumption_scripting.h"
#include "features/smart_object/smart_object_service.h"
#include "inventory_scripting.h"

#include "core/server.h"
#include "features/player/player_roster.h"
#include "shared/features/inventory/inventory_sync.h"
#include "shared/features/inventory/item_name_message.h"
#include "shared/game/cook_pot_catalog.h"
#include "shared/game/crafting_catalog.h"
#include "shared/game/custom_item_registry.h"
#include "shared/game/item_classes.h"
#include "shared/game/item_stats.h"
#include "shared/game/progression_catalog.h"
#include "shared/game/soul_rule_catalog.h"
#include "shared/inventory_item_properties.h"
#include "shared/item_stack.h"
#include "shared/scripting/level_guid.h"

#include <core_modules.h>
#include <logging/logger.h>
#include <networking/network_peer.h>

#include <mafianet/get_time.h>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <string_view>

namespace KCDC::Features::Inventory {
    namespace {
        constexpr const char *kLogger = "Inventory";

        // What one owner may have waiting between two ticks, and how many
        // wear reports one message may carry.
        constexpr std::size_t kMaximumPendingPerPeer = 64;
        constexpr std::size_t kMaximumWearClaims     = 64;
        constexpr std::size_t kMaximumClientBytes    = 16 * 1024;
        // Squared reach from a placed grindstone to the player sharpening on
        // it; the minigame seats them about 1.4 m from the entity.
        constexpr float kGrindstoneReachSquared = 3.0F * 3.0F;
        // Squared reach from a drying rack's, smokehouse's or cook pot's
        // placement to the player its animation aligned to it, the reach of a
        // cook pot's meal.
        constexpr float kPreparationReachSquared = 4.0F * 4.0F;

        std::uint64_t NowMs() {
            return MafiaNet::GetTimeUS() / 1000;
        }

        // Sharpening costs no item, so where the player stands is its proof.
        bool AtGrindstone(const glm::vec3 &position, MafiaNet::VirtualWorldId virtualWorld) {
            return KCDC::Features::SmartObject::SmartObjectService::Get().AtGrindstone(position, kGrindstoneReachSquared, virtualWorld);
        }

        // The `<level>:<guid>` of the station of `kind` nearest the player
        // whose game converted there, or empty when none is within reach: the
        // conversion names no station, so where the player stands is its
        // proof, as a grindstone's. Every fire a player can cook at is a cook
        // pot's fireplace: so_fireplace.xml's player_cooking runs on the
        // FireplaceSmartObjects the cook pot catalog lists, by their GUID.
        std::string PreparationStationAt(KCDC::Shared::Game::PreparationKind kind, const glm::vec3 &position, const std::string &level, MafiaNet::VirtualWorldId virtualWorld) {
            std::string nearest;
            float nearestSquared = kPreparationReachSquared;
            if (kind == KCDC::Shared::Game::PreparationKind::Cooking) {
                for (const auto &pot : KCDC::Shared::Game::g_CookPotPlacements) {
                    const glm::vec3 offset = position - pot.position;
                    if (pot.level == level && glm::dot(offset, offset) <= nearestSquared) {
                        nearest        = level + ":" + KCDC::Scripting::FormatLevelGuid(pot.guid);
                        nearestSquared = glm::dot(offset, offset);
                    }
                }
                return nearest;
            }
            // A drying rack or smokehouse the level places, unless a level edit took it out, or one a script placed.
            return KCDC::Features::SmartObject::SmartObjectService::Get().PreparationStationAt(kind, position, kPreparationReachSquared, virtualWorld);
        }

        // The survival XP the game gives per item it dries, smokes or cooks,
        // the S_Constants value C_RPGInventory::PrepareItems reads for the kind.
        float PreparationXpPerItem(KCDC::Shared::Game::PreparationKind kind) {
            const std::string_view name = kind == KCDC::Shared::Game::PreparationKind::Cooking ? "SurvivalXpRewardPerCookedItem" : kind == KCDC::Shared::Game::PreparationKind::Smoking ? "SurvivalXpRewardPerSmokedItem" : "SurvivalXpRewardPerDriedItem";
            return std::find_if(KCDC::Shared::Game::g_SoulRuleParameters.begin(), KCDC::Shared::Game::g_SoulRuleParameters.end(), [name](const KCDC::Shared::Game::SoulRuleParameter &parameter) {
                return parameter.name == name;
            })->value;
        }

        // The engine finds a key by the CRC of its lowercase spelling.
        std::string LowercaseNameKey(std::string_view key) {
            std::string lowered(key);
            std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char c) {
                return static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
            });
            return lowered;
        }

        nlohmann::json Result(const InventoryResult &result) {
            nlohmann::json items = nlohmann::json::array();
            for (const auto &unit : result.outputs) {
                items.push_back({{"id", unit.id}, {"amount", unit.amount}});
            }
            return {{"ok", result.ok}, {"code", result.code}, {"revision", result.revision}, {"items", std::move(items)}};
        }

        nlohmann::json Refused(const char *code) {
            return Result({false, code, 0, {}});
        }

        /** An optional expected revision; false when present but not a whole number. */
        bool ReadRevision(const nlohmann::json &request, const char *key, std::optional<std::uint64_t> &revision) {
            const auto found = request.find(key);
            if (found == request.end() || found->is_null()) {
                return true;
            }
            if (!found->is_number_unsigned()) {
                return false;
            }
            revision = found->get<std::uint64_t>();
            return true;
        }

        /** A request's optional `notify`, true when left out; false when it is given as anything but a boolean. */
        bool ReadNotify(const nlohmann::json &request, bool &notify) {
            const auto found = request.find("notify");
            notify           = true;
            if (found == request.end() || found->is_null()) {
                return true;
            }
            if (!found->is_boolean()) {
                return false;
            }
            notify = found->get<bool>();
            return true;
        }

        /** An amount scripts may name: a positive whole number up to `maximum`. */
        bool ReadAmount(const nlohmann::json &value, std::uint64_t maximum, std::uint32_t &amount) {
            if (!value.is_number_unsigned() || value.get<std::uint64_t>() == 0 || value.get<std::uint64_t>() > maximum) {
                return false;
            }
            amount = static_cast<std::uint32_t>(value.get<std::uint64_t>());
            return true;
        }

        /** Exact units: the code names the first problem, or is empty. */
        std::string ReadUnits(const nlohmann::json &request, std::vector<InventoryUnit> &units) {
            const auto found = request.find("units");
            if (found == request.end() || !found->is_array() || found->empty() || found->size() > Inventory::kMaximumRows) {
                return "invalidItems";
            }
            for (const auto &unit : *found) {
                if (!unit.is_object() || !unit.contains("id") || !unit.at("id").is_string() || !unit.contains("amount")) {
                    return "invalidItems";
                }
                InventoryUnit parsed {unit.at("id").get<std::string>(), 0};
                if (!ReadAmount(unit.at("amount"), Inventory::kMaximumAmount, parsed.amount)) {
                    return "invalidAmount";
                }
                units.push_back(std::move(parsed));
            }
            return {};
        }

        nlohmann::json EncodeEquipped(const std::map<std::string, std::uint32_t> &equipped) {
            nlohmann::json encoded = nlohmann::json::object();
            for (const auto &[id, amount] : equipped) {
                encoded[id] = amount;
            }
            return encoded;
        }

        const nlohmann::json &Metadata(const nlohmann::json &request) {
            static const nlohmann::json empty = nlohmann::json::object();
            const auto found                  = request.find("metadata");
            return found == request.end() || found->is_null() ? empty : *found;
        }
    } // namespace

    void InventoryService::Install() {
        RegisterInventoryHandlers(*this);
    }

    InventoryService::Session *InventoryService::SessionOf(std::uint64_t playerId) {
        const auto peer = _peers.find(playerId);
        if (peer == _peers.end()) {
            return nullptr;
        }
        const auto found = _sessions.find(peer->second);
        return found == _sessions.end() ? nullptr : &found->second;
    }

    const InventoryService::Session *InventoryService::SessionOf(std::uint64_t playerId) const {
        const auto peer = _peers.find(playerId);
        if (peer == _peers.end()) {
            return nullptr;
        }
        const auto found = _sessions.find(peer->second);
        return found == _sessions.end() ? nullptr : &found->second;
    }

    Inventory *InventoryService::Find(std::uint64_t playerId) {
        auto *session = SessionOf(playerId);
        return session == nullptr ? nullptr : &session->inventory;
    }

    const std::map<std::string, std::uint32_t> *InventoryService::Equipped(std::uint64_t playerId) const {
        const auto *session = SessionOf(playerId);
        return session == nullptr ? nullptr : &session->equipped;
    }

    bool InventoryService::IsReady(std::uint64_t playerId) const {
        const auto *session = SessionOf(playerId);
        return session != nullptr && session->projectionAccepted && session->acknowledgedRevision == session->inventory.Revision() && session->publishedRevision == session->inventory.Revision();
    }

    std::optional<std::uint64_t> InventoryService::RestoreGeneration(std::uint64_t playerId) const {
        const auto *session = SessionOf(playerId);
        return session != nullptr ? std::optional(session->equipmentRestore) : std::nullopt;
    }

    bool InventoryService::IsInitialized(std::uint64_t playerId) const {
        const auto *session = SessionOf(playerId);
        return session != nullptr && session->announced;
    }

    void InventoryService::OnPlayerConnect(MafiaNet::PeerGuid peer, std::uint64_t playerId) {
        _sessions.try_emplace(peer, _ids, playerId);
        _peers.insert_or_assign(playerId, peer);

        // Sent even when empty: it is what clears a previous server's names.
        auto *net = Framework::CoreModules::GetNetworkPeer();
        if (net == nullptr) {
            return;
        }
        KCDC::Shared::RPC::ItemNames table;
        table.replace = true;
        for (const auto &[key, name] : _itemNames) {
            table.entries.push_back({key, name});
        }
        net->SendRPC(table, MafiaNet::ToGuid(peer));

        // Likewise sent empty: it is what gives a previous server's numbers back.
        KCDC::Shared::RPC::ItemStats stats;
        stats.replace = true;
        for (const auto &[guid, values] : _itemStats) {
            stats.entries.push_back({KCDC::Shared::Game::ItemClassIdFromGuid(guid), values});
        }
        net->SendRPC(stats, MafiaNet::ToGuid(peer));
        PublishDefinitions();
    }

    bool InventoryService::SetItemName(const KCDC::Shared::Game::ItemClassEntry &item, const std::string &name) {
        if (item.nameKey.empty() || name.size() > KCDC::Shared::RPC::ItemNames::kMaximumNameBytes) {
            return false;
        }
        if (std::any_of(name.begin(), name.end(), [](unsigned char c) {
                return c < 0x20 || c == 0x7F;
            })) {
            return false;
        }
        std::string key = LowercaseNameKey(item.nameKey);
        if (name.empty()) {
            _itemNames.erase(key);
        }
        else {
            _itemNames.insert_or_assign(key, name);
        }

        auto *net = Framework::CoreModules::GetNetworkPeer();
        if (net == nullptr) {
            return true;
        }
        KCDC::Shared::RPC::ItemNames update;
        update.entries.push_back({std::move(key), name});
        net->BroadcastRPC(update);
        return true;
    }

    bool InventoryService::SetItemStats(const KCDC::Shared::Game::ItemClassEntry &item, const std::vector<KCDC::Shared::RPC::ItemStats::Value> &values) {
        if (!KCDC::Shared::Game::ItemCategoryHasStats(item.category)) {
            return false;
        }
        std::vector<KCDC::Shared::RPC::ItemStats::Value> accepted;
        for (const auto &value : values) {
            const auto stat = static_cast<KCDC::Shared::Game::ItemStat>(value.stat);
            if (value.stat >= KCDC::Shared::Game::kItemStatCount || !KCDC::Shared::Game::ItemStatAppliesTo(item.category, stat) || !KCDC::Shared::Game::ItemStatAccepts(stat, value.value)) {
                return false;
            }
            // One value per stat; the last spelling wins, as it does in an object literal.
            std::erase_if(accepted, [&value](const auto &other) { return other.stat == value.stat; });
            accepted.push_back({value.stat, KCDC::Shared::Game::ItemStatIsIntegral(stat) ? std::round(value.value) : value.value});
        }

        const std::string guid(item.guid);
        if (accepted.empty()) {
            if (_itemStats.erase(guid) == 0) {
                return true;
            }
        }
        else {
            _itemStats.insert_or_assign(guid, accepted);
        }

        auto *net = Framework::CoreModules::GetNetworkPeer();
        if (net == nullptr) {
            return true;
        }
        KCDC::Shared::RPC::ItemStats update;
        update.entries.push_back({KCDC::Shared::Game::ItemClassIdFromGuid(guid), std::move(accepted)});
        net->BroadcastRPC(update);
        return true;
    }

    const std::vector<KCDC::Shared::RPC::ItemStats::Value> *InventoryService::ItemStatsOf(const KCDC::Shared::Game::ItemClassEntry &item) const {
        const auto found = _itemStats.find(item.guid);
        return found != _itemStats.end() ? &found->second : nullptr;
    }

    void InventoryService::OnPlayerReady(MafiaNet::PeerGuid peer) {
        const auto found = _sessions.find(peer);
        if (found == _sessions.end()) {
            return;
        }
        found->second.ready  = true;
        found->second.resync = true;
    }

    void InventoryService::OnPlayerDisconnect(MafiaNet::PeerGuid peer) {
        const auto found = _sessions.find(peer);
        if (found == _sessions.end()) {
            return;
        }
        _peers.erase(found->second.playerId);
        _sessions.erase(found);
        std::scoped_lock lock(_incomingMutex);
        std::erase_if(_incoming, [peer](const Incoming &entry) {
            return entry.peer == peer;
        });
    }

    void InventoryService::Queue(MafiaNet::PeerGuid peer, MessageKind kind, const KCDC::Shared::RPC::InventoryMessage &message) {
        if (!message.valid || message.body.size() > kMaximumClientBytes) {
            return;
        }
        std::scoped_lock lock(_incomingMutex);
        const auto pending = std::count_if(_incoming.begin(), _incoming.end(), [peer](const Incoming &entry) {
            return entry.peer == peer;
        });
        if (static_cast<std::size_t>(pending) < kMaximumPendingPerPeer) {
            _incoming.push_back({peer, kind, message.body});
        }
    }

    void InventoryService::Receive(const KCDC::Shared::RPC::InventoryApplied &message, MafiaNet::PeerGuid peer) {
        Queue(peer, MessageKind::Applied, message);
    }

    void InventoryService::Receive(const KCDC::Shared::RPC::InventoryWear &message, MafiaNet::PeerGuid peer) {
        Queue(peer, MessageKind::Wear, message);
    }

    void InventoryService::Receive(const KCDC::Shared::RPC::InventoryConsume &message, MafiaNet::PeerGuid peer) {
        Queue(peer, MessageKind::Consume, message);
    }

    void InventoryService::Receive(const KCDC::Shared::RPC::InventoryEquipment &message, MafiaNet::PeerGuid peer) {
        Queue(peer, MessageKind::Equipment, message);
    }

    void InventoryService::Receive(const KCDC::Shared::RPC::InventoryRecover &message, MafiaNet::PeerGuid peer) {
        Queue(peer, MessageKind::Recover, message);
    }

    void InventoryService::Applied(Session &session, const nlohmann::json &message) {
        if (message.value("resync", false)) {
            session.resync = true;
            return;
        }
        const auto revision = message.find("revision");
        if (revision == message.end() || !revision->is_number_unsigned() || revision->get<std::uint64_t>() != session.publishedRevision) {
            return;
        }
        if (!message.contains("definitionRevision") || !message.at("definitionRevision").is_number_unsigned() || message.at("definitionRevision").get<std::uint64_t>() != session.registryRevision) {
            return;
        }
        if (!message.value("applied", false)) {
            session.projectionAccepted = false;
            if (const auto failure = message.find("definitionError"); failure != message.end() && failure->is_string()) {
                Framework::Logging::GetLogger(kLogger)->warn("Player {} item registration failed: {}", session.playerId, failure->get<std::string>().substr(0, 512));
            }
            // The client retries this revision with a bounded delay. Do not
            // acknowledge a partial native inventory as the complete revision.
            Framework::Logging::GetLogger(kLogger)->warn("Player {} could not project inventory revision {}", session.playerId, revision->get<std::uint64_t>());
            return;
        }
        // The rest of the inventory is shown; these rows the owner's game
        // could not make, and their client logs why. The revision still
        // counts as acknowledged: the client spends only from stacks it
        // shows, so a row it left out cannot be used, and one bad row must
        // not hold every gather, pickup, stash and trade. Every
        // acknowledgement names them again, so only a row that newly failed
        // is logged.
        std::set<std::string> unshown;
        if (const auto names = message.find("unshown"); names != message.end() && names->is_array()) {
            for (const auto &id : *names) {
                const auto row = id.is_string() ? session.published.find(id.get<std::string>()) : session.published.end();
                if (row == session.published.end()) {
                    continue;
                }
                if (!session.unshown.contains(row->first)) {
                    Framework::Logging::GetLogger(kLogger)->warn("Player {} could not show row {} ({}) of inventory revision {}", session.playerId, row->first, row->second.value("item", std::string()), revision->get<std::uint64_t>());
                }
                unshown.insert(row->first);
            }
        }
        session.unshown              = std::move(unshown);
        session.projectionAccepted   = true;
        session.acknowledgedRevision = revision->get<std::uint64_t>();
        if (!session.announced) {
            session.announced = true;
            KCDC::Scripting::EmitPlayerInventoryReady(session.playerId);
        }
    }

    void InventoryService::Wear(Session &session, const nlohmann::json &message) {
        const auto claims = message.find("claims");
        if (claims == message.end() || !claims->is_array() || claims->size() > kMaximumWearClaims) {
            return;
        }
        for (const auto &claim : *claims) {
            if (!claim.is_object() || !claim.contains("id") || !claim.at("id").is_string() || !claim.contains("metadata")) {
                continue;
            }
            const auto result = session.inventory.Wear(claim.at("id").get<std::string>(), claim.at("metadata"));
            if (!result.ok) {
                Framework::Logging::GetLogger(kLogger)->debug("Player {} wear report on row {} refused: {}", session.playerId, claim.at("id").get<std::string>(), result.code);
            }
        }
    }

    void InventoryService::AnswerConsume(const Session &session, const std::string &request, bool ok, const std::string &code) const {
        if (!ok) {
            Framework::Logging::GetLogger(kLogger)->debug("Player {} consumption {} refused: {}", session.playerId, request, code);
        }
        // The answer names the revision the owner must reach before its
        // projection moves again: a refused spend comes back with the sync.
        KCDC::Shared::RPC::InventoryConsumeResult answer;
        answer.body = nlohmann::json({{"request", request}, {"ok", ok}, {"code", code}, {"revision", session.inventory.Revision()}}).dump();
        Framework::CoreModules::GetNetworkPeer()->SendRPC(answer, MafiaNet::ToGuid(_peers.at(session.playerId)), MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
    }

    void InventoryService::Consume(Session &session, const nlohmann::json &message) {
        if (message.is_object() && message.contains("kind") && (message.at("kind") == "foodRequest" || message.at("kind") == "foodReceipt")) {
            UseFood(session, message);
            return;
        }
        if (message.is_object() && message.contains("kind") && message.at("kind") == "repair") {
            std::string request;
            std::vector<InventoryRepairPart> kits;
            std::vector<InventoryRepairPart> equipment;
            if (!InventoryGameplay::ReadRepair(message, request, kits, equipment)) {
                AnswerConsume(session, message.value("request", std::string()), false, "invalidRequest");
                return;
            }
            const auto result = session.inventory.Repair(kits, equipment);
            AnswerConsume(session, request, result.ok, result.code);
            if (!result.ok) {
                Framework::Logging::GetLogger(kLogger)->debug("Player {} repair refused: {}", session.playerId, result.code);
            }
            return;
        }
        if (message.is_object() && message.contains("kind") && (message.at("kind") == "dry" || message.at("kind") == "smoke" || message.at("kind") == "cook")) {
            Prepare(session, message);
            return;
        }
        if (message.is_object() && message.contains("kind") && message.at("kind") == "sharpen") {
            std::string request;
            InventoryRepairPart weapon;
            if (!InventoryGameplay::ReadSharpen(message, request, weapon)) {
                AnswerConsume(session, message.value("request", std::string()), false, "invalidRequest");
                return;
            }
            const auto *human = KCDC::Features::Player::Players::Find(session.playerId);
            const auto result = human == nullptr || !human->soul.CanAct() || !AtGrindstone(human->position, human->GetVirtualWorld()) ? InventoryResult {false, "notAtGrindstone"} : session.inventory.Sharpen(weapon);
            AnswerConsume(session, request, result.ok, result.code);
            return;
        }
        std::string request;
        InventoryConsumption kind {};
        std::vector<InventoryUnit> units;
        if (!InventoryGameplay::ReadConsume(message, request, kind, units)) {
            AnswerConsume(session, message.value("request", std::string()), false, "invalidRequest");
            return;
        }
        // Food must have a server reservation; legacy post-use reports cannot
        // bypass the pre-consumption decision.
        if (kind == InventoryConsumption::Use && std::any_of(units.begin(), units.end(), [&](const auto &unit) {
                const auto row   = session.inventory.Rows().find(unit.id);
                const auto *item = row != session.inventory.Rows().end() ? KCDC::Shared::Game::FindItemClass(row->second.itemClass) : nullptr;
                return item != nullptr && item->category == "Food";
            })) {
            AnswerConsume(session, request, false, "reservationRequired");
            return;
        }
        std::vector<InventoryRow> spent;
        const auto result = InventoryGameplay::Consume(session.inventory, kind, session.loaded, units, &spent);
        AnswerConsume(session, request, result.ok, result.code);
        if (!result.ok) {
            Framework::Logging::GetLogger(kLogger)->debug("Player {} spend refused: {}", session.playerId, result.code);
            return;
        }
        if (kind == InventoryConsumption::Shot) {
            const auto *shooter       = KCDC::Features::Player::Players::Find(session.playerId);
            const std::uint32_t world = shooter != nullptr ? shooter->GetVirtualWorld() : 0;
            for (const auto &round : spent) {
                session.spent.Fire(round, round.amount, world, NowMs());
            }
        }
        for (const auto &row : spent) {
            _itemUsed.push_back({session.playerId, row.itemClass, row.amount});
        }
    }

    void InventoryService::Prepare(Session &session, const nlohmann::json &message) {
        std::string request;
        KCDC::Shared::Game::PreparationKind kind {};
        std::vector<InventoryUnit> units;
        if (!InventoryGameplay::ReadPreparation(message, request, kind, units)) {
            AnswerConsume(session, message.value("request", std::string()), false, "invalidRequest");
            return;
        }
        const auto *player        = KCDC::Features::Player::Players::Find(session.playerId);
        const std::string station = player != nullptr && player->soul.CanAct() ? PreparationStationAt(kind, player->position, Server::Get()->GetModConfig().Get<std::string>("level"), player->GetVirtualWorld()) : std::string();
        if (station.empty()) {
            AnswerConsume(session, request, false, "notAtStation");
            return;
        }
        const auto products = InventoryGameplay::Prepared(session.inventory, kind, units);
        if (products.empty()) {
            AnswerConsume(session, request, false, "notPreparable");
            return;
        }
        const std::string reason = kind == KCDC::Shared::Game::PreparationKind::Cooking ? "cook" : kind == KCDC::Shared::Game::PreparationKind::Smoking ? "smoke" : "dry";
        nlohmann::json items     = nlohmann::json::array();
        for (std::size_t index = 0; index < units.size(); ++index) {
            items.push_back({{"id", units[index].id}, {"item", session.inventory.Rows().at(units[index].id).itemClass}, {"amount", units[index].amount}, {"product", products[index].itemClass}});
        }
        const auto playerId = session.playerId;
        const auto revision = session.inventory.Revision();
        const bool accepted = KCDC::Scripting::PlayerPreparing(playerId, {{"kind", reason}, {"station", station}, {"items", std::move(items)}});
        // A synchronous handler can change the inventory or disconnect the player.
        if (SessionOf(playerId) != &session) {
            return;
        }
        if (!accepted) {
            AnswerConsume(session, request, false, "playerPreparingRejected");
            return;
        }
        const auto result = session.inventory.Exchange(units, products, revision, reason);
        AnswerConsume(session, request, result.ok, result.code);
        // The game gave its survival XP with the conversion; the owner's game
        // held it back, and it lands only with a conversion that did.
        auto *human = KCDC::Features::Player::Players::Find(playerId);
        if (result.ok && human != nullptr) {
            std::uint64_t converted = 0;
            for (const auto &unit : units) {
                converted += unit.amount;
            }
            Server::Get()->GetProgression().AddXp(human, static_cast<std::uint8_t>(KCDC::Shared::Game::FindProgressionTrack("survival")), PreparationXpPerItem(kind) * static_cast<double>(converted), true);
        }
    }

    void InventoryService::UseFood(Session &session, const nlohmann::json &message) {
        if (!message.contains("request") || !message.at("request").is_string()) {
            return;
        }
        const auto request     = message.at("request").get<std::string>();
        std::uint64_t sequence = 0;
        const auto parsed      = std::from_chars(request.data(), request.data() + request.size(), sequence);
        if (parsed.ec != std::errc() || parsed.ptr != request.data() + request.size() || sequence == 0) {
            return;
        }
        auto *player   = KCDC::Features::Player::Players::Find(session.playerId);
        auto &use      = session.foodUse;
        const auto now = NowMs();
        if (message.at("kind") == "foodReceipt") {
            if (!use.pending || sequence != use.sequence || !message.contains("eaten") || !message.at("eaten").is_boolean()) {
                return;
            }
            use.pending = false;
            if (now > use.expires || session.equipmentRestore != use.restore) {
                return;
            }
            if (!message.at("eaten").get<bool>()) {
                use.refundPending = true;
                RefundFood(session);
                return;
            }
            if (player == nullptr || player->soul.incarnation != use.incarnation || player->GetVirtualWorld() != use.world) {
                return;
            }
            const auto playerId    = session.playerId;
            const auto description = use.description;
            _itemUsed.push_back({playerId, use.row.itemClass, 1});
            KCDC::Scripting::PlayerConsumed(playerId, description);
            return;
        }
        if (sequence <= use.sequence) {
            return;
        }
        if (use.refundPending || (use.pending && now <= use.expires)) {
            AnswerConsume(session, request, false, "usePending");
            return;
        }
        use          = {};
        use.sequence = sequence;
        if (player == nullptr || !player->soul.CanAct() || !IsReady(session.playerId) || !message.contains("revision") || !message.at("revision").is_number_unsigned() || message.at("revision") != session.inventory.Revision() || !message.contains("incarnation")
            || !message.at("incarnation").is_number_unsigned() || message.at("incarnation") != player->soul.incarnation || !message.contains("world") || !message.at("world").is_number_unsigned() || message.at("world") != player->GetVirtualWorld() || !message.contains("rowId")
            || !message.at("rowId").is_string()) {
            AnswerConsume(session, request, false, "invalidContext");
            return;
        }
        const auto found = session.inventory.Rows().find(message.at("rowId").get<std::string>());
        const auto *item = found != session.inventory.Rows().end() ? KCDC::Shared::Game::FindItemClass(found->second.itemClass) : nullptr;
        if (item == nullptr || item->category != "Food" || found->second.amount == 0 || session.unshown.contains(found->first)) {
            AnswerConsume(session, request, false, "invalidItem");
            return;
        }
        const auto row         = found->second;
        const auto playerId    = session.playerId;
        const auto revision    = session.inventory.Revision();
        const auto incarnation = player->soul.incarnation;
        const auto world       = player->GetVirtualWorld();
        const auto restore     = session.equipmentRestore;
        nlohmann::json description {{"source", "inventory"}, {"item", row.itemClass}, {"rowId", row.id}, {"potGuid", nullptr}, {"metadata", row.metadata}, {"poison", nullptr}, {"nativeEffects", true}};
        KCDC::Scripting::DescribeConsumable(description);
        const bool accepted = KCDC::Scripting::PlayerConsuming(playerId, description);
        const bool effects  = accepted && KCDC::Scripting::PlayerConsumptionEffects(playerId, description);
        // A synchronous handler can restore inventory, teleport or disconnect.
        player = KCDC::Features::Player::Players::Find(playerId);
        if (SessionOf(playerId) != &session || player == nullptr) {
            return;
        }
        if (!accepted || player->soul.incarnation != incarnation || player->GetVirtualWorld() != world || !player->soul.CanAct() || session.equipmentRestore != restore || session.inventory.Revision() != revision) {
            AnswerConsume(session, request, false, accepted ? "changedContext" : "cancelled");
            return;
        }
        const auto result = session.inventory.Remove({{row.id, 1}}, revision, "use");
        if (!result.ok) {
            AnswerConsume(session, request, false, result.code);
            return;
        }
        description["nativeEffects"] = effects;
        use.pending                  = true;
        use.expires                  = now + 10000;
        use.incarnation              = incarnation;
        use.world                    = world;
        use.restore                  = restore;
        use.row                      = row;
        use.description              = std::move(description);
        KCDC::Shared::RPC::InventoryConsumeResult answer;
        answer.body = nlohmann::json({{"request", request}, {"ok", true}, {"code", ""}, {"revision", result.revision}, {"nativeEffects", effects}}).dump();
        Framework::CoreModules::GetNetworkPeer()->SendRPC(answer, MafiaNet::ToGuid(_peers.at(playerId)), MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
    }

    void InventoryService::RefundFood(Session &session) {
        auto &use = session.foodUse;
        if (!use.refundPending) {
            return;
        }
        // An explicit restore supersedes the old inventory, including its
        // reservations. Otherwise retain a refund until capacity is available.
        if (session.equipmentRestore != use.restore || session.inventory.Add(use.row.itemClass, 1, use.row.metadata, std::nullopt, "use_refund").ok) {
            use.refundPending = false;
        }
    }

    void InventoryService::Wears(Session &session, std::map<std::string, std::uint32_t> equipped) {
        equipped                 = InventoryGameplay::CountEquipment(session.inventory, equipped);
        session.equipmentChanged = session.equipmentChanged || session.equipped != equipped;
        session.equipped         = std::move(equipped);
        for (const auto &[id, amount] : session.equipped) {
            if (InventoryGameplay::IsConsumableClass(session.inventory.Rows().at(id).itemClass, InventoryConsumption::Shot)) {
                session.loaded.insert(id);
            }
        }
    }

    void InventoryService::Equipment(Session &session, const nlohmann::json &message) {
        std::uint64_t revision = 0;
        std::map<std::string, std::uint32_t> items;
        if (!InventoryGameplay::ReadEquipment(message, revision, items, session.equipmentRestore) || revision < session.equipmentRevision) {
            return;
        }
        std::optional<Shared::Inventory::Quickslots> quickslots;
        if (const auto slots = message.find("quickslots"); slots != message.end()) {
            quickslots.emplace();
            if (!Shared::Inventory::Quickslots::Read(*slots, *quickslots) || !quickslots->ReferencesExist([&](const std::string &id) {
                    return session.inventory.Rows().contains(id);
                })) {
                return;
            }
        }
        Wears(session, std::move(items));
        if (quickslots) {
            session.equipmentChanged = session.equipmentChanged || session.quickslots != quickslots;
            session.quickslots       = std::move(quickslots);
        }
    }

    void InventoryService::Recover(Session &session, const nlohmann::json &message) {
        AmmunitionRecovery recovery;
        if (!SpentRounds::ReadRecovery(message, recovery)) {
            return;
        }
        const auto *shooter = KCDC::Features::Player::Players::Find(session.playerId);
        if (shooter == nullptr || !SpentRounds::WithinReach(shooter->position, recovery.position)) {
            return;
        }
        const std::uint32_t world = shooter->GetVirtualWorld();
        const auto *round         = session.spent.Find(recovery.fired, world, NowMs());
        if (round == nullptr) {
            return;
        }
        // A round that broke on impact comes back as the broken round, with
        // none of the whole one's properties; a whole one whose properties no
        // longer apply comes back as its class.
        KCDC::Shared::RPC::InventoryItem stack;
        stack.inventoryItemId = 1;
        stack.itemClass       = KCDC::Shared::Game::ItemClassIdFromGuid(recovery.landed);
        stack.amount          = 1;
        if (recovery.landed == round->itemClass) {
            auto whole = stack;
            if (KCDC::Shared::Inventory::ApplyItemProperties(round->metadata, whole)) {
                stack = whole;
            }
        }
        // Checked before the round is taken, so a stack that cannot go down
        // leaves the round to be found again.
        auto &ground = Server::Get()->GetGroundItems();
        if (!ground.CanPlaceFor(session.playerId, stack)) {
            return;
        }
        const auto playerId = session.playerId;
        session.spent.Claim(recovery.fired, world, NowMs());
        ground.PlaceFor(playerId, stack, recovery.position, recovery.rotation, world, recovery.nativeItemWuid);
    }

    bool InventoryService::Lodge(std::uint64_t shooterId, const std::string &fired, Inventory *target) {
        auto *session       = SessionOf(shooterId);
        const auto *shooter = KCDC::Features::Player::Players::Find(shooterId);
        if (session == nullptr || shooter == nullptr || target == nullptr) {
            return false;
        }
        const std::uint32_t world = shooter->GetVirtualWorld();
        const auto *round         = session->spent.Find(fired, world, NowMs());
        // Added before the round is taken, so a target that refuses it leaves
        // the round to be found again.
        if (round == nullptr || !target->Add(round->itemClass, 1, round->metadata, std::nullopt, "lodged").ok) {
            return false;
        }
        session->spent.Claim(fired, world, NowMs());
        return true;
    }

    void InventoryService::PublishDefinitions() {
        auto *net = Framework::CoreModules::GetNetworkPeer();
        if (net == nullptr) {
            return;
        }
        const auto &registry = KCDC::Shared::Game::CustomItemRegistry::Get();
        for (auto &[peer, session] : _sessions) {
            if (session.registryRevision == registry.Revision()) {
                continue;
            }
            // Small ordered envelopes avoid a registry-size-dependent packet.
            KCDC::Shared::RPC::CustomItemDefinitions message;
            message.body = nlohmann::json {{"reset", !session.registryRevision.has_value()}, {"begin", true}}.dump();
            net->SendRPC(message, MafiaNet::ToGuid(peer), MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
            for (const auto &[guid, definition] : registry.Definitions()) {
                if (session.registryRevision && definition.registeredAt <= *session.registryRevision) {
                    continue;
                }
                message.body = nlohmann::json {{"id", definition.id}, {"definition", definition.Encode()}}.dump();
                net->SendRPC(message, MafiaNet::ToGuid(peer), MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
            }
            message.body = nlohmann::json {{"end", true}, {"revision", registry.Revision()}}.dump();
            net->SendRPC(message, MafiaNet::ToGuid(peer), MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
            session.projectionAccepted = false;
            session.registryRevision   = registry.Revision();
        }
    }

    void InventoryService::Receive(const KCDC::Shared::RPC::CustomItemUse &message, MafiaNet::PeerGuid peer) {
        Queue(peer, MessageKind::CustomUse, message);
    }

    void InventoryService::CustomUse(Session &session, const nlohmann::json &message) {
        if (!message.contains("id") || !message.at("id").is_string() || !message.contains("revision") || !message.at("revision").is_number_unsigned()) {
            return;
        }
        const auto *row = session.inventory.ValidateCustomUse(message.at("id").get<std::string>(), message.at("revision").get<std::uint64_t>());
        if (row != nullptr && IsReady(session.playerId)) {
            _customUses.push_back({session.playerId, Inventory::EncodeRow(*row), session.inventory.Revision()});
        }
    }

    void InventoryService::Publish(Session &session) {
        std::map<std::string, nlohmann::json> current;
        for (const auto &[id, row] : session.inventory.Rows()) {
            current.emplace(id, nlohmann::json {{"item", row.itemClass}, {"amount", row.amount}, {"metadata", KCDC::Shared::Game::CustomItemRegistry::Get().PublicMetadata(row.metadata)}});
        }
        const std::optional<std::uint64_t> base = session.resync ? std::nullopt : session.publishedRevision;
        KCDC::Shared::RPC::InventorySync message;
        auto body                 = KCDC::Shared::Inventory::EncodeSync(base, session.inventory.Revision(), session.published, current);
        body["equipped"]          = EncodeEquipped(session.equipped);
        body["equipmentRestore"]  = session.equipmentRestore;
        if (session.quickslots) {
            body["quickslots"] = session.quickslots->Encode();
        }
        if (const auto *player = KCDC::Features::Player::Players::Find(session.playerId)) {
            session.publishedWorld = player->GetVirtualWorld();
            body["world"]          = *session.publishedWorld;
        }
        // What changed before the owner's game first showed the inventory is
        // what they load in with, not news.
        const auto notices = session.inventory.TakeNotices();
        if (session.publishedRevision && !notices.empty()) {
            body["notices"] = KCDC::Shared::Inventory::EncodeNotices(notices);
        }
        message.body              = body.dump();
        session.published         = std::move(current);
        session.publishedRevision = session.inventory.Revision();
        session.resync            = false;
        Framework::CoreModules::GetNetworkPeer()->SendRPC(message, MafiaNet::ToGuid(_peers.at(session.playerId)), MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
    }

    void InventoryService::DispatchChanges() {
        // Drained before any handler runs: a handler that changes an inventory
        // queues its own change for the next dispatch instead of re-entering.
        std::vector<std::pair<std::uint64_t, InventoryChange>> changes;
        for (auto &[peer, session] : _sessions) {
            RefundFood(session);
            for (auto &change : session.inventory.TakeChanges()) {
                changes.emplace_back(session.playerId, std::move(change));
            }
            if (session.equipmentChanged) {
                changes.emplace_back(session.playerId, InventoryChange {session.inventory.Revision(), "equipment", {}});
                session.equipmentChanged = false;
            }
        }
        for (const auto &[playerId, change] : changes) {
            KCDC::Scripting::EmitPlayerInventoryChanged(playerId, Inventory::EncodeChange(change));
        }
    }

    void InventoryService::Update() {
        std::vector<Incoming> incoming;
        {
            std::scoped_lock lock(_incomingMutex);
            incoming.swap(_incoming);
        }
        const auto now = NowMs();
        std::vector<MafiaNet::PeerGuid> flooding;
        for (const auto &entry : incoming) {
            auto session = _sessions.find(entry.peer);
            if (session == _sessions.end()) {
                continue;
            }
            if (!session->second.flood.Take(now)) {
                flooding.push_back(entry.peer);
                continue;
            }
            const auto message = nlohmann::json::parse(entry.body, nullptr, false);
            if (!session->second.soft.Take(now)) {
                // A spend the owner is holding its projection for is answered
                // even when it is not looked at, so the item comes back at once.
                if (entry.kind == MessageKind::Consume && message.is_object()) {
                    AnswerConsume(session->second, message.value("request", std::string()), false, "rateLimited");
                }
                continue;
            }
            if (!message.is_object()) {
                continue;
            }
            switch (entry.kind) {
            case MessageKind::CustomUse: CustomUse(session->second, message); break;
            case MessageKind::Applied: Applied(session->second, message); break;
            case MessageKind::Wear: Wear(session->second, message); break;
            case MessageKind::Consume: Consume(session->second, message); break;
            case MessageKind::Equipment: Equipment(session->second, message); break;
            case MessageKind::Recover: Recover(session->second, message); break;
            }
        }
        for (auto &[peer, session] : _sessions) {
            if (session.flooding) {
                flooding.push_back(peer);
            }
        }
        for (const auto peer : flooding) {
            if (_sessions.contains(peer)) {
                Framework::Logging::GetLogger(kLogger)->warn("Disconnecting a client that floods inventory messages");
                Framework::CoreModules::GetNetworkPeer()->KickPlayer(MafiaNet::ToGuid(peer), Framework::Networking::DisconnectionReason::KICKED_CUSTOM, "Too many inventory messages.");
            }
        }

        DispatchChanges();
        std::vector<CustomUseEvent> customUses;
        customUses.swap(_customUses);
        for (const auto &event : customUses) {
            const auto *inventory = Find(event.playerId);
            if (inventory != nullptr && inventory->Revision() == event.revision) {
                KCDC::Scripting::EmitPlayerCustomItemUse(event.playerId, event.row, event.revision);
            }
        }
        PublishDefinitions();
        std::vector<ItemUsed> used;
        used.swap(_itemUsed);
        for (const auto &event : used) {
            KCDC::Scripting::EmitPlayerItemUsed(event.playerId, event.itemClass, InventoryGameplay::UseKind(event.itemClass), event.amount);
        }

        for (auto &[peer, session] : _sessions) {
            if (session.clampedRevision != session.inventory.Revision()) {
                InventoryGameplay::ClampEquipment(session.inventory, session.equipped);
                if (session.quickslots) {
                    session.quickslots->ForEachReference([&](std::string &id) {
                        if (!id.empty() && !session.inventory.Rows().contains(id)) {
                            id.clear();
                        }
                    });
                }
                std::erase_if(session.loaded, [&session](const std::string &id) {
                    return !session.inventory.Rows().contains(id);
                });
                session.clampedRevision = session.inventory.Revision();
            }
            const auto *player      = KCDC::Features::Player::Players::Find(session.playerId);
            const bool worldChanged = player != nullptr && session.publishedWorld != player->GetVirtualWorld();
            if (session.ready && (session.resync || session.publishedRevision != session.inventory.Revision() || worldChanged)) {
                Publish(session);
            }
        }
    }

    void InventoryService::Reset() {
        _sessions.clear();
        _peers.clear();
        _itemNames.clear();
        _customUses.clear();
        KCDC::Shared::Game::CustomItemRegistry::Get().Reset();
        std::scoped_lock lock(_incomingMutex);
        _incoming.clear();
    }

    InventoryResult InventoryService::Grant(std::uint64_t playerId, const std::string &itemClass, std::uint32_t amount) {
        auto *inventory = Find(playerId);
        if (inventory == nullptr) {
            return {false, "inventoryUnavailable"};
        }
        if (amount > KCDC::Shared::RPC::kMaxGiveAmount) {
            return {false, "invalidAmount"};
        }
        return inventory->Add(itemClass, amount, nlohmann::json::object(), std::nullopt, "add", true);
    }

    InventoryResult InventoryService::Take(std::uint64_t playerId, const std::string &itemClass, std::uint32_t amount) {
        auto *inventory = Find(playerId);
        if (inventory == nullptr) {
            return {false, "inventoryUnavailable"};
        }
        if (amount > KCDC::Shared::RPC::kMaxGiveAmount) {
            return {false, "invalidAmount"};
        }
        return inventory->Take(itemClass, amount, std::nullopt, "remove", true);
    }

    bool InventoryService::Admit(std::uint64_t playerId) {
        auto *session = SessionOf(playerId);
        if (session == nullptr) {
            return false;
        }
        // Requests other services admit count against the same buckets as
        // the inventory's own messages; a kick waits for Update, outside the
        // caller's handler.
        const auto now = NowMs();
        if (!session->flood.Take(now)) {
            session->flooding = true;
            return false;
        }
        return session->soft.Take(now);
    }

    nlohmann::json InventoryService::EncodeInventory(const Inventory &inventory) {
        nlohmann::json items = nlohmann::json::array();
        for (const auto &[id, row] : inventory.Rows()) {
            items.push_back(Inventory::EncodeRow(row));
        }
        return {{"revision", inventory.Revision()}, {"items", std::move(items)}};
    }

    nlohmann::json InventoryService::ScriptGet(std::uint64_t playerId) const {
        const auto *session = SessionOf(playerId);
        if (session == nullptr) {
            return nullptr;
        }
        auto result = EncodeInventory(session->inventory);
        for (auto &row : result["items"]) {
            const auto worn = session->equipped.find(row.at("id").get<std::string>());
            row["equipped"] = worn == session->equipped.end() ? 0 : worn->second;
        }
        result["ready"] = IsReady(playerId);
        auto quickslots = session->quickslots;
        if (quickslots) {
            quickslots->ForEachReference([&](std::string &id) {
                if (!id.empty() && !session->inventory.Rows().contains(id)) {
                    id.clear();
                }
            });
        }
        result["quickslots"] = quickslots ? quickslots->Encode() : nlohmann::json(nullptr);
        return result;
    }

    nlohmann::json InventoryService::ScriptAdd(Inventory *inventory, const nlohmann::json &request, bool notify) {
        if (inventory == nullptr) {
            return Refused("inventoryUnavailable");
        }
        std::optional<std::uint64_t> revision;
        if (!ReadRevision(request, "revision", revision) || (request.contains("notify") && !ReadNotify(request, notify)) || !request.contains("item") || !request.at("item").is_string() || !request.contains("amount") || !Metadata(request).is_object()) {
            return Refused("invalidRequest");
        }
        std::uint32_t amount = 0;
        if (!ReadAmount(request.at("amount"), KCDC::Shared::RPC::kMaxGiveAmount, amount)) {
            return Refused("invalidAmount");
        }
        // The resolver `giveItem` and `GroundItem.spawn` take, so one spelling of an item works everywhere.
        const auto *item = KCDC::Features::Player::Players::ResolveItemClass(request.at("item").get<std::string>());
        if (item == nullptr) {
            return Refused("invalidItem");
        }
        return Result(inventory->Add(std::string(item->guid), amount, Metadata(request), revision, "add", notify));
    }

    nlohmann::json InventoryService::ScriptAdd(std::uint64_t playerId, const nlohmann::json &request) {
        bool notify = true;
        if (!ReadNotify(request, notify)) {
            return Refused("invalidRequest");
        }
        return ScriptAdd(Find(playerId), request, notify);
    }

    nlohmann::json InventoryService::ScriptRemove(Inventory *inventory, const nlohmann::json &request, bool notify) {
        if (inventory == nullptr) {
            return Refused("inventoryUnavailable");
        }
        std::optional<std::uint64_t> revision;
        if (!ReadRevision(request, "revision", revision) || (request.contains("notify") && !ReadNotify(request, notify))) {
            return Refused("invalidRequest");
        }
        std::vector<InventoryUnit> units;
        if (const auto code = ReadUnits(request, units); !code.empty()) {
            return Refused(code.c_str());
        }
        return Result(inventory->Remove(units, revision, "remove", notify));
    }

    nlohmann::json InventoryService::ScriptRemove(std::uint64_t playerId, const nlohmann::json &request) {
        bool notify = true;
        if (!ReadNotify(request, notify)) {
            return Refused("invalidRequest");
        }
        return ScriptRemove(Find(playerId), request, notify);
    }

    nlohmann::json InventoryService::ScriptUpdateCustom(std::uint64_t playerId, const nlohmann::json &request) {
        auto *inventory = Find(playerId);
        if (inventory == nullptr) {
            return Refused("inventoryUnavailable");
        }
        std::optional<std::uint64_t> revision;
        if (!ReadRevision(request, "revision", revision) || !request.contains("id") || !request.at("id").is_string()) {
            return Refused("invalidRequest");
        }
        auto patch = request;
        patch.erase("id");
        patch.erase("revision");
        return Result(inventory->UpdateCustom(request.at("id").get<std::string>(), patch, revision));
    }

    nlohmann::json InventoryService::ScriptSetProperties(std::uint64_t playerId, const nlohmann::json &request) {
        return ScriptSetProperties(Find(playerId), request);
    }

    nlohmann::json InventoryService::ScriptSetProperties(Inventory *inventory, const nlohmann::json &request) {
        if (inventory == nullptr) {
            return Refused("inventoryUnavailable");
        }
        std::optional<std::uint64_t> revision;
        if (!ReadRevision(request, "revision", revision) || !request.contains("id") || !request.at("id").is_string() || !Metadata(request).is_object()) {
            return Refused("invalidRequest");
        }
        return Result(inventory->SetProperties(request.at("id").get<std::string>(), Metadata(request), revision));
    }

    nlohmann::json InventoryService::ScriptSet(Inventory *inventory, const nlohmann::json &request) {
        if (inventory == nullptr) {
            return Refused("inventoryUnavailable");
        }
        std::optional<std::uint64_t> revision;
        const auto items = request.find("items");
        if (!ReadRevision(request, "revision", revision) || items == request.end() || !items->is_array()) {
            return Refused("invalidRequest");
        }
        if (items->size() > Inventory::kMaximumRows) {
            return Refused("inventoryCapacity");
        }
        std::vector<InventoryRow> rows;
        for (const auto &item : *items) {
            if (!item.is_object() || !item.contains("item") || !item.at("item").is_string() || !item.contains("amount") || (item.contains("id") && !item.at("id").is_string()) || !Metadata(item).is_object()) {
                return Refused("invalidRequest");
            }
            // A row names its class as `Inventory.add` takes it: a GUID or the exact table name.
            const auto *itemClass = KCDC::Features::Player::Players::ResolveItemClass(item.at("item").get<std::string>());
            if (itemClass == nullptr) {
                return Refused("invalidItem");
            }
            InventoryRow row {item.value("id", std::string()), std::string(itemClass->guid), 0, Metadata(item)};
            if (!ReadAmount(item.at("amount"), Inventory::kMaximumAmount, row.amount)) {
                return Refused("invalidAmount");
            }
            rows.push_back(std::move(row));
        }
        return Result(inventory->Set(rows, revision));
    }

    nlohmann::json InventoryService::ScriptWearOutfit(std::uint64_t playerId, const KCDC::Shared::Game::ClothingPresetEntry &preset) {
        nlohmann::json current = ScriptGet(playerId);
        if (current.is_null()) {
            return Refused("inventoryUnavailable");
        }
        nlohmann::json items = nlohmann::json::array();
        for (const auto &row : current.at("items")) {
            const bool clothing = InventoryGameplay::IsClothingClass(row.at("item").get<std::string>());
            items.push_back({{"id", row.at("id")}, {"item", row.at("item")}, {"amount", row.at("amount")}, {"metadata", row.at("metadata")}, {"equipped", clothing ? 0U : row.at("equipped").get<std::uint32_t>()}});
        }
        // A preset also names the weapons and tools its wearer carries; only
        // what goes on the body is handed out.
        for (const std::string_view itemGuid : preset.itemGuids) {
            const std::string itemClass(itemGuid);
            if (InventoryGameplay::IsClothingClass(itemClass)) {
                items.push_back({{"item", itemClass}, {"amount", 1U}, {"metadata", nlohmann::json::object()}, {"equipped", 1U}});
            }
        }
        return ScriptSet(playerId, {{"items", std::move(items)}, {"revision", current.at("revision")}, {"quickslots", current.at("quickslots")}});
    }

    nlohmann::json InventoryService::ScriptSet(std::uint64_t playerId, const nlohmann::json &request) {
        auto *session = SessionOf(playerId);
        if (session == nullptr) {
            return Refused("inventoryUnavailable");
        }
        std::optional<Shared::Inventory::Quickslots> quickslots;
        if (const auto slots = request.find("quickslots"); slots != request.end() && !slots->is_null()) {
            quickslots.emplace();
            if (!Shared::Inventory::Quickslots::Read(*slots, *quickslots) || !request.contains("items") || !request.at("items").is_array()) {
                return Refused("invalidQuickslots");
            }
            std::set<std::string> ids;
            for (const auto &row : request.at("items")) {
                if (row.is_object() && row.contains("id") && row.at("id").is_string()) {
                    ids.insert(row.at("id").get<std::string>());
                }
            }
            if (!quickslots->ReferencesExist([&](const std::string &id) {
                    return ids.contains(id);
                })) {
                return Refused("invalidQuickslots");
            }
        }
        // What a restored row says the body wears, checked before anything changes.
        std::vector<std::uint32_t> worn;
        std::size_t wornRows = 0;
        const auto items     = request.find("items");
        if (items != request.end() && items->is_array()) {
            for (const auto &item : *items) {
                const auto equipped = item.is_object() ? item.find("equipped") : item.end();
                if (!item.is_object() || equipped == item.end() || equipped->is_null()) {
                    worn.push_back(0);
                    continue;
                }
                if (!equipped->is_number_unsigned() || !item.contains("amount") || !item.at("amount").is_number_unsigned() || equipped->get<std::uint64_t>() > item.at("amount").get<std::uint64_t>()) {
                    return Refused("invalidEquipment");
                }
                if (equipped->get<std::uint64_t>() != 0) {
                    // The row names its class as `Inventory.add` takes it: a GUID or the table name.
                    const auto *worn = item.contains("item") && item.at("item").is_string() ? KCDC::Features::Player::Players::ResolveItemClass(item.at("item").get<std::string>()) : nullptr;
                    if (worn == nullptr || !InventoryGameplay::IsEquippableClass(std::string(worn->guid))) {
                        return Refused("invalidEquipment");
                    }
                }
                wornRows += equipped->get<std::uint64_t>() != 0 ? 1 : 0;
                worn.push_back(static_cast<std::uint32_t>(equipped->get<std::uint64_t>()));
            }
        }
        if (wornRows > InventoryGameplay::kMaximumEquippedRows) {
            return Refused("invalidEquipment");
        }
        auto result = ScriptSet(&session->inventory, request);
        if (!result.at("ok").get<bool>()) {
            return result;
        }
        // `set` answers with every row it wrote, in the order it was given them.
        std::map<std::string, std::uint32_t> equipped;
        const auto &written = result.at("items");
        for (std::size_t index = 0; index < worn.size() && index < written.size(); ++index) {
            if (worn[index] != 0) {
                equipped[written[index].at("id").get<std::string>()] = worn[index];
            }
        }
        Wears(*session, std::move(equipped));
        session->equipmentChanged  = session->equipmentChanged || session->quickslots != quickslots;
        session->quickslots        = std::move(quickslots);
        session->equipmentRevision = session->inventory.Revision();
        ++session->equipmentRestore;
        // An equipment-only restore can keep the inventory revision unchanged.
        session->resync = true;
        return result;
    }

    nlohmann::json InventoryService::ScriptTransfer(std::uint64_t sourceId, std::uint64_t targetId, const nlohmann::json &request) {
        return ScriptTransfer(Find(sourceId), Find(targetId), request);
    }

    nlohmann::json InventoryService::ScriptTransfer(Inventory *source, Inventory *target, const nlohmann::json &request) {
        if (source == nullptr || target == nullptr) {
            return Refused("inventoryUnavailable");
        }
        std::optional<std::uint64_t> sourceRevision, targetRevision;
        bool notify = true;
        if (!ReadRevision(request, "sourceRevision", sourceRevision) || !ReadRevision(request, "targetRevision", targetRevision) || !ReadNotify(request, notify)) {
            return Refused("invalidRequest");
        }
        std::vector<InventoryUnit> units;
        if (const auto code = ReadUnits(request, units); !code.empty()) {
            return Refused(code.c_str());
        }
        return Result(Inventory::Transfer(*source, *target, units, sourceRevision, targetRevision, "transfer", notify));
    }
} // namespace KCDC::Features::Inventory
