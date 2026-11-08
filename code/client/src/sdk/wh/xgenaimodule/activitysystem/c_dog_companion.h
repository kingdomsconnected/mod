#pragma once

#include "../../rpgmodule/c_soul.h"
#include "c_dog_objective.h"
#include "i_actor_activity_provider.h"

#include <cstddef>

namespace SDK::wh::xgenaimodule::activitysystem {
    /**
     * A dog's brain: what it can do, and what it has decided to do.
     *
     * Reached from a soul through `0x1808ADD68`, which asks the NPC manager
     * for the AI object behind that soul's WUID and hands it to
     * `FindSubSystemEntry` (`0x1808ADE7C`). That walks the AI object's
     * sub-systems, takes the one reporting kind 8 -- the
     * `C_DogCompanionSubbrain` that `CreateSubbrainByName("DogCompanion")`
     * builds -- and returns its `+288`, which is exactly where
     * `sub_180BC9B00` stores the companion it allocates: 0x1E30 bytes,
     * constructed at `0x180BC9BC4`.
     *
     * A dog only has one if its brain data names that subbrain. Possession
     * does not create it, so an animal spawned against a generic soul is
     * possessed, animated and perceived, and still answers null here forever.
     *
     * `C_DogInteractionManager::GetDogActor` and the reflected
     * `SetPlayerDogMode` are both just that resolve, which is why this calls
     * the same function rather than walking the offsets inside it.
     */
    class C_DogCompanion {
      public:
        /** The companion behind this soul, or nullptr when its brain has none. */
        static C_DogCompanion *ResolveForSoul(rpgmodule::I_Soul *soul);

        /** The objectives this dog has. Provider slot 3. */
        C_DogObjectiveSet *GetObjectiveSet() {
            return Provider()->GetActivityObjectiveSet();
        }

        /** What chooses between them. Provider slot 7. */
        C_DogObjectivePlanner *GetObjectivePlanner() {
            return Provider()->GetActivityObjectivePlanner();
        }

      private:
        /**
         * The activity provider embedded at `+0x18`.
         *
         * Embedded rather than pointed to: the game passes the address of the
         * vptr field itself as `this`, which every objective path does with
         * `lea rcx, [rdi+18h] / mov rax, [rcx] / call [rax+...]`.
         */
        I_ActorActivityProvider *Provider() {
            return reinterpret_cast<I_ActorActivityProvider *>(&m_providerVTable);
        }

        void *m_vtable0000;        // 0x000, C_DogCompanion vptr (ctor 0x180BC9BC4)
        void *m_pOwner0008;        // 0x008, what the ctor's actor argument returns from vtable +0x380; the companion registers itself in its vector at +0x630
        std::byte m_activity0010[0x8]; // 0x010, head of the activity object 0x180BC9D10 builds here; its provider vptr follows
        void *m_providerVTable; // 0x018

        static void AssertLayout() {
            static_assert(offsetof(C_DogCompanion, m_pOwner0008) == 0x08, "C_DogCompanion::m_pOwner0008 offset mismatch");
            static_assert(offsetof(C_DogCompanion, m_providerVTable) == 0x18, "C_DogCompanion::m_providerVTable offset mismatch");
        }
    };
} // namespace SDK::wh::xgenaimodule::activitysystem
