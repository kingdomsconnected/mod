#include "c_device_resource_layout.h"

#include "../../patterns.h"

#include <utils/hooking/hooking.h>

namespace SDK {
    bool CDeviceResourceLayout_DX12::Init(
        const SDeviceResourceLayoutDesc &description) {
        return hook::this_call<bool>(gPatterns.CDeviceResourceLayout_DX12__Init, this, description);
    }
} // namespace SDK
