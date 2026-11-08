#pragma once

#include <cstddef>
#include <cstdint>

namespace SDK {
    // KCD2's 0x20-byte params at CAnimatedCharacter+0xD8. SetParams copies
    // exactly 32 bytes (1808AC370); UpdatePhysicsInertia (1808A3C84) reads
    // inertia at +0xDC and acceleration inertia at +0xE0. Preserve the opaque
    // fields by copying GetParams, never by default-constructing a replacement.
    struct alignas(8) SAnimatedCharacterParams {
        float GetInertia() const {
            return m_inertia;
        }
        float GetInertiaAccel() const {
            return m_inertiaAccel;
        }
        float GetTimeImpulseRecover() const {
            return m_timeImpulseRecover;
        }
        void SetInertia(float inertia, float acceleration) {
            m_inertia      = inertia;
            m_inertiaAccel = acceleration;
        }

      private:
        std::uint32_t m_field0000; // 0x00, ctor 0 (CAnimatedCharacter ctor 0x180704504); not in CryEngine
        float m_inertia;      // 0x04
        float m_inertiaAccel; // 0x08
        // 0x0C, ctor 0; C_Actor::Physicalize's tail (0x1808ABF14) writes the
        // template's impulse recovery time here, UpdatePhysicsInertia reads it.
        float m_timeImpulseRecover;
        float m_field0010;                 // 0x10, ctor 0.3, read as a float by SetDesiredLocalLocation
        std::byte m_opaque0014[0x4];       // 0x14
        void *m_pAnimationBlending;        // 0x18, IAnimationBlending *, ctor null (CryEngine's last member)

        static void AssertLayout() {
            static_assert(offsetof(SAnimatedCharacterParams, m_field0010) == 0x10, "SAnimatedCharacterParams::m_field0010 offset mismatch");
            static_assert(offsetof(SAnimatedCharacterParams, m_pAnimationBlending) == 0x18, "SAnimatedCharacterParams::m_pAnimationBlending offset mismatch");
            static_assert(offsetof(SAnimatedCharacterParams, m_inertia) == 0x04, "SAnimatedCharacterParams::m_inertia offset mismatch");
            static_assert(offsetof(SAnimatedCharacterParams, m_inertiaAccel) == 0x08, "SAnimatedCharacterParams::m_inertiaAccel offset mismatch");
        }
    };
    static_assert(sizeof(SAnimatedCharacterParams) == 0x20, "SAnimatedCharacterParams size mismatch");
} // namespace SDK
