#pragma once

#include <cstddef>
#include <type_traits>

namespace SDK::wh::guimodule {
    /**
     * MSVC's `std::_Ref_count_base`, the control block behind every native
     * `shared_ptr`. The layout is the game's own: its inline refcounting reads
     * `uses` and `weaks` as the two dwords after the vtable.
     */
    struct SNativeRefCount {
        void *vftable;       // 0x00, destroy at slot 0, delete at slot 1
        volatile long uses;  // 0x08
        volatile long weaks; // 0x0C
    };

    static_assert(sizeof(SNativeRefCount) == 0x10, "SNativeRefCount size mismatch");

    /**
     * One `std::shared_ptr<T>` the mod owns, laid out as the native pair so it
     * can be passed straight into a native that takes one by value.
     *
     * Move-only: every copy the mod makes is a deliberate hand-over to a
     * native that consumes it, which Clone() spells out at the call site.
     * Release goes through the game's own `_Decref` because the control
     * block's destroy and delete are virtuals that must run on the heap that
     * allocated the object; the increment does not, being a plain atomic with
     * no dispatch.
     *
     * Every `T` it is used with is instantiated explicitly in
     * native_shared_ref.cpp, so a new one is added there.
     */
    template <typename T>
    class NativeSharedRef final {
      public:
        NativeSharedRef() = default;

        ~NativeSharedRef() {
            Release();
        }

        NativeSharedRef(const NativeSharedRef &)            = delete;
        NativeSharedRef &operator=(const NativeSharedRef &) = delete;

        NativeSharedRef(NativeSharedRef &&other) noexcept: m_object(other.m_object), m_control(other.m_control) {
            other.m_object  = nullptr;
            other.m_control = nullptr;
        }

        /**
         * The `shared_ptr<Derived>` to `shared_ptr<Base>` conversion, for a
         * native that takes a container of the base. Every engine type this
         * is used with derives singly, so the object pointer is unchanged.
         */
        template <typename Derived, typename = std::enable_if_t<std::is_base_of_v<T, Derived>>>
        NativeSharedRef(NativeSharedRef<Derived> &&other) noexcept: m_object(other.m_object)
                                                                  , m_control(other.m_control) {
            other.m_object  = nullptr;
            other.m_control = nullptr;
        }

        NativeSharedRef &operator=(NativeSharedRef &&other) noexcept {
            if (this != &other) {
                Release();
                m_object        = other.m_object;
                m_control       = other.m_control;
                other.m_object  = nullptr;
                other.m_control = nullptr;
            }
            return *this;
        }

        T *Get() const {
            return m_object;
        }

        explicit operator bool() const {
            return m_object != nullptr;
        }

        /** A second owned reference, for a native that consumes one. */
        NativeSharedRef Clone() const {
            NativeSharedRef copy;
            copy.m_object  = m_object;
            copy.m_control = m_control;
            if (copy.m_control != nullptr) {
                Incref(copy.m_control);
            }
            return copy;
        }

        void Release();

        /** The pair itself, which is what a native writes or reads through. */
        void *Slot() {
            return this;
        }

        /** Forgets the pair without releasing it, after a native consumed it. */
        void Disown() {
            m_object  = nullptr;
            m_control = nullptr;
        }

      private:
        template <typename Other>
        friend class NativeSharedRef;

        static void Incref(SNativeRefCount *control);

        T *m_object                = nullptr; // 0x00
        SNativeRefCount *m_control = nullptr; // 0x08

        static void AssertLayout() {
            static_assert(offsetof(NativeSharedRef, m_object) == 0x0, "NativeSharedRef::m_object offset mismatch");
            static_assert(offsetof(NativeSharedRef, m_control) == 0x8, "NativeSharedRef::m_control offset mismatch");
            static_assert(sizeof(NativeSharedRef) == 0x10, "NativeSharedRef must match std::shared_ptr's two-pointer layout");
        }
    };
} // namespace SDK::wh::guimodule
