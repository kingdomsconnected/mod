#pragma once

#include "../../types.h"

namespace SDK {
    class IEntity;

    /**
     * An entity's AI object: CryAI's representation of it, which the AI
     * system perceives and updates. KCD2's table is CryEngine's `IAIObject`
     * shifted by three slots (`GetAIType` is CE slot 14 and KCD2 slot 17,
     * read off `CAIObject`'s vtable 0x183A787A8), up to `SetViewDir`; KCD2
     * then adds one slot before `GetEntityDir`. Only the slots below are
     * named; `SetReceiver` is inherited unoverridden by `CAIObject`,
     * `CAIActor`, `CAIPlayer` and `CPuppet`.
     */
    class IAIObject {
      public:
        virtual ~IAIObject() = default; // +0x000
        virtual void Opaque01() = 0; // +0x008
        virtual void Opaque02() = 0; // +0x010
        virtual void Opaque03() = 0; // +0x018
        virtual void Opaque04() = 0; // +0x020
        virtual void Opaque05() = 0; // +0x028
        virtual void Opaque06() = 0; // +0x030
        /**
         * CryAISystem's IsEnabled: CAIObject+0x10 (0x181A731E0), shared by
         * every AI object class. C_ActorMovementController::Update
         * (0x1804B8E88) skips a body's whole look and aim pass while its
         * entity has an AI object that is not enabled and the actor is not
         * player-controlled.
         */
        virtual bool IsEnabled() const = 0; // +0x038, slot 7
        virtual void Opaque08() = 0; // +0x040
        virtual void Opaque09() = 0; // +0x048
        virtual void Opaque10() = 0; // +0x050
        virtual void Opaque11() = 0; // +0x058
        virtual void Opaque12() = 0; // +0x060
        virtual void Opaque13() = 0; // +0x068
        virtual void Opaque14() = 0; // +0x070
        virtual void Opaque15() = 0; // +0x078
        virtual void Opaque16() = 0; // +0x080
        virtual void Opaque17() = 0; // +0x088
        virtual void Opaque18() = 0; // +0x090
        virtual void Opaque19() = 0; // +0x098
        /**
         * Places the object at `position`, facing `forward`.
         *
         * CryEngine's entity transform hook (`CAIEntityComponent::ProcessEvent`
         * on `ENTITY_EVENT_XFORM`) calls it with the entity's world position
         * and forward direction. `CAIActor` overrides it (0x1803CF114): it
         * reads the proxy's body info without refreshing the movement state
         * (`CAIProxy::QueryBodyInfo(info, false)`) and then sets the view,
         * body, entity and move directions from that info rather than from
         * `forward` (0x1805909F8).
         */
        virtual void SetPos(const Vec3 &position, const Vec3 &forward) = 0; // +0x0A0
        virtual const Vec3 &GetPos() const                             = 0; // +0x0A8
        virtual void Opaque22() = 0; // +0x0B0
        virtual void Opaque23() = 0; // +0x0B8
        virtual void Opaque24() = 0; // +0x0C0
        virtual void Opaque25() = 0; // +0x0C8
        /**
         * The direction the body faces, as the AI last saw it: `CAIObject+0x44`.
         * `CAISystem` keeps it from the body info -- the animated facing, else
         * the entity's (`SAIBodyInfo::GetBodyDir`, 0x1808A2520).
         */
        virtual const Vec3 &GetBodyDir() const         = 0; // +0x0D0
        virtual void SetBodyDir(const Vec3 &direction) = 0; // +0x0D8
        virtual void Opaque28() = 0; // +0x0E0
        virtual void Opaque29() = 0; // +0x0E8
        virtual void Opaque30() = 0; // +0x0F0
        virtual void Opaque31() = 0; // +0x0F8
        virtual void Opaque32() = 0; // +0x100
        virtual void Opaque33() = 0; // +0x108
        virtual void Opaque34() = 0; // +0x110
        virtual void Opaque35() = 0; // +0x118
        virtual void Opaque36() = 0; // +0x120
        virtual void Opaque37() = 0; // +0x128
        virtual void Opaque38() = 0; // +0x130
        virtual void Opaque39() = 0; // +0x138
        virtual void Opaque40() = 0; // +0x140
        virtual void Opaque41() = 0; // +0x148
        virtual void Opaque42() = 0; // +0x150
        virtual void Opaque43() = 0; // +0x158
        virtual void Opaque44() = 0; // +0x160
        virtual void Opaque45() = 0; // +0x168
        virtual void Opaque46() = 0; // +0x170
        virtual void Opaque47() = 0; // +0x178
        virtual void Opaque48() = 0; // +0x180
        virtual void Opaque49() = 0; // +0x188
        virtual void Opaque50() = 0; // +0x190
        virtual void Opaque51() = 0; // +0x198
        virtual void Opaque52() = 0; // +0x1A0
        virtual void Opaque53() = 0; // +0x1A8
        virtual void Opaque54() = 0; // +0x1B0
        virtual void Opaque55() = 0; // +0x1B8
        virtual void Opaque56() = 0; // +0x1C0
        virtual void Opaque57() = 0; // +0x1C8
        virtual void Opaque58() = 0; // +0x1D0
        virtual void Opaque59() = 0; // +0x1D8
        virtual void Opaque60() = 0; // +0x1E0
        virtual void Opaque61() = 0; // +0x1E8
        virtual void Opaque62() = 0; // +0x1F0
        virtual void Opaque63() = 0; // +0x1F8
        virtual void Opaque64() = 0; // +0x200
        virtual void Opaque65() = 0; // +0x208
        virtual void Opaque66() = 0; // +0x210
        virtual void Opaque67() = 0; // +0x218
        virtual void Opaque68() = 0; // +0x220
        virtual void Opaque69() = 0; // +0x228
        virtual void Opaque70() = 0; // +0x230
        virtual void Opaque71() = 0; // +0x238

        /**
         * Points the object at the NPC that speaks for it, or at nothing.
         *
         * A bare store to `CAIObject+0xD0`. `C_NPCManager` sets it when it
         * registers an NPC and clears it again on detach, so null is a state
         * the engine itself leaves behind.
         */
        virtual void SetReceiver(void *receiver) = 0; // +0x240
    };

    /** The entity's AI object, or null when it has none. */
    IAIObject *GetEntityAIObject(IEntity *entity);

    /**
     * Strips an entity of its AI for good.
     *
     * CryAI's AIEVENT_DISABLE is not enough here: KCD2's decision layer is
     * Warhorse's own xgenaimodule (C_AIBrain, I_Intellect, C_BehaviorTreeSUBB),
     * which keeps running an actor that has merely been told to stop.
     *
     * Removing the AI object is what actually settles it, and the engine
     * already has that path. CScriptBind_Action::RegisterWithAI -- the native
     * side of the BasicAI:RegisterAI that gives every NPC its AI -- calls
     * IEntity::RegisterInAISystem with a zero-type AIObjectParams before
     * registering for real, and CEntity::RegisterInAISystem answers that by
     * clearing ENTITY_FLAG_HAS_AI, removing the object through the AI object
     * manager, zeroing the entity's AI object id and returning without
     * creating a replacement.
     *
     * The result is the state the local player is in: BasicAI:RegisterAI skips
     * `self == g_localActor`, so the player character never has an AI object
     * either. That is exactly what a networked puppet wants.
     *
     * Returns false when the entity kept its AI object, which the engine
     * refuses to remove while the entity carries the 0x200000 extended flag.
     */
    bool UnregisterEntityFromAI(IEntity *entity);

} // namespace SDK
