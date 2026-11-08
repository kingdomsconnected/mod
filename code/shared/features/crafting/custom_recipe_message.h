#pragma once

#include "shared/features/inventory/inventory_message.h"

#include <cstddef>

namespace KCDC::Shared::RPC {
    /**
     * Server to client: recipes scripts registered, for the game's own recipe
     * tables. A script's recipe is fixed for the session, so the list only
     * grows: `{"from": n, "recipes": [CustomRecipeToJson...]}` carries entries
     * n onwards of the server's registration order, and a client that already
     * holds an entry ignores it.
     */
    struct CustomRecipeDefinitions: InventoryMessage {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::CustomRecipeDefinitions");
        // A message is filled up to this many bytes. The largest definition the server accepts --
        // 28 steps of 1024-character conditions, 64 ingredients and products, about 40 KB, and
        // 256 KiB of pictures as base64, 342 KiB -- still fits the envelope alone.
        static constexpr std::size_t kBudgetBytes = kMaximumBytes / 2;
    };
} // namespace KCDC::Shared::RPC
