#include "c_item_database.h"

#include "../../patterns.h"

#include <utils/hooking/hooking.h>

namespace SDK::wh::entitymodule {
    C_ItemDatabase *C_ItemDatabase::GetInstance() {
        return reinterpret_cast<C_ItemDatabase *>(gPatterns.C_ItemDatabase__Instance);
    }

    const S_ItemClass *C_ItemDatabase::FindItemClass(const S_ItemClassGUID &itemClass) const {
        return hook::this_call<const S_ItemClass *>(gPatterns.C_ItemDatabase__FindItemClass, this, &itemClass);
    }
} // namespace SDK::wh::entitymodule
