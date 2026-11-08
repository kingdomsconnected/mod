#pragma once

#include "shared/features/inventory/inventory_sync.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <map>
#include <string>

namespace KCDC::Features::Items {
    /**
     * Item toasts the server asked for, waiting for the projection to show
     * the items they announce.
     *
     * The server names them with each sync, per class and only for changes
     * whose cause asked to be announced, so nothing here is inferred from how
     * the rows moved: a meal eaten, a round fired or a restored inventory is
     * never announced unless a script said so.
     */
    class InventoryNotifications final {
      public:
        void Reset() {
            _pending.clear();
        }

        /** Queues an applied sync's notices. A malformed list announces nothing. */
        void Queue(const nlohmann::json &message) {
            std::map<std::string, std::int64_t> notices;
            if (!KCDC::Shared::Inventory::ReadNotices(message, notices)) {
                return;
            }
            for (const auto &[itemClass, amount] : notices) {
                _pending[itemClass] += amount;
            }
            std::erase_if(_pending, [](const auto &entry) {
                return entry.second == 0;
            });
        }

        /** Call after projection succeeds. `show` returning false leaves the rest queued. */
        template <typename Show>
        void Flush(Show &&show) {
            for (auto entry = _pending.begin(); entry != _pending.end();) {
                // Several rows can exceed the native HUD's signed int count.
                const auto amount = static_cast<int>(std::clamp<std::int64_t>(entry->second, -std::numeric_limits<int>::max(), std::numeric_limits<int>::max()));
                if (!show(entry->first, amount)) {
                    return;
                }
                entry->second -= amount;
                if (entry->second == 0) {
                    entry = _pending.erase(entry);
                }
            }
        }

      private:
        std::map<std::string, std::int64_t> _pending;
    };
} // namespace KCDC::Features::Items
