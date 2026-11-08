#pragma once

#include <utils/safe_win32.h>

#include "core/feature_registry.h"
#include "sdk/wh/environmentmodule/c_respawn_manager.h"
#include "sdk/wh/playermodule/c_herb_gathering.h"
#include "shared/features/gathering/gathering_projection.h"

#include <nlohmann/json.hpp>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace KCDC::Features::Gathering {
    /**
     * Herb picking, which the server rewards.
     *
     * The native minigame runs here as in single player; its start and its
     * harvest are reported with the same-kind plants the game would harvest
     * together, and the server decides what was there to pick and adds the
     * herbs to the inventory. Plants the server says were picked around the
     * player are marked harvested in the native respawn manager, so they are
     * gone here too until they regrow.
     */
    class GatheringService final: public KCDC::Core::IFeatureService {
      public:
        static void Install();
        void Update() override;
        void Reset() override;
        void NativeContextChanged() override;

        /** Whether the native minigame may start on the plant the respawn manager tracks; tells the player why not. */
        bool CanStart(SDK::wh::playermodule::C_HerbGathering *minigame, SDK::EntityId proxy);
        void Started(SDK::wh::playermodule::C_HerbGathering *minigame, const SDK::wh::environmentmodule::RespawnInteractionStateLayout &source);
        void Completed(SDK::wh::environmentmodule::C_RespawnManager *manager);

      private:
        void Send(const nlohmann::json &request) const;
        void Apply(const nlohmann::json &message);
        void Hide(const KCDC::Shared::Gathering::SourceKey &key, const KCDC::Shared::Gathering::ProjectedSource &depletion);
        void Refused(const std::string &code);
        void ProjectPending(SDK::wh::environmentmodule::C_RespawnManager *manager);
        void ExpireProjection();
        void Forget();

        std::mutex _incomingMutex;
        std::vector<std::string> _incoming;
        bool _incomingOverflowed = false;
        KCDC::Shared::Gathering::Projection _projection;
        std::string _attempt;
        std::uint64_t _sequence   = 0;
        std::uint64_t _nextSync   = 0;
        std::uint64_t _nextExpiry = 0;
        std::optional<std::pair<std::int32_t, std::int32_t>> _syncCell;
        // The picked plant, hidden before the server answers.
        std::optional<KCDC::Shared::Gathering::SourceKey> _provisional;
        SDK::wh::environmentmodule::RespawnInteractionStateLayout _source {};
        bool _completed = false;
        std::atomic<bool> _nativeContextChanged {false};
    };
} // namespace KCDC::Features::Gathering
