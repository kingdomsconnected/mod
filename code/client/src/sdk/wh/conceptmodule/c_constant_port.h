#pragma once

#include "c_output_data_port.h"
#include "c_port_ref.h"

#include "../../rttr/variant.h"
#include "../../types.h"

#include <cstddef>

namespace SDK::wh::conceptmodule {
    /**
     * An output port that answers a fixed value. This is how the concept
     * graph carries an authored literal: `C_ConceptGraphDeserializer::
     * CreatePorts` builds one per constant in the data and either replaces
     * the node's member port of that name or appends it.
     *
     * Its `GetValue` (slot 16) copies the stored variant and consults nothing
     * else, which makes it the cheapest thing a `C_PortRef` can resolve to,
     * and the only one the mod can build without an authored port definition.
     */
    class C_ConstantPort final: public C_OutputDataPort {
      public:
        /** The returned port carries one reference, which `C_Node::AddPort` consumes. `value` is moved out, so it has to be the game's `std::string` layout. */
        static C_ConstantPort *Create(const SDK::CryStringT<char> &name, SDK::StdString &value);

        /** The variant `GetValue` hands out, untouched since `Create` moved it in. */
        const SDK::rttr::Variant &GetStoredValue() const {
            return m_value;
        }

        /** The same storage, for a native that move-assigns a variant. `Create` only takes a string, so any other type is written through here. */
        SDK::rttr::Variant *GetValueStorage() {
            return &m_value;
        }

      private:
        SDK::rttr::Variant m_value; // 0x30

        static void AssertLayout() {
            static_assert(offsetof(C_ConstantPort, m_value) == 0x30, "C_ConstantPort::m_value offset mismatch");
        }
    };

    static_assert(sizeof(C_ConstantPort) == 0x48, "C_ConstantPort size mismatch");

    /**
     * Pins a port reference to a fresh constant port carrying `value`.
     *
     * The port is deliberately not handed to a node: resolution only walks the
     * owner's ports when the reference's cache is empty, and this fills the
     * cache. The reference holds the only claim on the port, and rebinding
     * releases the old one.
     */
    void BindConstantValue(C_PortRef &reference, const char *portName, const char *value);
} // namespace SDK::wh::conceptmodule
