#pragma once

#include "i_clothing_system.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::entitymodule {
    /**
     * Global runtime clothing system.
     *
     * The primary table at 0x183E8B8A8 contains three inherited
     * I_ClothingSystem entries plus three opaque concrete entries. The
     * scalar deleting destructor and owner allocation both prove a 0x1C8
     * size. An I_ReadinessTask/I_ReadinessDebuggable secondary subobject
     * begins at +0x08.
     */
    class C_ClothingSystem : public I_ClothingSystem {
      public:
        ~C_ClothingSystem() override = default;

        virtual void unk_fn0003() = 0;
        virtual void unk_fn0004() = 0;
        virtual void unk_fn0005() = 0;

        static C_ClothingSystem *GetInstance();

      private:
        void *m_readinessTaskVptr; // +0x08
        // Ctor 0x1807D04D4; 0x1807D0730 builds 0x10-0x170: five 0x28-byte
        // entries (0x1803DF440), a container at 0xD8 (0x181666B34) and one at
        // 0x100 (0x1807D0848). None has an SDK type yet.
        std::byte m_opaque0010[0x158];
        void *m_pSelf0168;             // 0x168, 0x1807D0730 stores this+0x10
        std::byte m_opaque0170[0x8];   // 0x170, not written
        void *m_pSelf0178;             // 0x178, ctor stores this+0x10
        void *m_pSelf0180;             // 0x180, ctor stores this+0x170
        std::uint64_t m_value0188;     // 0x188, zeroed
        std::uint64_t m_value0190;     // 0x190, zeroed
        std::uint64_t m_value0198;     // 0x198, zeroed
        std::uint64_t m_value01A0;     // 0x1A0, zeroed
        std::uint64_t m_value01A8;     // 0x1A8, zeroed
        bool m_flag01B0;               // 0x1B0, zeroed
        std::byte m_padding01B1[0x7];
        std::uint64_t m_value01B8;     // 0x1B8, zeroed
        float m_value01C0;             // 0x1C0, ctor sets -1.0
        std::uint32_t m_value01C4;     // 0x1C4, zeroed

        static void AssertLayout() {
            static_assert(offsetof(C_ClothingSystem, m_pSelf0168) == 0x168, "C_ClothingSystem::m_pSelf0168 offset mismatch");
            static_assert(offsetof(C_ClothingSystem, m_flag01B0) == 0x1B0, "C_ClothingSystem::m_flag01B0 offset mismatch");
            static_assert(offsetof(C_ClothingSystem, m_value01C0) == 0x1C0, "C_ClothingSystem::m_value01C0 offset mismatch");
        }
    };

    static_assert(sizeof(C_ClothingSystem) == 0x1C8, "C_ClothingSystem size mismatch");
} // namespace SDK::wh::entitymodule
