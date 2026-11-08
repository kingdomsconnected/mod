#pragma once

#include "shared/item_stack.h"

#include <mafianet/bit_stream.h>
#include <networking/rpc/rpc_identifier.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>

namespace KCDC::Shared::RPC {
    struct GiveItem {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::GiveItem");

        static constexpr std::size_t kGuidLength = kItemClassGuidLength;

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
            valid = bs->Serialize(write, itemClassGuid.data(), static_cast<unsigned int>(itemClassGuid.size())) && bs->Serialize(write, amount);
        }

        bool IsSane() const {
            return valid && amount != 0 && amount <= kMaxGiveAmount && IsItemClassGuidText(std::string_view(itemClassGuid.data(), itemClassGuid.size()));
        }

        std::string Guid() const {
            return std::string(itemClassGuid.begin(), itemClassGuid.end());
        }
    };
} // namespace KCDC::Shared::RPC
