#pragma once

#include "features/inventory/inventory.h"
#include "shared/item_stack.h"
#include "shared/features/ground_item/drop_selection.h"

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace KCDC::Features::GroundItem {
    struct GroundStockStack {
        Shared::RPC::InventoryItem item;
        bool retiresHeldItem = false;
    };

    /** What a drop took out of the inventory, split into native-sized stacks. */
    struct GroundStockDrop {
        KCDC::Features::Inventory::InventoryResult result;
        std::vector<GroundStockStack> stacks;
    };

    /**
     * Stock moving between an inventory and the ground.
     *
     * A dropped stack is exactly the units the inventory gave up, and a
     * picked up one is exactly the stack: the ground holds nothing that did
     * not come out of an inventory or a script.
     */
    class GroundStock final {
      public:
        static constexpr float kPickupRange = 4.0F;
        static constexpr std::size_t kMaximumDropUnits = KCDC::Shared::Inventory::kMaximumDropUnits;
        // How far a falling stack may end up from where it was dropped before the server stops believing the report.
        static constexpr float kMaximumFallDistance = 8.0F;

        /** Reads unique rows with amounts and optional per-row held markers. */
        static bool ReadDropRequest(const std::string &body, std::vector<KCDC::Shared::Inventory::DropUnit> &units);
        /**
         * Removes the units (reason `drop`) only when every stack they become
         * can be placed; nothing is removed otherwise.
         *
         * One UI selection names all its rows in one request. No revision
         * is needed: exact row identities and amounts bound the removal,
         * even if a preceding action or wear has advanced the inventory.
         * A missing row or insufficient amount refuses the whole selection.
         */
        static GroundStockDrop Drop(KCDC::Features::Inventory::Inventory &inventory, const std::vector<KCDC::Shared::Inventory::DropUnit> &units, std::size_t availableStacks = kMaximumDropUnits);
        static KCDC::Features::Inventory::InventoryResult Pickup(KCDC::Features::Inventory::Inventory &inventory, const Shared::RPC::InventoryItem &stack, const char *reason = "pickup", bool notify = true);
        /** Empty when the claimant may take the stack, else the refusal. Only a resting stack's pose is the server's own. */
        static std::string PickupRefusal(const glm::vec3 &claimant, std::uint32_t claimantWorld, bool canAct, const glm::vec3 &stack, std::uint32_t stackWorld, bool resting);
        /** Where a fall reported as ending at `reported` is allowed to end. */
        static glm::vec3 SettledPosition(const glm::vec3 &origin, const glm::vec3 &reported);

        /**
         * Fills in what the class needs but the caller did not name.
         *
         * A missile class is held to tighter bounds than the rest -- the
         * receiver refuses one whose quality is zero or whose health is the
         * "unset" -1 -- so a stack of arrows asked for with neither is
         * completed rather than refused.
         */
        static void ApplyClassDefaults(Shared::RPC::InventoryItem &item);
        /** Whether this stock can be built as one native ground item. */
        static bool CanPlace(const Shared::RPC::InventoryItem &item);
    };

    /**
     * Stacks spoken for by somebody's arms.
     *
     * A claim reserves a stack for one carrier from the moment the server
     * orders the carry until that carrier's client reports it, or the claim
     * lapses. A stack a carry took up is retiring: it stays on the ground a
     * moment longer, for every other client to take its own copy of it into
     * the carrier's arms, and nobody else may claim or pick it up meanwhile.
     */
    class GroundCarryClaims final {
      public:
        // A claim covers the order's trip to the carrier and their report back.
        static constexpr std::uint64_t kClaimTimeoutMs = 5000;
        // How long a stack a carry took up stays for the other clients.
        static constexpr std::uint64_t kRetireDelayMs = 3000;

        /** Whether `carrier` could claim `stack` now: nobody else holds a live claim and it is not retiring. */
        bool IsClaimable(std::uint64_t stack, std::uint64_t carrier, std::uint64_t now) const;
        /** Claims `stack` for `carrier`, or renews their claim. False when it is not claimable. */
        bool Claim(std::uint64_t stack, std::uint64_t carrier, std::uint64_t now);
        /** Whether `carrier` holds a live claim on `stack`. */
        bool Holds(std::uint64_t stack, std::uint64_t carrier, std::uint64_t now) const;
        /** Whether anybody's arms have it: a live claim or retiring. */
        bool IsHeld(std::uint64_t stack, std::uint64_t now) const;
        void Release(std::uint64_t stack);
        /** A carry took the stack up; it goes once `kRetireDelayMs` has passed. */
        void Retire(std::uint64_t stack, std::uint64_t now);
        /** The retiring stacks now due to go, forgotten here; lapsed claims are dropped too. */
        std::vector<std::uint64_t> TakeDue(std::uint64_t now);
        /** The stack left the ground some other way. */
        void Forget(std::uint64_t stack);

      private:
        struct Hold {
            std::uint64_t carrier   = 0;
            std::uint64_t expiresAt = 0;
        };

        std::unordered_map<std::uint64_t, Hold> _claims;
        std::unordered_map<std::uint64_t, std::uint64_t> _retiring;
    };

    /** How many stacks each player has on the ground, so one player cannot fill the world. */
    class GroundStockCounts final {
      public:
        static constexpr std::uint32_t kMaximumPerPlayer = 64;

        bool CanAdd(std::uint64_t source, std::size_t count) const;
        void Add(std::uint64_t stack, std::uint64_t source);
        void Remove(std::uint64_t stack);
        std::uint32_t Count(std::uint64_t source) const;

      private:
        std::unordered_map<std::uint64_t, std::uint64_t> _sources;
        std::unordered_map<std::uint64_t, std::uint32_t> _counts;
    };
} // namespace KCDC::Features::GroundItem
