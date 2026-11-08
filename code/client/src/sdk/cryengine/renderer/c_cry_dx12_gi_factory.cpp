#include "c_cry_dx12_gi_factory.h"

#include "../../patterns.h"

#include <utils/hooking/hooking.h>

namespace SDK {
    CCryDX12GIFactory *CCryDX12GIFactory::Create() {
        return hook::call<CCryDX12GIFactory *>(gPatterns.CCryDX12GIFactory__Create);
    }
} // namespace SDK
