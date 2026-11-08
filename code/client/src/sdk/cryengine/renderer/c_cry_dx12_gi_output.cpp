#include "c_cry_dx12_gi_output.h"

#include "../../patterns.h"

#include <utils/hooking/hooking.h>

namespace SDK {
    CCryDX12GIOutput *CCryDX12GIOutput::Create(
        CCryDX12GIAdapter *adapter, std::uint32_t output) {
        return hook::call<CCryDX12GIOutput *>(gPatterns.CCryDX12GIOutput__Create, adapter, output);
    }
} // namespace SDK
