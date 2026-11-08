#pragma once

#include "shared/features/inventory/inventory_message.h"

namespace KCDC::Shared::RPC {
    struct GatheringRequest: InventoryMessage {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::GatheringRequest");
    };
    struct GatheringState: InventoryMessage {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::GatheringState");
    };
} // namespace KCDC::Shared::RPC
