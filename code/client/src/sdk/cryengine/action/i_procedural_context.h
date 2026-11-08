#pragma once

#include "../entitysystem/i_entity.h"

#include <cstddef>

namespace SDK {
    /**
     * Declared rather than included, because the controller hands these
     * contexts out (`FindProceduralContext`) and its header includes this one.
     * A caller that uses both includes `i_action_controller.h`, where
     * both types are complete.
     */
    struct IActionController;

    /**
     * State a Mannequin procedural clip shares across the clips of one
     * controller, created by name on the controller and living as long as it.
     *
     * The table (`IProceduralContext::vftable`, 0x183AF11D8) opens with the
     * four ICryUnknown entries -- the interface query, a zero, the factory
     * and the scalar deleting destructor -- then CryEngine's `Initialise`
     * and `Update`. `CActionController::CreateProceduralContext` (0x18078A1F0)
     * proves `Initialise` at +0x20 with the controller's root entity and the
     * controller itself.
     *
     * Most contexts only store those two. `wh::entitymodule::
     * CProceduralContextLook` (0x183AF1188) and `CProceduralContextAim`
     * (0x183AF1110) also cache the character's look and aim pose blenders at
     * +0x18 (`InitialisePoseBlenderLook`/`Aim`), which their `Update` and the
     * looking clip (`CProceduralClipLookingBlendBone::OnEnter`, 0x180C4F1E8)
     * call every frame. A character replaced under a live controller leaves
     * that pointer dangling; running `Initialise` again re-caches it from the
     * character now in slot 0.
     */
    class IProceduralContext {
      public:
        virtual void unk_fn0000() = 0; // +0x00, interface query
        virtual void unk_fn0001() = 0; // +0x08
        virtual void unk_fn0002() = 0; // +0x10, factory

      protected:
        virtual ~IProceduralContext() = default; // +0x18

      public:
        virtual void Initialise(IEntity &entity, IActionController &actionController) = 0; // +0x20, slot 4
        virtual void Update(float timePassedSeconds)                                    = 0; // +0x28, slot 5
    };

    /**
     * `wh::entitymodule::CProceduralContextLook`, the controller's
     * "ProceduralContextLook". Its `Update` (0x180C4EF54) turns the look pose
     * blender on only while both requests hold -- the game's, which
     * `C_Actor::SetProceduralLookRequest` (0x180E7C42C) writes from the
     * movement request's look target, and a looking clip's, which the clip
     * raises on entering and drops on leaving -- and then hands it the target.
     */
    class CProceduralContextLook: public IProceduralContext {
      public:
        /** As a looking clip of the current fragment would: with the game's request, the head follows the look target. */
        void SetClipRequestsLooking(bool requested) {
            m_clipRequestsLooking = requested;
        }

        /** The controller's root entity, as `Initialise` (0x180C4ED50) stored it. */
        IEntity *GetEntity() const {
            return m_pEntity;
        }

      private:
        IEntity *m_pEntity;                     // 0x08
        IActionController *m_pActionController; // 0x10
        void *m_pPoseBlenderLook;               // 0x18
        bool m_gameRequestsLooking;             // 0x20
        bool m_clipRequestsLooking;             // 0x21

        static void AssertLayout() {
            static_assert(offsetof(CProceduralContextLook, m_pEntity) == 0x08, "CProceduralContextLook::m_pEntity offset mismatch");
            static_assert(offsetof(CProceduralContextLook, m_pActionController) == 0x10, "CProceduralContextLook::m_pActionController offset mismatch");
            static_assert(offsetof(CProceduralContextLook, m_pPoseBlenderLook) == 0x18, "CProceduralContextLook::m_pPoseBlenderLook offset mismatch");
            static_assert(offsetof(CProceduralContextLook, m_gameRequestsLooking) == 0x20, "CProceduralContextLook::m_gameRequestsLooking offset mismatch");
            static_assert(offsetof(CProceduralContextLook, m_clipRequestsLooking) == 0x21, "CProceduralContextLook::m_clipRequestsLooking offset mismatch");
        }
    };
} // namespace SDK
