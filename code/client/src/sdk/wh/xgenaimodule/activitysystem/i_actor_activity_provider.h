#pragma once

#include "../c_npc_manager.h"

namespace SDK::wh::xgenaimodule::activitysystem {
    class C_DogObjectiveSet;
    class C_DogObjectivePlanner;

    /**
     * The sub-object embedded at `+0x18` of a dog's companion.
     *
     * Not on the actor. `C_DogInteractionManager::GetDogActor` (`0x1808ACA1C`)
     * looks the *AI* side up through the NPC manager and hands back the
     * companion, and it is that object -- not `C_Dog` -- whose `+0x18` holds a
     * vptr the game calls through with `this` set to the field's own address:
     * `lea rcx, [rdi+18h] / mov rax, [rcx] / call [rax+18h]`
     * at `0x1809F6B19` in `FindEligibleObjective`, and the same three
     * instructions in `QueueDogObjective` (`0x182A01AB2`) and
     * `ResolveObjectiveForTarget`. Reaching `+0x18` on the entity-side
     * `C_Dog` instead calls through whatever happens to be in the actor's own
     * opaque prefix, which is an access violation.
     *
     * It is the second of the four vptrs of `C_DogCompanionContext`, which
     * `0x180BC9D10` builds at `C_DogCompanion+0x10` and whose tables sit at
     * `0x183A81CC8`, `0x183A81D48`, `0x183A81CE8` and `0x183A81D08`. That is
     * also what every one of the dog's objectives is constructed with: the
     * base `C_DogObjective` constructor at `0x1806265D8` stores it at the
     * objective's own `+0x70`, so an objective asks this interface for the
     * things below rather than reaching for a global.
     *
     * Slots 3 and 7 return *different* objects, both inside the companion's
     * own allocation. Which a call wants is settled by the game's own use:
     * `FindEligibleObjective` passes slot 3's to `GetObjective`, the
     * companion's tick passes slot 7's to `UpdateObjectivePlanner`. Asking one
     * a question meant for the other returns a plausible wrong answer rather
     * than crashing, which is why they are typed apart here.
     */
    class I_ActorActivityProvider {
      public:
        virtual void unk_fn0000() = 0;
        virtual void unk_fn0001() = 0;
        virtual void unk_fn0002() = 0;

        /** The objectives this dog has. Null before its brain is up. */
        virtual C_DogObjectiveSet *GetActivityObjectiveSet() = 0; // slot 3

        /**
         * The person this dog belongs to, or nullptr while it belongs to
         * nobody.
         *
         * Read straight out of the companion's `+0x30`, which
         * `0x180BC9CE8` fills at construction and the soul-group callback at
         * `0x181769C24` refreshes: both resolve it the same way
         * `C_GetCompanionMaster` does, by taking the master WUID off the dog
         * soul's group at `+152` and asking the NPC manager for that NPC. So
         * the master follows possession, and a body possessed onto somebody
         * else's soul answers with *their* NPC rather than with the local
         * player's.
         *
         * Read rather than used: the objectives that would have wanted it are
         * the ones the game never runs. It is recorded because it is the
         * answer to "whose dog is this" that the AI side itself uses.
         */
        virtual C_NPC *GetCompanionMaster() = 0; // slot 4

        virtual void unk_fn0005() = 0;
        virtual void unk_fn0006() = 0;

        /**
         * What chooses between them.
         *
         * `C_DogCompanion` vtable slot 5 (`0x1809F6C28`) fetches it with
         * `lea rcx, [rbx+18h] / mov rax, [rcx] / call [rax+38h]` and hands the
         * result straight to `UpdateObjectivePlanner`. `0x38 / 8` is 7.
         */
        virtual C_DogObjectivePlanner *GetActivityObjectivePlanner() = 0; // slot 7
    };
} // namespace SDK::wh::xgenaimodule::activitysystem
