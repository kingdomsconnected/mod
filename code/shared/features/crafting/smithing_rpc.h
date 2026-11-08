#pragma once

#include <mafianet/bit_stream.h>
#include <networking/rpc/rpc_identifier.h>

#include <cmath>
#include <cstdint>
#include <string>

namespace KCDC::Shared::RPC {
    /** The longest smithing recipe id the shipped table uses is 104 characters. */
    inline constexpr std::size_t kMaxSmithingRecipeId = 128;

    /** How the server answered a smithing request. */
    enum class SmithingCode : std::uint8_t {
        Accepted = 0,

        /** The player's skills and perks have not reached the server yet. */
        CharacterUnavailable = 1,

        /** The station is not one of this level's smitheries. */
        UnknownStation = 2,

        /** The player is too far from the station, or cannot act. */
        NotAuthorized = 3,

        /** The player already has a workpiece on some anvil. */
        PlayerBusy = 4,

        /** Somebody else is working at this smithery. */
        StationBusy = 5,

        /** The recipe is not in the catalog, or the server's DLC policy excludes it. */
        UnknownRecipe = 6,

        /** The player does not have the perk that teaches the recipe. */
        RecipeNotKnown = 7,

        /** The player's craftsmanship is below the recipe's minimum. */
        SkillTooLow = 8,

        /** The inventory lacks a material the recipe needs. */
        MissingMaterials = 9,

        /** The server holds no inventory for the player. */
        InventoryUnavailable = 10,

        /** A `craftingStarting` handler refused the workpiece. */
        StartingRejected = 11,

        /** The named workpiece is not the player's current one. */
        UnknownSession = 12,

        /** The reported quality names no product; the workpiece failed. */
        InvalidQuality = 13,

        /** A `craftingCompleting` handler refused the product; the workpiece failed. */
        CompletingRejected = 14,

        /** An abandon arrived after the workpiece could have been worked; it failed. */
        AbandonTooLate = 15,

        /** The workpiece outlived the server's deadline before its report arrived; it failed. */
        SessionExpired = 16,
    };

    /** How the native minigame ended a workpiece. */
    enum class SmithingOutcome : std::uint8_t {
        /** Finished: the game would grant the product. */
        Success = 0,

        /** Broken at the anvil: the game spends part of the materials. */
        Failure = 1,

        /** The approved recipe never reached the anvil: nothing was worked. */
        Abandoned = 2,

        /** The native cancel path, distinct from a broken workpiece. */
        Cancelled = 3,

        /** The native session disappeared without a result or a normal cancel. */
        Interrupted = 4,
    };

    /** Why the server ended a workpiece on its own. */
    enum class SmithingCloseReason : std::uint8_t {
        /** A script cancelled it. */
        Cancelled = 0,

        /** It outlived the session lifetime. */
        Timeout = 1,

        /** The player walked away, died or changed world. */
        ContextInvalidated = 2,
    };

    /** Client -> server: the player pressed Craft on a recipe at a smithery. */
    struct SmithingBegin {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::SmithingBegin");

        std::uint32_t request = 0;
        std::uint64_t station = 0;
        std::string recipe;
        bool valid = true;

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            std::uint8_t length = write ? static_cast<std::uint8_t>(recipe.size()) : 0;
            valid               = (!write || recipe.size() <= kMaxSmithingRecipeId) && bs->Serialize(write, request) && bs->Serialize(write, station) && bs->Serialize(write, length) && length <= kMaxSmithingRecipeId;
            if (!valid) {
                return;
            }
            if (!write) {
                recipe.resize(length);
            }
            valid = bs->Serialize(write, recipe.data(), length);
        }

        bool IsSane() const {
            return valid && request != 0 && station != 0 && !recipe.empty();
        }
    };

    /**
     * Client -> server: the workpiece is over.
     *
     * `quality` is the workpiece's own quality as the minigame left it; the
     * server picks the product from it and ignores it for anything but a
     * success.
     */
    struct SmithingFinish {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::SmithingFinish");

        std::uint32_t session = 0;
        std::uint8_t outcome  = static_cast<std::uint8_t>(SmithingOutcome::Failure);
        float quality         = 0.0F;
        bool valid            = true;

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            valid = bs->Serialize(write, session) && bs->Serialize(write, outcome) && bs->Serialize(write, quality);
        }

        bool IsSane() const {
            return valid && session != 0 && outcome <= static_cast<std::uint8_t>(SmithingOutcome::Interrupted) && std::isfinite(quality);
        }
    };

    /** Server -> client: the answer to one `SmithingBegin`, and the session it opened. */
    struct SmithingVerdict {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::SmithingVerdict");

        std::uint32_t request = 0;
        std::uint32_t session = 0;
        std::uint8_t code     = static_cast<std::uint8_t>(SmithingCode::Accepted);
        bool valid            = true;

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            valid = bs->Serialize(write, request) && bs->Serialize(write, session) && bs->Serialize(write, code);
        }

        bool IsSane() const {
            return valid && request != 0 && code <= static_cast<std::uint8_t>(SmithingCode::SessionExpired) && (session != 0) == (code == static_cast<std::uint8_t>(SmithingCode::Accepted));
        }
    };

    /** Server -> client: the server ended this workpiece; the game's minigame is to be cancelled. */
    struct SmithingClosed {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::SmithingClosed");

        std::uint32_t session = 0;
        std::uint8_t reason   = static_cast<std::uint8_t>(SmithingCloseReason::Cancelled);
        bool valid            = true;

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            valid = bs->Serialize(write, session) && bs->Serialize(write, reason);
        }

        bool IsSane() const {
            return valid && session != 0 && reason <= static_cast<std::uint8_t>(SmithingCloseReason::ContextInvalidated);
        }
    };
} // namespace KCDC::Shared::RPC
