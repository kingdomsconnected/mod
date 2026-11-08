#include "c_constant_port.h"

#include "../../patterns.h"

#include <utils/hooking/hooking.h>

namespace SDK::wh::conceptmodule {
    C_ConstantPort *C_ConstantPort::Create(const SDK::CryStringT<char> &name, SDK::StdString &value) {
        C_ConstantPort *port = nullptr;
        hook::call<void>(gPatterns.C_ConstantPort__Create, &port, &name, &value);
        return port;
    }

    void BindConstantValue(C_PortRef &reference, const char *portName, const char *value) {
        const SDK::ScopedCryString name(portName);
        SDK::StdString text(value);

        C_ConstantPort *port = C_ConstantPort::Create(name, text);
        reference.BindResolvedTarget(port);

        // BindResolvedTarget took its own reference; the one Create handed
        // back is this function's to drop.
        port->Release();
    }
} // namespace SDK::wh::conceptmodule
