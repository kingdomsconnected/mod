#pragma once

#include "../../cryengine/entitysystem/i_entity_proxy.h"
#include "../framework/i_module_message_listener.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::entitymodule {
    class C_PlayerWeapon;

    /**
     * ABI overlay for C_PlayerWeapon's std::weak_ptr-like self reference.
     *
     * The factory writes the weapon pointer at +0x00 and its shared-control
     * block at +0x08, increments the control block's weak count, and the
     * weapon destructor releases that weak count.
     */
    struct PlayerWeaponWeakSelfLayout {
        C_PlayerWeapon *m_pObject;
        void *m_pControlBlock;
    };

    static_assert(sizeof(PlayerWeaponWeakSelfLayout) == 0x10,
                  "PlayerWeaponWeakSelfLayout size mismatch");

    /**
     * Player-weapon component used by the game-rules layer.
     *
     * The primary table at 0x1847AC610 contains the seven-slot IComponent
     * prefix plus 22 opaque C_PlayerWeapon entries. The factory allocates
     * exactly 0x48 bytes and installs an I_ModuleMessageListener secondary
     * vptr at +0x40.
     */
    class C_PlayerWeapon : public SDK::IComponent {
      public:
        ~C_PlayerWeapon() override = default;

        virtual void unk_fn0007() = 0; // first C_PlayerWeapon slot, +0x038
        virtual void unk_fn0008() = 0;
        virtual void unk_fn0009() = 0;
        virtual void unk_fn0010() = 0;
        virtual void unk_fn0011() = 0;
        virtual void unk_fn0012() = 0;
        virtual void unk_fn0013() = 0;
        virtual void unk_fn0014() = 0;
        virtual void unk_fn0015() = 0;
        virtual void unk_fn0016() = 0;
        virtual void unk_fn0017() = 0;
        virtual void unk_fn0018() = 0;
        virtual void unk_fn0019() = 0;
        virtual void unk_fn0020() = 0;
        virtual void unk_fn0021() = 0;
        virtual void unk_fn0022() = 0;
        virtual void unk_fn0023() = 0;
        virtual void unk_fn0024() = 0;
        virtual void unk_fn0025() = 0;
        virtual void unk_fn0026() = 0;
        virtual void unk_fn0027() = 0;
        virtual void unk_fn0028() = 0; // final primary slot, +0x0E0

        wh::framework::I_ModuleMessageListener *GetModuleMessageListener() {
            return reinterpret_cast<wh::framework::I_ModuleMessageListener *>(&m_moduleMessageListenerVptr);
        }

        const wh::framework::I_ModuleMessageListener *GetModuleMessageListener() const {
            return reinterpret_cast<const wh::framework::I_ModuleMessageListener *>(&m_moduleMessageListenerVptr);
        }

        C_PlayerWeapon *GetWeakSelfObject() const {
            return m_weakSelf.m_pObject;
        }

        void *GetInitializationContext() const {
            return m_pInitializationContext;
        }

        std::uint32_t GetInitializationContextId() const {
            return m_initializationContextId;
        }

        void *GetInitializationEndpoint() const {
            return m_pInitializationEndpoint;
        }

      private:
        PlayerWeaponWeakSelfLayout m_weakSelf; // +0x08
        std::uint64_t m_value0018;              // +0x18, factory 0x1839D68D4 zeroes
        std::uint64_t m_value0020;              // +0x20, factory zeroes
        void *m_pInitializationContext;         // +0x28
        std::uint32_t m_initializationContextId;// +0x30
        std::byte m_padding0034[0x4];           // +0x34
        void *m_pInitializationEndpoint;        // +0x38
        void *m_moduleMessageListenerVptr; // +0x40
    };

    static_assert(sizeof(C_PlayerWeapon) == 0x48, "C_PlayerWeapon size mismatch");
} // namespace SDK::wh::entitymodule
