#pragma once

#include "../../cryengine/action/i_game_object_profile_manager.h"
#include "../../cryengine/action/c_animated_character.h"
#include "../../cryengine/action/i_actor.h"
#include "../../cryengine/animation/c_char_instance.h"
#include "../../cryengine/animation/c_skeleton_anim.h"
#include "../../cryengine/entitysystem/i_entity.h"
#include "../../types.h"
#include "../dialogmodule/s_decision_alias.h"
#include "../rpgmodule/c_inventory_soul.h"
#include "../rpgmodule/c_soul.h"
#include "../shared/c_signal.h"
#include "c_actor_movement_controller.h"
#include "c_item.h"
#include "c_anim_action_ai_movement.h"
#include "c_horse_ride_context.h"
#include "i_actor_carry_corpse.h"
#include "c_ragdoll_manager.h"
#include "s_ledge_grab_input.h"
#include "c_actor_condition_controller.h"
#include "c_actor_dirt_helper.h"
#include "c_hit_death_reactions.h"
#include "c_idle_state.h"
#include "s_actor_params.h"
#include "s_actor_physics_ignore.h"
#include "i_clothing_attachment_owner.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::combatmodule {
    class C_CombatActor;
} // namespace SDK::wh::combatmodule

namespace SDK::wh::entitymodule {
    class C_ActorRotation;
    struct S_ActorClass;

    /**
     * Bits of `C_Actor`'s state byte that decide whether a body may be dressed.
     *
     * Not to be confused with the engine's own `E_ActorState` in
     * `e_actor_state.h`, which is the RTTR-registered 45-value set describing
     * what a body is *doing* and lives on the actor model. These two are
     * unrelated; this one is a byte on the actor, that one is a signalled
     * bitset on its model.
     */
    enum E_ActorStateBits : std::uint8_t {
        // The applied and wanted halves of "this body is put away", which
        // the engine reconciles later. Proven by the two writers: SetPutAway
        // writes `(putAway << 6)`, and RequestPutAway writes `(putAway << 7)`
        // and compares it against 0x40.
        // A ground placement waiting to run: RequestGroundPlacement
        // (slot 119, 0x181E7D400) sets it when forced or while its entity
        // check holds, CancelGroundPlacement (slot 120) and the immediate
        // path (0x181E7D450) clear it.
        ACTOR_STATE_GROUND_PLACEMENT_PENDING = 0x04,
        ACTOR_STATE_PUT_AWAY_APPLIED = 0x40,
        ACTOR_STATE_PUT_AWAY_WANTED  = 0x80,
    };

    class C_ActionActor;
    class C_ActorModel;
    class C_ActorSpeakMan;
    class C_ActorEquipmentHandler;
    class C_ItemAttachmentManager;
    class C_RiderExpansion;

    /**
     * What a body wears, as the engine builds it onto the character.
     *
     * Built on demand by C_Actor and destroyed again when the body is put
     * away, so it is only ever reached through it.
     */
    class C_ActorClothing;

    /**
     * Shared actor implementation base for players, NPCs, and animals.
     *
     * C_Actor's primary table at 0x183E74198 contains exactly 275 slots:
     * the 128-slot KCD2 IActor contract followed by 147 opaque C_Actor
     * entries. Its scalar deleting destructor proves the complete 0x9C0-byte
     * size. IGameObjectView and IGameObjectProfileManager secondary vptrs
     * reside at +0x40 and +0x48. The named health accessors directly prove
     * the stored health and maximum-health floats at +0x164 and +0x168;
     * GetHealth may instead delegate through an as-yet untyped runtime
     * I_Soul at +0x668. Actor soul setup and teardown register that exact
     * pointer with the RPG soul system. The actor's equipment-listener bridge
     * is owned through +0x678. Actor initialization constructs its movement
     * controller at +0x180; world transforms remain owned by IEntity at the
     * inherited +0x18 entity pointer. The zero-initialized Vec3 at +0x980 is
     * serialized as `LastNavMeshPos` for AI/NPC actors; it is a navigation
     * cache rather than the authoritative entity position. IActor slot 90 is
     * proven to return the actor's IAnimatedCharacter: its consumers dispatch
     * through the exact
     * CAnimatedCharacter table, including the graph-state pointer operations.
     */
    class C_Actor: public SDK::IActor {
      public:
        ~C_Actor() override = default;

        virtual void unk_fn0128() = 0; // first C_Actor-specific slot, +0x400
        virtual void unk_fn0129() = 0;
        virtual void unk_fn0130() = 0;
        virtual void unk_fn0131() = 0;
        virtual void unk_fn0132() = 0;
        /** Native pace consumed by the tag pass: AI state on actors, per-player state on C_Player. */
        virtual void SetAnimationMovementPace(float pace) = 0; // slot 133, +0x428
        virtual float GetAnimationMovementPace() const    = 0; // slot 134, +0x430
        virtual void unk_fn0135()                         = 0;
        virtual void unk_fn0136()                         = 0;
        virtual void unk_fn0137()                         = 0;
        virtual void unk_fn0138()                         = 0;
        virtual void unk_fn0139()                         = 0;
        virtual void unk_fn0140()                         = 0;
        virtual void unk_fn0141()                         = 0;
        virtual void unk_fn0142() = 0;
        /** This body's speech, allocated on first use. Never null. */
        virtual C_ActorSpeakMan *GetSpeakMan() = 0;
        virtual void unk_fn0144()              = 0;
        virtual void unk_fn0145()              = 0;
        virtual void unk_fn0146()              = 0;
        virtual void OnEntityRemoved()         = 0; // +0x498, ProcessEvent removal path
        virtual void unk_fn0148()              = 0;
        virtual void unk_fn0149()              = 0;
        virtual void unk_fn0150()              = 0;

        /**
         * Builds this body's physical entity.
         *
         * Slot 151. `C_RagdollManager::SetPhysicsProfile` (0x1804CA6B0)
         * dispatches it through `vtable + 1208` for physics profile 1, paired
         * with `PostPhysicalize` below at `+1216`, and guarded on
         * `m_state & 0x40`. `C_Animal::Physicalize` (0x1804CB0DC) overrides it
         * to add the auxiliary collision object.
         *
         * A body that has never been physicalized has no `IPhysicalEntity` at
         * all: it renders, animates and thinks, and every ray, sweep and
         * projectile passes through it.
         */
        virtual void Physicalize() = 0;

        /** Slot 152, the other half of what profile 1 does. */
        virtual void PostPhysicalize() = 0;
        virtual void unk_fn0153()              = 0;
        /**
         * Why a body is being brought back, the argument `Revive` below
         * takes.
         *
         * Both values come out of one expression: `C_ScriptBindActor::Revive`
         * computes `2 * (fromGameModeInit + 1)`, so a plain `actor:Revive()`
         * arrives as 2 and a game-mode init as 4. Reason 2 is also one of the
         * two values that make `C_Actor::Revive` re-register the DBA groups
         * outright, rather than only for a player-controlled actor.
         */
        enum EReviveReason : std::uint8_t {
            eReviveReason_Script       = 2,
            eReviveReason_GameModeInit = 4,
        };

        /**
         * The engine's own resurrect, and the only thing that undoes a death
         * in place; `C_ScriptBindActor::Revive` reaches this same slot.
         *
         * 0x1809CF380 resets the ragdoll manager, the carry-corpse state and
         * the action actor, restores the entity's update and render flags,
         * and calls `RegisterDBAGroups` -- the counterpart to the
         * `UnRegisterDBAGroups` in `HandleDeath`, without which the animation
         * databases stay unloaded. `C_Player::Revive` (0x180BC67E4) adds the
         * camera reset, `StopAnim` and save lock 6. Health is not touched.
         */
        virtual void Revive(EReviveReason reasonForRevive) = 0; // slot 154
        virtual void unk_fn0155()              = 0;
        virtual void unk_fn0156()              = 0;
        virtual void unk_fn0157()              = 0;
        virtual void unk_fn0158()              = 0;
        virtual void unk_fn0159()              = 0;
        virtual void unk_fn0160()              = 0;
        virtual void unk_fn0161()              = 0;

        /**
         * Slot 162 (0x18093AE58, which `C_Human` inherits), Lua
         * `actor:RequestDialog(target, alias, ...)`: starts the conversation
         * `alias` names between this actor and `targetEntityId`. The two flags
         * are forwarded into the request; which is which is not proven.
         */
        virtual bool RequestDialog(EntityId targetEntityId, const wh::dialogmodule::S_DecisionAlias &alias, bool unkFlag0, bool unkFlag1) = 0;

        /**
         * Slot 163 (`C_Human`: 0x18082A410), Lua `actor:CanTalk(user)`: whether
         * `talkerEntityId` may open a conversation with this actor. What
         * `BasicAIActions:GetActions` offers the native Talk entry on. `C_Human`
         * requires a soul that can speak, no combat hostility between the two,
         * and `C_DialogManager::CanStartDialog` -- that the dialogue graph has
         * something for the pair. `C_Actor` itself answers false.
         */
        virtual bool CanTalk(EntityId talkerEntityId, const wh::dialogmodule::S_DecisionAlias &alias) = 0;

        virtual void unk_fn0164()              = 0;
        virtual void unk_fn0165()              = 0;
        virtual void unk_fn0166()              = 0;
        virtual void unk_fn0167()              = 0;
        virtual void unk_fn0168()              = 0;
        virtual void unk_fn0169()              = 0;
        virtual void unk_fn0170()              = 0;
        virtual void unk_fn0171()              = 0;
        /**
         * Slot 172. On C_Player (0x18064429C) the player rotation's base
         * rotation (C_Actor+0x238, +0x34), written whatever the camera limits
         * say, with the view rebuilt from it and the current pitch -- the
         * setter `C_Horse::SetRotationWithRider` uses for a rider.
         * `SetViewRotation` leaves the base alone while camera limits are on.
         */
        virtual void SetBaseRotation(const Quat &rotation) = 0;
        /**
         * Slot 173, the base rotation setter `C_Actor::ProcessEvent`
         * (0x1804A903D) reaches on a rotation XFORM. On C_Horse
         * (0x181ECC550) the extended flag 0x200000 writes the base straight
         * into C_ActorRotation (+0x238) without turning the rider's angles;
         * without it the horse forwards to `SetRotationWithRider` (slot 172).
         * 0x200000 is not a CryEngine ENTITY_XFORM flag.
         */
        virtual void SetBaseRotationWithFlags(const Quat &rotation, std::uint32_t flags) = 0;
        virtual void unk_fn0174()              = 0;
        virtual void unk_fn0175()              = 0;
        virtual void unk_fn0176()              = 0;
        virtual void unk_fn0177()              = 0;
        virtual void unk_fn0178()              = 0;
        virtual void unk_fn0179()              = 0;
        /**
         * Queues the actor's native interactive action against a world object.
         *
         * C_ScriptBindActor::StartInteractiveActionByName forwards its four
         * Lua arguments straight through this slot (+0x5A0). AnimDoor uses it
         * for the human half of a door transition after selecting the action
         * name from C_AnimDoor's own animation table.
         */
        virtual void StartInteractiveActionByName(const char *actionName, SDK::EntityId objectEntityId, bool updateVisibility, float animationSpeed) = 0;

        /**
         * Takes the body back out of whatever interactive action it is in.
         *
         * The mirror of the slot above: it restores the player-control path
         * the start call suppressed, clears the actor model's interaction
         * target and broadcasts that change. Safe on an actor that is not
         * interacting, where it does nothing.
         */
        virtual void EndInteractiveAction() = 0; // slot 181, +0x5A8
        /**
         * Slots 182/183 (+0x5B0/+0x5B8), 0x1808F09A4/0x18285B358.
         * Set the model's signalled visor property, invalidate worn helmet
         * visuals (item flag 0x80000), and update the native audio switch.
         * The actor model and equipment listener must already exist.
         */
        virtual void OpenVisor()            = 0;
        virtual void CloseVisor()           = 0;
        /**
         * Slot 184 (+0x5C0), 0x1808F08BC in C_Actor, C_Human and C_Player.
         * Reads the actor model's signalled visor-closed property. The flag
         * alone does not imply an equipped helmet; check its visor type too.
         */
        virtual bool IsVisorClosed() const  = 0;
        virtual void unk_fn0185()           = 0;
        virtual void unk_fn0186()           = 0;
        virtual void unk_fn0187()           = 0;
        virtual void unk_fn0188()           = 0;
        virtual void unk_fn0189()           = 0;
        virtual void unk_fn0190()           = 0;
        /**
         * What this body is holding, and what it would hold if it drew.
         *
         * Slot 191 (+0x5F8). `C_Actor`'s own implementation is the shared
         * `return 0` stub, so this is null for anything that is not a human;
         * `C_Human` overrides it to return the manager it built in `Init`.
         * Every weapon question -- which set is selected, what is in which
         * slot, draw, holster -- goes through the returned object.
         */
        virtual C_ItemAttachmentManager *GetItemAttachmentManager() const = 0;
        virtual void unk_fn0192()                                         = 0;
        virtual void unk_fn0193()                                         = 0;
        virtual void unk_fn0194()                                         = 0;
        virtual void unk_fn0195()                                         = 0;
        virtual void unk_fn0196()                                         = 0;
        virtual void unk_fn0197()                                         = 0;
        virtual void unk_fn0198()                                         = 0;
        virtual void unk_fn0199()                                         = 0;
        virtual void unk_fn0200()                                         = 0;
        virtual void unk_fn0201()                                         = 0;
        virtual void unk_fn0202()                                         = 0;
        /**
         * The game's own high-level ride facade, slots 203-205.
         *
         * These are what Lua reaches as `CanMount`, `ForceMount` and
         * `ForceDismount` -- `C_ScriptBindHuman::ForceDismount` (0x182ABA5C4)
         * calls `DismountHorse` with force set. Each is a short forwarder onto
         * `C_ActionActor::GetRiderExpansion(...)` and then
         * `C_RiderExpansion` slots 8, 11 and 12, so they are the front door to
         * exactly the same machinery, with `reason` and `side` fixed at 0.
         *
         * Side 0 is not a detail: it lets the get-off pick whichever flank has
         * clear ground. `C_HorseRideContext::RequestDismount` runs
         * `ChooseDismountSide` **even when force is set**, and returns having
         * done nothing at all when both flanks are blocked -- so a dismount
         * beside a wall, or beside a horse that has fallen over, can be a
         * silent no-op whatever flags it was given.
         */
        virtual bool CanMountHorse(C_Actor *horse)         = 0; // slot 203, 0x18285B30C
        virtual bool MountHorse(C_Actor *horse, bool force) = 0; // slot 204, 0x180AD453C; C_Player 0x1819A6250 refuses past the soul's heavy-load threshold (C_Soul slot 17) and passes StartRide's flagB set
        virtual bool DismountHorse(bool force, bool place)  = 0; // slot 205, 0x18285C7E4
        virtual void unk_fn0206()                                         = 0;
        virtual void unk_fn0207()                                         = 0;
        virtual void unk_fn0208()                                         = 0;
        virtual void unk_fn0209()                                         = 0;
        /**
         * Slot 210 (0x180684928). Resumes the action controller and, when the
         * body supports them, finds or creates its "ProceduralContextAim",
         * "ProceduralContextLook" and "ProceduralContextHold" contexts; with
         * `queueActions` it also queues the looping pose actions that drive
         * them. `Revive` calls it with true right after `SetupActorModel`,
         * and `PostReloadExtension` does the same. `C_Player` overrides it
         * (0x18113A11C) to also queue its `C_PlayerMovementAction`.
         */
        virtual void SetupProceduralAnimationContexts(bool queueActions) = 0;
        /**
         * Clears the body's Mannequin action controller, slot 211.
         *
         * 0x180AA18E8: drops the aim-support flag, clears the resolved scope
         * context when there is one, calls the action controller's own
         * `Reset`, and releases the actor-side controller resources.
         *
         * This is the high-level way to get rid of an animation a body is
         * stuck in -- a scope still holding a fragment keeps playing it
         * whatever the action queue believes. Heavy-handed: it resets the
         * whole controller, not one scope.
         */
        virtual void ResetMannequinActionController() = 0; // slot 211

        /**
         * The engine's own death: AI notification, the `death` script event,
         * animation database teardown, AI event 9 for anything not
         * player-controlled, then actor action 15 through
         * `C_ActorStateExpansion::RequestDeadAction` with the same flag.
         *
         * Run once for an observed death. C_Human and C_Player override this
         * slot; the player also takes save lock 6, released by its Revive.
         * A request's return is not confirmation that its action entered.
         */
        virtual void HandleDeath(bool freshDeath) = 0; // slot 212, +0x6A0

        virtual void unk_fn0213() = 0;
        virtual void unk_fn0214() = 0;
        virtual void unk_fn0215() = 0;
        virtual void unk_fn0216() = 0;
        virtual void unk_fn0217() = 0;
        virtual void unk_fn0218() = 0;
        virtual void unk_fn0219() = 0;
        virtual void unk_fn0220() = 0;
        virtual void unk_fn0221() = 0;

        /**
         * Forbids this body the run and sprint movement styles.
         *
         * `0x18285EF98`, slot **222** -- not 220, which is a plain
         * `return *(this + 0x268)` getter this declaration used to land on.
         * The body proves the slot: it stores `allowSprint` at `+0x409` and
         * `allowRun` at `+0x40A`, the pair named below. Index 269 resolving
         * to `UpdateMannequinTags` (0x1804AAE90) proves the numbering these
         * `unk_fn` names count in.
         *
         * It then clamps the live desired speed down a tier -- 3.0 to 2.0 or
         * 1.0 without sprint, 2.0 to 1.0 without run -- but returns early for
         * a player-controlled body (`IsPlayerControlled`, 0x1804A8CE0), so on
         * the local player only the two bytes are written and the style gate
         * does the rest.
         *
         * `C_ActorActionCarryItem::SetCarryState` (0x18070D3AC) is the game's
         * own use: it saves `allowRun`, clears it while a body is carried and
         * writes the saved value back. Do the same; a constant stomps it.
         *
         * Lua reaches this through `C_ScriptBindActor` slot 29 with the two
         * arguments the other way round.
         */
        virtual void SetMovementRestriction(bool allowRun, bool allowSprint) = 0;

        virtual void unk_fn0223() = 0;
        virtual void unk_fn0224() = 0;
        virtual void NotifyActorActionRequest(const void *request) = 0; // slot 225; request type name is not established
        virtual void unk_fn0226() = 0;
        virtual void unk_fn0227() = 0;
        virtual void unk_fn0228() = 0;
        virtual void unk_fn0229() = 0;
        virtual void unk_fn0230() = 0;
        virtual void unk_fn0231() = 0;
        virtual void unk_fn0232() = 0;
        virtual void unk_fn0233() = 0;
        virtual void unk_fn0234() = 0;
        virtual void unk_fn0235() = 0;
        virtual void unk_fn0236() = 0;
        virtual void unk_fn0237() = 0;
        /** Slot 238 (+0x770); corpse looting and butcher Exit call this with the user EntityId. */
        virtual void RequestItemExchange(SDK::EntityId userEntityId) = 0;
        virtual void unk_fn0239() = 0;
        virtual void unk_fn0240() = 0;
        virtual void unk_fn0241() = 0;
        virtual void unk_fn0242() = 0;
        virtual void unk_fn0243() = 0;
        virtual void unk_fn0244() = 0;
        virtual void unk_fn0245() = 0;
        virtual void unk_fn0246() = 0;
        /**
         * The engine's own model (re)setup, slot 247 (0x180AE55E0).
         *
         * What `Init`, `SetPutAway`'s wake branch and `Revive` run: the model
         * load (slot 248 when CVar `g_setActorModelFromLua` is set, slot 249
         * otherwise -- the default), then `IAnimatedCharacter` slot 42 so
         * Mannequin rebinds to the character that is now in slot 0.
         *
         * `model` null keeps the model the actor recorded from its script
         * Properties on first use; a path replaces both `fileModel` and
         * `clientFileModel` in that record, and slot 0 is reloaded only when
         * it differs from `GetCurrentModel()`. The old character is released
         * inline, so this must run where no animation job is reading it.
         */
        virtual void SetupActorModel(const char *model) = 0;

        /** Slot 248: calls the script's `SetActorModel(isClient)`; ignores any path. */
        virtual bool CallScriptSetActorModel() = 0;

        /** Slot 249: the native half of `SetupActorModel`, without the rebind. */
        virtual void SetActorModelNative(const char *model) = 0;

        virtual void unk_fn0250() = 0;
        virtual void unk_fn0251() = 0;
        virtual void unk_fn0252() = 0;
        virtual void unk_fn0253() = 0;
        virtual void unk_fn0254() = 0;
        virtual void unk_fn0255() = 0;
        virtual void unk_fn0256() = 0;
        virtual void unk_fn0257() = 0;
        virtual void unk_fn0258() = 0;
        virtual void unk_fn0259() = 0;
        virtual void unk_fn0260() = 0;
        virtual void unk_fn0261() = 0;
        virtual void unk_fn0262() = 0;
        virtual void unk_fn0263() = 0;
        virtual void unk_fn0264() = 0;
        virtual void unk_fn0265() = 0;
        virtual void unk_fn0266() = 0;
        virtual void unk_fn0267() = 0;
        virtual void unk_fn0268() = 0;
        virtual void unk_fn0269() = 0;
        virtual void unk_fn0270() = 0;
        virtual void unk_fn0271() = 0;
        virtual void unk_fn0272() = 0;
        virtual void unk_fn0273() = 0;
        virtual void unk_fn0274() = 0; // final C_Actor slot, +0x890

        // SDK convenience accessors over the proven IActor slot-90 endpoint.
        SDK::CAnimatedCharacter *GetAnimatedCharacterRuntime() {
            return static_cast<SDK::CAnimatedCharacter *>(GetAnimatedCharacter());
        }

        SDK::IAnimationGraphState *GetAnimationGraphStateRuntime() {
            auto *animatedCharacter = GetAnimatedCharacterRuntime();
            return animatedCharacter != nullptr ? animatedCharacter->GetAnimationGraphStateRuntime() : nullptr;
        }

        // SDK convenience path through IEntity::GetCharacter. Human, player,
        // NPC, and horse actors conventionally use entity character slot 0.
        SDK::CCharInstance *GetCharacterInstanceRuntime(int slot = 0) {
            auto *entity = GetEntity();
            return entity != nullptr ? static_cast<SDK::CCharInstance *>(entity->GetCharacter(slot)) : nullptr;
        }

        SDK::CSkeletonAnim *GetSkeletonAnimationRuntime(int slot = 0) {
            auto *character = GetCharacterInstanceRuntime(slot);
            return character != nullptr ? character->GetSkeletonAnim() : nullptr;
        }

        C_ActorMovementController *GetMovementControllerRuntime() const {
            return m_pMovementController;
        }

        C_ActorModel *GetActorModel() const {
            return m_pActorModel;
        }

        /**
         * The model file slot 0 was last loaded from, as `C_Actor::LoadModel`
         * (0x180AE5688) records it before calling `LoadCharacter`. Empty
         * before the first load, never null.
         */
        const char *GetCurrentModel() const {
            return m_currentModel.m_pData != nullptr ? m_currentModel.m_pData : "";
        }

        /** Whether this body's locomotion is the AI's to drive; see the member. */
        bool IsAIControlled() const { return m_isAIControlled; }

        /**
         * Whether the frame's yaw delta turns this body: false while a camera
         * limit or rotation lock is held, when the body turns to the body
         * direction the movement controller resolved instead. sub_180441AFC.
         */
        /**
         * C_Player's override of slot 272, UpdateCollisionClassMask
         * (0x18115C3AC), which C_Actor::PrePhysicsMovement calls every frame:
         * the player collision class, and a collider that meets rigid bodies.
         * C_NPCActor's (0x18044CA98) gives an NPC's class and the collider flag
         * that skips them under ac_disableLivingVsRigidCollisions. It reads
         * only what every actor has.
         */
        void UpdatePlayerCollisionClassMask(bool flag) {
            hook::this_call<void>(gPatterns.C_Player__UpdateCollisionClassMask, this, flag);
        }

        bool TakesYawDelta() const { return m_cameraLimitCount <= 0 && m_rotationLockCount <= 0; }

        /** Camera limits in force (+0x174); under one, the actor rotation keeps its base instead of rebuilding it from its angles. */
        std::int32_t GetCameraLimitCount() const { return m_cameraLimitCount; }

        /** Rotation locks in force (+0x178); read with the camera limits by C_Actor::HasNoCameraLimitOrRotationLock (0x180441AFC). */
        std::int32_t GetRotationLockCount() const { return m_rotationLockCount; }

        /** The body's rotation state (base and view); see C_ActorRotation. */
        C_ActorRotation *GetRotation() const { return m_pRotation; }

        /** Whether Mannequin carries ProceduralContextAim for this body; see m_hasProceduralAimContext. */
        bool HasProceduralAimContext() const { return m_hasProceduralAimContext; }

        /** The holder of the Mannequin action playing this body's locomotion; see C_AIAnimationState. */
        C_AIAnimationState *GetAIAnimationState() const {
            return m_pAIAnimationState;
        }

        /**
         * The game's own locomotion builder, `C_Actor::EnsureLocomotionAction`
         * (0x1804ABAE0), with the arguments its tag pass passes it
         * (`RefreshLocomotionAction`, 0x1804ABA34): keeps a locomotion action
         * whose status is not none, and otherwise builds one from this body's
         * movement params and queues it on `controller`.
         *
         * The tag pass only asks for an AI-controlled body, so a body whose
         * action controller was flushed is handed its locomotion back here.
         * False when the body has no animation state or no character yet.
         */
        bool EnsureLocomotionAction(IActionController *controller);

        /** Null only for a half-constructed actor. */
        C_RagdollManager *GetRagdollManager() const {
            return m_pRagdollManager;
        }

        /** What `HandleEvent` (0x180706190) hands every hit record to; built by the constructor. */
        C_HitDeathReactions *GetHitDeathReactions() const {
            return m_pHitDeathReactions;
        }

        /** Built by the constructor (0x1808E0928); Init dereferences it unchecked (0x180AE8A20). */
        C_ActorConditionController *GetConditionController() const {
            return m_pConditionController;
        }

        /** The concrete soul: the UI name and the WUID are its own members, not interface slots. */
        wh::rpgmodule::C_Soul *GetSoul() const {
            return m_pSoul;
        }

        /** Slot 195 returns a four-byte flags UDT through RDX; food spends one. */
        bool UseFood(C_Item *item, std::int32_t amount, std::int32_t context, bool checkFood) {
            std::uint32_t result = 128;
            hook::this_call<void>(gPatterns.C_Actor__UseItem, this, &result, item, amount, context, checkFood);
            return (result & 0x1FEU) == 0;
        }

        /**
         * This body's combat side, or null before the engine has built one.
         *
         * C_Player's constructor creates it from a pool and stores it here;
         * an actor that never fights may never get one, so callers check.
         */
        /**
         * What the engine knows about this body's entity class; may be null.
         *
         * Named apart from `IActor::GetActorClass`, which is the engine's own
         * virtual answering a small ordinal rather than this row.
         */
        S_ActorClass *GetActorClassInfo() const {
            return m_pActorClass;
        }

        /**
         * This body's combat side, built if it has none yet.
         *
         * The engine builds the combat actor lazily, so `GetCombatActor`
         * below answers null for every body that has never fought -- which is
         * every body this mod spawns. Anything that means to drive a fight
         * has to come through here instead.
         */
        wh::combatmodule::C_CombatActor *GetOrCreateCombatActor();

        /** What the body already has, without building one. Often null. */
        wh::combatmodule::C_CombatActor *GetCombatActor() const {
            return m_pCombatActor;
        }

        wh::rpgmodule::C_InventorySoul *GetInventorySoulRuntime() {
            return m_pSoul != nullptr ? m_pSoul->GetInventorySoul() : nullptr;
        }

        const wh::rpgmodule::C_InventorySoul *GetInventorySoulRuntime() const {
            return m_pSoul != nullptr ? m_pSoul->GetInventorySoul() : nullptr;
        }

        C_Inventory *GetInventoryRuntime() {
            auto *inventorySoul = GetInventorySoulRuntime();
            return inventorySoul != nullptr ? inventorySoul->GetInventory() : nullptr;
        }

        C_EquipmentManager *GetEquipmentManagerRuntime() {
            auto *inventorySoul = GetInventorySoulRuntime();
            return inventorySoul != nullptr ? inventorySoul->GetEquipmentManager() : nullptr;
        }

        /**
         * The movement-status flags the locomotion gate tests, or zero when
         * the actor carries no status object yet.
         */
        std::uint8_t GetMovementStatusFlags() const {
            if (m_pMovementStatus == nullptr) {
                return 0;
            }
            return *(reinterpret_cast<const std::uint8_t *>(m_pMovementStatus) + 0x50);
        }

        std::uint32_t GetLocomotionGateState() const {
            return m_locomotionGateState;
        }

        /** The status object itself, for identifying what it is. */
        const void *GetMovementStatusObject() const {
            return m_pMovementStatus;
        }

        /**
         * Its vtable pointer, used to identify the object's runtime type.
         */
        const void *GetMovementStatusVTable() const {
            if (m_pMovementStatus == nullptr) {
                return nullptr;
            }
            return *reinterpret_cast<void *const *>(m_pMovementStatus);
        }

        void SetMovementState(const Vec3 &velocity, float speed) {
            // Published through the request, because the frame's commit copies
            // that over m_movementVelocity and would otherwise erase it.
            m_moveRequestType     = SDK::eCMT_Normal;
            m_moveRequestVelocity = velocity;

            // Set as well so a frame in which no commit runs still reads the
            // right thing. Both say the same, so the commit overwriting one
            // with the other changes nothing.
            m_movementVelocity = velocity;
            m_movementSpeed    = speed;
        }

        const Vec3 &GetMovementVelocity() const {
            return m_movementVelocity;
        }

        /**
         * Hands the inertia this body was physicalized with to its animated
         * character, exactly as the tail of `Physicalize` does: 0x1808ABF14
         * copies the character's params (slot 59), writes inertia,
         * acceleration inertia and impulse recovery time, sets them back
         * (slot 60) and reloads its inertia cache from the living entity
         * (`ResetInertiaCache`, slot 34). `UpdatePhysicsInertia` then gives
         * the living entity these values whenever nothing zeroes them.
         *
         * `CAnimatedCharacter::ResetState` zeroes those params (`ResetVars`
         * 0x18070470C), and `Revive` calls it (0x1809CF503) without the
         * params coming back, which leaves the body with no inertia.
         */
        void RestoreAnimatedCharacterInertia() {
            hook::call<void>(gPatterns.C_Actor__ApplyInertiaToAnimatedCharacter, m_pClassState, m_timeImpulseRecover, m_inertia, m_inertiaAccel);
        }

        /** What SetMovementRestriction last granted this body. */
        bool IsRunAllowed() const {
            return m_params.m_allowRun != 0;
        }

        bool IsSprintAllowed() const {
            return m_params.m_allowSprint != 0;
        }

        float GetMovementSpeed() const {
            return m_movementSpeed;
        }

        /**
         * Slots of m_pAnimationTagIds proven against the engine's own passes.
         *
         * The tag pass drives MoveSpeed and MoveDir through slots 58 and 59,
         * picking between the forward and backward tags in slots 25 and 26.
         * The actor's tag-to-model sync reads a third group through slot 62
         * alongside those two, which is the stance the body is standing,
         * sneaking, sitting or lying in. Slot 28 is the `player` tag, which
         * the pass sets from `IsPlayerControlled` every tick (0x1804AB2F9) and
         * which a player's own cart seats are animated under.
         */
        enum EAnimationTagId : std::uint32_t {
            ANIM_TAG_MOVE_DIR_FORWARD  = 25,
            ANIM_TAG_MOVE_DIR_BACKWARD = 26,
            ANIM_TAG_PLAYER            = 28,
            ANIM_TAG_GROUP_MOVE_SPEED  = 58,
            ANIM_TAG_GROUP_MOVE_DIR    = 59,
            ANIM_TAG_GROUP_STANCE      = 62,
        };

        /** One resolved id, or kInvalidId when the table is absent. */
        /** The body's move style for a movement tier (0 walk .. 3 dash), or -1; `GetMoveStyleIdForTier`, 0x1808324C0. */
        std::int32_t GetMoveStyleIdForTier(std::uint32_t tier) const {
            return tier < 4 ? m_moveStyleIds[tier] : -1;
        }

        std::uint32_t GetAnimationTagId(EAnimationTagId slot) const {
            if (m_pAnimationTagIds == nullptr) {
                return CTagDefinition::kInvalidId;
            }
            return m_pAnimationTagIds[slot];
        }

        /** The tag state every fragment for this body is selected through. */
        CTagState *GetTagState() {
            auto *animatedCharacter = GetAnimatedCharacterRuntime();
            if (animatedCharacter == nullptr) {
                return nullptr;
            }
            auto *controller = animatedCharacter->GetActionController();
            if (controller == nullptr) {
                return nullptr;
            }
            auto *context = controller->GetContext();
            return context != nullptr ? &context->GetTagState() : nullptr;
        }

        const Vec3 &GetLastNavMeshPosition() const {
            return m_lastNavMeshPosition;
        }

        C_ActorEquipmentHandler *GetEquipmentHandler() const {
            return m_pEquipmentHandler;
        }

        C_ActorClothing *GetClothing() const {
            return m_pClothing;
        }

        /** False while the body is refused a clothing object outright. */
        bool CanWearClothing() const {
            return m_canWearClothing;
        }

        std::uint8_t GetStateFlags() const {
            return m_state;
        }

        /** Whether the body is away *now*, not whether one has been asked for. */
        bool IsPutAway() const {
            return (m_state & ACTOR_STATE_PUT_AWAY_APPLIED) != 0;
        }

        /** Whether the engine last asked for the body to be away, applied or not. */
        bool IsPutAwayWanted() const {
            return (m_state & ACTOR_STATE_PUT_AWAY_WANTED) != 0;
        }

        /**
         * Puts the body away, or wakes it up.
         *
         * Every actor that holds no network channel is spawned put away:
         * C_Actor::Init asks whether it should be, and for anything that is
         * not the local player the answer is yes, so init ends by calling
         * this with true. Being put away is not only "may wear nothing" --
         * it also *disables the actor's update slot*, and an extension whose
         * slot carries no enable never has its Update called. That is what
         * leaves a spawned body with no locomotion: its movement controller
         * never runs, so nothing queues a Mannequin fragment and the skeleton
         * keeps its bind pose.
         *
         * Waking up is a whole sequence -- re-running the model setup,
         * re-binding the animated character, restoring physics and rendering,
         * rebuilding clothing, then re-enabling the update slot. Clearing the
         * two state bits by hand performs none of it, so call this instead.
         *
         * `C_Actor::Init` calls this on every actor it builds, gated on
         * `!IsPlayerControlled && !(flags & 0x1000000) && !(flagsExtended &
         * 0x40)` -- and `IsPlayerControlled` is only the game object's
         * channel id, which the engine writes after the extension is
         * initialized, so it is false there for everyone. Hence a spawned
         * body is always asleep and needs waking. On a half-built actor the
         * transition dereferences a member that does not exist yet, which is
         * what killed the horse.
         */
        void SetPutAway(bool putAway);

        /**
         * Asks for the body to be put away or woken, and returns.
         *
         * The call to make from a mod. `SetPutAway` is the raw transition and
         * writes only the applied bit; this writes the wanted one and queues
         * the actor, leaving the engine to perform it from the manager that
         * holds the process-wide lock. Doing the transition directly killed
         * the process on a horse twice, where this does not. `immediate`
         * performs it inline, which the game only does in particular game
         * states; leave it false.
         */
        void RequestPutAway(bool putAway, bool immediate = false);

        /**
         * This actor's C_NPC, through `C_NPCManager::GetNPCByEntityId` (its
         * vtable slot 4), or null for a body the manager never registered --
         * which a bIsDummy puppet is. The torch branch of the hand helper's
         * AddItemToHand dereferences it without checking; see
         * player_hand_use_hooks.cpp.
         */
        void *GetAnimationProxy();

        /** The queue every action this actor performs is posted to. */
        C_ActionActor *GetActionActor() const {
            return m_pActionActor;
        }

        /**
         * This actor's half of carrying a body, built on first use -- the
         * engine's own accessor, which every carry script bind goes through.
         */
        I_ActorCarryCorpse *GetCarryCorpse();

        /** The same, without building one: null for a body that never carried or was carried. */
        I_ActorCarryCorpse *FindCarryCorpse() const {
            return m_pCarryCorpse;
        }

        /**
         * This actor as its own aspect-profile manager.
         *
         * A secondary base, at +0x48: `C_Actor` implements
         * `IGameObjectProfileManager` there, and its `SetAspectProfile`
         * (0x18087ACA0) is what actually applies a profile -- reading the
         * ragdoll manager at interface+328 (= actor+0x190) for `Physics` and
         * writing interface+2408 (= actor+0x9B0) for `Update`, which is what
         * proves the offset.
         *
         * Reach for this rather than `IGameObject::SetAspectProfile` whenever
         * the aspect is `Update`: that one is not a network aspect, and the
         * game object's own setter walks off the end of its aspect table and
         * into an unreachable stub. See i_game_object_profile_manager.h.
         */
        SDK::IGameObjectProfileManager *GetProfileManager() {
            return reinterpret_cast<SDK::IGameObjectProfileManager *>(reinterpret_cast<std::byte *>(this) + 0x48);
        }

        /**
         * Whether the engine has finished building this body's actor model.
         *
         * The entity and the actor exist before the model at +0x990 does, and
         * natives reach through it without asking -- `IsItemDrawn` loads a
         * vtable out of its hand store at +0x258, the mount check queries its
         * state set at +0x370. Asked one frame too early, the first of those
         * crashed a session when a second player joined.
         */
        bool IsSettled() const {
            return m_pActorModel != nullptr;
        }

        /**
         * Whether the ragdoll manager would refuse to physicalize this body.
         *
         * The exact test `C_RagdollManager::SetPhysicsProfile` makes before
         * calling `Physicalize` for profile 1: `(m_state & 0x40) == 0`.
         */
        bool AcceptsPhysicalization() const {
            return (m_state & 0x40U) == 0;
        }

        /**
         * Whether a jump action is running -- actor state 1, `jump` in the
         * engine's own state table. Distinct from state 29, `inAir`, which
         * any airborne body carries: the jump action holds this one from
         * start until it reports finished, and clears `inAir` on the way out.
         */
        bool IsJumping() const;

        /**
         * Vaults over whatever ledge this body is standing at, if any: the
         * engine's whole vault, and the same entry the AI reaches through
         * actor state event 120. It runs the ledge query at this actor's own
         * pose, picks the rung and the sprint variant from what it finds, and
         * posts actor action type 0x1C.
         *
         * Re-queried rather than replayed, so it reproduces on a body other
         * than the one that vaulted first without any ledge geometry crossing
         * the wire -- a ledge id is an index into a runtime array and means a
         * different ledge on another machine. False is the query finding
         * nothing to vault.
         *
         * `input` is the request the vault was asked with. A player-controlled
         * actor -- the local player and every native remote copy -- is refused
         * without it, exactly as `C_PlayerInput::PreUpdate` would be with
         * neither key down; see S_LedgeGrabInput.
         */
        bool TryLedgeGrab(const S_LedgeGrabInput &input);

        /**
         * This body's riding subsystem, or null.
         *
         * The facade at slots 203-205 resolves this itself. A remote player's
         * mount calls `StartRide` here directly: `C_Player`'s `MountHorse`
         * adds a carried-weight refusal its owner has already passed.
         *
         * `StartRide`'s **first line** is an unconditional `CanMount`, which
         * wants one of the state flags 0, 7, 19 or 31 on the rider's own actor
         * model -- a body spawned with no brain never carries one, which is
         * why `RideHorse` exists. There is no force flag that skips it;
         * `MountHorse`'s bool is passed *through* to the action, not around
         * the check.
         */
        C_RiderExpansion *GetRiderExpansion() const;

        /**
         * Puts this body in the saddle of `horseEntityId`.
         *
         * Queues the game's own `C_ActorActionRider` directly rather than
         * through the `MountHorse` facade, which refuses a puppet outright --
         * see `GetRiderExpansion`. This mirrors `C_RiderExpansion::StartRide`
         * (0x180AD468C) exactly apart from that check: same parameter word,
         * same create-addref-queue-release. The occupancy check the facade
         * would have brought is done here instead, against the animal's own
         * rider-sync.
         *
         * `instant` is StartRide's force: the body is linked into the saddle
         * with no get-on played, the way Lua's `ForceMount` does it. Without
         * it the get-on runs, which is only sound on a horse standing still --
         * see `C_ActorActionRider::MakeParams`.
         *
         * False when it was refused; asking again next frame costs nothing.
         */
        bool RideHorse(EntityId horseEntityId, bool instant);

        /**
         * Begins getting this body out of the saddle, animation and all.
         *
         * A one-line call on `DismountHorse`, the game's own facade.
         *
         * Not instant: it moves the ride into its dismounting state and the
         * action plays the get-off. It does nothing while the mount is still
         * playing, and nothing when neither side of the horse has clear
         * ground, so ask again -- the animal's own rider-sync says whether it
         * took, and the running action does not.
         *
         * `immediate` detaches inline and skips the get-off. Only right when
         * the horse is about to stop existing, since the animated path
         * measures the ground beside an animal that has to still be there.
         */
        bool StopRiding(bool immediate = false);

        /**
         * Begins this body's get-off through its own rider expansion, the
         * animated `StopRide(force=false, reason=true, side=0, place=true)`
         * the player control itself ends a ride with.
         *
         * `StopRiding` reaches slot 205, and `C_Player` overrides it
         * (0x182AA1FFC) with a hand-off to its rider control (0x1819E13C0):
         * unforced, that only raises the control's dismount request at +0x31
         * for the rider's own input to act on once its horse has slowed, and
         * answers true having started nothing. A player body whose horse is
         * not steered by its own input never gets off that way. Same answer
         * as `StopRiding`: read the ride state to learn whether it took.
         *
         * `side` is the flank to get off on, as `C_RiderExpansion::StopRide`
         * takes it: 1 or 2, 0 for the engine's default.
         */
        bool BeginGetOff(std::uint8_t side);

        /**
         * What this body's ride is doing, `RIDE_STATE_NONE` when it has none.
         *
         * The only honest report of whether a get-off took, because
         * `StopRiding` cannot be one: it answers true when the request moved
         * the ride, when both flanks were blocked and nothing happened at
         * all, and when there was no ride to end. Reached through the running
         * rider action rather than the expansion, which keeps no state about
         * the ride it started.
         */
        C_HorseRideContext::RideState GetRideState() const;

        /** The ride this body is running, or null when it has none. */
        C_HorseRideContext *GetRideContext() const;

        /**
         * Whether this body may pull the rider of `victimEntityId` down, and
         * the request that starts it. Both are the game's own
         * `CanHorsePullDown` and `RequestHorsePullDown`, which take the victim
         * rather than the horse it is on.
         */
        bool CanPullDownRider(EntityId victimEntityId);
        bool RequestPullDownRider(EntityId victimEntityId);

        /**
         * Recomputes whether this body may wear anything.
         *
         * The engine decides it once, inside AcquireSoul, from whether the
         * entity's slot 0 holds a character and whether its
         * `Properties.esClothingConfig` names a registered config. A body
         * whose model is loaded afterwards -- NPC_NAI stubs out the script
         * that would normally load it at spawn -- is judged before it has one
         * and stays marked as unable to wear anything, and nothing re-asks.
         */
        void RefreshClothingSupport();

        /**
         * Gives the body the equipment listener that turns equipping into
         * visible clothing, if it has none.
         *
         * The engine gives one only to the player. Building a second would
         * strand the first and leave the inventory holding both, and the
         * construction path walks to the soul's inventory without checking,
         * so a body that has not acquired a soul yet is left alone.
         */
        void EnsureEquipmentHandler();

        /**
         * Builds the clothing object if the body may have one, then has it
         * rebuild what the body is wearing.
         *
         * Equipping records what a body owns; this is what redraws it. Does
         * nothing while the body is put away, the getter behind it refusing
         * while the body is put away and answering null rather than failing.
         */
        void RebuildClothing();

      private:
        // Starts after IGameObjectExtension's proven 0x40-byte data
        // prefix, so this block still covers the IGameObjectView and
        // IGameObjectProfileManager secondary vptrs at +0x40 and +0x48.
        const void *m_pGameObjectViewVTable;    // 0x40, IGameObjectView
        const void *m_pProfileManagerVTable;    // 0x48, IGameObjectProfileManager
        std::uint64_t m_state0050;              // 0x50
        // The block 0x1809CEFA0 builds at +0x58: a signal, the movement
        // status object, then a list whose 0x10-byte head node it allocates.
        wh::shared::C_Signal m_movementStatusSignal; // 0x58

        /**
         * Source of the movement-status flags the locomotion gate tests.
         *
         * CAnimActionAIMovement::SelectState reads bits 0 and 1 of this
         * object's byte at +0x50; with both clear (and the rest of the gate
         * clear too) it returns a forced non-locomotion state regardless of
         * how fast the actor says it is moving. The object's own type is not
         * identified, so it stays opaque -- only the offset is proven.
         */
        void *m_pMovementStatus; // 0x68

        void *m_movementStatusList[4];          // 0x70, zeroed; the head node hangs off the first
        std::uint64_t m_state0090;              // 0x90
        std::uint64_t m_state0098;              // 0x98

        // Twelve model signals, constructed in pairs of the same kind, and the
        // three-bit property the constructor builds after them.
        wh::shared::C_Signal m_signals[12]; // 0xA0
        std::uint8_t m_bitset0160[0x3];      // 0x160, built by 0x182864BC4 with three bits
        std::uint8_t m_state0163;            // 0x163
        float m_health;    // 0x164
        float m_maxHealth; // 0x168
        float m_state016C;                      // 0x16C, 9.8 by default
        std::uint32_t m_state0170;              // 0x170
        // Camera limit (slot 135) and rotation lock counts; while either is
        // held, UpdateAngles 0x180441928 drops the yaw delta.
        std::int32_t m_cameraLimitCount; // 0x174
        std::int32_t m_rotationLockCount; // 0x178
        float m_state017C;                      // 0x17C, 1 by default
        C_ActorMovementController *m_pMovementController; // 0x180

        // Built on first use by GetCarryCorpse (0x1803B6F84), its only
        // writer; null until the body has carried something or been carried.
        I_ActorCarryCorpse *m_pCarryCorpse; // 0x188

        // Built with the actor, so it outlives any one ragdoll. Proven from
        // both sides: C_ActorActiontRagdoll::Enter (0x180E79C68) calls
        // BeginRagdollAction on this offset, and the manager reads that same
        // actor back out of C_RagdollManager::m_actor at +0x88.
        C_RagdollManager *m_pRagdollManager; // 0x190

        std::uint64_t m_state0198[3];           // 0x198, zeroed
        // An owned 0x90-byte object (0x18119091C): two signals, this actor,
        // two 0x24-byte entries and a vector.
        void *m_pOwnedState01B0;                // 0x1B0
        std::uint64_t m_state01B8[3];           // 0x1B8, zeroed

        // The physics-ignore block the constructor builds with 0x1811FF97C:
        // a back-pointer, the gate word, two flags and the two ignore sets.
        C_Actor *m_pPhysicsIgnoreOwner; // 0x1D0

        /** Second gate input; the gate requires it to be zero to force idle. */
        std::uint32_t m_locomotionGateState; // 0x1D8
        std::byte m_padding01DC[0x4];
        std::uint16_t m_physicsIgnoreFlags; // 0x1E0
        std::uint8_t m_physicsIgnoreState;  // 0x1E2
        std::byte m_padding01E3[0x5];
        S_ActorPhysicsIgnore m_entityPhysicsIgnore;     // 0x1E8, "Actor_EntityPhysicsIgnore"
        S_ActorPhysicsIgnore m_additionalPhysicsIgnore; // 0x208, "Actor_AdditionalPhysicsIgnore"

        // Compared case-insensitively against the recorded model by
        // C_Actor::LoadModel, and assigned just before it loads slot 0.
        SDK::CryStringT<char> m_currentModel; // 0x228

        SDK::CryStringT<char> m_string0230; // 0x230, a second string the constructor builds beside the model path
        C_ActorRotation *m_pRotation; // 0x238
        C_HitDeathReactions *m_pHitDeathReactions; // 0x240
        void *m_state0248; // 0x248, cleared by the constructor, read by PrePhysicsMovement
        void *m_pOwnedState0250; // 0x250, owned 0x20-byte state holding a back-pointer to this actor
        C_ActorConditionController *m_pConditionController; // 0x258
        C_ActorDirtHelper *m_pDirtHelper; // 0x260, null until the body is dirtied
        void *m_pClassState; // 0x268, owned polymorphic object passed in by the subclass constructor, deleted in Release
        void *m_state0270; // 0x270
        // The combat component, which owns the pull-down contract.
        wh::combatmodule::C_CombatActor *m_pCombatActor; // 0x278
        // Owned: a single-qword handle whose deleter is virtual, built by the
        // actor's own constructor against itself.
        C_ActionActor *m_pActionActor; // 0x280
        S_ActorParams m_params; // 0x288
        std::byte m_state0410[0x18];            // 0x410, zeroed by 0x1809CF028
        std::uint64_t m_state0428;              // 0x428
        std::uint32_t m_state0430;              // 0x430
        std::byte m_state0434[0x94];            // 0x434, zeroed
        float m_state04C8;                      // 0x4C8, 1 by default
        float m_state04CC;                      // 0x4CC
        // The living entity's inertia, acceleration inertia and impulse
        // recovery time as Physicalize (0x1808AB488) took them from the
        // physics template (a field the template leaves unset keeps its value,
        // the recovery time drops to 0), and handed to the animated character.
        float m_inertia;                        // 0x4D0, 11 by default
        float m_inertiaAccel;                   // 0x4D4, 11 by default
        float m_timeImpulseRecover;             // 0x4D8
        std::byte m_state04DC[0xC];             // 0x4DC

        /**
         * What the engine believes this body is doing, and the only thing that
         * decides whether it stands or walks.
         *
         * CAnimActionAIMovement -- the Mannequin action that owns locomotion --
         * leaves MotionIdle for MotionMovement exactly when this vector's
         * squared length passes 0.01, and C_Actor's per-frame tag pass reads it
         * again to choose the MoveDir tag from the angle between it and the
         * body's facing.
         *
         * Nothing in the binary stores to it directly: the constructor zeroes
         * it and the movement controller fills it through a helper, on a path
         * an actor without an AI object never runs. A puppet driven from the
         * network therefore owns this field outright.
         */
        Vec3 m_movementVelocity; // 0x4E8

        /** Rotational counterpart of the above; identity while not turning. */
        Quat m_movementRotation; // 0x4F4

        /**
         * Last frame's pseudo-speed, and not a speed at all.
         *
         * `C_Actor::PostUpdate` copies GetDesiredSpeed's answer here every
         * frame unconditionally, and that answer is `CMovementRequest`'s
         * pseudo-speed: an `E_MovementSpeed` ordinal -- idle 0, walk 1, run 2,
         * sprint 3, dash 4 -- written by the movement controller from the
         * request, floored at walk, and never touched by physics. The tier the
         * MoveSpeed tag names is `round(it) - 1`.
         *
         * So the tag pass falling back to this when GetDesiredSpeed answers
         * zero and the velocity above is non-zero is not reaching for a
         * measured speed. It is reaching for the same intent one frame stale,
         * to stop the MoveSpeed tag flickering to none on a frame where the AI
         * animation state was cleared out from under a body still moving.
         * Anything under 0.1 is idle, and leaves the group empty.
         *
         * Nothing here describes how fast the body is going. The engine has no
         * such field on an actor; the physics status does.
         */
        float m_movementSpeed; // 0x504

        /**
         * The move request the actor accumulates over a frame, and the source
         * the fields above are published from.
         *
         * Once a frame the animated character sends the actor game-object
         * event 0x65, and the handler copies this record's velocity into
         * m_movementVelocity and its rotation into m_movementRotation, hands
         * the record to the animated character, then resets it -- type back to
         * Normal, everything else to default. That copy is what erases anything
         * written straight into m_movementVelocity, so this is the field a
         * replicated body has to publish through instead.
         *
         * A zero type means "no movement this frame" and makes the animated
         * character discard the record, so it is set alongside the velocity.
         */
        SDK::ECharacterMoveType m_moveRequestType; // 0x508
        Vec3 m_moveRequestVelocity;     // 0x50C
        Quat m_moveRequestRotation;     // 0x518

        // The rest of the SCharacterMoveRequest the three fields above open.
        SPredictedCharacterStates m_moveRequestPrediction; // 0x528
        bool m_moveRequestAllowStrafe;          // 0x548
        std::byte m_padding0549[0x3];
        float m_moveRequestProceduralLeaning;   // 0x54C
        bool m_moveRequestJumping;              // 0x550
        std::byte m_padding0551[0x7];
        std::byte m_state0558[0xC];             // 0x558
        Vec3 m_vector0564;                      // 0x564, zeroed

        /**
         * What the locomotion action is built from; `RefreshLocomotionAction`
         * hands its address to `EnsureLocomotionAction`. Its layout is not
         * needed, only where it sits.
         */
        std::byte m_locomotionParams[0xCC]; // 0x570

        /**
         * Whether this body's locomotion is the AI's to drive. `C_Actor::Init`
         * (0x180AE8885) sets it from the entity script table's `IsAIControlled`.
         * The tag pass only refreshes the locomotion action when it is set
         * (0x1804AB2CA), and both `C_ActorMovementController::Update` (0x1804BABAC)
         * and `C_Actor::PostUpdate` (0x180644B3A) branch on it.
         */
        bool m_isAIControlled; // 0x63C
        std::byte m_padding063D[0x3];
        // Whether the action controller has the ProceduralContextAim and
        // ProceduralContextLook contexts, as SetupProceduralAnimationContexts
        // (0x180684928) found them; ResetMannequinActionController (0x180AA18E8)
        // clears the first. With it set, UpdateAimPose (0x180E4A570) writes the
        // aim target into that context instead of the skeleton's aim blender.
        bool m_hasProceduralAimContext;  // 0x640
        bool m_hasProceduralLookContext; // 0x641
        std::byte m_padding0642[0x6];
        wh::shared::C_Signal m_signal0648;      // 0x648
        wh::shared::C_Signal m_signal0658;      // 0x658
        SDK::wh::rpgmodule::C_Soul *m_pSoul; // 0x668
        SDK::CryStringT<char> m_string0670;     // 0x670, built by 0x1809CF050
        C_ActorEquipmentHandler *m_pEquipmentHandler; // 0x678
        // The key the clothing object is built from, and the object itself.
        // Its getter builds one only when m_canWearClothing is set and the
        // put-away bit of m_state is clear, and the put-away transition
        // destroys it again.
        I_ClothingAttachmentOwner *m_pClothingKey; // 0x680, a C_ClothingAttachmentOwnerActor bound to this actor
        C_ActorClothing *m_pClothing; // 0x688
        C_IdleState m_idleState; // 0x690

        /**
         * The holder of the Mannequin action that plays this body's locomotion,
         * which `C_Actor::EnsureLocomotionAction` fills lazily.
         * `GetAIAnimationStateSpeed` (slot 134) answers zero while it is null,
         * which empties the MoveSpeed tag group.
         */
        C_AIAnimationState *m_pAIAnimationState; // 0x738
        std::uint64_t m_state0740[3];           // 0x740, zeroed

        /**
         * What the engine remembers about this body's entity class, resolved
         * once in C_Actor::Init from the class-name hash.
         *
         * Null for a class the actor-class database skipped -- one with no
         * ActionController, animation database or fileModel.
         */
        S_ActorClass *m_pActorClass; // 0x758

        /**
         * The controller definition's tag and group ids, resolved once per
         * actor and indexed by fixed slot.
         *
         * C_Actor::Init fails outright when this comes back null, and the
         * per-frame tag pass drives every tag through it: slot 58 is the
         * MoveSpeed group, slot 59 the MoveDir group, and slots 25 and 26 the
         * forward and backward tags it picks between from the angle of travel.
         */
        const std::uint32_t *m_pAnimationTagIds; // 0x760

        std::int32_t m_state0768;               // 0x768, -1 by default
        std::uint32_t m_state076C;              // 0x76C
        std::uint32_t m_state0770;              // 0x770
        std::uint32_t m_state0774;              // 0x774
        std::uint64_t m_state0778;              // 0x778
        Vec3 m_vector0780;                      // 0x780, zeroed
        std::uint32_t m_state078C;              // 0x78C
        std::uint64_t m_state0790;              // 0x790
        std::uint32_t m_state0798;              // 0x798
        std::uint32_t m_padding079C;
        // The 0x1E0-byte movement block 0x1809CEA50 builds against this actor.
        std::byte m_movementBlock[0x188];       // 0x7A0

        /**
         * The body's move style per movement tier -- 0 walk, 1 run, 2 sprint,
         * 3 dash -- or -1 for a tier it has none of. The table
         * `GetMoveStyleIdForTier` (0x1808324C0) reads, at +0x188 of the
         * movement block at +0x7A0.
         */
        std::int32_t m_moveStyleIds[4]; // 0x928
        std::byte m_movementBlockTail[0x48];    // 0x938, the rest of the movement block
        Vec3 m_lastNavMeshPosition; // 0x980
        std::uint32_t m_padding098C;
        C_ActorModel *m_pActorModel; // 0x990
        Vec3 m_vector0998;                      // 0x998, zeroed
        std::uint32_t m_padding09A4;
        std::uint64_t m_state09A8;              // 0x9A8
        std::uint8_t m_state09B0; // 0x9B0
        bool m_canWearClothing;   // 0x9B1
        std::uint8_t m_state;     // 0x9B2
        std::uint8_t m_state09B3; // 0x9B3
        bool m_state09B4;                       // 0x9B4
        std::byte m_padding09B5[0x3];
        std::uint64_t m_state09B8;              // 0x9B8

        // Never called. The offsets are what make the members above usable, so
        // they are pinned here, where the type is complete and its own members
        // are in scope.
        static void AssertLayout() {
            static_assert(offsetof(C_Actor, m_health) == 0x164);
            static_assert(offsetof(C_Actor, m_maxHealth) == 0x168);
            static_assert(offsetof(C_Actor, m_params) == 0x288, "C_Actor::m_params offset mismatch");
            static_assert(offsetof(C_Actor, m_movementStatusSignal) == 0x58, "C_Actor::m_movementStatusSignal offset mismatch");
            static_assert(offsetof(C_Actor, m_pOwnedState01B0) == 0x1B0, "C_Actor::m_pOwnedState01B0 offset mismatch");
            static_assert(offsetof(C_Actor, m_moveRequestPrediction) == 0x528, "C_Actor::m_moveRequestPrediction offset mismatch");
            static_assert(offsetof(C_Actor, m_vector0564) == 0x564, "C_Actor::m_vector0564 offset mismatch");
            static_assert(offsetof(C_Actor, m_signal0648) == 0x648, "C_Actor::m_signal0648 offset mismatch");
            static_assert(offsetof(C_Actor, m_string0670) == 0x670, "C_Actor::m_string0670 offset mismatch");
            static_assert(offsetof(C_Actor, m_movementBlock) == 0x7A0, "C_Actor::m_movementBlock offset mismatch");
            static_assert(offsetof(C_Actor, m_state09B8) == 0x9B8, "C_Actor::m_state09B8 offset mismatch");
            static_assert(offsetof(C_Actor, m_signals) == 0xA0, "C_Actor::m_signals offset mismatch");
            static_assert(offsetof(C_Actor, m_pPhysicsIgnoreOwner) == 0x1D0, "C_Actor::m_pPhysicsIgnoreOwner offset mismatch");
            static_assert(offsetof(C_Actor, m_entityPhysicsIgnore) == 0x1E8, "C_Actor::m_entityPhysicsIgnore offset mismatch");
            static_assert(offsetof(C_Actor, m_additionalPhysicsIgnore) == 0x208, "C_Actor::m_additionalPhysicsIgnore offset mismatch");
            static_assert(offsetof(C_Actor, m_pMovementStatus) == 0x68, "C_Actor::m_pMovementStatus offset mismatch");
            static_assert(offsetof(C_Actor, m_pCarryCorpse) == 0x188, "C_Actor::m_pCarryCorpse offset mismatch");
            static_assert(offsetof(C_Actor, m_pRagdollManager) == 0x190, "C_Actor::m_pRagdollManager offset mismatch");
            static_assert(offsetof(C_Actor, m_locomotionGateState) == 0x1D8, "C_Actor::m_locomotionGateState offset mismatch");
            static_assert(offsetof(C_Actor, m_currentModel) == 0x228, "C_Actor::m_currentModel offset mismatch");
            static_assert(offsetof(C_Actor, m_pCombatActor) == 0x278, "C_Actor::m_pCombatActor offset mismatch");
            static_assert(offsetof(C_Actor, m_isAIControlled) == 0x63C, "C_Actor::m_isAIControlled offset mismatch");
            static_assert(offsetof(C_Actor, m_cameraLimitCount) == 0x174, "C_Actor::m_cameraLimitCount offset mismatch");
            static_assert(offsetof(C_Actor, m_pRotation) == 0x238, "C_Actor::m_pRotation offset mismatch");
            static_assert(offsetof(C_Actor, m_hasProceduralAimContext) == 0x640, "C_Actor::m_hasProceduralAimContext offset mismatch");
            static_assert(offsetof(C_Actor, m_locomotionParams) == 0x570, "C_Actor::m_locomotionParams offset mismatch");
            static_assert(offsetof(C_Actor, m_pAIAnimationState) == 0x738, "C_Actor::m_pAIAnimationState offset mismatch");
            static_assert(offsetof(C_Actor, m_pActorClass) == 0x758, "C_Actor::m_pActorClass offset mismatch");
            static_assert(offsetof(C_Actor, m_pActionActor) == 0x280, "C_Actor::m_pActionActor offset mismatch");
            static_assert(offsetof(C_Actor, m_movementVelocity) == 0x4E8, "C_Actor::m_movementVelocity offset mismatch");
            static_assert(offsetof(C_Actor, m_movementRotation) == 0x4F4, "C_Actor::m_movementRotation offset mismatch");
            static_assert(offsetof(C_Actor, m_movementSpeed) == 0x504, "C_Actor::m_movementSpeed offset mismatch");
            static_assert(offsetof(C_Actor, m_moveRequestType) == 0x508, "C_Actor::m_moveRequestType offset mismatch");
            static_assert(offsetof(C_Actor, m_moveRequestVelocity) == 0x50C, "C_Actor::m_moveRequestVelocity offset mismatch");
            static_assert(offsetof(C_Actor, m_moveRequestRotation) == 0x518, "C_Actor::m_moveRequestRotation offset mismatch");
            static_assert(offsetof(C_Actor, m_pSoul) == 0x668, "C_Actor::m_pSoul offset mismatch");
            static_assert(offsetof(C_Actor, m_pEquipmentHandler) == 0x678, "C_Actor::m_pEquipmentHandler offset mismatch");
            static_assert(offsetof(C_Actor, m_pClothingKey) == 0x680, "C_Actor::m_pClothingKey offset mismatch");
            static_assert(offsetof(C_Actor, m_string0230) == 0x230, "C_Actor::m_string0230 offset mismatch");
            static_assert(offsetof(C_Actor, m_pHitDeathReactions) == 0x240, "C_Actor::m_pHitDeathReactions offset mismatch");
            static_assert(offsetof(C_Actor, m_pConditionController) == 0x258, "C_Actor::m_pConditionController offset mismatch");
            static_assert(offsetof(C_Actor, m_pDirtHelper) == 0x260, "C_Actor::m_pDirtHelper offset mismatch");
            static_assert(offsetof(C_Actor, m_pClassState) == 0x268, "C_Actor::m_pClassState offset mismatch");
            static_assert(offsetof(C_Actor, m_idleState) == 0x690, "C_Actor::m_idleState offset mismatch");
            static_assert(offsetof(C_Actor, m_pClothing) == 0x688, "C_Actor::m_pClothing offset mismatch");
            static_assert(offsetof(C_Actor, m_pAnimationTagIds) == 0x760, "C_Actor::m_pAnimationTagIds offset mismatch");
            static_assert(offsetof(C_Actor, m_moveStyleIds) == 0x928, "C_Actor::m_moveStyleIds offset mismatch");
            static_assert(offsetof(C_Actor, m_lastNavMeshPosition) == 0x980, "C_Actor::m_lastNavMeshPosition offset mismatch");
            static_assert(offsetof(C_Actor, m_pActorModel) == 0x990, "C_Actor::m_pActorModel offset mismatch");
            static_assert(offsetof(C_Actor, m_canWearClothing) == 0x9B1, "C_Actor::m_canWearClothing offset mismatch");
            static_assert(offsetof(C_Actor, m_state) == 0x9B2, "C_Actor::m_state offset mismatch");
        }
    };

    static_assert(sizeof(C_Actor) == 0x9C0, "C_Actor size mismatch");
} // namespace SDK::wh::entitymodule
