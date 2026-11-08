#pragma once

#include <cstddef>
#include <cstdint>

namespace SDK {
    struct SViewParams;
}

namespace SDK::wh::game {
    /**
     * Common game-camera contract.
     *
     * C_CameraManager's constructor writes a four-byte camera-kind value at
     * +0x08 for each embedded implementation: first person 0, third person 1,
     * dialog 2, combat 3, minigame 4, and bone animated 5. The source enum
     * identifier is stripped, so the field remains offset-labelled raw state.
     */
    class C_Camera {
      public:
        virtual ~C_Camera() = default;
        virtual void unk_fn0001(SViewParams &viewParams) = 0;
        virtual void unk_fn0002() {}
        virtual void unk_fn0003() {}
        virtual void unk_fn0004() {}
        virtual void unk_fn0005() {}
        virtual const char *unk_fn0006() const = 0;
        virtual void unk_fn0007() {}
        virtual void unk_fn0008() {}
        virtual void unk_fn0009() {}
        virtual void unk_fn0010() {}
        virtual void unk_fn0011() {}
        virtual void unk_fn0012() {}

        std::uint32_t GetCameraKind0008Raw() const { return m_cameraKind0008; }

      private:
        std::uint32_t m_cameraKind0008; // 0x08
        std::byte m_padding000C[0x4];   // 0x0C
    };

    static_assert(sizeof(C_Camera) == 0x10, "C_Camera size mismatch");
} // namespace SDK::wh::game
