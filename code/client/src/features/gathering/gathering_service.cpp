#include "gathering_service.h"

#include "core/application.h"
#include "features/player/player_inventory_service.h"
#include "features/world/world_service.h"
#include "common/module_support.h"
#include "sdk/cryengine/entitysystem/i_entity.h"
#include "sdk/wh/environmentmodule/c_environment_module.h"
#include "sdk/wh/rpgmodule/c_calendar.h"
#include "shared/features/gathering/gathering_message.h"
#include "shared/features/gathering/gathering_rules.h"

#include <core_modules.h>
#include <logging/logger.h>
#include <networking/network_peer.h>

#include <mafianet/get_time.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace KCDC::Features::Gathering {
    namespace {
        /** How often the picked plants around the player are asked for again when they stay in one cell. */
        constexpr std::uint64_t kSyncIntervalMs = 10000;

        /** How often plants that regrew are dropped. */
        constexpr std::uint64_t kExpiryIntervalMs = 1000;

        /** Native harvest radius used to name one exact plant (HarvestArea 0x182ADDCF0). */
        constexpr float kSourceRadius = 0.003F;

        /** Plants applied to the native respawn manager per frame. */
        constexpr std::size_t kAppliedPerFrame = 256;

        /** How many of the plants harvested together are reported, nearest first. */
        constexpr std::size_t kReportedSources = 64;

        /** How long the picked plant stays hidden if the server never answers. */
        constexpr std::uint64_t kProvisionalMs = 3600000;

        std::uint64_t NativeWorldMilliseconds(SDK::wh::rpgmodule::C_Calendar *calendar) {
            return static_cast<std::uint64_t>(std::max<std::int64_t>(0, calendar->GetWorldTimeMilliseconds()));
        }

        SDK::wh::environmentmodule::C_RespawnManager *RespawnManager() {
            auto *environment = SDK::wh::environmentmodule::C_EnvironmentModule::GetInstance();
            return environment != nullptr ? environment->GetRespawnManager() : nullptr;
        }

        SDK::wh::environmentmodule::RespawnInteractionStateLayout AreaOf(std::uint8_t kind, const glm::vec3 &position) {
            SDK::wh::environmentmodule::RespawnInteractionStateLayout area {};
            area.m_position       = KCDC::Common::ToNative(position);
            area.m_pickableAreaId = kind;
            area.m_available      = true;
            return area;
        }
    } // namespace

    void GatheringService::Install() {
        Framework::CoreModules::GetNetworkPeer()->RegisterRPC<KCDC::Shared::RPC::GatheringState>([](const KCDC::Shared::RPC::GatheringState &message, MafiaNet::Packet *) {
            if (KCDC::Core::gApplication == nullptr || !message.valid) {
                return;
            }
            auto &service = KCDC::Core::gApplication->GetGatheringService();
            std::scoped_lock lock(service._incomingMutex);
            if (service._incomingOverflowed) {
                return;
            }
            if (service._incoming.size() >= 1024) {
                service._incoming.clear();
                service._incomingOverflowed = true;
                return;
            }
            service._incoming.push_back(message.body);
        });
    }

    bool GatheringService::CanStart(SDK::wh::playermodule::C_HerbGathering *minigame, SDK::EntityId proxy) {
        auto *manager = RespawnManager();
        if (minigame == nullptr || minigame->IsActive() || manager == nullptr || !manager->HasActiveInteraction() || manager->GetTrackedEntityId() != proxy) {
            return false;
        }
        auto &inventory = KCDC::Core::gApplication->GetPlayerInventoryService();
        if (_projection.replayPending) {
            inventory.RefuseUnsupportedAction("Herb state is still loading. Please try again in a moment.");
            return false;
        }
        if (inventory.IsSynced()) {
            return true;
        }
        inventory.RefuseUnsupportedAction("Your inventory is still loading. Please try again in a moment.");
        return false;
    }

    void GatheringService::Send(const nlohmann::json &request) const {
        if (!KCDC::Core::gApplication->GetPlayerInventoryService().HasAuthority()) {
            return;
        }
        KCDC::Shared::RPC::GatheringRequest message;
        message.body = request.dump();
        Framework::CoreModules::GetNetworkPeer()->BroadcastRPC(message, MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
    }

    void GatheringService::Started(SDK::wh::playermodule::C_HerbGathering *minigame, const SDK::wh::environmentmodule::RespawnInteractionStateLayout &source) {
        if (!minigame->IsActive()) {
            return;
        }
        _source    = source;
        _attempt   = std::to_string(++_sequence);
        _completed = false;
        Send({{"action", "begin"}, {"id", _attempt}, {"kind", source.m_pickableAreaId}, {"position", {source.m_position.x, source.m_position.y, source.m_position.z}}});
    }

    void GatheringService::Completed(SDK::wh::environmentmodule::C_RespawnManager *manager) {
        if (_attempt.empty() || _completed) {
            return;
        }
        _completed          = true;
        const auto &current = manager->GetInteractionState();
        const float dx = current.m_position.x - _source.m_position.x, dy = current.m_position.y - _source.m_position.y, dz = current.m_position.z - _source.m_position.z;
        // The reused proxy is not an identity. A changed selected source
        // invalidates the attempt, even if its entity id stayed the same.
        if (current.m_pickableAreaId != _source.m_pickableAreaId || dx * dx + dy * dy + dz * dz > 0.000001F) {
            return;
        }

        // The plants the game would harvest with this one, at the widest
        // radius: the server keeps those within the player's own radius.
        std::vector<SDK::Vec3> sources;
        if (const auto *found = manager->QuerySources(_source.m_position, KCDC::Shared::Gathering::Radius(KCDC::Shared::Gathering::kMaximumSurvivalLevel), _source.m_pickableAreaId, true)) {
            for (auto **entry = found->m_pBegin; entry != found->m_pEnd; ++entry) {
                sources.push_back(static_cast<const SDK::wh::environmentmodule::RespawnInteractionStateLayout *>(*entry)->m_position);
            }
        }
        const auto planar = [this](const SDK::Vec3 &position) {
            const float x = position.x - _source.m_position.x, y = position.y - _source.m_position.y;
            return x * x + y * y;
        };
        std::sort(sources.begin(), sources.end(), [&](const SDK::Vec3 &a, const SDK::Vec3 &b) {
            return planar(a) < planar(b);
        });
        sources.resize(std::min(sources.size(), kReportedSources));
        nlohmann::json reported = nlohmann::json::array();
        for (const auto &position : sources) {
            reported.push_back({position.x, position.y, position.z});
        }
        Send({{"action", "complete"}, {"id", _attempt}, {"sources", std::move(reported)}});

        // Gone at once, as in single player: the server's answer either
        // confirms it with the real regrowth time or gives it back.
        auto *calendar = SDK::wh::rpgmodule::C_Calendar::GetInstance();
        if (calendar == nullptr) {
            return;
        }
        const auto key = KCDC::Shared::Gathering::Projection::KeyOf(_source.m_pickableAreaId, KCDC::Common::ToGlm(_source.m_position));
        if (_projection.depletions.contains(key)) {
            return;
        }
        _provisional = key;
        Hide(key, {NativeWorldMilliseconds(calendar) + kProvisionalMs, _source.m_pickableAreaId, KCDC::Common::ToGlm(_source.m_position)});
    }

    void GatheringService::Hide(const KCDC::Shared::Gathering::SourceKey &key, const KCDC::Shared::Gathering::ProjectedSource &depletion) {
        auto [entry, inserted] = _projection.depletions.try_emplace(key, depletion);
        if (!inserted) {
            if (entry->second.until == depletion.until) {
                return;
            }
            entry->second.until = depletion.until;
        }
        _projection.pending.push_back(key);
    }

    void GatheringService::Refused(const std::string &code) {
        Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("Harvest refused: {}", code);
        _attempt.clear();
        if (_provisional) {
            // Undo only our optimistic hide. A depleted refusal must keep
            // the plant hidden; the server replays its committed deadline.
            const auto found = _projection.depletions.find(*_provisional);
            if (code != "depleted" && found != _projection.depletions.end()) {
                if (_projection.claimed.contains(*_provisional)) {
                    _projection.pendingRespawns.push_back(found->second);
                }
                _projection.depletions.erase(found);
            }
            _provisional.reset();
        }
        KCDC::Core::gApplication->GetPlayerInventoryService().RefuseUnsupportedAction(code == "depleted" ? "This plant has already been gathered." : "The server could not accept this harvest. Please try again.");
    }

    void GatheringService::Apply(const nlohmann::json &message) {
        const auto world = message.find("virtualWorld");
        if (world == message.end() || !world->is_number_unsigned() || world->get<std::uint64_t>() > std::numeric_limits<std::uint32_t>::max()) {
            return;
        }
        // Gathering replies use one reliable ordered channel. Its world tag
        // is authoritative: local replica worlds are not serialized.
        const bool hadWorld = _projection.virtualWorld.has_value();
        if (_projection.ChangeWorld(world->get<std::uint32_t>())) {
            if (hadWorld) {
                _attempt.clear();
                _provisional.reset();
                _completed = false;
            }
            _syncCell.reset();
            _nextSync = 0;
        }
        const auto generation                             = message.find("syncGeneration");
        const std::optional<std::uint64_t> syncGeneration = generation != message.end() && generation->is_number_unsigned() ? std::optional(generation->get<std::uint64_t>()) : std::nullopt;
        if (!_projection.AcceptDelivery(syncGeneration)) {
            return;
        }
        const auto points = message.find("points");
        if (points == message.end()) {
            const auto status = message.value("status", std::string());
            if (message.value("id", std::string()) != _attempt) {
                return;
            }
            if (status == "refused") {
                Refused(message.value("code", std::string()));
            }
            else if (status == "granted") {
                _provisional.reset();
            }
            return;
        }
        if (!points->is_array() || points->size() > 2048) {
            return;
        }
        const bool respawn = message.value("respawn", false);
        for (const auto &point : *points) {
            const auto &position = point.at("position");
            const KCDC::Shared::Gathering::ProjectedSource state {point.at("until").get<std::uint64_t>(), point.at("kind").get<std::uint8_t>(), {position.at(0).get<float>(), position.at(1).get<float>(), position.at(2).get<float>()}};
            if (!std::isfinite(state.position.x) || !std::isfinite(state.position.y) || !std::isfinite(state.position.z)) {
                continue;
            }
            const auto key = KCDC::Shared::Gathering::Projection::KeyOf(state.kind, state.position);
            if (respawn) {
                if (_projection.depletions.erase(key) != 0 || _projection.claimed.contains(key)) {
                    _projection.pendingRespawns.push_back(state);
                }
                if (_provisional == key) {
                    _provisional.reset();
                }
            }
            else {
                // A committed harvest can arrive before our own refusal.
                // It is no longer a provisional hide that we may undo.
                if (_provisional == key) {
                    _provisional.reset();
                }
                Hide(key, state);
            }
        }
        if (message.value("full", false) && syncGeneration) {
            _projection.CompleteSync(*syncGeneration);
        }
    }

    void GatheringService::ProjectPending(SDK::wh::environmentmodule::C_RespawnManager *manager) {
        std::size_t applied = 0;
        while (!_projection.pendingRespawns.empty() && applied < kAppliedPerFrame) {
            const auto state = _projection.pendingRespawns.back();
            _projection.pendingRespawns.pop_back();
            manager->RestoreHarvestedArea(AreaOf(state.kind, state.position), kSourceRadius);
            _projection.claimed.erase(KCDC::Shared::Gathering::Projection::KeyOf(state.kind, state.position));
            ++applied;
        }
        // Finish restorations before applying a newer harvest of the same source.
        if (!_projection.pendingRespawns.empty()) {
            return;
        }
        while (!_projection.pending.empty() && applied < kAppliedPerFrame) {
            const auto key = _projection.pending.back();
            _projection.pending.pop_back();
            auto found = _projection.depletions.find(key);
            if (found == _projection.depletions.end()) {
                continue;
            }
            auto &state     = found->second;
            const auto area = AreaOf(state.kind, state.position);
            // Only a claim this client made can already exist; a new one is
            // not searched for (the native list is walked linearly).
            const auto deadline = state.until == 0 ? std::numeric_limits<std::uint64_t>::max() : state.until;
            if (_projection.claimed.insert(key).second || !manager->SetHarvestDeadline(area, kSourceRadius, deadline)) {
                manager->HarvestArea(area, deadline, kSourceRadius);
            }
            manager->ClearInteractionAt(area, kSourceRadius);
            ++applied;
        }
    }

    void GatheringService::Update() {
        if (_nativeContextChanged.exchange(false)) {
            Forget();
        }
        if (!KCDC::Core::gApplication->GetPlayerInventoryService().HasAuthority()) {
            return;
        }
        std::vector<std::string> incoming;
        bool overflowed = false;
        {
            std::scoped_lock lock(_incomingMutex);
            incoming.swap(_incoming);
            overflowed = std::exchange(_incomingOverflowed, false);
        }
        if (overflowed) {
            _projection.RecoverOverflow();
            _attempt.clear();
            _provisional.reset();
            _completed = false;
            _syncCell.reset();
            _nextSync = 0;
        }
        for (const auto &body : incoming) {
            try {
                Apply(nlohmann::json::parse(body));
            }
            catch (const nlohmann::json::exception &) {
            }
        }
        auto *manager  = RespawnManager();
        auto *calendar = SDK::wh::rpgmodule::C_Calendar::GetInstance();
        auto *player   = KCDC::Common::LocalPlayer();
        if (manager == nullptr || calendar == nullptr || player == nullptr || !KCDC::Features::World::WorldService::IsSessionActive()) {
            return;
        }

        // Asked again on entering another cell, and now and then in one.
        const auto now      = MafiaNet::GetTimeUS() / 1000;
        const auto position = player->GetWorldPos();
        const std::pair<std::int32_t, std::int32_t> cell {static_cast<std::int32_t>(std::floor(position.x / KCDC::Shared::Gathering::kSyncCellSize)), static_cast<std::int32_t>(std::floor(position.y / KCDC::Shared::Gathering::kSyncCellSize))};
        if (now >= _nextSync || _syncCell != cell) {
            Send({{"action", "sync"}, {"full", _projection.fullSyncRequired}, {"syncGeneration", _projection.syncGeneration}});
            _nextSync = now + kSyncIntervalMs;
            _syncCell = cell;
        }

        ProjectPending(manager);

        // The server's world clock is the one the calendar follows, so a
        // deadline it sends is a native one.
        if (now < _nextExpiry) {
            return;
        }
        _nextExpiry          = now + kExpiryIntervalMs;
        const auto nativeNow = NativeWorldMilliseconds(calendar);
        std::erase_if(_projection.depletions, [this, nativeNow](const auto &entry) {
            if (entry.second.until == 0 || entry.second.until > nativeNow) {
                return false;
            }
            _projection.pendingRespawns.push_back(entry.second);
            return true;
        });
    }

    void GatheringService::ExpireProjection() {
        auto *manager  = RespawnManager();
        auto *calendar = SDK::wh::rpgmodule::C_Calendar::GetInstance();
        if (manager == nullptr || calendar == nullptr) {
            return;
        }
        const auto until = std::max<std::uint64_t>(1, NativeWorldMilliseconds(calendar));
        for (const auto &[key, state] : _projection.depletions) {
            if (_projection.claimed.contains(key)) {
                manager->SetHarvestDeadline(AreaOf(state.kind, state.position), kSourceRadius, until);
            }
        }
        // A manual respawn may have removed a disabled depletion before its
        // queued restoration ran. Do not leave that permanent native claim
        // behind when the session ends.
        for (const auto &state : _projection.pendingRespawns) {
            if (_projection.claimed.contains(KCDC::Shared::Gathering::Projection::KeyOf(state.kind, state.position))) {
                manager->SetHarvestDeadline(AreaOf(state.kind, state.position), kSourceRadius, until);
            }
        }
    }

    void GatheringService::Forget() {
        _projection.Forget();
        _attempt.clear();
        _provisional.reset();
        _syncCell.reset();
        _nextSync   = 0;
        _nextExpiry = 0;
        _completed  = false;
    }

    void GatheringService::NativeContextChanged() {
        _nativeContextChanged.store(true);
    }

    void GatheringService::Reset() {
        _nativeContextChanged.store(false);
        ExpireProjection();
        Forget();
        std::scoped_lock lock(_incomingMutex);
        _incoming.clear();
        _incomingOverflowed = false;
    }
} // namespace KCDC::Features::Gathering
