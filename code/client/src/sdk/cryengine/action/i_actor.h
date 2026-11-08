#pragma once

#include "../../types.h"
#include "i_game_object.h"
#include "i_movement_controller.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::rpgmodule {
    class I_Soul;
} // namespace SDK::wh::rpgmodule

namespace SDK {

    class IActor;
    struct IAnimatedCharacter;
    struct IAnimationGraphState;
    class IEntity;
    class IInventory;
    class IItem;
    struct IPhysicalEntity;
    class IVehicle;
    struct SEntityEvent;
    struct SEntitySpawnParams;

    /**
     * ABI shell for CryAction's IGameObjectExtension base.
     *
     * WHGame's CGameObjectExtensionHelper<IActor> confirms 29 inherited
     * vtable slots and the exact 0x40-byte data prefix. The shared base
     * constructor at 0x181354AB4 zeroes it, and the owner fields sit at
     * +0x28, +0x30 and +0x38 rather than at CryAction's stock +0x08, +0x10
     * and +0x18. The opaque slots deliberately stay unnamed until their exact
     * KCD2 signatures are needed.
     */
    class IGameObjectExtension {
      public:
        virtual ~IGameObjectExtension()                                                                         = default; // +0x000
        virtual void ProcessEvent(SEntityEvent &event)                                                          = 0;       // +0x008
        virtual void unk_fn0002()                                                                               = 0;
        virtual void unk_fn0003()                                                                               = 0;
        virtual void unk_fn0004()                                                                               = 0;
        virtual void unk_fn0005()                                                                               = 0;
        virtual void unk_fn0006()                                                                               = 0;
        virtual bool Init(IGameObject *gameObject)                                                              = 0; // +0x038
        virtual void PostInit()                                                                                 = 0; // +0x040
        virtual void InitClient(int channelId)                                                                  = 0; // +0x048
        virtual void PostInitClient(int channelId)                                                              = 0; // +0x050
        virtual bool ReloadExtension(IGameObject *gameObject, const SEntitySpawnParams &params)                 = 0; // +0x058
        virtual void PostReloadExtension(IGameObject *gameObject, const SEntitySpawnParams &params)             = 0; // +0x060
        virtual bool GetEntityPoolSignature(TSerialize signature)                                               = 0; // +0x068
        virtual void Release()                                                                                  = 0; // +0x070
        virtual void FullSerialize(TSerialize serialize)                                                        = 0; // +0x078
        virtual bool NetSerialize(TSerialize serialize, EEntityAspects aspect, std::uint8_t profile, int flags) = 0; // +0x080
        virtual void unk_fn0017()                                                                               = 0;
        virtual void unk_fn0018()                                                                               = 0;
        virtual void PostSerialize()                                                                            = 0; // +0x098
        virtual void SetChannelId(ChannelId id)                                                                 = 0;
        virtual void unk_fn0021()                                                                               = 0;
        virtual void unk_fn0022()                                                                               = 0;
        virtual void unk_fn0023()                                                                               = 0;
        virtual void unk_fn0024()                                                                               = 0;
        virtual void unk_fn0025()                                                                               = 0;
        virtual void unk_fn0026()                                                                               = 0;
        virtual void unk_fn0027()                                                                               = 0;
        virtual void unk_fn0028()                                                                               = 0;

        IGameObject *GetGameObject() const {
            return m_pGameObject;
        }
        EntityId GetEntityId() const {
            return m_entityId;
        }
        IEntity *GetEntity() const {
            return m_pEntity;
        }

      protected:
        // KCD2 places four additional members ahead of the stock CryAction
        // trio. The shared base constructor at 0x181354AB4 zeroes exactly
        // +0x08/+0x10/+0x18/+0x20 as qwords, then +0x28, +0x30 (dword) and
        // +0x38, which C_Actor::Init consumes as the game object, entity id
        // and entity: it calls IGameObject::CaptureView/CaptureProfileManager
        // through +0x28 and passes *(uint32 *)(this + 0x30) to
        // IActorSystem::AddActor alongside `this`.
        std::byte m_opaque0008[0x20];
        IGameObject *m_pGameObject; // 0x28
        EntityId m_entityId;        // 0x30
        std::uint32_t _pad34;       // 0x34
        IEntity *m_pEntity;         // 0x38

      private:
        static void AssertLayout() {
            static_assert(offsetof(IGameObjectExtension, m_pGameObject) == 0x28, "IGameObjectExtension::m_pGameObject offset mismatch");
            static_assert(offsetof(IGameObjectExtension, m_entityId) == 0x30, "IGameObjectExtension::m_entityId offset mismatch");
            static_assert(offsetof(IGameObjectExtension, m_pEntity) == 0x38, "IGameObjectExtension::m_pEntity offset mismatch");
        }
    };

    /**
     * CryAction IActor interface used by wh::entitymodule::C_Actor.
     * The abstract CGameObjectExtensionHelper<C_Actor, IActor, 64> table at
     * 0x183B65E00 proves the complete 128-slot boundary. The first
     * actor-specific entry is SetHealth at vtable +0x0E8.
     */
    class IActor: public IGameObjectExtension {
      public:
        virtual ~IActor()                                = default;
        virtual void SetHealth(float health)             = 0; // +0x0E8
        virtual float GetHealth() const                  = 0; // +0x0F0
        virtual int GetHealthAsRoundedPercentage() const = 0; // +0x0F8
        virtual void SetMaxHealth(float maxHealth)       = 0; // +0x100
        virtual float GetMaxHealth() const               = 0; // +0x108
        virtual void unk_fn0034()                        = 0;
        virtual void unk_fn0035()                        = 0;
        virtual void unk_fn0036()                        = 0;
        virtual bool IsFallen() const                    = 0;
        virtual bool IsDead() const                      = 0;
        virtual void unk_fn0039()                        = 0;
        virtual void unk_fn0040()                        = 0;
        virtual void unk_fn0041()                        = 0;
        virtual void unk_fn0042()                        = 0;
        /**
         * One of the two ways the locomotion gate can be satisfied.
         *
         * CAnimActionAIMovement::SelectState returns a forced non-locomotion
         * state -- ignoring the actor's velocity entirely -- unless this
         * answers true or one of the movement-status flags beside it is set.
         */
        virtual bool IsLocomotionGateOpen() const                  = 0; // slot 44
        virtual void unk_fn0044()                                  = 0;
        virtual void unk_fn0045()                                  = 0;
        virtual void unk_fn0046()                                  = 0;
        virtual void unk_fn0047()                                  = 0;
        virtual void unk_fn0048()                                  = 0;
        virtual void unk_fn0049()                                  = 0;
        virtual void unk_fn0050()                                  = 0;
        virtual void unk_fn0051()                                  = 0;
        virtual void unk_fn0052()                                  = 0;
        virtual void unk_fn0053()                                  = 0;
        /**
         * Hides, or shows again, everything this body carries.
         *
         * `C_Human`'s implementation (0x1829FFEA4) queues
         * `C_ItemAttachmentManager::SetAttachmentsHidden` on the engine-lock
         * buffer, so the work lands with the lock held rather than inline:
         * owner -> entity -> character -> attachment manager, then
         * `IAttachment::HideAttachment` for each socket a carried item uses.
         * This is the exact call KCD2 makes for itself when a human's entity
         * is hidden or made invisible -- `C_Human::ProcessEvent` queues the
         * same worker for `ENTITY_EVENT_HIDE` (14) and `INVISIBLE` (41) --
         * and it is the attachment half of that path without the physics
         * suspension `CPhysicalProxy` adds to the event.
         *
         * It is attachment flags only: a socket holding an entity
         * (`CEntityAttachment`) renders through that entity and is untouched.
         * Slot 54 is CryAction's own `IActor::HideAllAttachments` position,
         * after the `GetGrabbedEntityId` stub at 53.
         */
        virtual void HideAllAttachments(bool hide)                 = 0; // +0x1B0
        virtual void unk_fn0055()                                  = 0;
        /**
         * The actor's view rotation, beside its entity rotation. On a
         * C_Player both reach the player rotation at `C_Actor+0x238`: the
         * setter (0x1806440A0) writes viewQuat and viewQuatFinal and, unless
         * a view limit is active, the base rotation; the getter (0x18094DB70)
         * returns viewQuatFinal.
         */
        virtual void SetViewRotation(const Quat &rotation)         = 0; // +0x1C0
        virtual Quat GetViewRotation() const                       = 0; // +0x1C8
        virtual void unk_fn0058()                                  = 0;
        /**
         * How far up the actor is looking, in radians, positive up. The view
         * rotation beside it (+0x1C8, 0x18094DB70) can already contain pitch;
         * `C_CameraFirstPerson`'s pose update (0x18094D030) extracts only its
         * yaw and applies this about X on top, which is the one reader
         * that proves what it is. Both are fields of the object at
         * `C_Actor+0x238`: the rotation at +0x24, this at +0x48.
         */
        virtual float GetViewPitch() const                         = 0; // +0x1D8, 0x18094DB90
        virtual void unk_fn0060()                                  = 0;
        /** The eye in entity space, from bone 10 of the skeleton; CryEngine's GetLocalEyePos. 0x18063D054. */
        virtual Vec3 GetLocalEyePos() const = 0; // +0x1E8
        virtual void unk_fn0062()                                  = 0;
        virtual void unk_fn0063()                                  = 0;
        virtual void unk_fn0064()                                  = 0;
        virtual void unk_fn0065()                                  = 0;
        virtual void unk_fn0066()                                  = 0;
        virtual void unk_fn0067()                                  = 0;
        virtual void unk_fn0068()                                  = 0;
        virtual IMovementController *GetMovementController() const = 0; // +0x228
        virtual void unk_fn0070()                                  = 0;
        virtual void unk_fn0071()                                  = 0;
        virtual void unk_fn0072()                                  = 0;
        virtual void unk_fn0073()                                  = 0;
        virtual void unk_fn0074()                                  = 0;
        virtual void unk_fn0075()                                  = 0;
        virtual void SetFlyMode(std::uint8_t flyMode)              = 0;
        virtual std::uint8_t GetFlyMode() const                    = 0;

        void Release() override                            = 0;
        virtual void unk_fn0078()                          = 0;
        virtual void unk_fn0079()                          = 0;
        virtual void unk_fn0080()                          = 0;
        virtual void unk_fn0081()                          = 0;
        virtual void InitLocalPlayer()                     = 0;
        virtual const char *GetActorClassName() const      = 0;
        virtual ActorClass GetActorClass() const           = 0;
        virtual void unk_fn0085()                          = 0;
        virtual const char *GetEntityClassName() const     = 0;
        virtual void unk_fn0087()                          = 0;
        virtual void unk_fn0088()                          = 0;
        void ProcessEvent(SEntityEvent &event) override    = 0; // overrides +0x008
        virtual void unk_fn0089()                          = 0;
        virtual IAnimatedCharacter *GetAnimatedCharacter() = 0;
        virtual void unk_fn0091()                          = 0;
        virtual void unk_fn0092()                          = 0;
        virtual void unk_fn0093()                          = 0;
        virtual void unk_fn0094()                          = 0;
        /** KCD2's shared default logs "not implemented" and returns false. */
        virtual bool Respawn() = 0;
        /** KCD2's shared default only logs "not implemented". */
        virtual void ResetToSpawnLocation() = 0;
        virtual void unk_fn0097()           = 0;
        virtual void unk_fn0098()           = 0;
        virtual void unk_fn0099()           = 0;
        virtual void unk_fn0100()           = 0;
        virtual void unk_fn0101()           = 0;

        // The KCD2 IActor tail extends through slot 127. The 2016 CryEngine
        // names no longer align with this region, so retain it as an exact
        // opaque shell until individual contracts are independently proven.
        virtual void unk_fn0102() = 0;
        virtual void unk_fn0103() = 0;
        virtual void unk_fn0104() = 0;
        virtual void unk_fn0105() = 0;
        virtual void unk_fn0106() = 0;
        virtual void unk_fn0107() = 0;
        virtual void unk_fn0108() = 0;
        virtual void unk_fn0109() = 0;
        virtual void unk_fn0110() = 0;
        virtual void unk_fn0111() = 0;
        virtual void unk_fn0112() = 0;

        /**
         * Dresses the actor as a soul without giving it one.
         *
         * The look is all it applies: no identity, no brain, no AI. This is
         * how an NPC_NAI gets an appearance despite its script stubbing out
         * SetActorModel (see C_ScriptBindActor::MakeLookAsSoul, which reaches
         * it through this slot).
         *
         * Note the slot arithmetic: IActor redeclares its destructor, which
         * overrides slot 0 rather than adding one, so an index counted from
         * the declarations comes out one too high. The neighbour at +0x380
         * takes another actor, and calling it with a soul reads garbage.
         */
        virtual void MakeLookAsSoul(wh::rpgmodule::I_Soul *soul) = 0; // +0x388
        virtual void unk_fn0114()                                = 0;
        virtual void unk_fn0115()                                = 0;
        virtual void unk_fn0116()                                = 0;
        virtual void unk_fn0117()                                = 0;
        virtual void unk_fn0118()                                = 0;
        virtual void unk_fn0119()                                = 0;
        virtual void unk_fn0120()                                = 0;
        virtual void unk_fn0121()                                = 0;
        virtual void unk_fn0122()                                = 0;
        virtual void unk_fn0123()                                = 0;
        virtual void unk_fn0124()                                = 0;
        virtual void unk_fn0125()                                = 0;
        virtual void unk_fn0126()                                = 0;
        virtual void unk_fn0127()                                = 0; // +0x3F8
    };

    static_assert(sizeof(IGameObjectExtension) == 0x40, "IGameObjectExtension size mismatch");
    static_assert(sizeof(IActor) == 0x40, "IActor size mismatch");
} // namespace SDK
