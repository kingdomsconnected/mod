#pragma once

#include "shared/features/inventory/inventory_quickslots.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>

namespace KCDC::Features::Items {
    /** Retries one projection revision without running a failing native every frame. */
    class InventoryProjectionRetry final {
      public:
        void Reset() {
            *this = {};
        }
        bool Pending() const {
            return _failures != 0 && _failures <= kDelays.size();
        }
        bool CanAttempt(std::uint64_t now) const {
            return _failures <= kDelays.size() && now >= _nextAttempt;
        }
        void Failed(std::uint64_t now) {
            ++_failures;
            if (Pending()) {
                _nextAttempt = now + kDelays[_failures - 1];
            }
        }

      private:
        static constexpr std::array<std::uint64_t, 3> kDelays {250, 1000, 3000};
        std::size_t _failures      = 0;
        std::uint64_t _nextAttempt = 0;
    };

    /** Equipment intent and its verification, independent of native item pointers. */
    class InventoryEquipmentReconciliation final {
      public:
        enum class Action {
            None,
            Dress,
            Complete,
            Failed
        };

        void Reset() {
            *this = {};
        }
        const std::map<std::string, std::uint32_t> &Desired() const {
            return _desired;
        }
        const std::map<std::string, std::uint32_t> &Sent() const {
            return _sent;
        }
        const std::optional<Shared::Inventory::Quickslots> &DesiredQuickslots() const {
            return _desiredQuickslots;
        }
        const std::optional<Shared::Inventory::Quickslots> &SentQuickslots() const {
            return _sentQuickslots;
        }
        /** Keep saved references the projection could not materialize, as with recorded equipment. */
        template <typename Unavailable>
        Shared::Inventory::Quickslots RecordQuickslots(Shared::Inventory::Quickslots actual, Unavailable &&unavailable) const {
            if (!_desiredQuickslots) {
                return actual;
            }
            const auto &desired = *_desiredQuickslots;
            const auto missing  = [&](const std::string &saved, const std::string &native) {
                return native.empty() && !saved.empty() && unavailable(saved);
            };
            // During restore, normalization caused by an unavailable primary
            // cannot fail verification. Once settled, report native selection
            // again so this fallback never hides later player choices.
            if (_pending || _failed) {
                if (missing(desired.weapons[desired.activeWeapon][0], actual.weapons[desired.activeWeapon][0])) {
                    actual.activeWeapon = desired.activeWeapon;
                }
                if (missing(desired.items[desired.activeItem], actual.items[desired.activeItem])) {
                    actual.activeItem = desired.activeItem;
                }
            }
            for (std::size_t slot = 0; slot < Shared::Inventory::Quickslots::kSlotCount; ++slot) {
                for (std::size_t part = 0; part < 2; ++part) {
                    if (missing(desired.weapons[slot][part], actual.weapons[slot][part])) {
                        actual.weapons[slot][part] = desired.weapons[slot][part];
                    }
                }
                if (missing(desired.items[slot], actual.items[slot])) {
                    actual.items[slot] = desired.items[slot];
                }
            }
            return actual;
        }
        std::optional<std::uint64_t> Restore() const {
            return _restore;
        }
        bool CanReport() const {
            return _known && !_pending && !_failed;
        }
        bool NeedsPoll(std::uint64_t now, bool nativeChanged = false) const {
            return (_pending && now >= _verifyAt) || (_deferred && !_pending && !_failed && now >= _echoDeadline) || (_failed && nativeChanged);
        }

        void Receive(std::map<std::string, std::uint32_t> record, std::optional<std::uint64_t> restore, std::uint64_t now, std::optional<Shared::Inventory::Quickslots> quickslots = std::nullopt) {
            const bool requested = !_known || (restore && (!_restore || *restore != *_restore));
            _known               = true;
            _restore             = restore;
            if (requested) {
                _desired           = std::move(record);
                _desiredQuickslots = std::move(quickslots);
                Restart(now);
                _echoDeadline = 0;
                _deferred     = false;
            }
            else if (!restore) {
                _desired           = std::move(record);
                _desiredQuickslots = std::move(quickslots);
                // Older servers do not distinguish a restore from an echo.
                // Keep their latest record and revisit it even if no sync follows.
                _deferred = true;
                if ((SameRows(_desired, _sent) && _desiredQuickslots == _sentQuickslots) || now >= _echoDeadline) {
                    _echoDeadline = 0;
                }
            }
            else if (_pending || _failed) {
                // Keep quantities and removals current while restoring. Once
                // settled, an older echo must not replace a later local choice.
                _desired           = std::move(record);
                _desiredQuickslots = std::move(quickslots);
            }
        }

        void Restart(std::uint64_t now) {
            _pending  = _known;
            _failed   = false;
            _attempts = 0;
            _verifyAt = now;
        }

        /** Called only when the current inventory is projected and the body is available. */
        Action Poll(std::uint64_t now, const std::map<std::string, std::uint32_t> &actual, std::optional<Shared::Inventory::Quickslots> quickslots = std::nullopt) {
            const bool matches = SameRows(actual, _desired) && (!_desiredQuickslots || quickslots == _desiredQuickslots);
            if (_failed && matches) {
                _failed         = false;
                _sent           = actual;
                _sentQuickslots = _desiredQuickslots ? std::move(quickslots) : std::nullopt;
                return Action::Complete;
            }
            if (_deferred && !_pending && !_failed && now >= _echoDeadline) {
                _deferred     = false;
                _echoDeadline = 0;
                if (!matches) {
                    Restart(now);
                }
            }
            if (!_pending || now < _verifyAt) {
                return Action::None;
            }
            if (_attempts != 0 && matches) {
                _pending        = false;
                _sent           = actual;
                _sentQuickslots = _desiredQuickslots ? std::move(quickslots) : std::nullopt;
                return Action::Complete;
            }
            if (_attempts == kMaximumAttempts) {
                // After the last attempt the body's outfit is the record; the
                // caller reports it. Pieces the game could not make are still
                // in `actual`, folded in by the caller.
                _pending = false;
                _failed  = false;
                _desired = actual;
                if (_desiredQuickslots && quickslots) {
                    _desiredQuickslots = std::move(quickslots);
                }
                return Action::Failed;
            }
            ++_attempts;
            _verifyAt = now + 2000;
            return Action::Dress;
        }

        void Reported(std::map<std::string, std::uint32_t> record, std::uint64_t now, std::optional<Shared::Inventory::Quickslots> quickslots = std::nullopt) {
            _desired           = record;
            _sent              = std::move(record);
            _desiredQuickslots = quickslots;
            _sentQuickslots    = std::move(quickslots);
            _echoDeadline      = now + 5000;
        }

      private:
        // A worn ammunition stack can shrink before its server row does.
        static bool SameRows(const std::map<std::string, std::uint32_t> &a, const std::map<std::string, std::uint32_t> &b) {
            return std::equal(a.begin(), a.end(), b.begin(), b.end(), [](const auto &left, const auto &right) {
                return left.first == right.first;
            });
        }

        static constexpr std::uint32_t kMaximumAttempts = 4;
        std::map<std::string, std::uint32_t> _desired;
        std::map<std::string, std::uint32_t> _sent;
        std::optional<Shared::Inventory::Quickslots> _desiredQuickslots;
        std::optional<Shared::Inventory::Quickslots> _sentQuickslots;
        std::optional<std::uint64_t> _restore;
        bool _known                 = false;
        bool _pending               = false;
        bool _failed                = false;
        bool _deferred              = false;
        std::uint32_t _attempts     = 0;
        std::uint64_t _verifyAt     = 0;
        std::uint64_t _echoDeadline = 0;
    };
} // namespace KCDC::Features::Items
