#pragma once

#include "../../cryengine/action/i_actor.h"
#include "../../cryengine/action/i_game_object_profile_manager.h"
#include "../../cryengine/action/i_hit_listener.h"
#include "../../cryengine/entitysystem/i_entity.h"
#include "../../cryengine/physics/event_phys_collision.h"
#include "../../patterns.h"
#include "../../types.h"
#include "../framework/wuid.h"
#include "../shared/c_signal.h"
#include "c_item_wrapper.h"

#include <utils/hooking/hooking.h>

namespace SDK::wh::entitymodule {
    class CProjectile: public IGameObjectExtension, public IHitListener, public IGameObjectProfileManager {
      public:
        /**
         * Projectile parameters. SetParams copies the owner and weapon
         * and sets the owner-is-player flag. The constructor at 0x182894158
         * initializes all 0x28 bytes.
         */
        struct SProjectileDesc final {
            // No weapon item, as C_Distract::SpawnDecoyProjectile182E9701C
            // passes it (qword_185332E90); SetParams stores it at +0x110.
            explicit SProjectileDesc(EntityId owner) {
                hook::this_call<void>(gPatterns.CProjectile__SProjectileDesc__Construct, this, owner, SDK::wh::framework::WUID::kInvalid);
            }

            ~SProjectileDesc() {
                hook::this_call<void>(gPatterns.CryStringT_char__Destruct, &m_geometryOverride);
            }

            SProjectileDesc(const SProjectileDesc &)            = delete;
            SProjectileDesc &operator=(const SProjectileDesc &) = delete;

          private:
            EntityId m_ownerEntityId;
            std::byte m_padding0004[0x4];
            std::uint64_t m_weaponItemWuid;
            std::uint8_t m_flag0010; // 0x10, ctor zeroes
            std::byte m_padding0011[0x7];
            CryStringT<char> m_geometryOverride;
            std::uint8_t m_flag0020; // 0x20, ctor sets 1
            std::uint8_t m_flag0021; // 0x21, ctor sets 1
            std::byte m_padding0022[0x6];

            static void AssertLayout() {
                static_assert(offsetof(SProjectileDesc, m_ownerEntityId) == 0x0, "CProjectile::SProjectileDesc::m_ownerEntityId offset mismatch");
                static_assert(offsetof(SProjectileDesc, m_weaponItemWuid) == 0x8, "CProjectile::SProjectileDesc::m_weaponItemWuid offset mismatch");
                static_assert(offsetof(SProjectileDesc, m_geometryOverride) == 0x18, "CProjectile::SProjectileDesc::m_geometryOverride offset mismatch");
                static_assert(sizeof(SProjectileDesc) == 0x28, "CProjectile::SProjectileDesc size mismatch");
            }
        };

        ~CProjectile() override                                                                             = default;
        virtual void OpaqueProjectile29()                                                                   = 0;
        virtual void OpaqueProjectile30()                                                                   = 0;
        virtual void OpaqueProjectile31()                                                                   = 0;
        virtual void SetParams(const SProjectileDesc &desc)                                                 = 0; // +0x100; C_Distract::SpawnDecoyProjectile182E9701C calls this slot.
        virtual void OpaqueProjectile33()                                                                   = 0;
        virtual void OpaqueProjectile34()                                                                   = 0;
        virtual void Launch(const Vec3 &position, const Vec3 &direction, const Vec3 &velocity, float scale) = 0; // +0x118
        virtual void OpaqueProjectile36()                                                                   = 0;
        virtual void OpaqueProjectile37()                                                                   = 0;
        virtual void OpaqueProjectile38()                                                                   = 0;
        virtual void OpaqueProjectile39()                                                                   = 0;
        virtual void OpaqueProjectile40()                                                                   = 0;
        virtual void OpaqueProjectile41()                                                                   = 0;
        virtual void OpaqueProjectile42()                                                                   = 0;
        virtual void OpaqueProjectile43()                                                                   = 0;
        virtual void OpaqueProjectile44()                                                                   = 0;
        virtual void OpaqueProjectile45()                                                                   = 0;
        virtual void OpaqueProjectile46()                                                                   = 0;
        virtual bool ProcessCollision(const EventPhysCollision &collision)                                  = 0; // +0x178
        virtual void OpaqueProjectile48()                                                                   = 0;

        IPhysicalEntity *GetPhysicalEntity() const {
            return m_pPhysicalEntity;
        }

        EntityId GetOwnerEntityId() const {
            return m_ownerEntityId;
        }

        C_Item *GetAmmunitionItem() const {
            return m_ammunition.GetItem();
        }

        EntityId GetTargetEntityId() const {
            return m_targetEntityId;
        }

        const Vec3 &GetLastPosition() const {
            return m_lastPosition;
        }

        const Vec3 &GetInitialPosition() const {
            return m_initialPosition;
        }

        const Vec3 &GetInitialDirection() const {
            return m_initialDirection;
        }

        const Vec3 &GetInitialVelocity() const {
            return m_initialVelocity;
        }

        std::uint8_t GetPhysicsProfile() const {
            return m_physicsProfile;
        }

        /**
         * Whether this projectile has already had a collision resolved.
         *
         * `CArrow::ProcessCollision` (0x182ACBFA8) tests this bit through
         * 0x182AA08CC and abandons the contact when it is set, then sets it
         * on every path that reaches the end of the function. So a projectile
         * gets exactly one hit, and any later contact is silently dropped.
         */
        bool HasCollided() const {
            return (m_flags & 0x1) != 0;
        }

        bool IsDestroying() const {
            return (m_flags & 0x8) != 0;
        }

        bool IsDestructionPending() const {
            return (m_flags & 0x400) != 0;
        }

        // NPC player puppets have no native channel, so SetParams leaves
        // their player-trail gate clear. A visual replica has no native
        // Update: start the same effect explicitly, once after launch.
        void EnablePlayerTrail() {
            m_flags |= 0x800;
            hook::this_call<void>(gPatterns.CProjectile__TrailEffect, this, true);
        }

        // The native distance/near-miss test only starts spatial whiz audio.
        // Advance its previous position exactly as Update does, without
        // running that update's nearby-entity or target-collision checks.
        void UpdateFlightAudio(const Vec3 &position) {
            hook::this_call<void>(gPatterns.CProjectile__UpdateWhiz, this, &position);
            m_lastPosition = position;
        }

        // Timer 0x110 is lifetime removal; 0x111 controls initial visibility
        // and must remain intact. ProcessEvent (0x182AAAFFC) proves both.
        void DisableLifetimeTimer() {
            GetEntity()->KillTimer(0x110);
        }

        // False means destruction was deferred; the game still owns the object.
        bool Destroy() {
            return hook::this_call<bool>(gPatterns.CProjectile__Destroy, this);
        }

      private:
        // Ctor 0x182A9F5B4.
        SDK::wh::shared::C_Signal m_signal0050; // 0x50, dispatch 0x185667118
        void *m_values0060[2];                  // 0x60, ctor zeroes
        IPhysicalEntity *m_pPhysicalEntity; // 0x70
        void *m_values0078[4];                  // 0x78, ctor zeroes
        std::uint32_t m_value0098;              // 0x98, ctor zeroes
        std::byte m_padding009C[0x4];
        std::uint32_t m_flags;   // 0xA0
        Vec3 m_lastPosition;     // 0xA4
        Vec3 m_initialPosition;  // 0xB0
        Vec3 m_initialDirection; // 0xBC
        Vec3 m_initialVelocity;  // 0xC8
        std::uint32_t m_values00D4[10];         // 0xD4, ctor zeroes
        std::uint32_t m_value00FC;              // 0xFC, ctor sets 1
        CryStringT<char> m_string0100;          // 0x100, ctor sets the empty string
        EntityId m_ownerEntityId;       // 0x108
        EntityId m_targetEntityId;      // 0x10C; target collision in 0x182AA1CE4
        std::uint64_t m_weaponItemWuid; // 0x110
        C_ItemWrapper m_ammunition;     // 0x118
        float m_value0130;                      // 0x130, ctor sets 1.0
        std::byte m_padding0134[0x4];
        void *m_value0138;                      // 0x138, ctor zeroes
        std::uint8_t m_state0140;               // 0x140, ctor zeroes
        std::uint8_t m_physicsProfile; // 0x141
        std::uint8_t m_state0142;               // 0x142, ctor sets 0xFF
        std::uint8_t m_state0143;               // 0x143, ctor sets 0xFF
        std::uint8_t m_state0144;               // 0x144, ctor zeroes
        std::byte m_padding0145[0x1];
        std::uint16_t m_state0146;              // 0x146, ctor zeroes
        void *m_values0148[3];                  // 0x148, ctor zeroes

        static void AssertLayout() {
            static_assert(offsetof(CProjectile, m_pPhysicalEntity) == 0x70, "CProjectile::m_pPhysicalEntity offset mismatch");
            static_assert(offsetof(CProjectile, m_flags) == 0xA0, "CProjectile::m_flags offset mismatch");
            static_assert(offsetof(CProjectile, m_lastPosition) == 0xA4, "CProjectile::m_lastPosition offset mismatch");
            static_assert(offsetof(CProjectile, m_initialPosition) == 0xB0, "CProjectile::m_initialPosition offset mismatch");
            static_assert(offsetof(CProjectile, m_initialDirection) == 0xBC, "CProjectile::m_initialDirection offset mismatch");
            static_assert(offsetof(CProjectile, m_initialVelocity) == 0xC8, "CProjectile::m_initialVelocity offset mismatch");
            static_assert(offsetof(CProjectile, m_ownerEntityId) == 0x108, "CProjectile::m_ownerEntityId offset mismatch");
            static_assert(offsetof(CProjectile, m_targetEntityId) == 0x10C, "CProjectile::m_targetEntityId offset mismatch");
            static_assert(offsetof(CProjectile, m_weaponItemWuid) == 0x110, "CProjectile::m_weaponItemWuid offset mismatch");
            static_assert(offsetof(CProjectile, m_ammunition) == 0x118, "CProjectile::m_ammunition offset mismatch");
            static_assert(offsetof(CProjectile, m_physicsProfile) == 0x141, "CProjectile::m_physicsProfile offset mismatch");
            static_assert(offsetof(CProjectile, m_signal0050) == 0x50, "CProjectile::m_signal0050 offset mismatch");
            static_assert(offsetof(CProjectile, m_values0078) == 0x78, "CProjectile::m_values0078 offset mismatch");
            static_assert(offsetof(CProjectile, m_values00D4) == 0xD4, "CProjectile::m_values00D4 offset mismatch");
            static_assert(offsetof(CProjectile, m_string0100) == 0x100, "CProjectile::m_string0100 offset mismatch");
            static_assert(offsetof(CProjectile, m_value0130) == 0x130, "CProjectile::m_value0130 offset mismatch");
            static_assert(offsetof(CProjectile, m_values0148) == 0x148, "CProjectile::m_values0148 offset mismatch");
        }
    };

    static_assert(sizeof(CProjectile) == 0x160, "CProjectile size mismatch");
} // namespace SDK::wh::entitymodule
