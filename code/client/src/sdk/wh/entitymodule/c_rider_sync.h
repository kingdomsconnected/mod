#pragma once

#include "../shared/c_signal.h"
#include "i_horse_rider_sync.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::entitymodule {
    class C_Actor;
    class C_Horse;
    class C_RiderSync;

    struct RiderSyncRuntimeStateLayout {
        C_RiderSync *m_pOwner; // 0x00
        float m_state0008; // 0x08, initialized to -1.0f
        std::uint32_t m_state000C;
        bool m_flag0010;
        bool m_flag0011;
        std::byte m_padding0012[0x6];
    };

    struct RiderSyncAnimationLayerLayout {
        std::uint32_t m_layerIndex;
        std::int32_t m_layerId;
    };

    struct RiderSyncAnimationLayerVectorLayout {
        RiderSyncAnimationLayerLayout *m_pBegin;
        RiderSyncAnimationLayerLayout *m_pEnd;
        RiderSyncAnimationLayerLayout *m_pCapacity;

        std::size_t GetCount() const {
            return m_pBegin != nullptr
                       ? static_cast<std::size_t>(m_pEnd - m_pBegin)
                       : 0;
        }
    };

    static_assert(sizeof(RiderSyncRuntimeStateLayout) == 0x18,
                  "RiderSyncRuntimeStateLayout size mismatch");
    static_assert(sizeof(RiderSyncAnimationLayerLayout) == 0x8,
                  "RiderSyncAnimationLayerLayout size mismatch");
    static_assert(sizeof(RiderSyncAnimationLayerVectorLayout) == 0x18,
                  "RiderSyncAnimationLayerVectorLayout size mismatch");

    /**
     * Concrete synchronization state joining a horse and rider.
     *
     * RTTI proves I_HorseRiderSync at +0x00 and the non-primary
     * UnsafeOp::CryDeferrable<0> state at +0x08. Its table at 0x183EAACF0
     * implements exactly the 24 inherited entries and adds no operations.
     * Construction stores the rider C_Actor at +0x28 and C_Horse at +0x30;
     * slot 7 dispatches through the rider while horse-runtime registration
     * uses the horse pointer. Sized deletion proves the complete 0x58-byte
     * layout.
     */
    class C_RiderSync : public I_HorseRiderSync {
      public:
        ~C_RiderSync() override = default;

        // SDK convenience accessors; these are not recovered virtual names.
        C_Actor *GetRider() const { return m_pRider; }
        C_Horse *GetHorse() const { return m_pHorse; }
        void *GetSyncContextRaw() const { return m_pSyncContext; }
        const RiderSyncRuntimeStateLayout *GetRuntimeState() const {
            return m_pRuntimeState;
        }
        const RiderSyncAnimationLayerLayout *GetAnimationLayers() const {
            return m_animationLayers.m_pBegin;
        }
        std::size_t GetAnimationLayerCount() const {
            return m_animationLayers.GetCount();
        }

      private:
        // 0x08..0x20: the UnsafeOp::CryDeferrable<0> state; ctor 0x181ECA1F0
        // writes &unk_185665430 and -1 handles, then clears two flag bits.
        wh::shared::C_Signal m_deferred0008; // 0x08
        std::uint8_t m_deferredFlags0018;    // 0x18, ctor clears bits 0-1
        std::byte m_padding0019[0x3];
        std::uint32_t m_value001C;           // 0x1C, ctor zeroes it
        void *m_pSyncContext; // 0x20
        C_Actor *m_pRider;    // 0x28
        C_Horse *m_pHorse;    // 0x30
        RiderSyncRuntimeStateLayout *m_pRuntimeState; // 0x38, owned
        RiderSyncAnimationLayerVectorLayout m_animationLayers; // 0x40

        static void AssertLayout() {
            static_assert(offsetof(C_RiderSync, m_deferred0008) == 0x08, "C_RiderSync::m_deferred0008 offset mismatch");
            static_assert(offsetof(C_RiderSync, m_deferredFlags0018) == 0x18, "C_RiderSync::m_deferredFlags0018 offset mismatch");
            static_assert(offsetof(C_RiderSync, m_value001C) == 0x1C, "C_RiderSync::m_value001C offset mismatch");
            static_assert(offsetof(C_RiderSync, m_pSyncContext) == 0x20, "C_RiderSync::m_pSyncContext offset mismatch");
            static_assert(offsetof(C_RiderSync, m_animationLayers) == 0x40, "C_RiderSync::m_animationLayers offset mismatch");
        }
    };

    static_assert(sizeof(C_RiderSync) == 0x58,
                  "C_RiderSync size mismatch");
} // namespace SDK::wh::entitymodule
