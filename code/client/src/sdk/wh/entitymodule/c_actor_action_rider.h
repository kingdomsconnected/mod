#pragma once

#include "../framework/i_action.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::entitymodule {
    class C_ActionActor;
    class C_HorseRideContext;

    /**
     * The action that puts an actor in a saddle.
     *
     * Mounting is not a pose written onto a body: this action resolves the
     * horse from the entity id it was built with, creates the `C_RiderSync`
     * joining the two, and lets Mannequin play the get-on. **The rider is
     * whoever owns the queue it was posted to**, which is what lets a body
     * other than the player be seated.
     *
     * Its parameters are one word copied whole into the action at `+0x50`.
     */
    class C_ActorActionRider: public wh::framework::I_Action {
      public:
        /**
         * Packs the block the factory takes.
         *
         * `instant` is the force `C_RiderExpansion::StartRide` stores at byte
         * 4 -- what Lua's `ForceMount` sets. `Enter` hands it to the ride's
         * `SetState(MOUNTING, force)` (0x180AA15B8), which links the rider
         * and settles on riding at once: no get-on is played and nothing is
         * queued on the horse. Clear, the animated get-on runs, paired with a
         * slave action on the horse and aligned to where the horse stood when
         * it began. `flagB` reaches the rider sync's runtime state and has no
         * reader.
         */
        static constexpr std::uint64_t MakeParams(std::uint32_t horseEntityId, bool instant = false, bool flagB = false) {
            return static_cast<std::uint64_t>(horseEntityId) | (static_cast<std::uint64_t>(instant) << 32) | (static_cast<std::uint64_t>(flagB) << 40);
        }

        /** What the entry hands the rider sync, and what dismount is asked of. */
        C_HorseRideContext *GetRideContext() const {
            return m_pRideContext;
        }

        /** The queue this was posted to, whose owner is therefore the rider. */
        C_ActionActor *GetActionActor() const {
            return m_pActionActor;
        }

        /** The horse this ride is for, as the factory stored it. */
        std::uint32_t GetHorseEntityId() const {
            return static_cast<std::uint32_t>(m_params);
        }

        /** Whether the child range at `+0x78..+0x80` holds any action. */
        bool HasChildActions() const {
            return m_childActionsBegin != m_childActionsEnd;
        }

      private:
        std::byte m_opaque0010[0x40];
        std::uint64_t m_params; // 0x50, the block the factory copied in
        const void *m_pTertiaryVTable; // 0x58, the third base the constructor installs
        C_ActionActor *m_pActionActor; // 0x60
        std::byte m_opaque0068[0x8];
        std::uint64_t m_state0070; // 0x70, zeroed by the constructor
        wh::framework::I_Action **m_childActionsBegin;    // 0x78
        wh::framework::I_Action **m_childActionsEnd;      // 0x80
        wh::framework::I_Action **m_childActionsCapacity; // 0x88
        C_HorseRideContext *m_pRideContext;               // 0x90
        void *m_pRiderSync;                               // 0x98, the C_RiderSync built on entry
        std::uint32_t m_state00A0; // 0xA0
        float m_state00A4;         // 0xA4, FLT_MAX by default

        static void AssertLayout() {
            static_assert(offsetof(C_ActorActionRider, m_params) == 0x50, "C_ActorActionRider::m_params offset mismatch");
            static_assert(offsetof(C_ActorActionRider, m_pActionActor) == 0x60, "C_ActorActionRider::m_pActionActor offset mismatch");
            static_assert(offsetof(C_ActorActionRider, m_childActionsBegin) == 0x78, "C_ActorActionRider::m_childActionsBegin offset mismatch");
            static_assert(offsetof(C_ActorActionRider, m_childActionsEnd) == 0x80, "C_ActorActionRider::m_childActionsEnd offset mismatch");
            static_assert(offsetof(C_ActorActionRider, m_childActionsCapacity) == 0x88, "C_ActorActionRider::m_childActionsCapacity offset mismatch");
            static_assert(offsetof(C_ActorActionRider, m_pRideContext) == 0x90, "C_ActorActionRider::m_pRideContext offset mismatch");
            static_assert(offsetof(C_ActorActionRider, m_pRiderSync) == 0x98, "C_ActorActionRider::m_pRiderSync offset mismatch");
        }
    };

    static_assert(sizeof(C_ActorActionRider) == 0xA8, "C_ActorActionRider size mismatch");
} // namespace SDK::wh::entitymodule
