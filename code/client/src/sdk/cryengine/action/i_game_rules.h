#pragma once

#include "../../patterns.h"
#include "../../types.h"
#include "../../wh/combatmodule/s_combat_hit_record.h"

#include <utils/hooking/hooking.h>

namespace SDK {
    class IActor;

    /**
     * IGameRules as KCD2's CGameRules implements it: primary vtable
     * 0x183AF1678, reached through IGameRulesSystem::GetCurrentGameRules.
     *
     * Only the slot the mod calls is named. The ones in front of it keep
     * the table order and nothing else; stock CryEngine's names cannot be
     * placed on them because KCD2's CGameRules is a CryEngine 2-era class
     * with its own additions.
     */
    class IGameRules {
      public:
        /**
         * `CGameRules::QueueHitRecord` (slot 165, 0x180725698): hands a blow's
         * record to its victim. `FlushHitRecords` (0x180DF0DC8) delivers it
         * through `SendHitToActor` (0x1804CA0E4) as game-object event 32 --
         * what the body reacts to, and plays the blow's impact from. Damage is
         * dispatched before a record is ever queued; this is presentation.
         */
        void QueueHitRecord(const wh::combatmodule::S_CombatHitRecord *record) {
            hook::this_call<void>(gPatterns.CGameRules__QueueHitRecord, this, record);
        }

        virtual void unk_fn0000() = 0;
        virtual void unk_fn0001() = 0;
        virtual void unk_fn0002() = 0;
        virtual void unk_fn0003() = 0;
        virtual void unk_fn0004() = 0;
        virtual void unk_fn0005() = 0;
        virtual void unk_fn0006() = 0;
        virtual void unk_fn0007() = 0;
        virtual void unk_fn0008() = 0;
        virtual void unk_fn0009() = 0;
        virtual void unk_fn0010() = 0;
        virtual void unk_fn0011() = 0;
        virtual void unk_fn0012() = 0;
        virtual void unk_fn0013() = 0;
        virtual void unk_fn0014() = 0;
        virtual void unk_fn0015() = 0;
        virtual void unk_fn0016() = 0;
        virtual void unk_fn0017() = 0;
        virtual void unk_fn0018() = 0;
        virtual void unk_fn0019() = 0;
        virtual void unk_fn0020() = 0;
        virtual void unk_fn0021() = 0;
        virtual void unk_fn0022() = 0;
        virtual void unk_fn0023() = 0;
        virtual void unk_fn0024() = 0;
        virtual void unk_fn0025() = 0;
        virtual void unk_fn0026() = 0;
        virtual void unk_fn0027() = 0;
        virtual void unk_fn0028() = 0;
        virtual void unk_fn0029() = 0;
        virtual void unk_fn0030() = 0;
        virtual void unk_fn0031() = 0;
        virtual void unk_fn0032() = 0;
        virtual void unk_fn0033() = 0;
        virtual void unk_fn0034() = 0;
        virtual void unk_fn0035() = 0;
        virtual void unk_fn0036() = 0;
        virtual void unk_fn0037() = 0;
        virtual void unk_fn0038() = 0;
        virtual void unk_fn0039() = 0;
        virtual void unk_fn0040() = 0;
        virtual void unk_fn0041() = 0;
        virtual void unk_fn0042() = 0;
        virtual void unk_fn0043() = 0;
        virtual void unk_fn0044() = 0;
        virtual void unk_fn0045() = 0;
        virtual void unk_fn0046() = 0;
        virtual void unk_fn0047() = 0;
        virtual void unk_fn0048() = 0;
        virtual void unk_fn0049() = 0;
        virtual void unk_fn0050() = 0;
        virtual void unk_fn0051() = 0;
        virtual void unk_fn0052() = 0;
        virtual void unk_fn0053() = 0;
        virtual void unk_fn0054() = 0;
        virtual void unk_fn0055() = 0;
        virtual void unk_fn0056() = 0;
        virtual void unk_fn0057() = 0;
        virtual void unk_fn0058() = 0;
        virtual void unk_fn0059() = 0;
        virtual void unk_fn0060() = 0;
        virtual void unk_fn0061() = 0;
        virtual void unk_fn0062() = 0;
        virtual void unk_fn0063() = 0;
        virtual void unk_fn0064() = 0;
        virtual void unk_fn0065() = 0;
        virtual void unk_fn0066() = 0;
        virtual void unk_fn0067() = 0;
        virtual void unk_fn0068() = 0;
        virtual void unk_fn0069() = 0;
        virtual void unk_fn0070() = 0;
        virtual void unk_fn0071() = 0;
        virtual void unk_fn0072() = 0;
        virtual void unk_fn0073() = 0;
        virtual void unk_fn0074() = 0;
        virtual void unk_fn0075() = 0;
        virtual void unk_fn0076() = 0;
        virtual void unk_fn0077() = 0;
        virtual void unk_fn0078() = 0;
        virtual void unk_fn0079() = 0;
        virtual void unk_fn0080() = 0;
        virtual void unk_fn0081() = 0;
        virtual void unk_fn0082() = 0;
        virtual void unk_fn0083() = 0;
        virtual void unk_fn0084() = 0;
        virtual void unk_fn0085() = 0;
        virtual void unk_fn0086() = 0;
        virtual void unk_fn0087() = 0;
        virtual void unk_fn0088() = 0;
        virtual void unk_fn0089() = 0;
        virtual void unk_fn0090() = 0;
        virtual void unk_fn0091() = 0;
        virtual void unk_fn0092() = 0;
        virtual void unk_fn0093() = 0;

        /**
         * Replaces the actor on `channelId` with one of `className`, keeping
         * its entity id, name, position and facing (0x1839DDAA8, +0x2F0).
         *
         * Removes the old entity at once, calls `IActorSystem::CreateActor`
         * with the same id, then moves the new actor to the old position.
         * Returns the old actor untouched when it already has that class,
         * and null when nothing is on the channel or the gEnv byte at
         * +0x238 is clear. That byte's meaning is not established.
         *
         * For the local channel KCD2's `CreateActor` forces both the entity
         * id and the GUID to 0x7777, the player soul's GUID, so the new
         * player resolves the same soul -- inventory, stats and all.
         *
         * Invalidates every pointer into the old actor, its game object and
         * its entity.
         *
         * **Not safe on the local player mid-session.** AI subscriptions
         * outlive the old player. The next C_AITransformManager worker tick
         * (0x180646260) can fault in C_LinkableObject::GetWorldPosition
         * (0x180647E98) because the linkable's provider at +0x18 is null.
         * The game rebuilds its player only during a level load.
         */
        virtual IActor *ChangePlayerClass(ChannelId channelId, const char *className) = 0; // +0x2F0
    };

    static_assert(sizeof(IGameRules) == 0x8, "IGameRules size mismatch");
} // namespace SDK
