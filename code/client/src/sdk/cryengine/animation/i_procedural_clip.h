#pragma once

#include "../action/i_mannequin_action.h"
#include "../entitysystem/i_entity.h"
#include "i_character_instance.h"

#include <cstddef>

namespace SDK {
    /**
     * A Mannequin procedural clip: the code half of a fragment's ProcLayer,
     * built per playback and bound to the body that plays it.
     *
     * Stock CryEngine: `Initialise`
     * stores the entity, character instance, scope and action in that order
     * after the vtable, and KCD2 keeps it -- every clip resolves its body
     * from +0x08 (`GetClipActor`, 0x180897BA4; the bone camera clip's
     * OnEnter, 0x18286B5AC). The action is the one whose fragment the
     * clip plays in (`C_ProceduralClipAnimEvent`, 0x18085731C, raises its
     * event for that action's body).
     */
    class IProceduralClip {
      public:
        virtual ~IProceduralClip() = default;

        /** The body playing the fragment this clip belongs to. */
        IEntity *GetEntity() const {
            return m_pEntity;
        }

        /** The action whose fragment holds this clip; the clip keeps one reference. */
        IAction *GetAction() const {
            return m_action;
        }

      private:
        IEntity *m_pEntity;              // 0x08
        ICharacterInstance *m_charInstance; // 0x10
        void *m_scope;                   // 0x18, IScope *
        IAction *m_action;               // 0x20, _smart_ptr<IAction>; every IProceduralClip destructor (e.g. 0x180E0D950) releases it

        static void AssertLayout() {
            static_assert(offsetof(IProceduralClip, m_pEntity) == 0x08, "IProceduralClip::m_pEntity offset mismatch");
            static_assert(offsetof(IProceduralClip, m_charInstance) == 0x10, "IProceduralClip::m_charInstance offset mismatch");
            static_assert(offsetof(IProceduralClip, m_scope) == 0x18, "IProceduralClip::m_scope offset mismatch");
            static_assert(offsetof(IProceduralClip, m_action) == 0x20, "IProceduralClip::m_action offset mismatch");
        }
    };

    static_assert(sizeof(IProceduralClip) == 0x28, "IProceduralClip size mismatch");
} // namespace SDK
