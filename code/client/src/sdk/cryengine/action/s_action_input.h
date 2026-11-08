#pragma once

#include <cstddef>
#include <cstdint>

namespace SDK {
    /**
     * One input bound to an action. 0x108 bytes, as
     * `CActionMapAction::AddAndGetActionInput` (0x180A10DA0) allocates; only
     * read in place. The name's string object is at +0x08 with its text
     * pointer at +0x18, which `CActionMap::ReBindActionInput` (0x180A12A9C)
     * compares and prints directly.
     */
    class SActionInput {
      public:
        /** The engine's key name: "w", "space", "lshift", "mouse1", "xi_a". Never null. */
        const char *GetInput() const {
            return m_input != nullptr ? m_input : "";
        }

        /** The lowercase CRC32 of the name, which is how the engine finds an input. */
        std::uint32_t GetInputCrc() const {
            return m_inputCrc;
        }

      private:
        std::int32_t m_inputDevice;   // 0x00, EActionInputDevice
        std::byte m_opaque0004[0x14]; // 0x04, the rest of the name's string object
        const char *m_input;          // 0x18
        std::byte m_opaque0020[0x78]; // 0x20, the default input's string object at 0x40
        std::uint32_t m_inputCrc;     // 0x98
        std::byte m_opaque009C[0x6C]; // 0x9C

        static void AssertLayout() {
            static_assert(offsetof(SActionInput, m_inputDevice) == 0x00, "SActionInput::m_inputDevice offset mismatch");
            static_assert(offsetof(SActionInput, m_input) == 0x18, "SActionInput::m_input offset mismatch");
            static_assert(offsetof(SActionInput, m_inputCrc) == 0x98, "SActionInput::m_inputCrc offset mismatch");
        }
    };

    static_assert(sizeof(SActionInput) == 0x108, "SActionInput size mismatch");
} // namespace SDK
