#pragma once

#include <cstddef>
#include <cstdint>

namespace SDK::wh::entitymodule {
    // C_Player owns this object by value-pointer and includes this header.
    class C_Player;

    /**
     * The player's dog-companion interaction state, the owned 0x28-byte
     * object at `C_Player+0xCE0`.
     *
     * Not polymorphic. Its first field is the player it belongs to: the
     * stealth-kill availability callback 0x180AE3520 reads the player's
     * interaction block through it before republishing the HUD status.
     *
     * `UpdateDogHudAvailability` (0x180AE3538) publishes whether the player
     * has a dog soul to the process-wide HUD status channel `dog` -- reached
     * from `C_Player`'s slot-187 initialisation (0x180F426F8) for every
     * player instance, and from that callback.
     */
    class C_DogInteractionManager final {
      public:
        C_Player *GetPlayer() const {
            return m_pPlayer;
        }

      private:
        C_Player *m_pPlayer; // 0x00
        std::byte m_opaque0008[0x4];
        bool m_stealthKillSubscribed; // 0x0C, set once C_Player::AcquireSoul (0x180F426F8) subscribes 0x180AE3520; cleared by 0x181926EA8
        std::byte m_opaque000D[0x1B];

        static void AssertLayout() {
            static_assert(offsetof(C_DogInteractionManager, m_pPlayer) == 0x00, "C_DogInteractionManager::m_pPlayer offset mismatch");
            static_assert(offsetof(C_DogInteractionManager, m_stealthKillSubscribed) == 0x0C, "C_DogInteractionManager::m_stealthKillSubscribed offset mismatch");
        }
    };

    static_assert(sizeof(C_DogInteractionManager) == 0x28, "C_DogInteractionManager size mismatch");
} // namespace SDK::wh::entitymodule
