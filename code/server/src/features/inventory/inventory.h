#pragma once

#include "shared/features/inventory/inventory_repair.h"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace KCDC::Features::Inventory {
    /** A stack of one item class with one set of properties. */
    struct InventoryRow {
        std::string id;
        std::string itemClass;
        std::uint32_t amount    = 0;
        nlohmann::json metadata = nlohmann::json::object();

        bool operator==(const InventoryRow &) const = default;
    };

    /** A count of units taken from one named row. */
    struct InventoryUnit {
        std::string id;
        std::uint32_t amount = 0;

        bool operator==(const InventoryUnit &) const = default;
    };

    /** Units of one kind an exchange hands over; the properties go through the policy like any grant's. */
    struct InventoryCredit {
        std::string itemClass;
        std::uint32_t amount    = 0;
        nlohmann::json metadata = nlohmann::json::object();
    };

    /** One unit a native repair touched, with its properties before and after; `spent` when a kit ran out. */
    struct InventoryRepairPart {
        std::string id;
        nlohmann::json before = nlohmann::json::object();
        nlohmann::json after  = nlohmann::json::object();
        bool spent            = false;
        bool equipped         = false;
    };

    /** One row an operation touched: absent before means created, absent after means emptied. */
    struct InventoryRowChange {
        std::string id;
        std::optional<InventoryRow> before;
        std::optional<InventoryRow> after;
    };

    /** What one successful operation did to one inventory. */
    struct InventoryChange {
        std::uint64_t revision = 0;
        std::string reason;
        std::vector<InventoryRowChange> rows;
    };

    /** An operation's answer. `outputs` names the rows that received units. */
    struct InventoryResult {
        bool ok = false;
        std::string code;
        std::uint64_t revision = 0;
        std::vector<InventoryUnit> outputs;
    };

    /** Row identities, unique for the life of the server process. */
    class InventoryIds final {
      public:
        std::string Next() {
            return std::to_string(_next++);
        }
        std::string Peek() const {
            return std::to_string(_next);
        }

      private:
        std::uint64_t _next = 1;
    };

    /**
     * One player's items, held in memory for their session.
     *
     * Every operation validates in full before it changes anything, so a
     * refusal leaves the rows, the revision and the change list exactly as
     * they were. Properties go through the shared property policy on every
     * write, which makes the stored metadata canonical: two rows of one class
     * with equal metadata are the same kind of item, and a grant or transfer
     * merges into the row that already holds it. Nonstackable custom items
     * keep separate unit rows even when their properties match.
     *
     * Nothing here persists. A script saves what it needs from
     * `playerInventoryChanged` and restores it with `Set`.
     */
    class Inventory final {
      public:
        static constexpr std::size_t kMaximumRows     = 4096;
        static constexpr std::size_t kMaximumBytes    = 448 * 1024;
        static constexpr std::size_t kMaximumIdLength = 64;
        static constexpr std::uint32_t kMaximumAmount = 2147483647U;
        /** Kits one repair may use: CombineItems splits the next one off each time the last runs out. */
        static constexpr std::size_t kMaximumRepairKits = KCDC::Shared::Inventory::kMaximumRepairKits;

        explicit Inventory(InventoryIds &ids, std::size_t maximumRows = kMaximumRows): _ids(ids), _maximumRows(maximumRows) {}

        std::uint64_t Revision() const {
            return _revision;
        }
        const std::map<std::string, InventoryRow> &Rows() const {
            return _rows;
        }
        /** Changes since the last call, oldest first. */
        std::vector<InventoryChange> TakeChanges();
        /**
         * Units per class the owner's game is to announce since the last call:
         * gains positive, losses negative, summed over the notifying changes.
         */
        std::map<std::string, std::int64_t> TakeNotices();

        // `notify` has the owner's game announce the change with its own
        // item-received and item-removed toasts. It is off unless the caller
        // asks: most changes are the player's own doing, which their game
        // already showed them, or bookkeeping that is no news to them.
        InventoryResult Add(const std::string &itemClass, std::uint32_t amount, const nlohmann::json &metadata, std::optional<std::uint64_t> revision, const std::string &reason = "add", bool notify = false);
        InventoryResult Remove(const std::vector<InventoryUnit> &units, std::optional<std::uint64_t> revision, const std::string &reason = "remove", bool notify = false);
        /** Spends `amount` of a class across its rows, all or nothing. */
        InventoryResult Take(const std::string &itemClass, std::uint32_t amount, std::optional<std::uint64_t> revision, const std::string &reason = "remove", bool notify = false);
        /** Takes `debits` and adds `credits` as one change, all or nothing; `outputs` names the rows the credits landed in, in order. */
        InventoryResult Exchange(const std::vector<InventoryUnit> &debits, const std::vector<InventoryCredit> &credits, std::optional<std::uint64_t> revision, const std::string &reason, bool notify = false);
        /** Resolves an owned, usable custom row at the expected revision. */
        const InventoryRow *ValidateCustomUse(const std::string &id, std::uint64_t revision) const;
        /** Patches custom fields; null clears an override or private data. */
        InventoryResult UpdateCustom(const std::string &id, const nlohmann::json &patch, std::optional<std::uint64_t> revision);
        /** Replaces one row's properties; its identity and amount stay. */
        InventoryResult SetProperties(const std::string &id, const nlohmann::json &metadata, std::optional<std::uint64_t> revision);
        /** Replaces every row. A row with no id is given one. */
        InventoryResult Set(const std::vector<InventoryRow> &rows, std::optional<std::uint64_t> revision);
        /** The owner's report that a row wore down. Accepted only as a loss. */
        InventoryResult Wear(const std::string &id, const nlohmann::json &metadata);
        /**
         * One native repair: every kit it used and the equipment unit it
         * raised, committed together or not at all. A worn equipment unit
         * keeps its row and untouched copies move out; a loose one moves out
         * itself, so the equipment record keeps naming the copy on the body.
         */
        InventoryResult Repair(const std::vector<InventoryRepairPart> &kits, const InventoryRepairPart &equipment, bool equipped);
        /** Repairs the complete selection atomically, charging each distinct kit once. */
        InventoryResult Repair(const std::vector<InventoryRepairPart> &kits, const std::vector<InventoryRepairPart> &equipment);
        /**
         * One native sharpening: a weapon the grindstone takes has its health
         * raised, which costs no item. Where the player stands is the
         * caller's to check; this checks the weapon and that only its health
         * rose.
         */
        InventoryResult Sharpen(const InventoryRepairPart &weapon);

        static InventoryResult Transfer(Inventory &source, Inventory &target, const std::vector<InventoryUnit> &units, std::optional<std::uint64_t> sourceRevision, std::optional<std::uint64_t> targetRevision, const std::string &reason = "transfer", bool notify = false);

        /** A row and a change as scripts and the sync wire spell them. */
        static nlohmann::json EncodeRow(const InventoryRow &row);
        static nlohmann::json EncodeChange(const InventoryChange &change);

      private:
        /** Pending edits: each touched row's new state, or nothing when it empties. */
        struct Edits {
            std::map<std::string, std::optional<InventoryRow>> rows;
        };

        static InventoryResult Refuse(const std::string &code);
        static std::size_t RowBytes(const InventoryRow &row);
        static bool ValidId(const std::string &id);
        bool IsStale(std::optional<std::uint64_t> revision) const;
        /** Units checked against the rows as `edits` would leave them. */
        std::optional<std::string> CheckUnits(const std::vector<InventoryUnit> &units, const Edits &edits) const;
        void Debit(const std::vector<InventoryUnit> &units, Edits &edits) const;
        /** Credits a grant, splitting nonstackable custom items into unit rows. */
        void CreditUnits(const std::string &itemClass, std::uint32_t amount, const nlohmann::json &metadata, Edits &edits, std::vector<InventoryUnit> &outputs) const;
        /** Adds units of one kind, merging where it can; returns the receiving row. */
        std::string Credit(const std::string &itemClass, std::uint32_t amount, const nlohmann::json &metadata, Edits &edits) const;
        const InventoryRow *Current(const std::string &id, const Edits &edits) const;
        std::string NewId(const Edits &edits) const;
        std::optional<std::string> CheckCapacity(const Edits &edits) const;
        /** One repaired unit's row as `edits` leave it, the health it had and the normalized result; the starting properties must be the row's. */
        std::optional<std::string> ReadRepairPart(const InventoryRepairPart &part, InventoryRow &row, float &before, float &after, nlohmann::json &metadata) const;
        /** Raises the equipment's health, spending the kits, as one change; the counts are the caller's to bound. */
        InventoryResult RaiseHealth(const std::vector<InventoryRepairPart> &kits, const std::vector<InventoryRepairPart> &equipment, const std::string &reason);
        InventoryResult Commit(Edits edits, const std::string &reason, std::vector<InventoryUnit> outputs, bool notify = false);

        InventoryIds &_ids;
        std::size_t _maximumRows;
        std::map<std::string, InventoryRow> _rows;
        std::uint64_t _revision = 0;
        std::size_t _bytes      = 0;
        // Class and canonical metadata to the rows holding that kind of item.
        std::map<std::string, std::set<std::string>> _kinds;
        std::vector<InventoryChange> _changes;
        std::map<std::string, std::int64_t> _notices;
    };
} // namespace KCDC::Features::Inventory
