#include "c_node.h"

#include "../../patterns.h"

#include <utils/hooking/hooking.h>

namespace SDK::wh::conceptmodule {
    bool C_Node::AttachPort(I_Port *port) {
        I_Port *handle = port;
        return hook::this_call<bool>(gPatterns.C_Node__AddPort, this, &handle);
    }

    I_Port *C_Node::FindPortByName(const SDK::CryStringT<char> &name) const {
        I_Port *result = nullptr;
        hook::this_call<void>(gPatterns.C_Node__FindPortByName, this, &result, &name);
        return result;
    }
} // namespace SDK::wh::conceptmodule
