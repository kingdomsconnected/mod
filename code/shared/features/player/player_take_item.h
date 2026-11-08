#pragma once

#include "shared/item_stack.h"

#include <mafianet/bit_stream.h>
#include <networking/rpc/rpc_identifier.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace KCDC::Shared::RPC {
    /**
     * The server asking one player's own client to take items back out.
     *
     * Carries a `requestId` where `GiveItem` does not, because a grant always
     * lands and a take may not: only the owning client knows what the player is
     * holding, so the answer is the point.
     */
    struct TakeItem {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::TakeItem");

        static constexpr std::size_t kGuidLength = kItemClassGuidLength;

        std::uint64_t requestId = 0;
        std::array<char, kGuidLength> itemClassGuid {};
        std::uint32_t amount = 0;
        bool valid           = true;

        void SetGuid(std::string_view guid) {
            itemClassGuid.fill('\0');
            if (guid.size() == itemClassGuid.size()) {
                std::copy(guid.begin(), guid.end(), itemClassGuid.begin());
            }
        }

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            valid = bs->Serialize(write, requestId) && bs->Serialize(write, itemClassGuid.data(), static_cast<unsigned int>(itemClassGuid.size())) && bs->Serialize(write, amount);
        }

        /** Zero is refused rather than passed on, where the native would read it as every one of that class. */
        bool IsSane() const {
            return valid && requestId != 0 && amount != 0 && amount <= kMaxGiveAmount && IsItemClassGuidText(std::string_view(itemClassGuid.data(), itemClassGuid.size()));
        }

        std::string Guid() const {
            return std::string(itemClassGuid.begin(), itemClassGuid.end());
        }
    };

    /** What the owning client took out, against the request that asked. */
    struct TakeItemResult {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::TakeItemResult");

        std::uint64_t requestId = 0;

        /** Units actually removed, which may be fewer than asked for, or none. */
        std::uint32_t removed = 0;

        /** False when the client had no initialised inventory to look in at all. */
        bool answered = false;

        bool valid = true;

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            valid = bs->Serialize(write, requestId) && bs->Serialize(write, removed) && bs->Serialize(write, answered);
        }

        bool IsSane() const {
            return valid && requestId != 0 && removed <= kMaxGiveAmount && (answered || removed == 0);
        }
    };
} // namespace KCDC::Shared::RPC
