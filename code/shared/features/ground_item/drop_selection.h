#pragma once

#include "shared/item_stack.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace KCDC::Shared::Inventory {
    struct DropUnit {
        std::string id;
        std::uint32_t amount = 0;
        bool held            = false;
    };

    /** One selection can fill the player's ground-stack allowance. */
    inline constexpr std::size_t kMaximumDropUnits = 64;

    /**
     * Collects the native drop iterator's callbacks into one request.
     * Several native stacks may contain units of the same server row.
     */
    class DropSelection final {
      public:
        void Begin() {
            if (_depth++ == 0) {
                _units.clear();
                _valid = true;
            }
        }

        bool IsActive() const {
            return _depth != 0;
        }

        void Refuse() {
            _valid = false;
        }

        void Add(const std::string &id, std::uint32_t amount, bool held) {
            if (!_valid) {
                return;
            }
            if (id.empty() || id.size() > 64 || amount == 0 || amount > KCDC::Shared::RPC::kMaxInventoryStackAmount) {
                Refuse();
                return;
            }
            const auto found = std::find_if(_units.begin(), _units.end(), [&id](const DropUnit &unit) {
                return unit.id == id;
            });
            if (found != _units.end()) {
                if (amount > KCDC::Shared::RPC::kMaxInventoryStackAmount - found->amount) {
                    Refuse();
                    return;
                }
                found->amount += amount;
                found->held = found->held || held;
            }
            else if (_units.size() < kMaximumDropUnits) {
                _units.push_back({id, amount, held});
            }
            else {
                Refuse();
            }
        }

        /** Only the outer selection produces a payload; refusals send none. */
        std::optional<std::string> Finish() {
            if (_depth == 0 || --_depth != 0) {
                return std::nullopt;
            }
            std::optional<std::string> body;
            if (_valid && !_units.empty()) {
                nlohmann::json units = nlohmann::json::array();
                for (const auto &unit : _units) {
                    units.push_back({{"id", unit.id}, {"amount", unit.amount}, {"held", unit.held}});
                }
                body = nlohmann::json({{"units", std::move(units)}}).dump();
            }
            _units.clear();
            return body;
        }

        bool IsValid() const {
            return _valid;
        }

      private:
        std::vector<DropUnit> _units;
        std::uint32_t _depth = 0;
        bool _valid          = true;
    };
} // namespace KCDC::Shared::Inventory
