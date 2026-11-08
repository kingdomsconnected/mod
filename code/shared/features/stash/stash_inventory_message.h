#pragma once

#include "shared/features/inventory/inventory_message.h"

namespace KCDC::Shared::RPC {
    struct StashInventorySnapshot: InventoryMessage {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::StashInventorySnapshot");
    };

    struct StashInventoryTransfer: InventoryMessage {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::StashInventoryTransfer");
    };

    /** Server-authorized virtual view, including its initial inventory snapshot. */
    struct StashVirtualOpen: InventoryMessage {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::StashVirtualOpen");
    };
} // namespace KCDC::Shared::RPC
