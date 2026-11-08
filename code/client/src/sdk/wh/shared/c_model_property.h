#pragma once

#include <cstddef>
#include <cstdint>

namespace SDK::wh::shared {
    /**
     * One field of a game model: a value with a change signal beside it.
     *
     * Warhorse's model system is a property container rather than a plain
     * struct. Every entry is this shape regardless of what it holds, which is
     * what makes a model enumerable at a fixed stride, and its four-slot table
     * is the same for every instantiation: the deleting destructor, a getter
     * folded onto `return *(this + 8)`, a name accessor that answers nothing
     * in the shipped build, and `GetSignal` folded onto `return this + 24`.
     *
     * There is no virtual setter. Most writes are inlined at the call site,
     * which fires the signal itself, so `SetValueWithoutSignal` below is the
     * write half of that and nothing more -- a subscriber will not hear it.
     * The bool instantiation does have an out-of-line setter that signals,
     * `C_ModelProperty<bool>::SetValue` (0x18072FA34); a model that needs it
     * calls it for its own field.
     *
     * The signal's own fields are spelled out here rather than factored into a
     * type of their own: it occupies `0x18` to `0x2C`, and a struct holding a
     * pointer cannot be twenty bytes long. Only its address is ever handed
     * out, which is all the game's connect helper takes.
     *
     * The value occupies a whole eight-byte slot whatever its width, which is
     * why it is unioned rather than padded: every type the combat model uses
     * is four bytes or less except CTimeValue, and all of them start at +0x08.
     */
    template <typename T>
    class C_ModelProperty {
      public:
        static_assert(sizeof(T) <= 0x8, "C_ModelProperty value does not fit its slot");

        /** No subscriber; what the constructor writes into both handles. */
        static constexpr std::uint16_t kNoHandle = 0xFFFFU;

        T GetValue() const {
            return m_value;
        }

        /**
         * Writes the value without telling anyone.
         *
         * This is what a replicated body wants: the network is the authority
         * for the field, and re-entering the local subsystems that subscribed
         * to it is the opposite of the intent. Named for what it omits so no
         * caller assumes otherwise.
         */
        void SetValueWithoutSignal(T value) {
            m_value = value;
        }

        /** The whole slot, for a field whose type is not yet established. */
        std::uint64_t GetValueBits() const {
            return m_valueBits;
        }

        bool HasSubscribers() const {
            return m_signalHandleA != kNoHandle || m_signalHandleB != kNoHandle;
        }

        /** Address the game's own connect helper takes to subscribe. */
        const void *GetSignalAddress() const {
            return &m_pSignalDispatch;
        }

      private:
        const void *m_pVTable; // 0x00
        union {
            T m_value;                 // 0x08
            std::uint64_t m_valueBits; // 0x08
        };
        const void *m_pOwner;          // 0x10
        const void *m_pSignalDispatch; // 0x18
        std::uint16_t m_signalHandleA; // 0x20
        std::uint16_t m_signalHandleB; // 0x22
        std::byte m_padding0024[0x4];  // 0x24, the signal's own tail padding
        std::byte m_unset0028[0x4];    // 0x28, no property ctor writes it (e.g. C_CombatActorModel's 0x1810F1480 stops at +0x22)
        std::uint32_t m_customDefault; // 0x2C, only for custom-default traits
    };

    static_assert(sizeof(C_ModelProperty<bool>) == 0x30, "C_ModelProperty<bool> size mismatch");
    static_assert(sizeof(C_ModelProperty<std::int32_t>) == 0x30, "C_ModelProperty<int32> size mismatch");
    static_assert(sizeof(C_ModelProperty<float>) == 0x30, "C_ModelProperty<float> size mismatch");
    static_assert(sizeof(C_ModelProperty<std::int64_t>) == 0x30, "C_ModelProperty<int64> size mismatch");

    /**
     * A model property holding a reference to another object.
     *
     * Same stride and the same signal position; only the value slot means
     * something different. The combat model uses these for the opponent and
     * for seven further references whose targets are not yet established, so
     * the pointer is handed out raw.
     */
    class C_ModelRefProperty {
      public:
        const void *GetTarget() const {
            return m_pTarget;
        }

        const void *GetSignalAddress() const {
            return &m_pSignalDispatch;
        }

      private:
        const void *m_pVTable;         // 0x00
        const void *m_pTarget;         // 0x08
        const void *m_pOwner;          // 0x10
        const void *m_pSignalDispatch; // 0x18
        std::uint16_t m_signalHandleA; // 0x20
        std::uint16_t m_signalHandleB; // 0x22
        std::byte m_padding0024[0x4];  // 0x24, the signal's own tail padding
        std::byte m_unset0028[0x8];    // 0x28, not written by the model ctors
    };

    static_assert(sizeof(C_ModelRefProperty) == 0x30, "C_ModelRefProperty size mismatch");

    /**
     * A model field several subsystems ask for at once, resolved by taking
     * the highest request.
     *
     * Five sources write their own byte; the effective value beside them is
     * the maximum of the five, and the signal fires only when that maximum
     * moves. The engine's own setter is
     * `wh::shared::C_ModelMaxProperty::SetSource` at 0x180395D9C, which is
     * where this layout is read from -- sources at `+0x18`, effective at
     * `+0x1D`, signal at `+0x08`, and nothing else in the twenty bytes
     * between the table pointer and the end of the property.
     *
     * The combat model uses one of these for the guard: see
     * `C_CombatActorModel::GetGuardRequest`.
     */
    class C_ModelMaxProperty {
      public:
        static constexpr std::size_t kSourceCount = 5;

        /** What one source last asked for, or 0 when it has asked for nothing. */
        std::uint8_t GetSource(std::size_t source) const {
            return source < kSourceCount ? m_sources[source] : 0;
        }

        /** The highest of the five, which is what the model actually holds. */
        std::uint8_t GetEffective() const {
            return m_effective;
        }

      private:
        const void *m_pVTable;                  // 0x00
        std::byte m_signal[0x10];               // 0x08
        std::uint8_t m_sources[kSourceCount];   // 0x18
        std::uint8_t m_effective;               // 0x1D
        std::byte m_padding001E[0x2];           // 0x1E

        static void AssertLayout() {
            static_assert(offsetof(C_ModelMaxProperty, m_sources) == 0x18, "C_ModelMaxProperty::m_sources offset mismatch");
            static_assert(offsetof(C_ModelMaxProperty, m_effective) == 0x1D, "C_ModelMaxProperty::m_effective offset mismatch");
        }
    };

    static_assert(sizeof(C_ModelMaxProperty) == 0x20, "C_ModelMaxProperty size mismatch");
} // namespace SDK::wh::shared
