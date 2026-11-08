#pragma once

#include <cstddef>
#include <cstdint>

namespace SDK::wh::shared {
    /**
     * Native listener collection with one re-entrancy cursor per callback.
     *
     * C_Listeners<I_ItemSlotListener, 1> is constructor- and sized-delete-
     * proven to occupy 0x28 bytes. The callback count controls the terminal
     * int32 cursor array; every cursor is initialized to -1 while idle.
     */
    template <typename T, std::size_t CallbackCount> class C_Listeners {
      public:
        virtual ~C_Listeners() = default;

        std::size_t GetCount() const {
            return m_pListenersBegin != nullptr
                       ? static_cast<std::size_t>(m_pListenersEnd -
                                                  m_pListenersBegin)
                       : 0;
        }

        T *GetListener(std::size_t index) const {
            return index < GetCount() ? m_pListenersBegin[index] : nullptr;
        }

      private:
        T **m_pListenersBegin;    // 0x08
        T **m_pListenersEnd;      // 0x10
        T **m_pListenersCapacity; // 0x18
        std::int32_t m_dispatchCursors[CallbackCount]; // 0x20
    };

    static_assert(sizeof(C_Listeners<void, 1>) == 0x28,
                  "C_Listeners<T, 1> size mismatch");
} // namespace SDK::wh::shared
