#pragma once

#include "../../patterns.h"
#include "../../types.h"
#include <cstddef>
#include <cstdint>
#include <utils/hooking/hooking.h>

namespace SDK::wh::rpgmodule {
    class I_Soul;
    class I_Event;

    struct S_SoulEntityIdentity180D571E4 {
        std::uint64_t soulWuid;
        EntityId entityId;

        std::byte m_padding000C[0x4]; // alignment; ctor 0x180D571E4 writes only +0 and +8

        void SetSoul(I_Soul *soul) {
            hook::this_call<void>(gPatterns.S_SoulEntityIdentity__Constructor, this, soul);
        }

      private:
        static void AssertLayout() {
            static_assert(offsetof(S_SoulEntityIdentity180D571E4, soulWuid) == 0);
            static_assert(offsetof(S_SoulEntityIdentity180D571E4, entityId) == 8);
        }
    };

    static_assert(sizeof(S_SoulEntityIdentity180D571E4) == 0x10);

    struct S_MissileHitData {
        S_SoulEntityIdentity180D571E4 attacker; // 0x00
        S_SoulEntityIdentity180D571E4 target;   // 0x10
        Vec3 position;                          // 0x20
        Vec3 launchPosition;                    // 0x2C
        Vec3 velocity;                          // 0x38
        Vec3 normal;                            // 0x44

        // Collision material from +0xA0, not a body-part index.
        std::int32_t bodyMaterial;

        std::byte m_padding0054[0x4]; // alignment; ctor 0x182762908 writes +0x50 then +0x58

        std::uint64_t weaponWuid;        // 0x58
        std::uint64_t ammunitionWuid;    // 0x60
        std::int32_t ammunitionCategory; // 0x68, descriptor slot10, clamped<5
        std::byte m_padding006C[0x4]; // alignment; ctor writes +0x68 then +0x70

        std::uint64_t defenseItemWuid;      // 0x70
        std::uint64_t mountedActorSoulWuid; // 0x78
        std::int32_t surface;               // 0x80
        float scale;                        // 0x84

        S_MissileHitData() {
            hook::this_call<void>(gPatterns.S_MissileHitData__Constructor, this);
        }

      private:
        static void AssertLayout() {
            static_assert(offsetof(S_MissileHitData, attacker) == 0x00);
            static_assert(offsetof(S_MissileHitData, target) == 0x10);
            static_assert(offsetof(S_MissileHitData, position) == 0x20);
            static_assert(offsetof(S_MissileHitData, launchPosition) == 0x2C);
            static_assert(offsetof(S_MissileHitData, velocity) == 0x38);
            static_assert(offsetof(S_MissileHitData, normal) == 0x44);
            static_assert(offsetof(S_MissileHitData, bodyMaterial) == 0x50);
            static_assert(offsetof(S_MissileHitData, weaponWuid) == 0x58);
            static_assert(offsetof(S_MissileHitData, ammunitionWuid) == 0x60);
            static_assert(offsetof(S_MissileHitData, ammunitionCategory) == 0x68);
            static_assert(offsetof(S_MissileHitData, defenseItemWuid) == 0x70);
            static_assert(offsetof(S_MissileHitData, mountedActorSoulWuid) == 0x78);
            static_assert(offsetof(S_MissileHitData, surface) == 0x80);
            static_assert(offsetof(S_MissileHitData, scale) == 0x84);
        }
    };

    static_assert(sizeof(S_MissileHitData) == 0x88);

    // Native intrusive handle; release the returned reference in-game.
    struct S_EventHandle182D291D0 {
        I_Event *event = nullptr;
    };

    static_assert(sizeof(S_EventHandle182D291D0) == 8);
    static_assert(offsetof(S_EventHandle182D291D0, event) == 0);
} // namespace SDK::wh::rpgmodule
