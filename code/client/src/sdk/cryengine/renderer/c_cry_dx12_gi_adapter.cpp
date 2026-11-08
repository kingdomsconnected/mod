#include "c_cry_dx12_gi_adapter.h"

#include "../../patterns.h"

#include <utils/hooking/hooking.h>

namespace SDK {
    CCryDX12GIAdapter *CCryDX12GIAdapter::Create(
        CCryDX12GIFactory *factory, std::uint32_t adapter) {
        return hook::call<CCryDX12GIAdapter *>(gPatterns.CCryDX12GIAdapter__Create, factory, adapter);
    }
} // namespace SDK
