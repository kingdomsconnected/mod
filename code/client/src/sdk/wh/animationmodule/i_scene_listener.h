#pragma once

namespace SDK::wh::animationmodule {
    /**
     * Receives animation-scene state changes.
     *
     * The standalone table at 0x183E50E58 contains exactly the virtual
     * destructor and one pure callback before the following COL. RTTI places
     * this interface at +0x10 in C_CombatActorCollisions.
     */
    class I_SceneListener {
      public:
        virtual ~I_SceneListener() = default;
        virtual void unk_fn0001() = 0;
    };

    static_assert(sizeof(I_SceneListener) == 0x8,
                  "I_SceneListener size mismatch");
} // namespace SDK::wh::animationmodule
