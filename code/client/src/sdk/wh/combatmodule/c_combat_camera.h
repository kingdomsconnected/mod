#pragma once

#include "../../types.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::combatmodule {
    class C_CombatCameraManager;

    /**
     * Common base for the native combat-camera implementations.
     *
     * RTTI proves this base for Guard, Attack, HitReaction, Death, Animate,
     * and Bone cameras. Its abstract table at 0x183B09B08 contains exactly
     * nine entries, while scalar deletion proves the 0x30-byte extent. The
     * operation names and signatures remain stripped and therefore opaque.
     */
    class C_CombatCamera {
      public:
        virtual ~C_CombatCamera() = default;
        virtual void unk_fn0001() = 0;
        virtual void unk_fn0002() = 0;
        virtual void unk_fn0003() {}
        virtual void unk_fn0004() {}
        virtual void unk_fn0005() {}
        virtual void unk_fn0006() {}
        virtual void unk_fn0007() {}
        virtual void unk_fn0008() = 0;

        C_CombatCameraManager *GetManager() const { return m_pManager; }
        void *GetState0010Raw() const { return m_pState0010; }
        std::uint32_t GetState0018Raw() const { return m_state0018; }
        float GetState001CRaw() const { return m_state001C; }
        const SDK::Vec3 &GetPosition0020Raw() const { return m_position0020; }
        float GetState002CRaw() const { return m_state002C; }

      private:
        C_CombatCameraManager *m_pManager; // 0x08
        void *m_pState0010;                // 0x10
        std::uint32_t m_state0018;         // 0x18
        float m_state001C;                 // 0x1C
        SDK::Vec3 m_position0020;           // 0x20
        float m_state002C;                 // 0x2C
    };

    static_assert(sizeof(C_CombatCamera) == 0x30,
                  "C_CombatCamera size mismatch");
} // namespace SDK::wh::combatmodule
