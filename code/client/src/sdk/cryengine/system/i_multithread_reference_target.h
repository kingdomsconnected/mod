#pragma once

#include <cstddef>
#include <cstdint>

namespace SDK {
    /**
     * Global CryEngine reference-counted base.
     * I_Action's base descriptor places this subobject at zero. Native
     * AddRef 0x181A72640 atomically increments the int at +8; Release
     * 0x1804A76F0 decrements it and dispatches deletion through slot zero.
     * Only the observed int specialization is modeled here.
     */
    template <typename T>
    class _i_multithread_reference_target {
      public:
        virtual ~_i_multithread_reference_target() = default;
        virtual void AddRef()                      = 0;
        virtual void Release()                     = 0;
        /** Whether this is the only reference (0x1825049D8). */
        virtual bool Unique() const = 0;

      private:
        static_assert(sizeof(T) == sizeof(std::int32_t));
        T m_referenceCount; // 0x08
        std::byte m_opaque000C[0x4];

        static void AssertLayout() {
            static_assert(offsetof(_i_multithread_reference_target, m_referenceCount) == 0x08);
        }
    };

    static_assert(sizeof(_i_multithread_reference_target<int>) == 0x10);
} // namespace SDK
