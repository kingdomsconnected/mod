#pragma once

#include <cstdint>

namespace KCDC::Shared::Game {
    /**
     * What decides whether a body may take an item into its arms.
     *
     * One set of rules for both ends: the server asks it before it orders a
     * carry, and the carrier's client before it offers the pick-up prompt,
     * so the prompt never offers what the server would turn down.
     */
    struct ItemCarryState {
        // Alive, conscious and in control of the body: `HumanSoulState::CanAct`.
        bool canAct = false;
        // In a saddle or a cart, where the use key is the game's own.
        bool mounted = false;
        // A body on the shoulder; the game holds one load at a time.
        bool carryingBody = false;
        bool carryingItem = false;
    };

    /** Why this body may not take an item up, or null when it may. */
    inline const char *ItemCarryRefusal(const ItemCarryState &state) {
        if (!state.canAct) {
            return "they cannot act";
        }
        if (state.mounted) {
            return "they are riding";
        }
        if (state.carryingBody) {
            return "they are carrying somebody";
        }
        if (state.carryingItem) {
            return "they are already carrying an item";
        }
        return nullptr;
    }

    /** A carry a client reported as started, as the server sees it. */
    struct ItemCarryReport {
        // The item tables have the class.
        bool classKnown = false;
        // Which bodies have a carrying walk for it (`CarryWalkBodies`).
        std::uint8_t walkBodies = 0;
        // It answers the order the server gave this carrier.
        bool ordered = false;
        // The ground item it says it took up, or zero.
        std::uint64_t groundItemNetworkId = 0;
        // That ground item is claimed for this carrier already.
        bool groundClaimed = false;
        // That ground item could be claimed for this carrier now: it exists,
        // is within reach, and nothing else holds it.
        bool groundClaimable = false;
    };

    enum class ItemCarryVerdict : std::uint8_t {
        // Accepted as it stands: the server decided it already.
        Accept,
        // The player started it -- at a pile, or at a prop the game's own
        // prompt carried -- so the gamemode has the say.
        AskGamemode,
        Refuse,
    };

    /**
     * How the server answers a reported carry, and why when it refuses.
     *
     * Every other client makes an item of the class to replay the carry, so
     * a class the game lacks is refused, and one with no carrying walk is
     * refused unless the server itself asked for it. A ground item is only
     * taken up by the carrier it is claimed for -- or, when the game's own
     * prompt started the carry, one that could still be claimed for them.
     */
    inline ItemCarryVerdict JudgeItemCarryReport(const ItemCarryReport &report, const char *&refusal) {
        refusal = nullptr;
        if (!report.classKnown) {
            refusal = "the game has no such item class";
            return ItemCarryVerdict::Refuse;
        }
        if (report.groundItemNetworkId != 0) {
            if (report.groundClaimed) {
                return ItemCarryVerdict::Accept;
            }
            if (!report.ordered && report.groundClaimable) {
                return ItemCarryVerdict::AskGamemode;
            }
            refusal = "that ground item is not theirs to take up";
            return ItemCarryVerdict::Refuse;
        }
        if (report.ordered) {
            return ItemCarryVerdict::Accept;
        }
        if (report.walkBodies == 0) {
            refusal = "nobody carries that item in their arms";
            return ItemCarryVerdict::Refuse;
        }
        return ItemCarryVerdict::AskGamemode;
    }
} // namespace KCDC::Shared::Game
