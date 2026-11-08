#pragma once

#include "../../types.h"

#include <cstddef>

namespace SDK::wh::rpgmodule {
    /**
     * Base of every cause the RPG event bus carries. All 95 concrete causes
     * are `C_XCause : C_Cause<S_XData> : I_Cause`, so the vtable is four slots
     * wide for every one and the payload always begins at 0x08.
     */
    class I_Cause {
      public:
        virtual ~I_Cause() = default;
        virtual void unk_fn0001() = 0;

        /** Double dispatch into the effect-building visitor, at visitor slot 51. */
        virtual void Accept(void *visitor) = 0;

        /** The cause's name, as a function-local static that outlives the cause. */
        virtual const CryStringT<char> *GetName() const = 0;
    };

    static_assert(sizeof(I_Cause) == 0x8);

    /** `Payload` is the `S_XData` the factory copies in by value, and the only state a cause carries. */
    template <typename Payload> class C_Cause: public I_Cause {
      public:
        const Payload &GetPayload() const {
            return m_payload;
        }

      private:
        Payload m_payload; // 0x08

        static void AssertLayout() {
            static_assert(offsetof(C_Cause, m_payload) == 0x8);
        }
    };
} // namespace SDK::wh::rpgmodule
