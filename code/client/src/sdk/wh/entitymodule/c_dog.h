#pragma once

#include "c_animal.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::entitymodule {
    /** Exact constructor/destructor-proven layout of C_Dog's derived state. */
    struct DogRuntimeStateLayout {
        void *m_pRuntimeObject; // 0x00, retained and lazily created
        std::uint32_t m_state0008;
        float m_state000C; // initialized to -1.0f
    };

    static_assert(sizeof(DogRuntimeStateLayout) == 0x10,
                  "DogRuntimeStateLayout size mismatch");

    /**
     * Concrete dog actor created by IGameFramework::CActorCreator<C_Dog>.
     *
     * The factory allocates exactly 0x9F8 bytes. C_Dog's primary table at
     * 0x183B71668 contains the 280 inherited C_Animal entries followed by one
     * stripped dog-specific operation. Construction retains C_Animal's
     * IGameObjectView and IGameObjectProfileManager secondary vptrs at +0x40
     * and +0x48, then initializes the 0x10-byte derived tail at +0x9E8.
     */
    class C_Dog : public C_Animal {
      public:
        static constexpr const char *ActorClassName = "C_Dog";
        static constexpr ActorClass ActorClassId = ActorClass_Dog;

        ~C_Dog() override = default;

        virtual void unk_fn0280() = 0; // final C_Dog slot, +0x8C0

        /**
         * Plays the praise animation with `partnerEntityId`, the way petting
         * does.
         *
         * `C_Dog::Bond` (`0x1829DF430`) with the partner passed in rather than
         * taken from the local player, which is the only thing that stops the
         * native being called outright on a body somebody else owns. The gate
         * (`CanBondNow`, `0x1829DF5CC`) is not repeated either: it measures
         * the distance, facing and height between the *local* player and the
         * dog, and the client that owns the animal has already answered it.
         *
         * Returns whether the dog's queue accepted the action. The person's
         * half of the clip is queued by the action itself, from the entity id
         * given here.
         */
        bool Bond(EntityId partnerEntityId, std::uint32_t variant);

        DogRuntimeStateLayout *GetDogRuntimeState() { return &m_runtimeState; }
        const DogRuntimeStateLayout *GetDogRuntimeState() const {
            return &m_runtimeState;
        }

      private:
        DogRuntimeStateLayout m_runtimeState; // 0x9E8
    };

    static_assert(sizeof(C_Dog) == 0x9F8, "C_Dog size mismatch");
} // namespace SDK::wh::entitymodule
