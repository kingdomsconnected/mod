#pragma once

#include "player_combat_hit_report.h"
#include <algorithm>
#include <cstdint>
#include <vector>

namespace KCDC::Shared::RPC {
    /** Admission can lag receipt while a swing waits for its replicated fence. */
    class CombatReportQueue {
      public:
        struct Entry {
            CombatHitReport report;
            std::uint64_t sender           = 0;
            std::uint64_t receivedAt       = 0;
            std::uint32_t otherIncarnation = 0;
            std::uint8_t otherEpoch        = 0;
        };
        static constexpr std::uint64_t kWaitMs = 5000;

        bool Push(const Entry &entry) {
            if (_pending.size() >= 512 || std::count_if(_pending.begin(), _pending.end(), [&](const Entry &held) {
                    return held.sender == entry.sender;
                }) >= 32) {
                return false;
            }
            _pending.push_back(entry);
            return true;
        }

        /** Returning false retains an entry only within the admission deadline. */
        template <typename TProcess>
        void Drain(std::uint64_t now, TProcess process) {
            std::vector<Entry> batch;
            batch.swap(_pending);
            for (const Entry &entry : batch) {
                if (now >= entry.receivedAt && now - entry.receivedAt < kWaitMs && !process(entry)) {
                    Push(entry);
                }
            }
        }

        void Forget(std::uint64_t player, std::uint64_t opponent = 0) {
            std::erase_if(_pending, [&](const Entry &entry) {
                const std::uint64_t other = entry.report.IsClaim() ? entry.report.victimNetworkId : entry.report.attackerNetworkId;
                return (entry.sender == player && (opponent == 0 || other == opponent)) || (other == player && (opponent == 0 || entry.sender == opponent));
            });
        }

      private:
        std::vector<Entry> _pending;
    };
} // namespace KCDC::Shared::RPC
