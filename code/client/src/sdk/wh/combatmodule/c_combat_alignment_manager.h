#pragma once

#include "c_combat_actor_updated_object.h"

namespace SDK::wh::combatmodule {
    /**
     * A fighter's queue of body alignments: turning to and closing on the
     * opponent for an attack, a block or a paired action.
     *
     * RTTI (0x18421D790) proves the chain through C_CombatActorUpdatedObject
     * and C_CombatActorObject, so the owning fighter is at +0x08; the
     * constructor (0x180917F3C) registers it as update kind 3, and
     * C_CombatActor+0x250 holds it. Update slot 6, `ProcessClipEvents`
     * (0x180A70F20, behind the thunk 0x180A70F18), drains the clip events
     * the alignment procedural clips queue at +0x30 and installs each
     * solver on the aligned body's `entitymodule::C_AlignmentManager` before
     * registering it here; slot 7, `UpdateSolverTargets` (0x1809E2D08,
     * thunk 0x1809E2D00), retargets the fighter's own installed solvers
     * every frame. Only reached by pointer here; the rest of the layout is
     * not modeled.
     */
    class C_CombatAlignmentManager : public C_CombatActorUpdatedObject {
      public:
        ~C_CombatAlignmentManager() override = default;
    };
} // namespace SDK::wh::combatmodule
