#pragma once

#include "../../patterns.h"
#include "../shared/c_signal.h"

#include <utils/hooking/hooking.h>

#include <cstddef>
#include <cstdint>

namespace SDK::wh::entitymodule {
    class C_Actor;

    /**
     * Slot 0 is the only implementation of the interface's first operation
     * (0x18085451C). Slot 1 is proven from the other side: the unconscious
     * action asks it through this same table (0x182898668), and the only
     * virtual const predicate in it is `IsRagdoll` at 0x180644E80.
     */
    class I_RagdollManager {
      public:
        /**
         * `defer` leaves the request for `C_RagdollManager::Update` instead of
         * building actor action 8 here. Refused silently by several internal
         * gates, so this says what was asked; `IsRagdoll` says what happened.
         */
        virtual void RequestRagdoll(bool defer) = 0; // slot 0

        virtual bool IsRagdoll() const = 0; // slot 1
    };
    static_assert(sizeof(I_RagdollManager) == 8);

    // RTTI 0x183B5A7B8; constructor 0x1803F115C, allocation 0x18137E694.
    class C_RagdollManager: public I_RagdollManager {
      public:
        /**
         * What `TryAutomaticStandUp` and the game's StandUp script binding
         * pass. The other observed mode is 1; neither has an established name.
         */
        static constexpr std::uint32_t kAutomaticStandUpMode = 0;

        /**
         * The physics profiles the body switches between.
         *
         * Only these two are named: `SetPhysicsProfile`'s own body proves
         * them, and they are the two CryEngine's `CActor::Revive` uses. The
         * ragdoll profiles (2 and 3) are read but never asked for here.
         */
        enum PhysicsProfile : std::uint8_t {
            /** Stood down -- `Reset`, `RestoreAnimatedCharacter`, no collider. CryEngine's `eAP_NotPhysicalized`. */
            NotPhysicalized = 0,
            /** An ordinary animated actor with a live collider. CryEngine's `eAP_Alive`. */
            Alive = 1,
        };

        C_Actor *GetActor() const {
            return m_actor;
        }
        std::uint8_t GetPhysicsProfile() const {
            return m_profile;
        }

        /**
         * Set through slot 5 (0x1816EA1C0), cleared only by `Reset`. While it
         * is set `RequestRagdoll` and `RequestStandUp` both refuse silently.
         */
        bool IsRagdollDisabled() const {
            return m_ragdollDisabled != 0;
        }

        /** Whether a deferred `RequestRagdoll(true)` is waiting for `Update`, which consumes it. */
        bool HasDeferredRagdollRequest() const {
            return (m_requestFlags & kDeferredRagdollFlag) != 0;
        }

        /**
         * Switches the body's physics profile, running the engine's whole
         * transition rather than a piece of it.
         *
         * `Alive` (0x1804CA6B0) does six things: enables the first collider
         * group, either blends out of a ragdoll or calls the actor's
         * `Physicalize` and `PostPhysicalize`, restores the animated
         * character, queues the blend back from ragdoll, and enables the
         * second collider group. Calling the two vtable slots by hand does
         * the middle pair and none of the rest.
         *
         * **It opens with `if (m_profile != profile || <serializing>)`, and a
         * freshly spawned body already reports `Alive` while having no
         * physical entity at all.** So asking for `Alive` on its own is a
         * silent no-op. Set the profile to `eAP_NotPhysicalized` before
         * asking for `Alive` when the body has no physical entity.
         */
        bool SetPhysicsProfile(PhysicsProfile profile) {
            return hook::this_call<bool>(gPatterns.C_RagdollManager__SetPhysicsProfile, this, static_cast<std::uint8_t>(profile));
        }

        /** Rebuilds physics after a model change, preserving the ragdoll profile. */
        void Rephysicalize() {
            hook::this_call<void>(gPatterns.C_RagdollManager__Rephysicalize, this);
        }

        /**
         * The only way out of the ragdoll action short of it ending on its
         * own. Gated inside on the body actually being in a ragdoll, so it is
         * safe on one that is already up.
         */
        void RequestStandUp(std::uint32_t mode) {
            hook::this_call<void>(gPatterns.C_RagdollManager__RequestStandUp, this, mode);
        }

      private:
        /** `RequestRagdoll(true)` sets it and `Update` consumes it by calling `RequestRagdoll(false)`. */
        static constexpr std::uint8_t kDeferredRagdollFlag = 0x04;

        // Ctor 0x1803F115C.
        SDK::wh::shared::C_Signal m_signal0008; // 0x08, dispatch 0x185667518
        SDK::wh::shared::C_Signal m_signal0018; // 0x18, dispatch 0x185667518
        SDK::wh::shared::C_Signal m_signal0028; // 0x28, dispatch 0x185665430
        SDK::wh::shared::C_Signal m_signal0038; // 0x38, dispatch 0x185666CE8
        SDK::wh::shared::C_Signal m_signal0048; // 0x48, dispatch 0x185666CE8
        std::uint8_t m_state0058;               // 0x58, ctor zeroes
        std::byte m_padding0059[0x7];
        SDK::wh::shared::C_Signal m_signal0060; // 0x60, dispatch 0x185665AC0
        std::uint8_t m_state0070[2];            // 0x70, Reset 0x180ACAB48 clears both bytes
        std::byte m_padding0072[0x6];
        SDK::wh::shared::C_Signal m_signal0078; // 0x78, dispatch 0x185667508
        C_Actor *m_actor;
        std::byte m_opaque0090[0x14];
        std::uint32_t m_timer00A4;      // 0xA4, Reset 0x180ACAB48 seeds it from an entity-module cvar (+0xB4)
        std::uint8_t m_profile;
        std::uint8_t m_profile00A9;     // 0xA9, Reset copies the same profile query into it
        std::uint8_t m_ragdollDisabled; // 0xAA
        std::byte m_padding00AB[0x1];
        float m_values00AC[2];          // 0xAC, Reset zeroes
        std::uint32_t m_value00B4;      // 0xB4, Reset zeroes
        float m_values00B8[4];          // 0xB8, Reset zeroes
        std::uint8_t m_requestFlags; // 0xC8
        std::byte m_opaque00C9[0x7];

        static void AssertLayout() {
            static_assert(offsetof(C_RagdollManager, m_actor) == 0x88);
            static_assert(offsetof(C_RagdollManager, m_signal0008) == 0x08);
            static_assert(offsetof(C_RagdollManager, m_signal0060) == 0x60);
            static_assert(offsetof(C_RagdollManager, m_signal0078) == 0x78);
            static_assert(offsetof(C_RagdollManager, m_state0070) == 0x70);
            static_assert(offsetof(C_RagdollManager, m_profile) == 0xA8);
            static_assert(offsetof(C_RagdollManager, m_ragdollDisabled) == 0xAA);
            static_assert(offsetof(C_RagdollManager, m_requestFlags) == 0xC8);
        }
    };
    static_assert(sizeof(C_RagdollManager) == 0xD0);
} // namespace SDK::wh::entitymodule
