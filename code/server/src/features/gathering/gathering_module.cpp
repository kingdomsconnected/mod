#include "gathering_module.h"

#include "shared/features/gathering/gathering_rules.h"
#include "shared/game/gathering_catalog.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace KCDC::Features::Gathering {
    namespace {
        // A source list the game could not have produced is refused whole.
        constexpr std::size_t kMaximumReportedSources = 128;
        // The game compares plants in the plane; a slope still moves them.
        constexpr float kMatchHeight = 1.5F;

        bool ReadPosition(const nlohmann::json &value, glm::vec3 &position) {
            if (!value.is_array() || value.size() != 3) {
                return false;
            }
            for (const auto &component : value) {
                if (!component.is_number()) {
                    return false;
                }
            }
            position = glm::vec3(value.at(0).get<float>(), value.at(1).get<float>(), value.at(2).get<float>());
            return std::isfinite(position.x) && std::isfinite(position.y) && std::isfinite(position.z);
        }

        bool SamePlant(const glm::vec3 &a, const glm::vec3 &b) {
            return KCDC::Shared::Gathering::InHarvestRadius(a.x - b.x, a.y - b.y, GatheringModule::kMatchRadius) && std::abs(a.z - b.z) <= kMatchHeight;
        }
    } // namespace

    void GatheringModule::SetSources(const std::vector<KCDC::Shared::Game::GatheringSource> &sources) {
        _sources.clear();
        for (const auto &source : sources) {
            _sources[CellOf(0, source.position)].push_back(source);
        }
    }

    std::vector<const KCDC::Shared::Game::GatheringSource *> GatheringModule::SourcesInArea(const KCDC::Features::Area::AreaService &areas, const KCDC::Features::Area::AreaRecord &area, std::uint32_t virtualWorld) const {
        std::vector<const KCDC::Shared::Game::GatheringSource *> found;
        if (area.destroying || (area.definition.virtualWorld && *area.definition.virtualWorld != MafiaNet::VIRTUAL_WORLD_GLOBAL && *area.definition.virtualWorld != virtualWorld)) {
            return found;
        }
        const auto low = CellOf(0, area.boundsMin), high = CellOf(0, area.boundsMax);
        for (auto cell = _sources.lower_bound({0, low.x, std::numeric_limits<std::int32_t>::min()}); cell != _sources.end() && cell->first.x <= high.x; ++cell) {
            if (cell->first.y < low.y || cell->first.y > high.y) {
                continue;
            }
            for (const auto &source : cell->second) {
                if (areas.Contains(area, source.position)) {
                    found.push_back(&source);
                }
            }
        }
        return found;
    }

    std::vector<KCDC::Shared::Game::GatheringSource> GatheringModule::GetHerbs(const KCDC::Features::Area::AreaService &areas, const KCDC::Features::Area::AreaRecord &area, std::uint32_t virtualWorld, std::uint64_t worldMs) const {
        std::vector<KCDC::Shared::Game::GatheringSource> available;
        for (const auto *source : SourcesInArea(areas, area, virtualWorld)) {
            if (FindDepleted(virtualWorld, source->kind, source->position, worldMs, true) == nullptr) {
                available.push_back(*source);
            }
        }
        return available;
    }

    std::vector<GatheringPoint> GatheringModule::RemoveHerbs(const KCDC::Features::Area::AreaService &areas, const KCDC::Features::Area::AreaRecord &area, std::uint32_t virtualWorld, std::uint64_t worldMs) {
        std::vector<GatheringPoint> removed;
        for (const auto *source : SourcesInArea(areas, area, virtualWorld)) {
            const auto *existing = FindDepleted(virtualWorld, source->kind, source->position, worldMs, true);
            if (existing != nullptr && existing->removed) {
                continue;
            }
            // Keep an already-picked plant's exact identity for client correction.
            GatheringPoint point {source->kind, existing != nullptr ? existing->position : source->position, 0, ++_sequence, true};
            Deplete(virtualWorld, point);
            removed.push_back(point);
        }
        return removed;
    }

    std::vector<GatheringPoint> GatheringModule::RespawnHerbs(const KCDC::Features::Area::AreaService &areas, const KCDC::Features::Area::AreaRecord &area, std::uint32_t virtualWorld, std::uint64_t worldMs) {
        std::vector<GatheringPoint> restored;
        if (area.destroying || (area.definition.virtualWorld && *area.definition.virtualWorld != MafiaNet::VIRTUAL_WORLD_GLOBAL && *area.definition.virtualWorld != virtualWorld)) {
            return restored;
        }
        for (auto cell = _depleted.begin(); cell != _depleted.end();) {
            if (cell->first.virtualWorld != virtualWorld) {
                ++cell;
                continue;
            }
            std::erase_if(cell->second, [&](const auto &entry) {
                const auto &point = entry.second;
                if (!areas.Contains(area, point.position)) {
                    return false;
                }
                if (point.until == 0 || point.until > worldMs) {
                    restored.push_back(point);
                }
                return true;
            });
            cell = cell->second.empty() ? _depleted.erase(cell) : std::next(cell);
        }
        return restored;
    }

    bool GatheringModule::ReadRequest(const std::string &body, GatheringRequest &request) {
        const auto parsed = nlohmann::json::parse(body, nullptr, false);
        if (!parsed.is_object() || !parsed.contains("action") || !parsed.at("action").is_string()) {
            return false;
        }
        GatheringRequest read;
        read.action = parsed.at("action").get<std::string>();
        if (read.action == "sync") {
            if (parsed.contains("full")) {
                if (!parsed.at("full").is_boolean()) {
                    return false;
                }
                read.full = parsed.at("full").get<bool>();
            }
            if (parsed.contains("syncGeneration")) {
                if (!parsed.at("syncGeneration").is_number_unsigned()) {
                    return false;
                }
                read.syncGeneration = parsed.at("syncGeneration").get<std::uint64_t>();
            }
            request = std::move(read);
            return true;
        }
        if ((read.action != "begin" && read.action != "complete") || !parsed.contains("id") || !parsed.at("id").is_string()) {
            return false;
        }
        read.id = parsed.at("id").get<std::string>();
        if (read.id.empty() || read.id.size() > 32) {
            return false;
        }
        if (read.action == "begin") {
            if (!parsed.contains("kind") || !parsed.at("kind").is_number_unsigned() || parsed.at("kind").get<std::uint64_t>() > 255 || !parsed.contains("position") || !ReadPosition(parsed.at("position"), read.position)) {
                return false;
            }
            read.kind = static_cast<std::uint8_t>(parsed.at("kind").get<std::uint64_t>());
        }
        else if (parsed.contains("sources")) {
            const auto &sources = parsed.at("sources");
            if (!sources.is_array() || sources.size() > kMaximumReportedSources) {
                return false;
            }
            for (const auto &source : sources) {
                glm::vec3 position;
                if (!ReadPosition(source, position)) {
                    return false;
                }
                read.sources.push_back(position);
            }
        }
        request = std::move(read);
        return true;
    }

    std::string GatheringModule::Begin(std::uint64_t playerId, const GatheringRequest &request, const glm::vec3 &player, std::uint32_t virtualWorld, std::uint64_t nowMs, std::uint64_t worldMs) {
        auto &state = _players[playerId];
        if (!state.limit.Take(nowMs)) {
            return "rateLimited";
        }
        const auto *kind = KCDC::Shared::Game::FindGatheringKind(request.kind);
        if (kind == nullptr || kind->respawnHours <= 0.0F) {
            return "unknownKind";
        }
        if (!KCDC::Shared::Gathering::WithinDistance(player.x, player.y, player.z, request.position.x, request.position.y, request.position.z, kReach)) {
            return "tooFar";
        }
        if (IsDepleted(virtualWorld, request.kind, request.position, worldMs)) {
            return "depleted";
        }
        state.attempt = Attempt {request.id, request, virtualWorld, nowMs};
        return {};
    }

    GatheringHarvest GatheringModule::Complete(std::uint64_t playerId, const GatheringRequest &completion, const glm::vec3 &player, std::uint32_t virtualWorld, bool canAct, std::uint64_t nowMs, std::uint64_t worldMs) {
        auto found = _players.find(playerId);
        if (found == _players.end() || !found->second.attempt || found->second.attempt->id != completion.id) {
            return {"unknownAttempt"};
        }
        const auto attempt = *found->second.attempt;
        const auto elapsed = nowMs - attempt.startedMs;
        if (nowMs >= attempt.startedMs && elapsed < kMinimumMs) {
            return {"tooEarly"};
        }
        found->second.attempt.reset();
        if (nowMs < attempt.startedMs || elapsed > kMaximumMs) {
            return {"expired"};
        }
        const auto &source = attempt.request.position;
        if (virtualWorld != attempt.virtualWorld || !KCDC::Shared::Gathering::WithinDistance(player.x, player.y, player.z, source.x, source.y, source.z, kReach)) {
            return {"tooFar"};
        }
        if (!canAct) {
            return {"cannotAct"};
        }
        if (const auto *depleted = FindDepleted(virtualWorld, attempt.request.kind, source, worldMs)) {
            return {"depleted", attempt.request.kind, 0, source, {}, {*depleted}};
        }
        const auto *kind = KCDC::Shared::Game::FindGatheringKind(attempt.request.kind);
        const auto level = _hooks.survivalLevel(playerId);
        if (!level) {
            return {"progressionUnavailable"};
        }
        auto *inventory = _hooks.inventory(playerId);
        if (inventory == nullptr) {
            return {"inventoryUnavailable"};
        }

        // The game harvests every available same-kind plant within the
        // player's radius (HarvestArea 0x182ADDCF0). Ordinary harvesting
        // counts the sources the owner names, but only within
        // the radius it derives from the player's own level, one per plant,
        // never one already picked, and never more than the cap.
        const auto survival = std::clamp(*level, 1.0F, KCDC::Shared::Gathering::kMaximumSurvivalLevel);
        const auto radius   = KCDC::Shared::Gathering::Radius(survival);
        std::vector<glm::vec3> counted {source};
        for (const auto &candidate : completion.sources) {
            if (counted.size() >= kMaximumSources) {
                break;
            }
            if (!KCDC::Shared::Gathering::InHarvestRadius(candidate.x - source.x, candidate.y - source.y, radius) || IsDepleted(virtualWorld, attempt.request.kind, candidate, worldMs)) {
                continue;
            }
            if (std::none_of(counted.begin(), counted.end(), [&](const glm::vec3 &plant) {
                    return SamePlant(plant, candidate);
                })) {
                counted.push_back(candidate);
            }
        }

        const auto amount = KCDC::Shared::Gathering::CalculateYield(survival, static_cast<std::uint32_t>(counted.size()), kind->amount);
        const nlohmann::json proposal {{"item", kind->itemClass}, {"amount", amount}, {"kind", kind->id}, {"position", {source.x, source.y, source.z}}, {"virtualWorld", virtualWorld}};
        bool allowed = false;
        try {
            allowed = _hooks.harvesting(playerId, proposal);
        }
        catch (...) {
            allowed = false;
        }
        if (!allowed) {
            return {"gatheringHarvestRejected"};
        }
        // A synchronous script handler may have removed these herbs.
        for (const auto &plant : counted) {
            if (const auto *depleted = FindDepleted(virtualWorld, kind->id, plant, worldMs)) {
                return {plant == source ? "depleted" : "gatheringHarvestRejected", kind->id, 0, source, {}, {*depleted}};
            }
        }
        const auto granted = inventory->Add(std::string(kind->itemClass), amount, nlohmann::json::object(), std::nullopt, "gather", true);
        if (!granted.ok) {
            return {granted.code};
        }
        GatheringHarvest harvest {"", kind->id, amount, source, granted.outputs, {}};
        for (const auto &plant : counted) {
            GatheringPoint point {kind->id, plant, RespawnDeadline(kind->id, worldMs), ++_sequence};
            Deplete(virtualWorld, point);
            harvest.depleted.push_back(point);
        }
        _hooks.experience(playerId, kExperience);
        return harvest;
    }

    bool GatheringModule::IsDepleted(std::uint32_t virtualWorld, std::uint8_t kind, const glm::vec3 &position, std::uint64_t worldMs) const {
        return FindDepleted(virtualWorld, kind, position, worldMs) != nullptr;
    }

    const GatheringPoint *GatheringModule::FindDepleted(std::uint32_t virtualWorld, std::uint8_t kind, const glm::vec3 &position, std::uint64_t worldMs, bool exactSource) const {
        if (exactSource) {
            const auto cell = _depleted.find(CellOf(virtualWorld, position));
            if (cell == _depleted.end()) {
                return nullptr;
            }
            const auto point = cell->second.find({kind, position.x, position.y, position.z});
            return point != cell->second.end() && (point->second.until == 0 || point->second.until > worldMs) ? &point->second : nullptr;
        }
        // Ordinary harvest matching retains its tolerance across cell edges.
        const auto low  = CellOf(virtualWorld, position - glm::vec3(kMatchRadius, kMatchRadius, 0.0F));
        const auto high = CellOf(virtualWorld, position + glm::vec3(kMatchRadius, kMatchRadius, 0.0F));
        for (auto x = low.x; x <= high.x; ++x) {
            for (auto y = low.y; y <= high.y; ++y) {
                const auto cell = _depleted.find({virtualWorld, x, y});
                if (cell == _depleted.end()) {
                    continue;
                }
                for (const auto &[key, point] : cell->second) {
                    // Scripted operations address individual authored sources;
                    // the anti-double-pick tolerance must not merge neighbours.
                    const bool same = point.removed ? point.position == position : SamePlant(point.position, position);
                    if (point.kind == kind && (point.until == 0 || point.until > worldMs) && same) {
                        return &point;
                    }
                }
            }
        }
        return nullptr;
    }

    void GatheringModule::Deplete(std::uint32_t virtualWorld, GatheringPoint point) {
        _depleted[CellOf(virtualWorld, point.position)].insert_or_assign({point.kind, point.position.x, point.position.y, point.position.z}, point);
    }

    std::optional<std::vector<GatheringPoint>> GatheringModule::Sync(std::uint64_t playerId, std::uint32_t virtualWorld, const glm::vec3 &position, std::uint64_t nowMs, std::uint64_t worldMs, bool full) {
        auto &state = _players[playerId];
        if (!state.syncLimit.Take(nowMs)) {
            return std::nullopt;
        }
        const auto cell  = CellOf(virtualWorld, position);
        const bool moved = full || state.syncCell != cell;
        std::vector<GatheringPoint> points;
        for (auto x = cell.x - 1; x <= cell.x + 1; ++x) {
            for (auto y = cell.y - 1; y <= cell.y + 1; ++y) {
                const auto found = _depleted.find({virtualWorld, x, y});
                if (found == _depleted.end()) {
                    continue;
                }
                for (const auto &[key, point] : found->second) {
                    if ((point.until == 0 || point.until > worldMs) && (moved || point.sequence > state.syncSequence)) {
                        points.push_back(point);
                    }
                }
            }
        }
        state.syncCell     = cell;
        state.syncSequence = _sequence;
        if (points.empty() && !moved) {
            return std::nullopt;
        }
        return points;
    }

    bool GatheringModule::Covers(const glm::vec3 &position, const glm::vec3 &point) {
        const auto a = CellOf(0, position), b = CellOf(0, point);
        return std::abs(a.x - b.x) <= 1 && std::abs(a.y - b.y) <= 1;
    }

    void GatheringModule::Prune(std::uint64_t worldMs) {
        for (auto cell = _depleted.begin(); cell != _depleted.end();) {
            std::erase_if(cell->second, [worldMs](const auto &entry) {
                const auto &point = entry.second;
                return point.until != 0 && point.until <= worldMs;
            });
            cell = cell->second.empty() ? _depleted.erase(cell) : std::next(cell);
        }
    }

    void GatheringModule::Regrow() {
        _depleted.clear();
        for (auto &[playerId, player] : _players) {
            player.syncCell.reset();
        }
    }

    bool GatheringModule::SetRespawnTime(double minHours, double maxHours) {
        if (!std::isfinite(minHours) || !std::isfinite(maxHours) || minHours < 0 || maxHours < minHours || maxHours > 87600 || (maxHours > 0 && maxHours * 3600000.0 < 1)) {
            return false;
        }
        _respawnTime = GatheringRespawnTime {minHours, maxHours};
        return true;
    }

    std::uint64_t GatheringModule::RespawnDeadline(std::uint8_t kind, std::uint64_t worldMs) {
        if (!_respawnTime) {
            return worldMs + static_cast<std::uint64_t>(static_cast<double>(KCDC::Shared::Game::FindGatheringKind(kind)->respawnHours) * kDefaultRespawnMultiplier * 3600000.0);
        }
        if (_respawnTime->maxHours == 0) {
            return 0;
        }
        const auto minimum = std::max<std::uint64_t>(1, static_cast<std::uint64_t>(_respawnTime->minHours * 3600000.0));
        const auto maximum = std::max(minimum, static_cast<std::uint64_t>(_respawnTime->maxHours * 3600000.0));
        return worldMs + std::uniform_int_distribution<std::uint64_t>(minimum, maximum)(_random);
    }

    std::map<std::uint32_t, std::vector<GatheringPoint>> GatheringModule::Reschedule(std::uint64_t worldMs) {
        Prune(worldMs);
        std::map<std::uint32_t, std::vector<GatheringPoint>> changed;
        for (auto &[cell, points] : _depleted) {
            for (auto &[key, point] : points) {
                if (point.removed) {
                    continue;
                }
                point.until    = RespawnDeadline(point.kind, worldMs);
                point.sequence = ++_sequence;
                changed[cell.virtualWorld].push_back(point);
            }
        }
        return changed;
    }

    std::vector<GatheringPoint> GatheringModule::Respawn(std::uint32_t virtualWorld, const std::optional<glm::vec3> &position, float radius, std::uint64_t worldMs) {
        std::vector<GatheringPoint> restored;
        if (position && (!std::isfinite(position->x) || !std::isfinite(position->y) || !std::isfinite(position->z) || !std::isfinite(radius) || radius < 0)) {
            return restored;
        }
        for (auto cell = _depleted.begin(); cell != _depleted.end();) {
            if (cell->first.virtualWorld != virtualWorld) {
                ++cell;
                continue;
            }
            std::erase_if(cell->second, [&](const auto &entry) {
                const auto &point = entry.second;
                if (position && !KCDC::Shared::Gathering::WithinDistance(point.position.x, point.position.y, point.position.z, position->x, position->y, position->z, radius)) {
                    return false;
                }
                if (point.until == 0 || point.until > worldMs) {
                    restored.push_back(point);
                }
                return true;
            });
            cell = cell->second.empty() ? _depleted.erase(cell) : std::next(cell);
        }
        return restored;
    }

    void GatheringModule::Forget(std::uint64_t playerId) {
        _players.erase(playerId);
    }

    GatheringModule::Cell GatheringModule::CellOf(std::uint32_t virtualWorld, const glm::vec3 &position) {
        return {virtualWorld, static_cast<std::int32_t>(std::floor(position.x / KCDC::Shared::Gathering::kSyncCellSize)), static_cast<std::int32_t>(std::floor(position.y / KCDC::Shared::Gathering::kSyncCellSize))};
    }
} // namespace KCDC::Features::Gathering
