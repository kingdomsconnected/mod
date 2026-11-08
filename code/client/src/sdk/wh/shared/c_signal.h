#pragma once

#include <cstddef>
#include <cstdint>

namespace SDK::wh::shared {
    /**
     * A model signal: the dispatch a subscriber connects to and the two
     * subscriber handles beside it.
     *
     * The same 0x10 bytes `C_ModelProperty` carries at +0x18, standing on
     * their own as a member. Every constructor writes the dispatch, a
     * process-wide static per signal kind, and `kNoHandle` into both handles;
     * the kinds have no names in the shipped build, only their addresses.
     */
    class C_Signal {
      public:
        /** No subscriber; what the constructor writes into both handles. */
        static constexpr std::uint16_t kNoHandle = 0xFFFFU;

        bool HasSubscribers() const {
            return m_handleA != kNoHandle || m_handleB != kNoHandle;
        }

        /**
         * Tells every subscriber of a one-bool signal the new value, as the
         * game's own setters do once they have written the member the
         * signal guards (0x180393524; the camera request's raise at
         * 0x1816A3EC0 and its drop at 0x180A96740 call it).
         */
        void EmitBool(bool value);

        /** Address the game's own connect helper takes to subscribe. */
        const void *GetAddress() const {
            return this;
        }

      private:
        const void *m_pDispatch; // 0x00
        std::uint16_t m_handleA; // 0x08
        std::uint16_t m_handleB; // 0x0A
        std::byte m_padding000C[0x4];

        static void AssertLayout() {
            static_assert(offsetof(C_Signal, m_handleA) == 0x08, "C_Signal::m_handleA offset mismatch");
        }
    };

    static_assert(sizeof(C_Signal) == 0x10, "C_Signal size mismatch");
} // namespace SDK::wh::shared
