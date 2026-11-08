#pragma once

#include "shared/features/inventory/inventory_message.h"

namespace KCDC::Shared::RPC {
    /**
     * Owner to server: a native use or shot spent units of the projected
     * inventory. `{ request, kind: "use" | "shot", units: [{ id, amount }] }`.
     * A repair is `{ request, kind: "repair", equipment: [{ id, before,
     * after, equipped }], kits: [{ id, before, after, spent }] }`, one unit
     * each. A kit used across several selected items appears only once.
     * The owner holds its projection until the answer's
     * revision is synced;
     * every one is answered, a refused one included.
     */
    struct InventoryConsume: InventoryMessage {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::InventoryConsume");
    };

    /** Server to owner: `{ request, ok, code, revision }` for one `InventoryConsume`. */
    struct InventoryConsumeResult: InventoryMessage {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::InventoryConsumeResult");
    };

    /**
     * Owner to server: what the body wears, `{ revision, items: { rowId: wornUnits } }`,
     * at most 48 rows. `revision` is the inventory revision the owner had
     * synced: a report older than a script's restore is ignored.
     */
    struct InventoryEquipment: InventoryMessage {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::InventoryEquipment");
    };

    /** Owner to server: a round it fired came to rest, `{ item, landed, position: [x, y, z] }`; `landed` is the broken round when the impact broke it. */
    struct InventoryRecover: InventoryMessage {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::InventoryRecover");
    };
} // namespace KCDC::Shared::RPC
