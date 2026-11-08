#pragma once

#include <utils/safe_win32.h>

#include "native_repair_batch.h"
#include "sdk/wh/entitymodule/c_inventory.h"
#include "sdk/wh/entitymodule/c_item.h"
#include "shared/item_stack.h"

#include <function2/function2.hpp>
#include <nlohmann/json.hpp>

#include <atomic>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace KCDC::Features::Items {
    /** Units of one server row held by one native stack. */
    struct NativeUnit {
        std::string id;
        std::uint32_t amount = 0;

        bool operator==(const NativeUnit &) const = default;
    };

    /** A server row the game wore down, with the properties it now has. */
    struct NativeWearClaim {
        std::string id;
        nlohmann::json metadata;
    };

    /** Units a native action spent out of one projected stack. */
    struct NativeConsumption {
        KCDC::Shared::RPC::ItemClassId itemClass;
        std::vector<NativeUnit> units;
    };

    /**
     * Keeps the local player's native inventory equal to the server's rows.
     *
     * The rows are the truth and the native stacks follow them. A stack the
     * projection made, or one already there that matches a row exactly, is
     * kept; anything else is removed, and what is missing is created. A
     * stack whose properties drifted is written back to the row's, except
     * wear the game caused and the server was told about (`TakeWear`), which
     * is left alone until the server's rows catch up.
     *
     * Only item identities (WUIDs) are kept between frames, never pointers.
     */
    class NativeInventoryProjection final {
      public:
        /**
         * Brings the native stacks to `rows`. A row the game cannot show --
         * a class it does not know, or one `CreateItems` will not make as
         * asked -- is left out and named in `unshown` with the reason, and
         * every other row is still projected. False, with `error`, only
         * when the projection itself could not be kept consistent.
         *
         * When a kind shrinks, worn stacks are kept before loose ones.
         * `takeOff`, when given, is handed each worn stack the rows no longer
         * hold before it is removed, so the body takes it off first; a worn
         * stack that only shrinks stays worn.
         */
        bool Apply(SDK::wh::entitymodule::C_Inventory *inventory, const std::map<std::string, nlohmann::json> &rows, std::map<std::string, std::string> &unshown, std::string &error, const fu2::function<void(SDK::wh::entitymodule::C_Item &) const> &takeOff = {});

        /**
         * Reads what the game did to one stack's properties. A loss (lower
         * quality, health or poison charges, nothing higher) becomes a claim
         * per row the stack holds, and the stack is kept as it is. False when
         * the change is not a loss, so the caller projects again to undo it.
         */
        bool TakeWear(SDK::wh::entitymodule::C_Inventory *inventory, std::uint64_t wuid, std::vector<NativeWearClaim> &claims);
        /**
         * Reads a sharpening the grindstone left on one projected unit: the
         * row it is, as projected and as it is now. False unless the stack is
         * one unit of one row and only its health rose.
         */
        bool TakeSharpening(SDK::wh::entitymodule::C_Inventory *inventory, std::uint64_t wuid, NativeRepairPart &part) const;

        /**
         * Reads which server row one unit of `item` is, before a repair
         * changes it, skipping the rows `taken` already claims. False when
         * the stack is not projected as its kind, worn or not.
         */
        bool ReadRepairStart(SDK::wh::entitymodule::C_Item &item, const std::vector<NativeRepairPart> &taken, NativeRepairPart &part) const;
        /**
         * Reads what the repair left of `part`. Only health may differ: the
         * item effects a repair perk adds are left out, and the next
         * projection takes them back off. Called before CombineItems can
         * remove an exhausted kit or merge either unit into another stack.
         */
        static bool ReadRepairResult(SDK::wh::entitymodule::C_Item &item, bool kit, NativeRepairPart &part);

        /**
         * Reads which projected stacks of the classes `armed` accepts the game
         * shrank -- a meal eaten, a round fired -- and returns the server rows
         * those units came from. The stacks keep what is left until the
         * server's rows catch up.
         */
        std::vector<NativeConsumption> TakeConsumption(SDK::wh::entitymodule::C_Inventory *inventory, const fu2::function<bool(std::uint64_t nativeId, const KCDC::Shared::RPC::ItemClassId &) const> &armed);

        /** The native stack holding units of a server row, or zero. */
        std::uint64_t NativeIdOf(const std::string &rowId) const {
            for (const auto &[wuid, state] : _native) {
                for (const auto &unit : state.units) {
                    if (unit.id == rowId) {
                        return wuid;
                    }
                }
            }
            return 0;
        }

        bool IsBoundTo(const SDK::wh::entitymodule::C_Inventory *inventory) const {
            return _initialized.load() && _inventory.load() == inventory;
        }
        bool ManagesNativeItem(const SDK::wh::entitymodule::C_Item *item) const {
            return item != nullptr && ManagesHolder(item->GetOwner());
        }
        bool ManagesHolder(const SDK::wh::entitymodule::C_ItemHolder *holder) const {
            // Read by hooks on the game's worker threads, hence the atomics.
            const auto *inventory = _inventory.load();
            return _initialized.load() && inventory != nullptr && holder != nullptr && holder == inventory;
        }
        /** The server rows one native stack holds units of, as of the last apply; empty when it holds none. */
        std::vector<NativeUnit> UnitsOf(std::uint64_t wuid) const {
            const auto found = _native.find(wuid);
            return found != _native.end() ? found->second.units : std::vector<NativeUnit> {};
        }
        /**
         * The server rows of the kind a native stack is, summed over every
         * stack of that kind as of the last apply, its own rows first. A stack
         * no apply has seen -- one the game split off -- is placed by its
         * properties. Empty when it matches no projected kind.
         */
        std::vector<NativeUnit> UnitsOfKind(SDK::wh::entitymodule::C_Item &item) const;
        /**
         * A native item the game lends the local player and takes back: a
         * smithery's tool while a workpiece is worked. It passes through the
         * native inventory without being the player's, so the projection
         * neither removes it nor counts it as a row's stack until it is
         * returned. Game thread only.
         */
        void Lend(std::uint64_t wuid) {
            _lent.insert(wuid);
        }
        void Return(std::uint64_t wuid) {
            _lent.erase(wuid);
        }
        /** Whether two native stacks carry exactly the same server properties. */
        static bool SameProperties(SDK::wh::entitymodule::C_Item &left, SDK::wh::entitymodule::C_Item &right);
        /** Canonical properties for a delegated body's initial census. */
        static bool ReadNativeRow(SDK::wh::entitymodule::C_Item &item, nlohmann::json &row, std::string &error);
        void Clear();

      private:
        struct NativeState {
            std::string group;
            KCDC::Shared::RPC::InventoryItem properties;
            std::vector<NativeUnit> units;
            // Worn by the game and claimed; kept until the rows agree.
            bool wearPending = false;
        };

        std::map<std::uint64_t, NativeState> _native;
        // A kind CreateItems would not make as asked, with the rows it was
        // asked for then; tried again once those rows differ.
        struct FailedKind {
            std::vector<NativeUnit> units;
            std::string reason;
        };

        std::set<std::uint64_t> _lent;
        std::map<std::string, FailedKind> _failedKinds;
        std::atomic<SDK::wh::entitymodule::C_Inventory *> _inventory = nullptr;
        std::atomic<bool> _initialized                               = false;
    };
} // namespace KCDC::Features::Items
