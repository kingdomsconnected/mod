#pragma once

#include "shared/features/inventory/inventory_message.h"

namespace KCDC::Shared::RPC {
    struct GroundItemDropRequest: InventoryMessage {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::GroundItemDropRequest");
    };
} // namespace KCDC::Shared::RPC
