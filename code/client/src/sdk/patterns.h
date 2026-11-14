#pragma once

#include <cstdint>

namespace SDK {
    /**
     * Addresses resolved from WHGame.dll at startup.
     *
     * Keep binary signatures here rather than in individual SDK classes so
     * version-dependent code has one audit point.
     */
    class Patterns {
      public:
        std::uint64_t C_Player__OnItemEquipped                                 = 0; // 0x181351de0
        std::uint64_t C_Player__OnItemUnequipped                               = 0; // 0x18179f7c8
        std::uint64_t C_Player__RefreshHelmetActionHint                        = 0; // 0x180c60630
        std::uint64_t C_Player__RefreshHeadLight                               = 0; // 0x180BC53B8
        std::uint64_t C_EquipmentManager__ReadEquippedVisorTypeId              = 0; // 0x181F1EEE0
        std::uint64_t I_ActionSets__SetActionEnabled                           = 0; // 0x180a26028
        std::uint64_t C_Human__OnEntityRemoved                                 = 0; // 0x180FC485C
        std::uint64_t C_Player__OnEntityRemoved                                = 0; // 0x180FC414C
        std::uint64_t C_Player__ProcessEvent                                   = 0; // 0x18108DD28
        std::uint64_t C_StealthWatcher__BindLocalPlayer                        = 0; // 0x18197B554
        std::uint64_t C_PlayerStateHandler__BindPlayer                         = 0; // 0x180A46428
        std::uint64_t PlayerTrespass__OnAreaChanged                            = 0; // 0x180875D94
        std::uint64_t C_NPCManager__RegisterNPC                                = 0; // 0x180417AFC
        std::uint64_t C_NPCManager__LocalPlayerNPCCache                        = 0; // 0x185494498
        std::uint64_t RebindLocalPlayerScriptVariables                         = 0; // 0x180B41D58
        std::uint64_t I_ActionSets__ClearActionCallbacks                       = 0; // 0x18085A914
        std::uint64_t C_ActionSets__ParseActionHelp                            = 0; // 0x180A13558
        std::uint64_t C_ActionHintManager__HandleAction                        = 0; // 0x1812D82A4
        std::uint64_t C_PlayerInput__Destructor                                = 0; // 0x18100DD14
        std::uint64_t C_Soul__UpdatePlayerMaterialAudio                        = 0; // 0x180BE21E8
        std::uint64_t SubmitPlayerMaterialAudioSwitch                          = 0; // 0x180BE2D2C
        std::uint64_t C_ExhaustedBuffInstance__TriggerPlayerExhaustionEffect   = 0; // 0x182BCCCF0
        std::uint64_t C_StarvationBuffInstance__TriggerHungerEffect            = 0; // 0x182BDA808
        std::uint64_t C_Player__SetFeedbackSound                               = 0; // 0x180905920
        std::uint64_t ExecuteEntityAudioTrigger                                = 0; // 0x1827C1108
        std::uint64_t C_SoulContext__UpdateCombatDanger                        = 0; // 0x1809D12C8
        std::uint64_t C_SoulBuffInstance__StartActorEffect                     = 0; // 0x1805D0914
        std::uint64_t C_ChatFollowManager__Update                              = 0; // 0x18082DDC4
        std::uint64_t WHGlobalEnvironment__GetInstance                         = 0; // 0x1809155c8
        std::uint64_t C_Player__ReviveLocalGlobalsReturn                       = 0; // 0x180bc6849
        std::uint64_t C_CombatModule__DestroyCombatActor                       = 0; // 0x1810EF4B4
        std::uint64_t C_Player__OnPostUpdate                                   = 0; // 0x181229578, IGameFrameworkListener slot 1 on the +0xA68 base
        std::uint64_t C_DogInteractionManager__UpdateDogHudAvailability        = 0; // 0x180AE3538
        std::uint64_t C_Player__UpdateMovementAudio                            = 0; // 0x18072E8A0, slot 258
        std::uint64_t C_Human__UpdateMovementAudio                             = 0; // 0x180B70E24, slot 258
        std::uint64_t C_Player__IsGod                                          = 0; // 0x182AA82F8, slot 39
        std::uint64_t C_UIApseInventory__UseConsumableItem                     = 0; // 0x181F49C00
        std::uint64_t C_Player__UseItem                                        = 0; // 0x181F23B30, slot 195
        std::uint64_t C_MissileWeaponPlayerController__RegisterInputActions    = 0; // 0x181786020
        std::uint64_t C_MissileWeaponPlayerController__RefreshMissileActionMap = 0; // 0x182AA9E0C
        std::uint64_t C_Player__HandleDeath                                    = 0; // 0x182aa94d0
        std::uint64_t C_SoulList__GetOrCreateSoul                              = 0; // 0x18068A244, I_SoulSystem slot 23
        std::uint64_t C_Player__InitLocalPlayer                                = 0; // 0x180b8460c
        std::uint64_t C_Player__NotifyActorActionRequest                       = 0; // 0x180C61978
        std::uint64_t C_Actor__NotifyActorActionRequest                        = 0; // 0x180B25134
        std::uint64_t C_PlayerProfileWHManager__SetSaveLock                    = 0; // 0x1809daff4
        std::uint64_t CScreenEffects__PostUpdate                               = 0; // 0x181028cd8
        std::uint64_t C_PlayerMaterialEffects__Update                          = 0; // 0x18082ed64
        std::uint64_t C_Player__Init                                           = 0; // 0x180a4c1dc
        std::uint64_t CreateCombatPlayer                                       = 0; // 0x180BC6E90
        std::uint64_t C_CombatPlayer__Init                                     = 0; // 0x180B3E820
        std::uint64_t C_CombatPlayer__ShutdownCombatComponents                 = 0; // 0x1810E9898
        std::uint64_t C_Player__Release                                        = 0; // 0x180b51eec
        std::uint64_t C_Player__Update                                         = 0; // 0x18082f238
        std::uint64_t C_Player__PostUpdate                                     = 0; // 0x181028c80
        std::uint64_t C_Player__RegisterLocalInputActions                      = 0; // 0x1817fa9b4
        std::uint64_t C_Player__UnregisterLocalInputActions                    = 0; // 0x1819521bc
        std::uint64_t C_PlayerInput__RegisterLocalInputActions                 = 0; // 0x18100e7d0
        std::uint64_t C_PlayerInput__OnAction                                  = 0; // 0x1808e5324
        std::uint64_t C_CameraManager__Update                                  = 0; // 0x18082f704
        std::uint64_t C_PlayerInput__PreUpdate                                 = 0; // 0x18082F77C
        std::uint64_t C_PlayerMovementAction__CalculateState                   = 0; // 0x1809490d8
        std::uint64_t C_PlayerMovementAction__ResolveActor                     = 0; // 0x18094a190
        std::uint64_t C_PlayerMovementAction__Construct                        = 0; // 0x18113A22C, (self, entityId), 0xF8 bytes
        std::uint64_t C_NPCActor__UpdateCollisionClassMask                     = 0; // 0x18044CA98, C_Actor vtable slot 272
        std::uint64_t C_Player__UpdateCollisionClassMask                       = 0; // 0x18115C3AC, C_Actor vtable slot 272
        std::uint64_t C_ActorRotation__Update                                  = 0; // 0x1804415E0, (rotation, frame movement params)
        // CAIProxy primary vtable slot 1: the per-frame request an AI object's
        // UpdateProxy drives its actor with.
        std::uint64_t CAIProxy__Update                                         = 0; // 0x1808372A8
        // Alchemy table entry, actions and result boundaries.
        std::uint64_t AlchemyBackCallback                            = 0; // 0x181FFEC90
        std::uint64_t C_AlchemyResource__BaseConstructor             = 0; // 0x1806C2B08
        std::uint64_t C_AlchemyResource__Constructor                 = 0; // 0x1808D4260
        std::uint64_t C_Alchemy__CreateReturnAction                  = 0; // 0x1806C6A00
        std::uint64_t C_Alchemy__Destructor                          = 0; // 0x180FC0BA8, nondeleting
        std::uint64_t C_Alchemy__EvaluateContainer                   = 0; // 0x182E16680, native reward boundary
        std::uint64_t C_Alchemy__OnEntryActionFinished               = 0; // 0x1808981B8
        std::uint64_t C_Alchemy__QueueAction                         = 0; // 0x1815FC55C
        std::uint64_t C_Alchemy__Reset                               = 0; // 0x1806C4F1C
        std::uint64_t C_Alchemy__SelectObject                        = 0; // 0x1815FC494
        std::uint64_t C_Alchemy__RequestCleanup                      = 0; // 0x1819C790C, normal exit request, slot 46
        std::uint64_t C_Alchemy__Shutdown                            = 0; // 0x1809F1A8C
        std::uint64_t C_Alchemy__Start                               = 0; // 0x180897E1C
        std::uint64_t C_Alchemy__Update                              = 0; // 0x180737920
        std::uint64_t C_Blacksmithing__AcceptSelectedRecipe          = 0; // 0x1811A072C, the Craft button's callback
        std::uint64_t C_Blacksmithing__Cancel                        = 0; // 0x182E59578, normal cancellation, slot 6
        std::uint64_t C_Blacksmithing__ConsumeRecipeMaterials        = 0; // 0x1812C1950, native material debit
        std::uint64_t C_Blacksmithing__FinishWorkpiece               = 0; // 0x1812C0CB4, native outcome boundary
        std::uint64_t C_Blacksmithing__Start                         = 0; // 0x180B674A4
        std::uint64_t C_BlacksmithingSlotToolAttachment__Attach      = 0; // 0x18193F148, slot 1
        std::uint64_t C_BlacksmithingSlotToolAttachment__Detach      = 0; // 0x180B67CBC, slot 2
        std::uint64_t C_RPGMinigames__FinishBlacksmithing            = 0; // 0x180B68718, primary slot73
        std::uint64_t C_RPGMinigames__IsAlchemyRecipeRangeKnown      = 0; // 0x182CF8534, primary slot11
        std::uint64_t C_RPGMinigames__IsAlchemyRecipeStepKnown       = 0; // 0x181326BEC, primary slot10
        std::uint64_t C_RPGMinigames__OnAlchemyFinished              = 0; // 0x18193573C
        std::uint64_t C_RPGMinigames__OnAlchemyStarted               = 0; // 0x180898550
        std::uint64_t C_ReturnIngredientAction__ReturnHeldIngredient = 0; // 0x182E186B8
        std::uint64_t C_ScriptBind_Alchemy__StartAlchemy             = 0; // 0x180896C64
        std::uint64_t C_TakeIngredientAction__OnAnimationEvent       = 0; // 0x1816F9E40
        std::uint64_t C_UseBellowsAction__Pump                       = 0; // 0x1806C6500
        std::uint64_t C_UseBellowsAction__ReturnBellows              = 0; // 0x1817BAD3C
        std::uint64_t C_UseMortarAction__StartAction                 = 0; // 0x1806C3EB0
        std::uint64_t C_UseResourceAction__OnAnimationEvent          = 0; // 0x1806C29D8
        std::uint64_t C_UseRetortAction__FinishDistillation          = 0; // 0x182E16258
        std::uint64_t C_UseRetortAction__GetRequiredBellowsCycles    = 0; // 0x182E16834
        std::uint64_t C_UseRetortAction__Pump                        = 0; // 0x182E1993C

        std::uint64_t C_BodyPartData__Construct        = 0; // 0x180EE0404
        std::uint64_t C_BodyPartData__Destroy          = 0; // 0x180827B9C
        std::uint64_t CryGFxFSCommandHandler__Callback = 0; // 0x181179C4C
        // Native Mannequin emote playback and lifecycle edges.
        std::uint64_t CryActionAllocate         = 0; // 0x1804F75C0
        std::uint64_t TAction__Construct        = 0; // 0x18160E52C
        std::uint64_t IAction__Release          = 0; // 0x1804B2F14
        std::uint64_t IAction__ForceFinish      = 0; // 0x180681FAC
        std::uint64_t IAction__Stop             = 0; // 0x18275F89C
        std::uint64_t IAction__SetFragment      = 0; // 0x1804AD95C
        std::uint64_t IAction__Enter            = 0; // 0x181A7DDC0
        std::uint64_t IAction__Exit             = 0; // 0x181A7DDD0
        std::uint64_t IAction__GetTargetPos     = 0; // 0x18078A048
        std::uint64_t CActionController__Update = 0; // 0x1804B3DF8
        std::uint64_t CActionController__Queue  = 0; // 0x180684D8C
        std::uint64_t CActionController__Flush  = 0; // 0x1804B011C
        // Hooked: the look context's per-frame update (NativePlayerModule::LookContextUpdate).
        std::uint64_t CProceduralContextLook__Update = 0; // 0x180C4EF54
        std::uint64_t C_CallbackAction__VTable  = 0; // 0x183A450B0
        std::uint64_t C_Signal__EmitBool        = 0; // 0x180393524, every one-bool model signal's dispatch: each subscriber with the value
        std::uint64_t C_AlchemyAnimAction__VTable    = 0; // 0x183A47948, written by its constructor 0x1806C6CF0
        // The grindstone player's three actions: the walk-in, the wheel loop
        // and the walk-out, each written by its constructor.
        std::uint64_t C_HumanSharpeningInAction__VTable  = 0; // 0x183F748D0, constructor 0x182EA5ED8
        std::uint64_t C_HumanSharpeningAction__VTable    = 0; // 0x184740048, constructor 0x182EA5D44
        std::uint64_t C_HumanSharpeningOutAction__VTable = 0; // 0x18473FF58, constructor 0x182EA5F8C
        // XGenAI identity maps: WUID -> AI object -> entity, and EntityGUID -> WUID.
        std::uint64_t C_AIObjectManager__Instance           = 0; // 0x185481FC8
        std::uint64_t C_AIObjectManager__FindAIObjectByWUID = 0; // 0x18047C1F8
        std::uint64_t xgenaimodule__EntityGuidWuidMap       = 0; // 0x185496338
        // Brain-carrying XGenAI objects by WUID: the registry and lookup the
        // brain-variable script binds go through.
        std::uint64_t xgenaimodule__IntelligentObjectManager = 0; // 0x1854930D0
        std::uint64_t C_IntelligentObjectManager__FindObject = 0; // 0x18070DB90
        std::uint64_t xgenaimodule__FindWUIDByEntityGuid    = 0; // 0x18047E638
        // Hooked, not called: the local player's AI links for the body it
        // carries and the one it stealth-attacks. Both are free functions.
        std::uint64_t xgenaimodule__WriteCarryLinks         = 0; // 0x18341FC68
        std::uint64_t xgenaimodule__WriteStealthAttackLinks = 0; // 0x183423CE0
        // The lookups those two make for the other end of the link: an
        // actor's WUID through its EntityGUID, and the AI object of the kind
        // a link takes under that WUID.
        std::uint64_t xgenaimodule__GetActorWuid          = 0; // 0x183446640
        std::uint64_t xgenaimodule__FindLinkableAIObject  = 0; // 0x18047C1AC
        // A native animal flee: the message that hands an animal's brain its
        // escape, and the stop that takes the move back when the flee ends.
        std::uint64_t xgenaimodule__SendMessageToWuid     = 0; // 0x1804C968C
        std::uint64_t xgenaimodule__StopNpcMovement       = 0; // 0x1807D7B24
        std::uint64_t CActionController__GetFragID        = 0; // 0x18140E294
        std::uint64_t CActionController__GetTagDefinition = 0; // 0x180399874
        std::uint64_t CTagDefinition__FindCRC             = 0; // 0x18078BCEC
        std::uint64_t CTagDefinition__Set                 = 0; // 0x1804AC88C
        std::uint64_t CTagDefinition__SetGroupImpl        = 0; // 0x1804AB420

        // `CCGFAttachment`'s vtable: the static-mesh payload a character
        // attachment draws. The game builds one inline wherever it hangs a
        // `.cgf` on a socket -- `CProceduralClipAttachProp::OnEnter`,
        // `CScriptBind_Entity::SetAttachmentCGF` -- by allocating 0x18 bytes
        // and writing this pointer over the front, and so does the mod.
        std::uint64_t CCGFAttachment__VTable = 0x0; // 0x183E5F4C0

        // `CEntityAttachment`'s constructor: the payload that carries a whole
        // entity on a socket. `C_BlacksmithingWorkpieceAttachment` builds one
        // into 0x28 bytes it allocates and binds it to the tongs' socket
        // (0x180B66624); its deleting destructor frees through
        // `FreeActionStorage`, so the mod allocates with `CryActionAllocate`.
        std::uint64_t CEntityAttachment__Construct = 0x0; // 0x1809DFD44

        // `wh::animationmodule::C_ProceduralClipAnimEvent::OnEnter`, slot 12
        // of the `AnimEvent` procedural clip: raises its event name on the
        // body's `AnimEventContext` signal, which the native smithing action
        // listens to (0x18148A1F8, 0x180857500).
        std::uint64_t C_ProceduralClipAnimEvent__OnEnter = 0x0; // 0x18085731C

        // Address of CCryAction's global IGameFramework * holder.
        std::uint64_t IGameFramework__Instance = 0x0; // 0x185497F70

        // Holder of Warhorse's own global registry: a lazily constructed
        // 0x1F0-byte POD carrying the game-side modules. Despite the name it
        // once had here, this is NOT CryEngine's gEnv -- that is the static
        // struct below. Known fields: C_CameraManager +0x38, the GUI module
        // +0xE8, the player module +0x128, the environment module +0x138.
        std::uint64_t WHGlobalEnvironment__Instance = 0x0; // 0x18549D388

        // C_UIMenu::EnterState, which brings up one of the game's own menu
        // roots. Its argument is the context: 1 the front end, 2 and 3 the
        // in-game pages.
        std::uint64_t C_UIMenu__EnterState = 0x0; // 0x180C04B38

        // Plays one of the GUI module's built-in UI audio cues through the
        // game's own sound object and mixer. The concrete controller type is
        // not RTTI-exposed, hence the address-qualified descriptive name.
        std::uint64_t C_UISoundController180559ED0__Play = 0x0; // 0x180559ED0

        // Base of CryEngine's gEnv, which is a static struct rather than a
        // pointer holder.
        //
        // Every CEngineModule_*::Initialize writes its subsystem straight into
        // it, which yields the field map: pPhysicalWorld +0x30, pInput +0x40,
        // pCryPak +0x50, pEntitySystem +0xA0, pConsole +0xA8, pAudioSystem
        // +0xB8, pSystem +0xC0, pRenderer +0x108.
        std::uint64_t SSystemGlobalEnvironment__Base = 0x0; // 0x18492D800

        // Executes an ATL trigger on the global audio object -- the 2D path,
        // taken by the GUI sound controller and the AI sound node when they
        // have no entity. Pinned by the 0x80-byte frame that holds the
        // SAudioRequest and its request data side by side, then `mov rdi, rdx`
        // before anything else.
        std::uint64_t ExecuteGlobalAudioTrigger180558908 = 0x0; // 0x180558908

        // Reached directly rather than through the interface, as the game's
        // own callers do. Call it through SDK::CPhysicalWorld.
        std::uint64_t CPhysicalWorld__RayWorldIntersection = 0x0; // 0x1804846E4

        // SpawnParams' default constructor: the only thing that knows where the
        // shared empty-string header its `audioRtpc` must point at lives.
        // Reached through SDK::MakeSpawnParams.
        std::uint64_t SpawnParams__Construct = 0x0; // 0x180422DA4

        // Reached through SDK::SEntityProximityQuery. Both are the engine's
        // own because a result set past the query's inline arena is allocated
        // on the game's heap.
        std::uint64_t SEntityProximityQuery__Construct        = 0x0; // 0x180C8890C
        std::uint64_t SEntityProximityQuery__FreeResultBuffer = 0x0; // 0x180C88FBC

        // Address of the global I3DEngine * holder.
        std::uint64_t I3DEngine__Instance = 0x0; // 0x18492D808

        // Address of the global holder of the 3D engine's object manager,
        // the owner of the vegetation-group table.
        std::uint64_t CObjManager__Instance = 0x0; // 0x185496FB0

        // Address of the FlashUI registry pointer used by native page setup.
        std::uint64_t IFlashUI__Instance                   = 0x0; // 0x18492D938
        std::uint64_t CCryPak__OpenPackWithBindingRoot     = 0x0; // 0x180DA1A5C
#if defined(KCDC_AGENT_TESTING)
        std::uint64_t CXmlNode__SaveToFile = 0x0; // 0x180A6B2F0, IXmlNode slot 59
        std::uint64_t CHardwareMouse__ConfineCursor = 0x0; // 0x1808EB2E0, the game's only ClipCursor to the window
#endif
        std::uint64_t CFlashUIElement__AddEventListener    = 0x0; // 0x180566138
        std::uint64_t CFlashUIElement__RemoveEventListener = 0x0; // 0x1805660E4
        std::uint64_t CFlashUIElement__Render = 0x0; // 0x181006468, Render and RenderLockless
        std::uint64_t C_UIHudMask__Instance = 0x0; // 0x18548BFA8, native singleton holder

        // Builds a soul's inventory and equipment manager if it has none.
        //
        // C_Soul::InitFromEntity calls this as its last identity step, so a
        // soul the engine built has already been through it.
        std::uint64_t C_Soul__EnsureInventoryRuntime = 0x0; // 0x180A30568

        // Recomputes whether an actor's entity can wear clothing and
        // stores the answer at actor +0x9B1. C_Actor::AcquireSoul runs it once
        // during entity init.
        std::uint64_t C_Actor__RefreshClothingSupport = 0x0; // 0x18068A3F4

        // Builds an actor's C_ActorEquipmentHandler, registers it with the
        // soul's inventory as an equipment listener and caches it at +0x678.
        std::uint64_t C_Actor__CreateEquipmentHandler = 0x0; // 0x1808D28FC

        // Reads which of a Mannequin tag group's members is currently set by
        // walking the group's own tag list. A group's members are packed as a
        // value inside the group's bits, and only that list says which values
        // exist, so the dictionary answers rather than the caller decoding.
        std::uint64_t CTagDefinition__GetGroup = 0x0; // 0x1804AB47C

        // Writes one member of a tag group, clearing the group first.
        std::uint64_t CTagState__SetGroup = 0x0; // 0x1804AB3E8

        // Recomputes an actor's Mannequin tags once a frame from its own
        // velocity and speed, clearing the MoveSpeed and MoveDir groups when
        // it reads as standing still. A replicated body's tags have to be
        // written after this rather than before it.
        std::uint64_t C_Actor__UpdateMannequinTags = 0x0; // 0x1804AAE90

        // Builds a C_ActorActionRider. Takes the allocator an action queue
        // carries at +0x50, an out pointer for the action, and a pointer to
        // the parameter word it copies whole into the action at +0x50.
        std::uint64_t C_ActorActionRider__Create = 0x0; // 0x180AD40A0

        // `C_ActorActionRider::Enter`, vtable slot 38: the one place a queued
        // mount becomes a seat. It resolves the horse, refuses (false) when
        // there is none or it has no soul, and otherwise builds the rider
        // sync and calls `C_RiderSync::AttachToHorse`, which hands a callback
        // to the horse entity's slot-0 character without asking whether there
        // is one. A false answer drops the action from its queue slot.
        std::uint64_t C_ActorActionRider__Enter = 0x0; // 0x180AA1474

        // Slot 10 rolls back the last child after a failed entry. The native
        // assumes the child range is nonempty, even when Enter was refused.
        std::uint64_t C_ActorActionRider__RemoveLastChildAction = 0x0; // 0x1829E17D8

        // Typed item-handling expansion accessor. The raw virtual lookup does
        // not create missing expansions; this native asks the registered
        // factory to construct type 3 and attaches it to the actor first.
        std::uint64_t C_ActionActor__GetItemHandlingExpansion = 0x0; // 0x1808C9E94

        // Typed rider-expansion accessor, type 5, created on demand like the
        // one above. C_Actor slots 203, 204 and 205 -- and the Lua CanMount,
        // ForceMount and ForceDismount behind them -- forward onto its slots
        // 8, 11 and 12.
        std::uint64_t C_ActionActor__GetRiderExpansion = 0x0; // 0x180682A0C

        // `C_ActionActor::FindRiderAction()`, the running ride itself: a
        // predicate walk over the queue's live actions for type 1. The
        // expansion's own dismount opens with this and then reads the ride
        // context off the action, which is the only route to a ride's state
        // for a body that is not the client actor.
        //
        // The pointer it answers with is borrowed -- it is released before
        // returning and stays alive only because the queue still holds a
        // reference -- so read through it and do not keep it.
        std::uint64_t C_ActionActor__FindRiderAction = 0x0; // 0x180856C44

        // `C_Horse::SetRiderSync(C_RiderSync *)`, which writes the property at
        // C_Horse+0x9E8. Its only callers are C_RiderSync::AttachToHorse and
        // ::DetachFromHorse, so a non-null argument is a rider taking the
        // saddle and null is it being given up -- the same field the engine's
        // own mount check reads to refuse an occupied animal.
        std::uint64_t C_Horse__SetRiderSync = 0x0; // 0x180B4565C

        // The ride state machine, `C_HorseRideContext::SetState(state, force)`.
        //
        // Every edge passes through it: the mount from the rider action's
        // entry, the get-off from a dismount request, and the tear-down from
        // that action's exit, which forces a dismount rather than clearing the
        // state. It moves when the engine decides, before the animation it
        // queues has played -- which is what a transition must be read from.
        std::uint64_t C_HorseRideContext__SetState = 0x0; // 0x180619210

        // C_Horse's override of the tag pass above, vtable slot 269.
        //
        // It stores the frame's movement params into the inline state at actor
        // +0x9F0, calls the shared pass, and then rewrites the MoveDir group
        // from the horse's own speed (`m_pHorseRuntimeData + 0x110`, backward
        // flag at +0x124), clearing the group when that speed is zero -- which
        // a body placed from the network always reads as. Replicated tags have
        // to be written after this, not after the pass it calls.
        std::uint64_t C_Horse__UpdateMannequinTags = 0x0; // 0x1806CF62C
        std::uint64_t HorseRuntimeData__ApplyPendingJumpRequest = 0x0; // 0x1806CFABC
        // The rider's horse command intake: gait move style, flags, backing.
        std::uint64_t SubmitHorseMoveCommand = 0x0; // 0x180A5019C

        // Puts a horse that has stood ~15 s under a player rider back onto its
        // navmesh. Reached only from the ridden-idle timer at 0x1806CCD08, and
        // takes the runtime's upkeep block (`HorseRiddenUpkeepLayout`). It
        // asks the entity's AI object for its navigation agent without a null
        // check, so a horse spawned without an AI faults inside it.
        std::uint64_t HorseRiddenUpkeep__SnapToNavMesh = 0x0; // 0x1829F2020

        // `C_RiderPlayerControl::TryPlaceHorseNearPlayer(IEntity *horse, bool
        // useNavigationPlacement, bool)`, the whistle's far branch: it finds a
        // spot in the player's view cone and puts the animal there. Reached
        // from `CallHorse` once the horse is past the configured call distance,
        // and from the `TeleportHorseToPlayer` behaviour-tree node. It looks
        // the horse's AI object up by entity id and both of its placement
        // branches dereference the result without a null check, so a horse
        // spawned without an AI faults inside it.
        std::uint64_t C_RiderPlayerControl__TryPlaceHorseNearPlayer = 0x0; // 0x1829F2B80

        // Picks which locomotion state the AI movement action plays this
        // frame: idle, turn, movement or in-air. Its early-out answers a
        // forced non-locomotion state before it ever reads the actor's
        // velocity, which is why a replicated body has to override the result
        // rather than feed the inputs.
        std::uint64_t CAnimActionAIMovement__SelectState = 0x0; // 0x1804ADA10

        // Builds and queues an actor's locomotion action when the one it has
        // is gone (status none). The tag pass reaches it only for an
        // AI-controlled body; see C_Actor::EnsureLocomotionAction.
        std::uint64_t C_Actor__EnsureLocomotionAction = 0x0; // 0x1804ABAE0

        // Animation-only local motion input, before parameter smoothing.
        std::uint64_t CAnimatedCharacter__SetDesiredLocalLocation = 0x0; // 0x18083B82C

        // `C_PlayerInput::OnSprint`, hooked for the edge it makes: it forces
        // the walk/jog byte at C_PlayerInput+0x7C back to jog on every frame
        // the sprint key is held, which is what ends a walk-by-default pace.
        std::uint64_t C_PlayerInput__OnSprint = 0x0; // 0x1814827AC

        // `C_PlayerInput::StopSprint`, the one place a sprint ends: a
        // released key, movement stopping and stamina all reach it.
        std::uint64_t C_PlayerInput__StopSprint         = 0x0; // 0x181482A08
        std::uint64_t C_PlayerInput__SetSprintRequested = 0x0; // 0x1814828D0
        std::uint64_t C_PlayerInput__StartSprint        = 0x0; // 0x18148291C
        std::uint64_t C_PlayerInput__OnXiMoveX          = 0x0; // 0x180EFE1E8
        std::uint64_t C_PlayerInput__OnXiMoveY          = 0x0; // 0x180EFE20C
        std::uint64_t C_RiderPlayerInput__HandleAction  = 0x0; // 0x1808E5688
        std::uint64_t C_RiderPlayerInput__OnSlowdown    = 0x0; // 0x181A88670

        // The `C_PlayerInput` constructor, which stores 1 at +0x7C -- the
        // whole reason the game starts in jog.
        std::uint64_t C_PlayerInput__Construct = 0x0; // 0x180799E9C

        // `C_Actor::SetMovementRestriction`, vtable slot 222: the only writer
        // of the run and sprint permissions at actor +0x40A and +0x409.
        std::uint64_t C_Actor__SetMovementRestriction = 0x0; // 0x18285EF98

        // Puts a body away or wakes it up.
        //
        // C_Actor::Init spawns every actor that holds no network channel put
        // away, which disables its update slot. The wake-up half re-runs the
        // model setup, re-binds the animated character, rebuilds physics,
        // rendering and clothing, and ends by re-enabling the update slot.
        std::uint64_t C_Actor__SetPutAway = 0x0; // 0x18087A558

        // Asks for a body to be put away or woken, and the call to make from
        // outside the engine.
        //
        // The transition above has three callers: entity init, this request's
        // own immediate branch, and a budgeted manager on the Warhorse global
        // environment (`+0xE0`) that performs it while holding the
        // process-wide lock at `0x18492D988`. Only the last is a mid-frame
        // call, and it is the one to imitate -- calling the transition
        // directly from a mod call site killed the process on a horse twice,
        // where the request does not.
        //
        // The two states live apart: bit 0x80 of actor `+0x9B2` is what is
        // wanted, bit 0x40 what has been applied. The transition writes only
        // the applied bit; this writes the wanted one and queues the actor
        // when they disagree, leaving the engine to pick the moment. Actor in
        // RCX, wanted in DL, an immediate flag in R8B the game only sets in
        // particular game states.
        std::uint64_t C_Actor__RequestPutAway = 0x0; // 0x181E7E0E0

        // Builds the actor's clothing object if it may have one, then has
        // it rebuild what the body is wearing.
        std::uint64_t C_Actor__RebuildClothing = 0x0; // 0x18068BEA4

        // Puts one item on a body and tells the equipment listeners.
        std::uint64_t C_EquipmentManager__EquipItemRaw = 0x0; // 0x1808F8DBC

        // The edge pair for "which item is in which weapon slot": Set writes
        // *(this + 0x98 + 8*slot), Clear zeroes it and its occupancy bit at
        // +0xD8. EquipItemRaw calls Clear to evict before it calls Set, so
        // nothing outside has to unequip first. UnequipItemRaw is not the
        // counterpart -- it is the armor path over the worn vector at +0x40.
        std::uint64_t C_EquipmentManager__SetWeaponSlotItem   = 0x0; // 0x180476374
        std::uint64_t C_EquipmentManager__ClearWeaponSlotItem = 0x0; // 0x180474C70

        // C_ItemAttachmentManager::SetHandItem(hand, item) -- the only writer
        // of the actor's hand-item state at C_ActorModel+0x258, set on attach
        // and cleared with g_InvalidItemWUID on detach. Two hands, 0 right and
        // 1 left. This is what "actually out" means; the equipment manager's
        // weapon slot is not, since an equip is mirrored into it.
        std::uint64_t C_ItemAttachmentManager__SetHandItem = 0x0; // 0x1808D0758

        // C_ModelBitsetProperty<E_ActorState,...>::Set(state, on), the one
        // writer of the actor-state set at C_ActorModel+0x370. Called through
        // SDK::wh::entitymodule::C_ActorModel::SetActorState.
        std::uint64_t C_ModelBitsetProperty_E_ActorState__Set = 0x0; // 0x1808324D4

        // Takes one item off a body and tells the equipment listeners.
        std::uint64_t C_EquipmentManager__UnequipItemRaw = 0x0; // 0x180478CE8

        // Parses an 8-4-4-4-12 GUID string, optionally brace wrapped, into the
        // 16-byte CryGUID layout that S_ItemClassGUID shares.
        std::uint64_t ParseGuidString = 0x0; // 0x180719B1C

        // The process-wide C_BuffManager, built on first use behind a mutex.
        std::uint64_t C_BuffManager__GetInstance = 0x0; // 0x180E820D4

        // The single creation choke point of the buff system, manager vtable
        // slot 2: every path that gives anyone a buff reaches it. Slot 0 calls
        // it non-virtually, so the function is what to detour -- a vtable
        // patch misses the common path. Returns a _smart_ptr by sret in RDX.
        std::uint64_t C_BuffManager__CreateAndAttachBuff = 0x0; // 0x1804675A8

        // Resolves an entity's AI object through the AI object manager,
        // returning null when the entity registered none.
        std::uint64_t IEntity__GetAIObject = 0x0; // 0x1803CF0D0

        // The one place an entity's AI object is created: it removes any
        // object the entity already had and builds a replacement only when the
        // parameters carry a non-zero type word at +0x3A0.
        std::uint64_t CEntity__RegisterInAISystem = 0x0; // 0x180AEDE20

        // Builds the native DX12 root signature for a device resource layout.
        std::uint64_t CDeviceResourceLayout_DX12__Init = 0x0; // 0x180B1B114

        // Creates the engine-facing DXGI factory wrapper.
        std::uint64_t CCryDX12GIFactory__Create = 0x0; // 0x180C19CF4

        // Enumerates and wraps one native DXGI adapter.
        std::uint64_t CCryDX12GIAdapter__Create = 0x0; // 0x181DFDC80

        // Enumerates and wraps one native DXGI output.
        std::uint64_t CCryDX12GIOutput__Create = 0x0; // 0x181DFDE30

        // Engine-facing flip of the main viewport. `this` carries the engine
        // device at +0x10 and NCryDX12::CSwapChain at +0x18, which retains the
        // real IDXGISwapChain at +0xA8.
        std::uint64_t CCryDX12SwapChain__Present = 0x0; // 0x1804EDCB0

        // CSystem's window procedure, registered for the L"CryENGINE" window
        // class by CD3D9Renderer.
        std::uint64_t CSystem__WndProc = 0x0; // 0x180A57EAC

        // ISystem slot +0xE8, where the escape menu, Lua's System.Quit() and
        // WM_CLOSE all end up. Warhorse's signature is Quit(std::string_view
        // reason, unsigned int exitCode).
        std::uint64_t CSystem__Quit = 0x0; // 0x18245DBA4

        // wh::C_ModManager's scan of mods/, the platform folders and
        // mod_order.txt, run once from its constructor in CSystem::Init.
        std::uint64_t C_ModManager__ScanMods = 0x0; // 0x180D9DC4C

        // C_ModManager vtable slot 5: hands each enabled mod to a callable.
        // Every consumer of the mod list -- paks, mod.cfg, Lua, localization
        // and table patches -- goes through it.
        std::uint64_t C_ModManager__ForEachEnabledMod = 0x0; // 0x1804D98BC

        // IGameFramework vtable slot 11; one call per rendered frame on the
        // main thread, after the game's own update.
        std::uint64_t CCryAction__PostUpdate = 0x0; // 0x180669C88

        // IGameFramework vtable slot 13, and the only place the engine's
        // paused state is entered or left. Reason-counted:
        // PauseGame(bool pause, uint16 reason, bool releaseAll).
        std::uint64_t CCryAction__PauseGame = 0x0; // 0x180A55648

        // float wh::guimodule::GetApsePauseRatio(): the cvar
        // wh_ui_ApsePauseRatio, default 1000. Every UI screen that slows the
        // world rather than pausing it -- the inventory, map and journal,
        // skip time, fast travel -- pushes 1/ratio into t_GameScale.
        std::uint64_t GetApsePauseRatio = 0x0; // 0x180F492B8

        // CView::UpdateAudioListener(CCamera const &): places the view's
        // audio-listener entity from the camera matrix.
        std::uint64_t CView__UpdateAudioListener = 0x0; // 0x1807F0AEC

        // Quat_tpl<float>::SetRotationXYZ(const Ang3 &) and its inverse
        // Ang3_tpl<float>::Ang3_tpl(const Quat &), both radians. camgoto and
        // C_CameraDebug::SetOrbitPosition use them, so nothing here needs
        // Euler maths of its own.
        std::uint64_t Quat__SetRotationXYZ = 0x0; // 0x18053F6C0
        std::uint64_t Ang3__FromQuat       = 0x0; // 0x18058B6E8

        // C_CameraManager::SetDebugCameraMode -- the only call needed to take
        // the view over: it points the manager's active camera at the embedded
        // C_CameraDebug and the engine does the rest. Undefined hands it back.
        // Every other mode is behind a cheat gate that refuses silently.
        std::uint64_t C_CameraManager__SetDebugCameraMode = 0x0; // 0x1839BAEB8

        // C_CameraDebug::SetMode. Seeds the free pose from the render camera,
        // raycasts a custom orbit point when Orbit follows Free, and cycles
        // the orbit target when Orbit re-selects itself.
        std::uint64_t C_CameraDebug__SetMode = 0x0; // 0x1839BB944

        // C_CameraDebug::SetOrbitPosition(Vec3 yawRadians, pitchRadians,
        // distance) -- writes the orbit transform state whole.
        std::uint64_t C_CameraDebug__SetOrbitPosition = 0x0; // 0x1839BBE88

        // C_CameraDebug's one-time seed of the free pose, self-guarded.
        std::uint64_t C_CameraDebug__EnsureInitialized = 0x0; // 0x1839BB4F4

        // IRenderer vtable slot 11. CSystem::Init calls this when
        // sys_intromoviesduringinit is set, which allocates the
        // CIntroMovieRenderer and starts the Bink reel named by
        // wh_sys_StartupVideoFileName -- by default the publisher logo train
        // Videos/startup/startup_01/startup_01.bk2. CGame::CompleteInit then
        // blocks in StopRenderIntroMovies until that stream drains.
        std::uint64_t CD3D9Renderer__StartRenderIntroMovies = 0x0; // 0x1807995BC

        // Gate the menu controller runs before opening the main menu. It
        // returns true once it has queued the first-time EULA and
        // telemetry-consent overlays, which makes the caller bail out before
        // the menu appears.
        std::uint64_t C_UIMenu__ShowFirstTimeOverlays = 0x0; // 0x1819AA718

        // Tail of game init: loads scripts/main.lua, fires system events 36/37
        // and stops the intro reel. First point with a guaranteed live console.
        std::uint64_t CGame__CompleteInit = 0x0; // 0x180BF3808

        // The entry point every game start funnels through -- retail New Game,
        // the New Game (debug) entries and +map alike. Builds the start
        // parameters and queues them for C_NewGameHelper.
        std::uint64_t C_UISaveLoad__StartNewGame = 0x0; // 0x18184715C

        // C_UIMenu's primary vtable, which is how the menu is picked out of
        // the GUI module's page vector. Taken from the `lea` in its
        // constructor: the game reaches its pages through 32 identical
        // GetPage<T> instantiations that no signature can tell apart.
        std::uint64_t C_UIMenu__VTable = 0x0; // 0x183EF5A60

        // The same, for the page that owns the main-menu backdrop, plus the
        // private method that re-enters the menu state and restarts it.
        std::uint64_t C_UILoadingScreen__VTable                   = 0x0; // 0x183EF7C30
        std::uint64_t C_UILoadingScreen__ReturnToMainMenuState    = 0x0; // 0x182BBAB30
        std::uint64_t C_UILoadingScreen__OnSystemEvent            = 0x0; // 0x1808AE710
        std::uint64_t C_UILoadingScreen__Destructor               = 0x0; // 0x182BB7E4C
        std::uint64_t C_UILoadingScreen__StopVideoOrLoadingScreen = 0x0; // 0x1809AEA84

        // The readiness service's stock task, built in place by the mod to
        // hold a load completion (event 21) natively, and the observer entry
        // every load completion waits through.
        std::uint64_t C_ReadinessProgress__Constructor = 0x0; // 0x180B40B18
        std::uint64_t C_ReadinessProgress__Destructor  = 0x0; // 0x180D2C2F0
        std::uint64_t C_ReadinessObserver__Start       = 0x0; // 0x180FC3EC0
        std::uint64_t C_ReadinessObserver__StartEx     = 0x0; // 0x180FC3F48

        // The game session's started/ended latch (owner class unproven, on
        // C_WHGameModules[0]+0xB8): broadcasts module message 52 on the first
        // start and 77 on the matching end. A load completion starts it --
        // the event-21 lambda, the level switch's end and C_NewGameHelper's
        // readiness observer -- and ReturnToMainMenuState ends it.
        std::uint64_t GameSessionLatch__Start = 0x0; // 0x180FC41E8
        std::uint64_t GameSessionLatch__End   = 0x0; // 0x180FC40F0

        // The full-screen overlay page, picked out of the page vector by its
        // primary vtable like the others, and the method that publishes the
        // game's own screen tint as eight forced Global_User_* post-effect
        // writes. The screen-effect composition detours the latter.
        std::uint64_t C_UIOverlay__VTable         = 0x0; // 0x183EF7D40
        std::uint64_t C_UIOverlay__ApplyUserColor = 0x0; // 0x1808BDD5C

        // The only producer of event 33, a deferred call drained at the top of
        // C_Game::Update: queued by event 21 outside a level switch, and by
        // event 4114 at the end of one. It dispatches synchronously.
        std::uint64_t guimodule__RaiseLevelGameplayStart = 0x0; // 0x181F96510

        // CSystem's loading-time service pump, ISystem vtable slot +0x30.
        std::uint64_t CSystem__SynchronousLoadingTick = 0x0; // 0x1806A0708

        // Address of the global wh::game::C_Game * holder and its runtime
        // prefab-manager operations.
        std::uint64_t C_Game__Instance                        = 0x0; // 0x18549D300
        std::uint64_t C_RuntimePrefabManager__CreatePrefabRaw = 0x0; // 0x1809E4D9C
        std::uint64_t C_RuntimePrefabManager__MovePrefabRaw   = 0x0; // 0x1825BE03C
        std::uint64_t C_RuntimePrefabManager__DeletePrefabRaw = 0x0; // 0x1809E4F94

        // Native C_AnimalSpawner spawn/despawn-entry processing pass.
        std::uint64_t C_AnimalSpawner__ProcessSpawnEntriesRaw = 0x0; // 0x180627E78
        std::uint64_t C_AnimalSpawner__ProcessSpawnEntryRaw   = 0x0; // 0x180628274
        std::uint64_t C_AnimalSpawner__RetireSpawnEntryRaw    = 0x0; // 0x180627F3C

        // `C_AnimalSpawner::SetSpawningEnabled(bool, void *queryParams)`, the
        // one place the flag at spawner `+0x88` is written -- reached both by
        // the area callback the spawner registers with the proximity system
        // and by the pass mirroring the flag onto every linked spawner. The
        // pass it then runs reads the flag both ways: enabled spawns each
        // entry, disabled retires each entry.
        std::uint64_t C_AnimalSpawner__SetSpawningEnabled = 0x0; // 0x180627E00

        // `C_SoulPoolBase::FinalizeAndSubmitSpawnRequest`, the private step
        // every soul a pool picks passes through on its way to
        // `C_EntitySpawnerQueue::SubmitSpawnRequest`, whose request id it
        // returns. `C_SoulPoolBase::SpawnSouls` is its only caller.
        std::uint64_t C_SoulPoolBase__FinalizeAndSubmitSpawnRequest = 0x0; // 0x180626BDC

        // `wh::shared::C_ScopedEngineLock`'s constructor and destructor -- the
        // engine's RAII guard over the entity and script world.
        // `CEntitySystem::SpawnEntity` holds it only for the span it owns, so
        // with `autoInit = false` it is released before the caller runs
        // `InitEntity`. Every native that spawns in two steps therefore takes
        // an outer one of these across the whole sequence.
        std::uint64_t C_ScopedEngineLock__Construct = 0x0; // 0x1803D3308
        std::uint64_t C_ScopedEngineLock__Destruct  = 0x0; // 0x1803D497C

        // `C_ScriptBindActor::Butcher(TScriptHandle user)`, the native behind
        // `BasicAnimal:OnButcher`. It resolves the butchering player's actor
        // and queues a `C_ActorActionButchering` on that player's own action
        // queue, so it is the exact moment one client's player lays claim to
        // one carcass -- and the only one, since the action leaves nothing
        // behind that a later tick could read.
        std::uint64_t C_ScriptBindActor__Butcher = 0x0; // 0x181F27590

        // `C_Soul::CanBeButchered`, vtable slot 144. `BasicAnimal:GetActions`
        // gates the whole butcher-or-loot branch on it, so refusing it is how
        // a client stops offering an action on a carcass somebody else has
        // already claimed -- without touching the soul's own state.
        std::uint64_t C_Soul__CanBeButchered = 0x0; // 0x18094E0F8
        std::uint64_t C_Actor__RequestItemExchange      = 0x0; // 0x180C40074, virtual slot 238
        std::uint64_t C_Human__RequestItemExchange      = 0x0; // 0x18194BE44, override
        std::uint64_t C_ActorActionButchering__Enter    = 0x0; // 0x182896E34, virtual slot 38
        std::uint64_t C_ActorActionButchering__Complete = 0x0; // 0x182895AB8, private member
        std::uint64_t C_ActorActionButchering__Exit     = 0x0; // 0x182898BE8, virtual slot 39

        // `C_ScriptBindBase::ResolveActorFromArgs(IFunctionHandler *)`, the
        // shared first step of every actor script bind: it takes the `self`
        // the Lua call was made on and hands back the `C_Actor` behind it.
        // Called from the butcher hook, because the victim is that `self` and
        // the bind's own arguments name only the butchering player.
        std::uint64_t C_ScriptBindBase__ResolveActorFromArgs = 0x0; // 0x18082B834

        // Address of the global wh::entitymodule::C_EntityModule * holder.
        std::uint64_t C_EntityModule__Instance = 0x0; // 0x185487A60

        // Native post-spawn vegetation scale encoder.
        std::uint64_t CVegetation__SetScale = 0x0; // 0x18354954C

        // Address of the global CVisAreaManager * holder.
        std::uint64_t CVisAreaManager__Instance = 0x0; // 0x185496F98

        // Address of the global wh::entitymodule::C_ClothingSystem * holder.
        std::uint64_t C_ClothingSystem__Instance = 0x0; // 0x1854866A8

        // Address of the global wh::entitymodule::C_InventoryManager * holder.
        std::uint64_t C_InventoryManager__Instance               = 0x0; // 0x185487A58
        std::uint64_t C_InventoryManager__FindInventoryByWuidRaw = 0x0; // 0x1823CED5C
        std::uint64_t C_InventoryManager__GetOrCreateInventory   = 0x0; // 0x180A305F4
        std::uint64_t C_InventoryManager__DestroyInventory       = 0x0; // 0x1809666CC

        // Address of the global wh::environmentmodule::C_WeatherProfileDatabase object.
        std::uint64_t C_WeatherProfileDatabase__Instance = 0x0; // 0x185326240

        // The global wh::entitymodule::C_ClothingPresetDatabase and
        // C_WeaponPresetDatabase objects, and each one's key lookup.
        std::uint64_t C_ClothingPresetDatabase__Instance   = 0x0; // 0x185324520
        std::uint64_t C_ClothingPresetDatabase__FindByGuid = 0x0; // 0x18087E31C
        std::uint64_t C_WeaponPresetDatabase__Instance     = 0x0; // 0x185325FA0
        std::uint64_t C_WeaponPresetDatabase__FindByGuid   = 0x0; // 0x18087E5E0

        // The two recipe databases, wh::playermodule::C_AlchemyRecipeDatabase and
        // C_BlacksmithRecipeDatabase, and their lookups by key.
        std::uint64_t C_AlchemyRecipeDatabase__Instance          = 0x0; // 0x185332AF0
        std::uint64_t C_AlchemyRecipeDatabase__FindRecipeById    = 0x0; // 0x1808C3C0C
        std::uint64_t C_BlacksmithRecipeDatabase__Instance       = 0x0; // 0x185332C90
        std::uint64_t C_BlacksmithRecipeDatabase__FindRecipeById = 0x0; // 0x18085930C
        // I_UIBook slot 6 on C_UIBook: the alchemy book page, whose pictures follow the id it is given.
        std::uint64_t C_UIBook__SetRecipe = 0x0; // 0x1808C3E30

        // The global wh::entitymodule::C_ItemDatabase object and its lookup
        // of an item class by GUID.
        std::uint64_t C_ItemDatabase__Instance      = 0x0; // 0x185325820
        std::uint64_t C_ItemDatabase__FindItemClass = 0x0; // 0x180468340

        // The world clock's own setter. Forward only: it returns without doing
        // anything for a value below the current world time.
        std::uint64_t C_Calendar__SetWorldTime = 0x0; // 0x180FC4A44

        // Writes the world time ratio and mirrors it into
        // ITimeOfDay::SAdvancedInfo::animationSpeed.
        std::uint64_t C_Calendar__SetWorldTimeRatio = 0x0; // 0x180FC4B50

        // Pushes the world clock's hour of day, or the faked one, into
        // CTimeOfDay. Everything that moves the clock ends here.
        std::uint64_t C_Calendar__PublishTimeOfDay         = 0x0; // 0x180A58F3C
        std::uint64_t C_Signal180875DF8__Broadcast         = 0;   // Calendar day-change signal
        std::uint64_t C_TimeOfDayBlender__BlendTimeOfDay   = 0;   // 0x1814EA6A0
        std::uint64_t C_TimeOfDayBlender__ApplyPresetBlend = 0;   // 0x180552A58
        // The engine picking its own next sky when the blend queue runs dry.
        std::uint64_t C_TimeOfDayBlender__MakeRandomEntry = 0; // 0x1813478B4
        std::uint64_t ExecuteBlendTimeOfDayCommand        = 0; // 0x181F2E010
        std::uint64_t C_WeatherSystem__Update             = 0; // 0x180552874

        // Raw native question-dialog open routine.
        std::uint64_t C_UIModalDialogQuestion__OpenQuestionDialog = 0x0; // 0x1811A0D58

        // The interactive-dialogue HUD's constructor. There is no accessor for
        // this object: the GUI module builds it through RTTR reflection and keeps
        // the shared_ptr in a type-keyed registry, so the only cheap way to the
        // real instance is to be told when the game makes it.
        std::uint64_t C_UIHudInteractiveDialog__Ctor = 0x0; // 0x181876784

        // Raw native one-button info-dialog open routine.
        std::uint64_t C_UIModalDialogInfo__OpenInfoDialog = 0x0; // 0x182B14C6C
        std::uint64_t C_UIModalDialogInfo__CloseRaw       = 0x0; // 0x182B11AB0

        // Raw native four-choice random-event dialog open routine.
        std::uint64_t C_UIModalDialogRandomEvent__OpenRaw  = 0x0; // 0x182B15268
        std::uint64_t C_UIModalDialogRandomEvent__CloseRaw = 0x0; // 0x182B11D6C

        // Raw native inventory item-dialog open and close routines.
        std::uint64_t C_UIModalDialogItem__OpenRaw  = 0x0; // 0x182B14E80
        std::uint64_t C_UIModalDialogItem__CloseRaw = 0x0; // 0x182B11BC4

        // Resolves the GUI module's cached native HUD-messages instance.
        std::uint64_t C_UIHudMessages__GetInstance = 0x0; // 0x182AF9960

        // Shows a named tutorial through the player tutorial manager.
        std::uint64_t C_TutorialManager__ShowTutorial = 0x0; // 0x182043DB0
        // `C_Actor::Fall(hitPos)`, the actor's vslot 40: builds a type-5 hit
        // record that `ProcessActorHitRecord` turns into a ragdoll fall. The
        // physics' deferred commands call it on a rider whose horse struck a
        // wall, and the ragdoll action replaces the rider action.
        std::uint64_t C_Actor__Fall = 0x0; // 0x18285CF2C

        // `C_Player`'s vslot 223, COMDAT-folded into six other tables: stops
        // every animation layer of the entity's character
        // (`CSkeletonAnim::StopAnimationsAllLayers`, 0x180AA9CB8). On a player
        // its one caller is `C_Player::HandleDeath` (0x182AA94D0, the call at
        // 0x182AA9510). Its body past the prologue also occurs at
        // 0x1837DBA3B; the prologue makes the pattern unique.
        std::uint64_t C_Player__StopAnimations = 0x0; // 0x182860950

        // rpgmodule's way to the platform's IStatistics, and the two debug
        // console commands that fetch it themselves: every path that reaches a
        // platform achievement goes through one of the three, on Steam and on
        // the Microsoft Store build alike.
        std::uint64_t rpgmodule__GetPlatformStatistics         = 0x0; // 0x182D4EC9C
        std::uint64_t ConsoleCommand__DebugUnlockAchievement   = 0x0; // 0x18257F60C
        std::uint64_t ConsoleCommand__DebugProgressAchievement = 0x0; // 0x18257F414

        // Opens the skip-time screen. Its first argument is the
        // E_SkipTimeMode, and every way into the screen -- the wait dial, a
        // bed, a timed activity -- goes through it.
        std::uint64_t C_SkipTime__BeginSkipTime = 0x0; // 0x1812BFE0C

        // Raw native inventory browsing and mutation routines.
        std::uint64_t C_Inventory__AddItemRaw            = 0x0; // 0x1808D534C
        std::uint64_t C_ItemHolder__RemoveItem           = 0x0; // 0x180479758
        std::uint64_t C_Item__AdjustAmount               = 0x0; // 0x182A68AD4
        std::uint64_t C_Inventory__RemoveAllItemsRaw     = 0x0; // 0x180478BCC
        std::uint64_t C_Inventory__FindItemByClassRaw    = 0x0; // 0x1808D315C
        std::uint64_t C_Inventory__GetMoneyRaw           = 0x0; // 0x1808C5790
        std::uint64_t C_Inventory__RemoveMoneyRaw        = 0x0; // 0x182A1FDFC
        std::uint64_t C_Inventory__CreateItems           = 0x0; // 0x182A1EA34
        std::uint64_t C_Inventory__RemoveItemsByClass    = 0x0; // 0x1812C1B14
        std::uint64_t C_Inventory__DeleteItems           = 0x0; // 0x180823F60
        std::uint64_t C_Inventory__MoveItems             = 0x0; // 0x182A1FB08
        std::uint64_t C_ItemHolder__TransferItemToHolder = 0x0; // 0x1804CC9F8

        // Native ground-item drop and physical bind/spawn paths.
        std::uint64_t C_Human__DropInventoryItem = 0x0; // 0x1818523B4
        std::uint64_t WH__DropItemsUpToAmount    = 0x0; // 0x1819B1610
        std::uint64_t C_Item__SpawnPickable      = 0x0; // 0x1808D0848
        std::uint64_t C_Item__UnbindPickable     = 0x0; // 0x1804796DC

        // Reflected item-descriptor browsing/filtering adapters.
        std::uint64_t I_ItemDescriptor__GetMatchingItems  = 0x0; // 0x1813744A0
        std::uint64_t I_ItemDescriptor__FullyMatchesItems = 0x0; // 0x181374434

        // Reflected wh::entitymodule player-inventory reward operation.
        std::uint64_t CreatePlayerReward                  = 0x0; // 0x180516A0C
        std::uint64_t CreateItemDelivery                  = 0x0; // 0x181EE6CF0
        std::uint64_t CreateItemReferenceDescriptor       = 0x0; // 0x182A62184
        std::uint64_t CreateItemReferenceVectorDescriptor = 0x0; // 0x182A62264

        // Reflected native equipment mutation routines.
        std::uint64_t C_EquipmentManager__EquipPlayersItem         = 0x0; // 0x181228434
        std::uint64_t C_EquipmentManager__UnequipPlayersItem       = 0x0; // 0x182A03194
        std::uint64_t C_EquipmentManager__UnequipPlayersArmorSlots = 0x0; // 0x182A030F8
        std::uint64_t C_EquipmentManager__UnequipAllArmor          = 0x0; // 0x180478C90

        // Native item-pickup lifecycle and its non-trivial transfer descriptor.
        std::uint64_t C_ItemPickUpHelper__StartPickup               = 0x0; // 0x182A6A640
        std::uint64_t C_ItemPickUpHelper__CancelPickup              = 0x0; // 0x182A68A7C
        std::uint64_t C_ItemAttachmentManager__BeginItemTransfer    = 0x0; // 0x1808CEE04
        std::uint64_t C_ItemAttachmentManager__CompleteItemTransfer = 0x0; // 0x1808CF6FC

        // Visual-only actor item-handling request/action path used for remote
        // pickup presentation without invoking a gameplay transfer helper.
        std::uint64_t CreateSharedActorAnimRequestImpl                = 0x0; // 0x1808E2458
        std::uint64_t C_ActorAnimsItemHandlingExpansion__CreateAction = 0x0; // 0x1808CA900

        // Accepted player attack commit and the native actor-action replay
        // path used to present that same attack on a puppet.
        std::uint64_t C_CombatPlayerController__ReleaseAttack           = 0x0; // 0x180D4A99C
        std::uint64_t C_CombatActionAttackDatabase__Instance            = 0x0; // 0x18531EE80
        std::uint64_t C_CombatActionAttackFactory__CreateSelectedAttack = 0x0; // 0x1809158C4
        std::uint64_t C_Action__SetState                                = 0x0; // 0x18090D1B0

        // Resolves the actor's C_NPC (C_NPCManager::GetNPCByEntityId, slot 4),
        // or null for a body the manager never registered. AddItemToHand's
        // torch branch dereferences it blind.
        std::uint64_t GetActorAnimationProxy = 0x0; // 0x180953208

        // The two steps that move an item into a body's hand and record how
        // to put it back: C_HumanHandHelper::AddItemToHand (vtable slot 3)
        // and its per-hand twin for the handled item. Both ask IsLightItemById
        // and, for a light, read the hand's lit state off the owner's C_NPC
        // without checking it exists -- the torch branch.
        std::uint64_t AddHandledItemToHand             = 0x0; // 0x181ED6500
        std::uint64_t C_HumanHandHelper__AddItemToHand = 0x0; // 0x1829FFEEC
        std::uint64_t IsLightItemById                  = 0x0; // 0x1808F9E08

        // Sends item-handling state event 118 into the owner's movement
        // state machine and returns true only when a state answered by
        // installing the transition driver -- the root gate every native
        // held-item visual (spawn, hand attach, fire) sits behind.
        std::uint64_t C_ItemAttachmentManager__RequestItemHandling = 0x0; // 0x1808CC428

        // Completes the item-handling driver installed by RequestItemHandling.
        std::uint64_t C_ItemAttachmentManager__FinishItemHandling = 0x0; // 0x1808CA260

        // `C_ItemAttachmentManager::GetHandledItemId()` -- the one item the
        // body is handling, through explicit hidden return storage. It takes
        // no hand: a body handles one item at a time.
        std::uint64_t C_ItemAttachmentManager__GetHandledItemId = 0x0; // 0x1809D34E8

        // Exact torch put-away used by the player's R-toggle. It reads the
        // manager's typed torch WUID slot and drives native detach/cleanup.
        std::uint64_t C_ItemAttachmentManager__HolsterTorch = 0x0; // 0x1808EEA24

        // The CryModule tracked-allocator function pointer. A queued action is
        // released by the controller and freed on the game heap, so the mod
        // must allocate it there too.
        std::uint64_t CryModuleAllocFnPtr = 0x0; // 0x18549D378

        // Reflected C_Item `ItemClass` property getter.
        std::uint64_t C_UIFlashListItem__Construct        = 0;   // 0x1808f67c4
        std::uint64_t C_UIFlashInfoItem__Construct        = 0;   // 0x18055e528
        std::uint64_t C_UIApseInventory__BuildActionHint  = 0;   // 0x18055f9cc
        std::uint64_t C_UIApseInventory__ActivateItem     = 0;   // 0x1811a2dc8
        std::uint64_t InventoryActionCallback__Invoke     = 0;   // 0x181f493e0
        std::uint64_t InventorySecondaryActionCallback__Invoke = 0; // 0x181f47e70; Eat/Drink/Learn, before effects
        std::uint64_t C_Item__NotifyChanged               = 0;   // 0x1804663DC
        std::uint64_t C_Item__CloneForHolder              = 0;   // 0x1808f104c
        std::uint64_t C_ScriptBindPickableItem__GetUIName = 0;   // 0x182ABDBB4
        std::uint64_t C_Item__GetItemClassRaw = 0x0; // 0x182A696C0
        // The item's display name as a plain const char*, which is what
        // C_Item::GetName builds its std::string on top of.
        std::uint64_t C_Item__GetNameCStrRaw          = 0x0; // 0x1804796C4
        std::uint64_t C_Item__GetNameRaw              = 0x0; // 0x182A69C6C
        std::uint64_t C_Item__GetCategoryRaw          = 0x0; // 0x1807267DC
        std::uint64_t C_Item__GetTypeRaw              = 0x0; // 0x1819ACDF0
        std::uint64_t C_Item__GetSubtypeRaw           = 0x0; // 0x182A69D30
        std::uint64_t C_Item__GetNewUnitPriceRaw      = 0x0; // 0x18096F68C
        std::uint64_t C_Item__GetCurrentUnitPriceRaw  = 0x0; // 0x1804CCD18
        std::uint64_t C_Item__GetNewStackPriceRaw     = 0x0; // 0x18191DDF8
        std::uint64_t C_Item__GetCurrentStackPriceRaw = 0x0; // 0x1804CC1D8
        std::uint64_t C_Item__GetQualityRaw           = 0x0; // 0x180469A70
        std::uint64_t C_Item__GetConditionRaw         = 0x0; // 0x18096F7D4
        std::uint64_t C_Item__GetMaxQualityRaw        = 0x0; // 0x1808F6F34
        std::uint64_t C_Item__SetItemPhaseIdRaw       = 0x0; // 0x182A6B09C
        std::uint64_t C_Item__AdvanceItemPhaseRaw     = 0x0; // 0x182A689E4
        std::uint64_t C_Item__SetItemHealthRaw        = 0x0; // 0x1808D61B8
        std::uint64_t C_Item__WashItemRaw             = 0x0; // 0x182A6B758

        // A piece of armour's dirt and blood -- what its clothing is drawn
        // dirty and bloody from -- and the two setters that redraw it.
        std::uint64_t C_Item__GetOrCreateArmorRuntimeData = 0x0; // 0x180469484
        std::uint64_t C_ArmorRuntimeData__SetDirt         = 0x0; // 0x180470C60
        std::uint64_t C_ArmorRuntimeData__SetBlood        = 0x0; // 0x180AB0F08

        // The two discovery-status vtables, taken from the `lea` in each
        // class's constructor. Only the tables are wanted: their status getters
        // are folded field reads shared with unrelated classes, so the slot is
        // the only safe place to intercept them.
        std::uint64_t C_POI__DiscoveryVTable             = 0x0; // 0x183A64998
        std::uint64_t C_RPGLocation__DiscoveryVTable     = 0x0; // 0x183A7B310
        std::uint64_t C_POI__SetDiscoveryStatusAndNotify = 0x0; // 0x181FCCFA0
        std::uint64_t C_RPGLocationManager__VTable       = 0x0; // 0x183AB6400

        // Interns one C_FogOfWar per level name; cached lookups also pass here.
        std::uint64_t C_FOWManager__GetOrCreateFogOfWar = 0x0; // 0x180C3E5E0
        std::uint64_t C_FOWManager__Load              = 0x0; // 0x180C3D920, saved fog chunk reader
        std::uint64_t C_FOWManager__Reset             = 0x0; // 0x180C3E4CC, module-message level reset

        // Resolves one '@'-prefixed localization key, and the single choke
        // point for every key in the game: ILocalizationManager vtable slot 23,
        // reached both from LocalizeStringInternal and, through it, from every
        // Scaleform text field CryGFxTranslator hands over. On a miss the label
        // is copied through verbatim, which is why an unknown key renders as a
        // literal "@ui_something".
        std::uint64_t CLocalizedStringsManager__LocalizeLabel = 0x0; // 0x18051D468

        // C_Keybinds::ParseSuperactions, which turns the whole of
        // libs/config/keybindSuperactions.xml into the game's binding
        // vocabulary in one pass. Detoured to hand it a document with the
        // mod's own superactions in it, so those are rebindable, conflict
        // checked and saved to the profile exactly like the game's own.
        std::uint64_t C_Keybinds__ParseSuperactions = 0x0; // 0x1808FB4CC

        // CryStringT<char>::operator=(const char *). Call it through
        // SDK::CryStringT::Assign rather than directly.
        std::uint64_t CryStringT_char__AssignCStr = 0x0; // 0x1804F6868

        // CryStringT<char>'s constructor from a C string and its destructor,
        // the pair every native that builds a temporary string calls. Use them
        // through SDK::ScopedCryString rather than directly.
        std::uint64_t CryStringT_char__Construct = 0x0; // 0x1804F66CC
        std::uint64_t CryStringT_char__Destruct  = 0x0; // 0x1804FC624

        // SUIArguments' constructor, destructor and typed string append, so
        // an argument list the mod passes into a native is built and freed
        // on the game's heap. Use them through SDK::ScopedUIArguments.
        std::uint64_t SUIArguments__Construct            = 0x0; // 0x1807F7170
        std::uint64_t SUIArguments__Destruct             = 0x0; // 0x1807F71C8
        std::uint64_t SUIArguments__AppendStringWithType = 0x0; // 0x1803C2034

        // The C_UIFlashBase pair every native UI list goes through: set a
        // declared array -- from reflected row objects or from plain
        // arguments -- then invoke the declared function that reads it. Call
        // them through SDK::wh::guimodule::C_UIFlashBase.
        std::uint64_t C_UIFlashBase__SetFlashArray     = 0x0; // 0x1807F7114
        std::uint64_t C_UIFlashBase__SetFlashArguments = 0x0; // 0x1807F6FA4
        std::uint64_t C_UIFlashBase__CallFlashFunction = 0x0; // 0x180566BCC

        // Readable books. The typed flash call
        // and the uint append are the two halves of C_UIBook::SetFullBook the
        // mod replays; PushContentToUI is the one place every document hands
        // its content to the screen; Start/Finish bracket the reading
        // minigame; ApplyReadingStarted is the first-read XP and read marker.
        std::uint64_t C_UIFlashBase__CallFlashFunctionFloatIntInt = 0x0; // 0x182BB0DBC, CallFlashFunction<float,int,int>
        std::uint64_t SUIArguments__AppendUInt                    = 0x0; // 0x1803C1D4C
        std::uint64_t C_Actor__GetMinigame                        = 0x0; // 0x1804CEDF8

        // Stone throwing (docs/siege-defences.md). StartWithPile is the grab --
        // the only place the grab fragment is built -- and is hooked, as are
        // Finish (primary slot 5) and SetStoneUsageState, whose state 2 is the
        // throw itself. The pile is resolved and reset through its own natives.
        std::uint64_t C_StoneThrowing__StartWithPile             = 0x0; // 0x182EB14A8, hooked
        std::uint64_t C_StoneThrowing__Finish                    = 0x0; // 0x182EB2A9C, primary slot 5, hooked
        std::uint64_t C_StoneThrowing__RequestThrow              = 0x0; // 0x182EB2FDC, hooked
        std::uint64_t C_StoneThrowingPile__Find                  = 0x0; // 0x182EAB590
        std::uint64_t C_StoneThrowingPile__ResetStonePile        = 0x0; // 0x180D7DD78
        std::uint64_t C_StoneThrowingPile__SetActiveStoneCount   = 0x0; // 0x180D7F160
        std::uint64_t C_StoneThrowingPile__SetStoneUsageState    = 0x0; // 0x180D7F28C, hooked
        std::uint64_t C_StoneThrowingPile__PrepareStoneForGrab   = 0x0; // 0x180D7F074
        // The window and battlement lean (C_BattleArchery, minigame kind 10).
        std::uint64_t C_BattleArchery__Start                      = 0x0; // 0x182E5AF34
        std::uint64_t C_Battlement__GetUnoccupiedSlot             = 0x0; // 0x182E58B6C
        std::uint64_t C_BattlementManager__TryStartBattleArchery  = 0x0; // 0x180DCB804, I_BattlementManager slot 0, hooked
        std::uint64_t C_Player__CanReadBookHere                   = 0x0; // 0x180A511E0
        std::uint64_t C_Reading__Start                            = 0x0; // 0x182EB1184
        std::uint64_t C_Reading__Finish                           = 0x0; // 0x182EB26D8, primary slot 5, hooked
        std::uint64_t C_Reading__ReturnToInventory                = 0x0; // 0x182EAF924, hooked
        std::uint64_t C_DocumentItem__PushContentToUI             = 0x0; // 0x182A6AEBC, hooked
        std::uint64_t C_RPGMinigames__ApplyReadingStarted         = 0x0; // 0x182CFB2AC, primary slot 38, hooked

        // The two halves of the map's "checkpoint" action -- the marker the
        // player drops on the map. The action toggles between them, so
        // together they see every placement, move and clear. The placer takes
        // the cursor's world x and y from the flash movie and z from
        // I3DEngine::GetTerrainElevation, so its position is world metres.
        std::uint64_t C_UIMap__PlaceCheckpointAtCursor = 0x0; // 0x182AFDC74
        std::uint64_t C_UIMap__RemoveCheckpoint        = 0x0; // 0x182B00ACC

        // The finder is a member returning a shared_ptr by value and the
        // reference it answers is owned; call both through SDK::C_UIMap.
        std::uint64_t C_UIMap__HasCheckpoint               = 0x0; // 0x180DCB430
        std::uint64_t C_UIMap__FindCheckpointForCurrentMap = 0x0; // 0x180DCB460

        // The Apse's primary vtable. GetUIApse shares its prologue with ten
        // sibling GetPage<T> instantiations, so the page is found by vtable
        // through C_GUIModule::FindPage instead; C_UIMap sits in it at +0x1508.
        std::uint64_t C_UIApse__VTable = 0x0; // 0x183ED9140

        // I_UIApse slot 2, `Hide(bool immediate)`, with `this` the interface
        // at C_UIApse+0x10: the one exit every Apse close goes through -- the
        // `apse:close` key, a view toggled shut, and anything the mod asks.
        std::uint64_t I_UIApse__Hide = 0x0; // 0x180C44F48
        std::uint64_t I_UIApse__Show = 0x0; // 0x181F4AA00, consumes its callback even on refusal.
        std::uint64_t C_UIApseCamera__MoveToView = 0x0; // 0x180C42E18, called before ChangeView by navigation.

        // `C_UIApse::ChangeView(E_UIApseView::Type, bool)`, `this` the Apse
        // itself: every move between pages once the screen is up -- the tab
        // keys, `SwitchView`, the panels' own buttons -- writes the view
        // through here. `Show` sets its first view without it.
        std::uint64_t C_UIApse__ChangeView = 0x0; // 0x180C43154

        // `C_Actor::CopyForInventoryPreview(C_Actor *source)`, run on a dummy
        // each time the Apse character page registers it as a mirror of the
        // player: clones the player's inventory into the dummy's own, then
        // copies the body description onto its look-as layer.
        std::uint64_t C_Actor__CopyForInventoryPreview = 0x0; // 0x1808F08E0

        // `C_UIApseCharacter::BindCharacterHub()`: binds the stage's links --
        // `DummyPlayer` among them -- from the level's prefab. The character
        // page's show runs it first and then dresses and places the figure,
        // so the slot holds the prefab's man again on every show, and is
        // empty until the first.
        std::uint64_t C_UIApseCharacter__BindCharacterHub = 0x0; // 0x1816E9F8C

        // `C_UIApseCamera::GetCameraParams(SViewParams *)`: the pose the
        // pause screen's camera hands the camera manager -- position,
        // identity rotation, FOV in radians. Its only caller is the camera's
        // table (0x183B8F420).
        std::uint64_t C_UIApseCamera__GetCameraParams = 0x0; // 0x1814E40A0

        // `CKeyboard::ChangeLanguage(HKL)`: records the layout at this+0xB0
        // and rebuilds the per-scancode character table every native key
        // label reads. The engine only runs it from CKeyboard::Init.
        std::uint64_t CKeyboard__ChangeLanguage = 0x0; // 0x1813B784C

        // An offscreen pass into a texture-backed display context, the way
        // Sandbox's QViewport draws one model: the pass builder (a static
        // returning by value), the renderer's context lookup, the camera's
        // constructor and frustum, and a light's constructor and setters.
        std::uint64_t SRenderingPassInfo__CreateGeneralPassRenderingInfo = 0x0; // 0x18043E940
        std::uint64_t CRenderer__FindDisplayContext                      = 0x0; // 0x1805719C4
        std::uint64_t CCamera__Construct                                 = 0x0; // 0x180584004
        std::uint64_t CCamera__SetFrustum                                = 0x0; // 0x18053909C
        std::uint64_t SRenderLight__Construct                            = 0x0; // 0x1805A337C
        std::uint64_t SRenderLight__Destruct                             = 0x0; // 0x1805A3B10
        std::uint64_t SRenderLight__SetLightColor                        = 0x0; // 0x1805A3720
        std::uint64_t SRenderLight__SetRadius                            = 0x0; // 0x1837DE53C

        // The map legend's category toggle, and the only way to re-evaluate
        // marks of one type that are already placed. Takes the NORMALIZED type
        // index, not a raw E_CompassMarkType.
        std::uint64_t C_UIMap__SetPoiMarkersOfTypeVisible = 0x0; // 0x182B02B34

        // Redraws the checkpoint on the map screen. A move writes the mark's
        // Vec3 in place, which the compass reads live but the screen does not.
        std::uint64_t C_UIMap__SyncCheckpointToFlash = 0x0; // 0x180DCB1A0

        // The one choke point on the map screen coming up and going away: it
        // owns both writes to the map-up flag at +0x748, and the only code
        // caller of the show path (0x1819D53C0) is inside it. Hooked so the
        // screen's edges are signalled rather than polled for.
        std::uint64_t C_UIMap__OnUIStateChanged = 0x0; // 0x180562300

        // Rebuilds the map screen's POI rows and legend from the game's own
        // mark list. Its one caller, C_UIMap::PopulateAndOpen (0x180DC95D4),
        // runs it on every open with the movie loaded, so it is detoured to
        // push the mod's rows right after the game's.
        std::uint64_t C_UIMap__RefreshPoiMarkers = 0x0; // 0x180A1AE28
        // A map label's RPG location; only the label code calls it. Hooked.
        std::uint64_t C_UIMap__FindMapLabelLocation = 0x0; // 0x180D104A8

        // Builds one map POI's flash row from an S_EntityMapMark, reading its
        // id, type, state and source and registering nothing. Call it through
        // SDK::wh::guimodule::MakeFlashMapPoiMarker.
        std::uint64_t MakeFlashMapPoiMarker = 0x0; // 0x1815B9804

        // Whether a fast travel may start, as an E_TravelStartResult. Starts
        // nothing. Call it through SDK::wh::playermodule::C_FastTravel.
        std::uint64_t C_FastTravel__ValidateTravelStart = 0x0; // 0x182DDE634

        // The map screen's fast-travel offer, raised nowhere else: the
        // `start_fast_travel` hint and the route line Flash draws. Runs on
        // POI selection, taking the page and the POI's GUID string by value.
        std::uint64_t PrepareFastTravelToPoi = 0x0; // 0x182AFFE7C

        // The same page's teardown for a selection that travels nowhere.
        // Same two arguments, and it destroys the GUID string, so it is a
        // drop-in for the offer.
        std::uint64_t ClearFastTravelPoi = 0x0; // 0x18161BCBC

        // The game's own shared_ptr release. Anything borrowed out of a
        // game-owned shared_ptr goes back through it: the control block's
        // destroy and delete are virtuals that must run on the heap that
        // allocated the object.
        std::uint64_t std__Ref_count_base__Decref = 0x0; // 0x1804F6588

        // The HUD compass and its marks. GetUICompass takes the GUI module at
        // WHGlobalEnvironment +0xE8 and memoizes its answer, so the compass is
        // one object for the process; MakeCompassMark returns the only
        // reference to a new mark, and AddMark and RemoveMark each consume
        // one. Call them through SDK::C_UICompass and SDK::MakeCompassMark.
        //
        // There is no type setter: the icon name is read once, at
        // registration, so a new icon means a new mark.
        std::uint64_t GetUICompass                = 0x0; // 0x180C4E098
        std::uint64_t MakeCompassMark             = 0x0; // 0x181F75F30
        std::uint64_t C_UICompass__AddMark        = 0x0; // 0x180C4D1E8
        std::uint64_t C_UICompass__RemoveMark     = 0x0; // 0x180A193C0
        std::uint64_t C_UICompass__RemoveAllMarks = 0x0; // 0x18197B198
        std::uint64_t C_CompassMark__SetState     = 0x0; // 0x181F52390

        // --- Combat --------------------------------------------------------
        //
        // The whole of an unarmed swing and of the arm guard, as the engine
        // itself performs them. All four are members of the fighter's own
        // combat side rather than of the player's controller, which is what
        // lets a body driven from the network use them: the automation drives
        // an NPC through exactly the same calls.

        // `C_Actor::GetOrCreateCombatActor()` -- the combat side is built
        // lazily, so `C_Actor + 0x278` is null on any body that has never
        // fought, which is every puppet. Builds it, runs its initialiser and
        // destroys it again if that fails.
        std::uint64_t C_Actor__GetOrCreateCombatActor = 0x0; // 0x18072DC90

        // `C_Actor::IsPlayerControlled()` -- the game object's network channel
        // id is non-zero, i.e. driven by a player rather than by AI. NOT "is
        // the local player". The attack factory's unopposed row selection
        // (0x1818A4B7C) returns nothing when this fails.
        std::uint64_t C_Actor__IsPlayerControlled = 0x0; // 0x1804A8CE0
        // Return addresses of the IsPlayerControlled calls that mean "this
        // machine's player" (NativePlayerModule::IsLocalPlayerSite).
        std::uint64_t LocalPlayerSite__RefreshRiderMagnetismController = 0x0; // 0x180A4EA0E
        std::uint64_t LocalPlayerSite__EndInteractiveAction            = 0x0; // 0x1807021DE
        std::uint64_t LocalPlayerSite__StartInteractiveActionByName    = 0x0; // 0x1807022C2
        std::uint64_t LocalPlayerSite__StartJumpAction                 = 0x0; // 0x180CF8069
        std::uint64_t LocalPlayerSite__BeginCorpsePutDown              = 0x0; // 0x1828601D5
        std::uint64_t LocalPlayerSite__UnregisterDBAGroups             = 0x0; // 0x18227DDD3
        std::uint64_t LocalPlayerSite__RiderMagnetismFollow            = 0x0; // 0x1829F1CDC
        std::uint64_t LocalPlayerSite__ScriptIsPlayer                  = 0x0; // 0x182ABE463
        std::uint64_t LocalPlayerSite__CanStartChatWithSelf            = 0x0; // 0x18082B306
        std::uint64_t LocalPlayerSite__CanStartChatWithCounterpart     = 0x0; // 0x18082B31B
        std::uint64_t LocalPlayerSite__CanStartChatWithParticipant     = 0x0; // 0x18082B372
        std::uint64_t LocalPlayerSite__HorseScaredTutorial             = 0x0; // 0x180CF8323
        std::uint64_t LocalPlayerSite__UnconsciousWake                 = 0x0; // 0x182BDD88E
        std::uint64_t LocalPlayerSite__IsThirdPerson                   = 0x0; // 0x1804A8C9B
        std::uint64_t LocalPlayerSite__SpeedFeedbackSound              = 0x0; // 0x180F41CDA
        std::uint64_t LocalPlayerSite__AuxTorchLight                   = 0x0; // 0x1804CE333
        std::uint64_t LocalPlayerSite__TorchLightArchetype             = 0x0; // 0x1804CE5D8
        std::uint64_t LocalPlayerSite__FirstPersonViewState            = 0x0; // 0x181352009
        std::uint64_t LocalPlayerSite__ShootingAimSway                 = 0x0; // 0x1828A130B
        std::uint64_t LocalPlayerSite__StartInteractiveAction          = 0x0; // 0x182861AD1
        std::uint64_t LocalPlayerSite__CombatStealthInput              = 0x0; // 0x1810F0840
        // The stone throwing minigame's player-only branches, and the shared
        // C_Minigame ones it reaches: input, camera, save lock, action maps and
        // the interactor's forced target. A remote player's body replays a
        // throw through the same minigame and must touch none of them.
        std::uint64_t LocalPlayerSite__StoneThrowingStart             = 0x0; // 0x182EB1595
        std::uint64_t LocalPlayerSite__StoneThrowingInputPhase        = 0x0; // 0x182EB000E
        std::uint64_t LocalPlayerSite__StoneThrowingInteractionAction = 0x0; // 0x182EB3D66
        std::uint64_t LocalPlayerSite__StoneThrowingFinishInput       = 0x0; // 0x182EB2B5E
        std::uint64_t LocalPlayerSite__StoneThrowingFinishControl     = 0x0; // 0x182EB2CD7
        std::uint64_t LocalPlayerSite__MinigameBeginInteraction       = 0x0; // 0x1809F0A20
        std::uint64_t LocalPlayerSite__MinigamePrepareInteraction     = 0x0; // 0x1806188D1
        std::uint64_t LocalPlayerSite__MinigameEndInteraction         = 0x0; // 0x1809F11D0
        std::uint64_t LocalPlayerSite__MinigameReleasePlayerInput     = 0x0; // 0x1809F120C
        std::uint64_t LocalPlayerSite__MinigameSetPlayerInputEnabled  = 0x0; // 0x180B68181
        std::uint64_t LocalPlayerSite__MinigameClearActionMap         = 0x0; // 0x1809F1E52
        std::uint64_t LocalPlayerSite__MinigameSetActionMap           = 0x0; // 0x18085A43C
        std::uint64_t LocalPlayerSite__MinigameSetInteractionTarget   = 0x0; // 0x1819B53E2
        std::uint64_t LocalPlayerSite__MinigameClearInteractionTarget = 0x0; // 0x1819D1C56
        // The lean's own player-only branches: Start binds this machine's
        // input and action map, the state controls set its camera limits,
        // Finish releases them and places the body at the slot's exit.
        std::uint64_t LocalPlayerSite__BattleArcheryStart    = 0x0; // 0x182E5B0AD
        std::uint64_t LocalPlayerSite__BattleArcheryControls = 0x0; // 0x182E5A39C
        std::uint64_t LocalPlayerSite__BattleArcheryFinish   = 0x0; // 0x182E5B6CA
        std::uint64_t LocalPlayerSite__MovementAudioSuffix = 0x0; // 0x18157A719
        std::uint64_t LocalPlayerSite__MaterialMovementAudio = 0x0; // 0x180B70CE6
        std::uint64_t LocalPlayerSite__BodyfallAudio = 0x0; // 0x180B7020C
        std::uint64_t LocalPlayerSite__CombatTimewarp      = 0x0; // 0x1810C0C16
        std::uint64_t LocalPlayerSite__CombatTimewarpFx    = 0x0; // 0x1810C0C8E
        std::uint64_t LocalPlayerSite__HorseMovementAudio  = 0x0; // 0x18147C9FB
        // C_ActorConditionController::Init leaves the player's body without
        // the controller that sets its Mannequin `ActorCondition` tag -- the
        // drunk, injured and armored walks every other body gets.
        std::uint64_t LocalPlayerSite__ActorConditionControllerInit = 0x0; // 0x180D5A119

        // `C_CombatActor::SetAttackInputClass(inputClassId, active)` -- writes
        // the model's input class at +0x240 and its active flag at +0x2A0.
        // With request bit 5 this is what moves a fighter into
        // ReadingToStrike, which the release half needs. Cleared with -1.
        std::uint64_t C_CombatActor__SetAttackInputClass = 0x0; // 0x18093A28C

        // `C_CombatPlayerController::BeginAttack(inputClassId)` -- the press.
        // A KCD2 swing is both halves, so both are replicated.
        std::uint64_t C_CombatPlayerController__BeginAttack = 0x0; // 0x180D4AAC4

        // `C_CombatPlayerController::UpdateAttackTarget(allowExtendedRange)`
        // -- the local player's per-frame target pick: `FindAttackTarget`
        // (0x180686A50) over the candidate vector, then the fighter's
        // `SetTarget` with what it found or with nothing.
        std::uint64_t C_CombatPlayerController__UpdateAttackTarget = 0x0; // 0x1809D411C

        // Clears an uncommitted local-player attack input. Successful release
        // reaches it after PerformAttack has already queued the action.
        std::uint64_t C_CombatPlayerController__AbortAttack = 0x0; // 0x180939C84

        // `C_CombatPlayerController::BeginGuard()` / `EndGuard()` -- the block
        // key's press and release, as `C_CombatPlayerInput::OnBlock`
        // (0x1827C07B8) makes them.
        std::uint64_t C_CombatPlayerController__BeginGuard = 0x0; // 0x1827C29E0
        std::uint64_t C_CombatPlayerController__EndGuard   = 0x0; // 0x1827BCE04

        // `wh::shared::C_ModelArrayPropertyBool::SetElement(index, value)` --
        // one of the eight combat request bits at C_CombatActorModel+0xB58.
        // C_CombatActor::PostUpdate ends combat mode once the running action
        // finishes and no bit is set, so a held guard has to hold its bit.
        std::uint64_t C_ModelArrayPropertyBool__SetElement = 0x0; // 0x18093A3B0

        // The `combat_action_type` row named `freeGuard`, which the player's
        // own path puts its fists up through. A field of the action-type
        // database and -1 until it loads, so read it at the moment of use.
        std::uint64_t CombatActionTypeId__FreeGuard = 0x0; // 0x185320D18

        // The two brain ticks, `C_AIBrain*::Update`, slot 7 of each concrete kind.
        // Every NPC's thinking happens inside one of them.
        std::uint64_t C_AIBrainMultiSubb__Update  = 0x0; // 0x1807600B4
        std::uint64_t C_AIBrainSingleSubb__Update = 0x0; // 0x1807F942C

        // `C_CombatActor::TryEnterCombatMode(optionalMode)` -- the one gate a body
        // passes through to put its fists up, and it has no bypass: the entry
        // virtual it calls (slot 216) is the only caller of `EnterCombatMode`.
        std::uint64_t C_CombatActor__TryEnterCombatMode = 0x0; // 0x180C5A1BC

        // `C_CombatActor::LeaveCombatMode()` -- what puts a body's fists back
        // down, and the edge a puppet's lost stance is noticed on.
        std::uint64_t C_CombatActor__LeaveCombatMode = 0x0; // 0x180D5C6F0

        // Automatic empty-combat-queue exit, distinct from explicit stand-down.
        std::uint64_t C_CombatActor__PostUpdate = 0x0; // 0x181002E08
        std::uint64_t C_CombatActorClinch__IsPairedAttackAlignmentValid = 0x0; // 0x18072E234

        // `C_CombatActor::LeaveCombatModeIfActive()` (slot 211, the body both
        // the fighter and player tables point at) -- the guarded exit: leaves
        // through the LeaveCombatMode virtual only while in combat mode.
        std::uint64_t C_CombatActor__LeaveCombatModeIfActive = 0x0; // 0x1810030F8

        // `C_CombatActor::RequestFightingStance(request)` -- fists up or down,
        // the pose the punch and the guard both come out of. Runs as an actor
        // action on C_Actor+0x280, so it is asynchronous;
        // C_CombatActorModel+0x008 says when the stance has landed.
        std::uint64_t C_CombatActor__RequestFightingStance = 0x0; // 0x1812EE110

        // `C_CombatActor::RequestGuard(request, source)` -- the arm guard.
        //
        // Writes one of the five sources of the model's guard property at
        // `C_CombatActorModel + 0x630`, and when the highest of the five
        // moves, builds and runs the matching guard action. Called for every
        // fighter in the world, so a detour on it sees every guard that goes
        // up or down without anything having to be polled.
        std::uint64_t C_CombatActor__RequestGuard = 0x0; // 0x18093A4D4

        // `C_CombatActor::SetCombatZone(zoneId, reason, alsoSetPrevious)` --
        // the direction of the combat star.
        //
        // Writes the model's zone at +0x0F0 and fires its signal, which is
        // what the attack and guard factories read when they pick a fragment.
        // Also called for every fighter, so the local player's aim arrives
        // here rather than being sampled.
        std::uint64_t C_CombatActor__SetCombatZone = 0x0; // 0x18090F824

        // `C_CombatActor::RunAction(handle)` queues a combat action. This is
        // distinct from the `C_ActionActor::QueueAction` vtable slot.
        //
        // NOTE the queue is the second `C_ActionActor`, at
        // `C_CombatActor + 0x200`, not the `C_Actor + 0x280` one that carries
        // the rider action. It AddRefs the handle for itself, so the caller
        // still owns the reference it passed in.
        std::uint64_t C_CombatActor__RunAction = 0x0; // 0x18090C850

        // `C_CombatActorAction::CanBeReplacedBy(const I_CombatActorActionPrivate
        // *const *incoming) const` -- the replace test RunAction asks the
        // action running in the slot (vtable slot 7, shared by every combat
        // action): true when nothing comes in or the incoming action's state
        // is above the running one's (+0x38), as a signed compare.
        std::uint64_t C_CombatActorAction__CanBeReplacedBy = 0x0; // 0x1814962EC

        // `C_CombatActorActionAttack`'s slot 5 of the helper-owner interface
        // it carries at +0x88: takes a combo's next strike. The helper's combo
        // handler (0x18164A4A8) builds the strike through CreateAttackAction
        // and hands it here, not to RunAction; it is stored as the running
        // attack's next action (`C_Action::HasNextAction`) only while there is
        // none, and starts when the running attack ends. The sync attack's
        // helper-owner vtable shares this slot.
        std::uint64_t C_CombatActorActionAttack__QueueNextAttack = 0x0; // 0x18164A5E0

        // `C_CombatActionAttackFactory::CreateAttackAction(&out, inputClass,
        // flags)` -- builds the swing.
        //
        // `this` is the factory object stored inline at action manager +0x50,
        // not a handle to it. Reads the zone off the model, answers null when
        // the fighter may not swing, and otherwise hands back a handle
        // carrying one reference.
        std::uint64_t C_CombatActionAttackFactory__CreateAttackAction = 0x0; // 0x180939310

        // `C_CombatPlayerController::PerformAttack(inputClass)` -- the local
        // player's committed swing, reached when the left mouse button is
        // released with a charge held.
        //
        // Detoured rather than the factory above because it is the one place
        // that means "this player has just thrown a punch": the factory is
        // also entered for combos and for the AI.
        std::uint64_t C_CombatPlayerController__PerformAttack = 0x0; // 0x180D4B06C

        // `C_CombatActorOpponentManager::SetOpponent(IEntity *)` -- who this
        // fighter is squared up against.
        //
        // The pointer it publishes at `C_CombatActorModel + 0xCF8` is what
        // every melee path opens by reading: `ProcessHit` answers "no contact"
        // without one, so a body with no opponent cannot land a blow at all.
        // There is no faction test in here, which is what lets two players be
        // set against each other.
        std::uint64_t C_CombatActorOpponentManager__SetOpponent = 0x0; // 0x180B2158C

        // `C_CombatActorStealth::RequestStealthAttack(victimEntityId, kind)`,
        // entered through the I_CombatActorStealth base at +0x18 -- the only
        // way into `ExecutePendingStealthAttack` (0x18275F2EC).
        //
        // The pending attack pairs the two bodies through
        // `C_CombatActor::SetPairedActor`, which is `SetOpponent` underneath,
        // and then enters a guard whose stealth signal reads the attacker's
        // opponent at `C_CombatActorModel + 0xCF8` without a null test. It
        // answers false on its own for a missing or flag-0x40 victim, so a
        // false here is an outcome every caller already handles.
        std::uint64_t C_CombatActorStealth__RequestStealthAttack = 0x0; // 0x18275EF4C

        // `C_CombatActorTimewarp::BeginTimewarp()` -- the combat slow-motion.
        //
        // Gated on `C_Actor::IsPlayerControlled` for the fighter or its
        // opponent, and that predicate is the game object's network channel
        // id. Every replicated player body answers true to it, so a fight this
        // peer is only watching would scale this peer's global timer.
        std::uint64_t C_CombatActorTimewarp__BeginTimewarp = 0x0; // 0x1810C0BEC

        // `C_CombatActorSlotPlanner::DispatchDueSlots(&entries, model,
        // planner)` -- the body of the planner's update.
        //
        // Posts each expired entry onto the *opponent's* director: the
        // attacker schedules the defender's reaction. Choreography, and the
        // wrong machine's decision as soon as the defender is another player.
        std::uint64_t C_CombatActorSlotPlanner__DispatchDueSlots = 0x0; // 0x181E3D7B0

        // `C_CombatActorDirector::FindSlotObject(kind)` -- the window object
        // of one kind a fighter keeps, which DispatchDueSlots looks up on the
        // opponent before handing it to `AddTriggerForOpponent`, the call that
        // opens the window. Also reached from inside a window's own activation.
        std::uint64_t C_CombatActorDirector__FindSlotObject            = 0x0; // 0x180B277A0
        std::uint64_t C_CombatActorMultiTrigger__AddTriggerForOpponent = 0x0; // 0x180914204
        std::uint64_t C_CombatMultiTriggerBlock__VTable                = 0x0; // 0x183AECD20

        // `C_CombatActor::RequestCombatZone(zone)` -- the player input's way
        // of turning towards a direction, guard side and zone signal included.
        std::uint64_t C_CombatActor__RequestCombatZone = 0x0; // 0x180938E94

        // `C_CombatActor::TryApplyCombatZone(zone, side)` -- what the zone
        // request runs where it applies a zone at once: side and zone.
        std::uint64_t C_CombatActor__TryApplyCombatZone = 0x0; // 0x180936A28

        // `C_CombatActor::RequestRiposte()` -- the counter after a perfect
        // block, asked for while the counter window is open; and the bool
        // model-property setter it raises the request through.
        std::uint64_t C_CombatActor__RequestRiposte = 0x0; // 0x181899D6C
        std::uint64_t C_ModelPropertyBool__SetValue = 0x0; // 0x18072FA34

        // `C_CombatActorAnimCollisions::ProcessHit()` -- one melee contact.
        //
        // Reached from the pending-hit update once the clip has armed the latch
        // and the model's countdown at +0x450 has run out, so it is the contact
        // instant itself rather than a per-frame sample. It opens by reading
        // the opponent at `C_CombatActorModel + 0xCF8` and returns without one.
        // C_CombatActionHelperAttack::Enter subscribes the attack's collision
        // process to the record it broadcasts.
        std::uint64_t C_CombatActorAnimCollisions__ProcessHit = 0x0; // 0x180D530D8

        // `C_CombatActorAnimCollisions::ArmContact(pending, forced, sequence,
        // context, keepIndex)` -- what makes a swing able to raise a contact.
        //
        // Nothing raises a contact until this has run: it sets the enabled byte
        // at +0x28, the pending latch at +0x29 that UpdatePendingHit waits on,
        // and the forced byte at +0x2A. Five of its seven callers are the
        // C_CombatActorActionSync* family passing forced = 1; the sixth is
        // C_CombatActionHelperAttack::TryArmAttackContact, whose four-way
        // condition is the ordinary swing's only route here.
        std::uint64_t C_CombatActorAnimCollisions__ArmContact = 0x0; // 0x180D4B610

        // `C_CombatSoul::ProcessCombatHit(S_EventHandle &, const
        // S_CombatHitData &)` -- the melee damage choke point.
        //
        // The only caller of C_EventFactory::CreateCombatHitEvent, and the
        // receiver is proven by its vtable position immediately before
        // C_CombatSoul::ProcessMissileHit, whose shape it mirrors. Every blow
        // that becomes damage passes through here whatever raised it. Note it
        // is byte-identical to ProcessMissileHit for its first 0x44 bytes, so
        // the signature has to run past the prologue to separate them.
        std::uint64_t C_CombatSoul__ProcessCombatHit = 0x0; // 0x180726DF4

        // --- Player versus player -------------------------------------------
        //
        // A blow is resolved on one machine only: the one where the victim is
        // the real body. Everything below is a seam that either makes the
        // engine's own fight happen between two players or keeps a peer from
        // resolving a contact it does not own.

        // The two collision-process hit handlers, `(process, record)`. The
        // geometric one (victim at record +0xC8) carries an ordinary swing; the
        // animation one (victim at record +0x90) the contacts armed through
        // C_CombatActorAnimCollisions. Everything a contact produces -- the
        // victim's hit reaction, the attacker's FailedAttack, the damage event --
        // happens inside one of these.
        std::uint64_t C_CombatActorCollisionProcess__OnGeometricHit = 0x0; // 0x180C5235C
        std::uint64_t C_CombatActorCollisionProcess__OnAnimationHit = 0x0; // 0x180D53778

        // One weapon shape's sweep for one step of a swing, against the world
        // and against bodies alike. It clears the actor's contact records,
        // runs the scene query, and leaves the first contact with something
        // this attack had not touched in C_CombatActorCollisions +0x170.
        std::uint64_t C_CombatActorCollisions__SweepWeaponShape = 0x0; // 0x180A7A124

        // A contact's impact, which only the machine that resolved it plays.
        // The hit record's constructor, `CGameRules::QueueHitRecord(record)`
        // (slot 165) the collision handler ends with, and the victim-side
        // `PlayHitMaterialEffect(unused, record)` -- the clash or armour sound,
        // sparks and blood as one material effect.
        std::uint64_t S_CombatHitRecord__Construct        = 0x0; // 0x180B2454C
        std::uint64_t CGameRules__QueueHitRecord          = 0x0; // 0x180725698
        std::uint64_t CombatHitRecord__PlayMaterialEffect = 0x0; // 0x180B24A0C

        // `C_CombatRPG::IsHostileTo(const C_CombatActor *)` -- whether the
        // local player's opponent picker may offer this fighter. A faction
        // relation, so two players are never hostile natively.
        std::uint64_t C_CombatRPG__IsHostileTo = 0x0; // 0x18082A334

        // The picker's per-candidate refusal, `(list, actor)`. Its first test
        // skips any IsPlayerControlled actor, which is every replicated body.
        std::uint64_t CombatOpponentCandidates__ShouldSkip = 0x0; // 0x1809DFE38

        // The picker's per-frame candidate refresh, `(list, dt)`, and the grid
        // callback it feeds each actor through, `(&list, actor)`.
        std::uint64_t CombatOpponentCandidates__Update      = 0x0; // 0x1808315BC
        std::uint64_t CombatOpponentCandidates__OnCandidate = 0x0; // 0x181E571B0

        // `C_CombatActorTimeCop::BeginAttackTimeline(params)` -- schedules a
        // swing's start, hit and withdraw; `ApplySlotDelay(seconds, unused,
        // blend)` pushes the hit later and slows the animation to match.
        std::uint64_t C_CombatActorTimeCop__BeginAttackTimeline = 0x0; // 0x180D4D950
        std::uint64_t C_CombatActorTimeCop__ApplySlotDelay      = 0x0; // 0x1813B7D20

        // The per-combat-group recursive lock the engine holds around
        // ApplySlotDelay, and its acquire/release.
        std::uint64_t C_CombatActor__GetCombatGroupLock = 0x0; // 0x1823D0D20
        std::uint64_t RecursiveSRWLock__Acquire         = 0x0; // 0x1803D902C
        std::uint64_t RecursiveSRWLock__Release         = 0x0; // 0x18042DC54

        // The 3D engine's layer physics owner, the step that turns one layer
        // file's records into colliders -- once per build, whether the file
        // was read synchronously or streamed -- and the one place they are
        // destroyed, for one layer or, through DeactivateAllLayers, all. See
        // SDK::wh::engine3d::C_PhysicalizationManager.
        std::uint64_t C_PhysicalizationManager__Instance            = 0x0; // 0x185496F38
        std::uint64_t C_PhysicalizationManager__BuildLayerColliders = 0x0; // 0x180845740
        std::uint64_t C_PhysicalizationManager__DeactivateLayer     = 0x0; // 0x180A60470

        // `C_CombatRPG::GetAttackSpeedMultiplier()` -- the time scale of a
        // swing's timeline. Its only caller is BeginAttackTimeline.
        std::uint64_t C_CombatRPG__GetAttackSpeedMultiplier = 0x0; // 0x180D4DD2C

        // A dry run of a blow against the opponent that answers whether it
        // would kill; it picks the lethal variant of a sync attack or defense.
        std::uint64_t CombatRPG__WouldKillOpponent = 0x0; // 0x180C62288

        // The allocators of the defense-side actions, `(&handle, owner)`, each
        // handing back one reference, and the time alignment every perfect
        // block build ends with, `(factory, &action->params)`.
        std::uint64_t C_CombatActorActionPerfectBlock__Allocate         = 0x0; // 0x18164F0E4
        std::uint64_t C_CombatActorActionSyncPerfectBlock__Allocate     = 0x0; // 0x1814840F4
        std::uint64_t C_CombatActorActionFailedAttack__Allocate         = 0x0; // 0x182749BA0
        std::uint64_t C_CombatActorActionHit__Allocate                  = 0x0; // 0x180D49C28
        std::uint64_t C_CombatActorActionBlock__Allocate                = 0x0; // 0x180C559F8
        std::uint64_t C_CombatActionPerfectBlockFactory__ApplyAlignment = 0x0; // 0x181483378

        // The dodge factory's row query, `(factory, &query, &params)`, and its
        // builder, `(factory, &handle, &params)`.
        std::uint64_t C_CombatActionDodgeFactory__Query  = 0x0; // 0x18090B30C
        std::uint64_t C_CombatActionDodgeFactory__Create = 0x0; // 0x181482FAC

        // Primary vtables, which is how a queued action says what it is.
        std::uint64_t C_CombatActorActionAttack__VTable              = 0x0; // 0x183A61C58
        std::uint64_t C_CombatActorActionPerfectBlock__VTable        = 0x0; // 0x183BE12C0
        std::uint64_t C_CombatActorActionSyncPerfectBlock__VTable    = 0x0; // 0x183B933B0
        std::uint64_t C_CombatActorActionSyncPerfectBlockHit__VTable = 0x0; // 0x183E40740
        std::uint64_t C_CombatActorActionSyncAttack__VTable          = 0x0; // 0x183A8B608
        std::uint64_t C_CombatActorActionSyncHit__VTable             = 0x0; // 0x183A993D8
        std::uint64_t C_CombatActorActionSyncTransition__VTable      = 0x0; // 0x183B0B2C0
        std::uint64_t C_CombatActorActionFailedAttack__VTable        = 0x0; // 0x1846F61A0
        std::uint64_t C_CombatActorActionHit__VTable                 = 0x0; // 0x183A989B0
        std::uint64_t C_CombatActorActionBlock__VTable               = 0x0; // 0x183A89688
        std::uint64_t C_CombatActorActionDodge__VTable               = 0x0; // 0x183B92428

        // The static CombatDb tables the defense-side actions are built from.
        std::uint64_t C_CombatActionPerfectBlockDatabase__Instance        = 0x0; // 0x18531F820
        std::uint64_t C_CombatActionFailedAttackDatabase__Instance        = 0x0; // 0x18531F2D0
        std::uint64_t C_CombatActionHitDatabase__Instance                 = 0x0; // 0x18531F600
        std::uint64_t C_CombatActionSyncPerfectBlockHitDatabase__Instance = 0x0; // 0x18531FC60
        std::uint64_t C_CombatActionBlockDatabase__Instance               = 0x0; // 0x18531EFA0
        std::uint64_t C_CombatActionDodgeDatabase__Instance               = 0x0; // 0x18531F1C0

        // `C_ActorSpeakMan::Speak(const S_SpeakData &)`. Plays a voice only
        // when the sound name is set, and arms the timer the update draws on.
        std::uint64_t C_ActorSpeakMan__Speak = 0x0; // 0x180954CB8

        // `C_UIHudBubbles::SetBubblePosition(unsigned, const Vec3 &, float)`.
        // The one write that places a bubble. `this` is the I_UIHudBubbles
        // sub-object, hence the walk back 0x58 in its body.
        std::uint64_t C_UIHudBubbles__SetBubblePosition = 0x0; // 0x1814BEA44

        // `C_ActorSpeakMan::HideBubble()`. Releases the bubble and resets the
        // smoothing. Needed between lines: text is pushed only while id is 0.
        std::uint64_t C_ActorSpeakMan__HideBubble = 0x0; // 0x180955CA0

        // `C_UISubtitles::SetText(const CryStringT &text, const CryStringT
        // &speaker, bool, bool force, bool localize)`, I_UISubtitles vslot1.
        // Localizes both keys and calls the movie's SetSubtitles(text,
        // speaker). `this` is the I_UISubtitles sub-object at +0x58.
        std::uint64_t C_UISubtitles__SetText = 0x0; // 0x181457948

        // `C_DialogInstance::SpeakLine180954638(void *line)`. Speaks one
        // sequence line: subtitle through I_UISubtitles::SetText, then
        // C_ActorSpeakMan::Speak with pContext = the instance.
        std::uint64_t C_DialogInstance__SpeakLine = 0x0; // 0x180954638

        // `C_CutscenePlayer::EnqueueCutscene(const std::shared_ptr<C_CutsceneConfiguration> &)`.
        // Queues a cutscene a quest graph's `C_CutsceneHandler` asked for;
        // its only caller is that node's EnqueueCutscene port. Does nothing
        // when the configuration names no cutscene of the table.
        std::uint64_t C_CutscenePlayer__EnqueueCutscene = 0x0; // 0x182B3691C

        // The `combat_input_class` row id of `attack_heavy`, which despite the
        // name is what the LEFT CLICK attack uses -- `attack_light`'s id is
        // registered by the same database and read by nothing.
        //
        // A field of C_CombatInputClassDatabase (`+0xF4`) rather than a global
        // of its own, and -1 until that database has loaded, so read it at the
        // moment of use exactly as the game does.
        std::uint64_t CombatInputClassId__AttackHeavy = 0x0; // 0x185321014

        // --- Native main menu -------------------------------------------
        //
        // Every entry on a page is an AddButton call and every click comes
        // back by name. The `menu_pages` / `menu_buttons` tables supply only
        // the text, container and tooltip of a button C++ has already decided
        // to add, so the composer is the only place a page can be changed.

        // `C_UIMenu::ShowRootMain(context)`. Sets the page to RootMain, asks
        // flash for the root frame and then adds Continue/TryAgain, NewGame,
        // the three debug starts, LoadGame, Settings, HelpOverlays, DLC,
        // Credits and Exit in that order.
        std::uint64_t C_UIMenu__ShowRootMain = 0x0; // 0x180F6820C

        // `C_UIMenu::SetPage(E_MenuPage)`. Stores the page at +0xA1, looks its
        // `menu_pages` row up and drives flash `ClearAll`, `SetMenuColor` and
        // `PreparePage`. Every ShowXxxPage opens with it.
        std::uint64_t C_UIMenu__SetPage = 0x0; // 0x180F69448

        // `C_UIMenu::OnFlashButton(CryStringT<char> buttonName)`, bound
        // to the movie's `OnButton` event. Parses the name back into an
        // E_ButtonId, finds that button's row and calls the std::function
        // AddButton stored on it. The by-value string is passed by pointer;
        // this callback releases its native reference before returning.
        std::uint64_t C_UIMenu__OnFlashButton = 0x0; // 0x180C07A98

        // `C_UIMenuBase::AddButton(E_ButtonId, std::function<void()>, bool
        // enabled, CryStringT<char> tooltipOverride)`.
        //
        // Reads Text, Tooltip and Container off the button's table row, stores
        // the handler on it and emits flash `AddBasicButton`. Both trailing
        // arguments are caller-owned temporaries this function destroys.
        std::uint64_t C_UIMenuBase__AddButton = 0x0; // 0x180558CCC

        // `AddBasicButton(const CryStringT<char> &function, const char *const
        // &buttonId, const int &container, const CryStringT<char> &text, const
        // CryStringT<char> &tooltip, const bool &disabled)`.
        //
        // The two strings are AddButton's own temporaries, so their contents
        // may be reassigned through the engine's operator= without ownership
        // crossing the module boundary. That is the only reach a mod has at a
        // button's label, which AddButton reads off the row and never exposes.
        //
        // Resolved from AddButton's call site: the instantiation is folded
        // with an identical twin, so no signature over its entry is unique.
        std::uint64_t C_UIMenuBase__EmitBasicButton = 0x0; // 0x1805589EC

        // `E_ButtonId::ToString(E_ButtonId)`. The same spelling the movie
        // sends back through `OnButton`, so it is what a name comparison has
        // to be built from rather than a literal.
        std::uint64_t E_ButtonId__ToString = 0x0; // 0x180558EB8

        // `C_UISaveLoad::ShowLoadPage(int mode)`. Sets the page to LoadGame,
        // emits one flash `AddLoadButton` per savegame and closes with a Back
        // button carrying its own handler.
        std::uint64_t C_UISaveLoad__ShowLoadPage = 0x0; // 0x182BAA354

        // The five-argument flash invoke `ShowLoadPage` builds its rows with:
        // `(const CryStringT<char> &function, const int &, const int &,
        //   const CryStringT<char> &label, const CryStringT<char> &tooltip,
        //   const bool &enabled)`. Folded with an identical twin, so it is
        // resolved from that call site rather than from its own prologue.
        std::uint64_t C_UISaveLoad__CallFlash5 = 0x0; // 0x182BA1AA4

        // `C_UISaveLoad::OnFlashLoadButton(int, int)`, bound to the movie's
        // `OnLoadButton` event. Both integers are the ones the matching
        // AddLoadButton row was given. It does nothing unless the menu is on
        // the LoadGame or SaveGame page.
        std::uint64_t C_UISaveLoad__OnFlashLoadButton = 0x0; // 0x182BA885C

        // --- Scaleform movie handle ---------------------------------------
        //
        // Not called through: these are virtuals, dispatched through the
        // interface in `i_flash_player.h`. They are resolved by signature so
        // that map can be checked before anything dispatches through it -- a
        // slot taken on faith puts a call on a neighbouring function with the
        // wrong arguments, which reads as a null write, not a bad index.
        std::uint64_t CFlashPlayer__SetVariable = 0x0; // 0x1824517A4, slot 39
        std::uint64_t CFlashPlayer__IsAvailable = 0x0; // 0x1805686C8, slot 42
        std::uint64_t CFlashPlayer__Invoke      = 0x0; // 0x1814914E0, slot 46

        // --- Native confirmation modal ------------------------------------
        //
        // `C_UIMenuBase::ShowConfirmation` stores two std::function handlers
        // on the menu_confirmations row and then emits the flash call below.
        // Only the emit is reachable from a mod, so the modal is raised there
        // and answered by detouring the event, as the buttons are.

        // flash `AddConfirmation(id, heading, confirmLabel, cancelLabel,
        // Number, Number)`. Folded with an identical twin, so it is resolved
        // from ShowConfirmation's own call site.
        std::uint64_t C_UIMenuBase__CallFlashAddConfirmation = 0x0; // 0x182BA1CE0

        // --- Settings menu rows -----------------------------------------
        //
        // The settings pages are built the same way as the root pages: a
        // `ShowXxxPage` on C_UISettings emits one flash row per setting and
        // closes with C_UIMenu::ShowPage. The row emitters take the row id as
        // a string, so a row the E_ButtonId table has never heard of is drawn
        // like any other; its events come back by that string, and the game's
        // own handlers ignore a spelling E_ButtonId::FromString does not know.

        // `C_UISettings::ShowSoundSettingsPage()`: SetPage(SoundSettings),
        // master/music/sfx sliders, footer buttons, ShowPage.
        std::uint64_t C_UISettings__ShowSoundSettingsPage = 0x0; // 0x180F6A0E0

        // `C_UISettings::ShowGameSettingsPage()`: SetPage(GameSettings), the
        // Game page's rows, ShowPage.
        std::uint64_t C_UISettings__ShowGameSettingsPage = 0x0; // 0x180F6AD04

        // `CAnimationDatabaseManager::ReloadDatabase(IAnimationDatabase*)`:
        // ClearDatabase, then the file read again; ReloadAll's only callee.
        std::uint64_t CAnimationDatabaseManager__ReloadDatabase = 0x0; // 0x1836C2150

        // `C_UIMenu::ShowPage()`, the flash `ShowPage` every page builder ends
        // with. Called on the C_UIMenu, not on the sub-page building it.
        std::uint64_t C_UIMenu__ShowPage = 0x0; // 0x181F8DB80

        // `C_UIMenuBase::AddValueButton(const CryStringT<char> &id, const
        // CryStringT<char> &label, bool enabled, float value, float min, float
        // max, CryStringT<char> tooltip, int container)`. The tooltip is a
        // by-value temporary this function destroys.
        std::uint64_t C_UIMenuBase__AddValueButton = 0x0; // 0x181F8BB00

        // `C_UIMenuBase::AddChoicesButton(const CryStringT<char> &id, const
        // CryStringT<char> &label, bool enabled, CryStringT<char> tooltip, int
        // container)`, destroying the by-value tooltip.
        std::uint64_t C_UIMenuBase__AddChoicesButton = 0x0; // 0x181F8B810

        // `C_UIMenuBase::AddChoiceOption(const CryStringT<char> &id, int index,
        // const CryStringT<char> &label, bool enabled, const CryStringT<char>
        // &tooltip, bool)`. The label is emitted as given, not localized.
        std::uint64_t C_UIMenuBase__AddChoiceOption = 0x0; // 0x181F8B8E0

        // `C_UIMenuBase::SetValue(const CryStringT<char> &id, float value, int
        // container)` and `SetChoice(const CryStringT<char> &id, int index,
        // int)`, the second int 0 at every game call site.
        std::uint64_t C_UIMenuBase__SetValue  = 0x0; // 0x181F8D9C0
        std::uint64_t C_UIMenuBase__SetChoice = 0x0; // 0x181F8D8D0

        // The movie's `OnInteractiveValue(id, value)` and
        // `OnInteractiveChoice(id, index)`, bound on C_UISettings. Both take
        // the id as a by-value CryStringT they destroy themselves.
        std::uint64_t C_UISettings__OnInteractiveValue  = 0x0; // 0x182BA8718
        std::uint64_t C_UISettings__OnInteractiveChoice = 0x0; // 0x181F8CDC0

        // --- Player profile attributes ------------------------------------
        //
        // The typed wrappers every IPlayerProfileListener in the game reads
        // and writes its settings through: `bool (IPlayerProfile *, const char
        // *name, T *value)`, each converting through TFlowInputData so the mod
        // never builds or frees one of those variants itself. The float and
        // int pairs are template twins with no unique prologue, so they are
        // resolved from C_UISettings's own load and save calls.
        std::uint64_t PlayerProfile__GetFloatAttribute  = 0x0; // 0x18079C104
        std::uint64_t PlayerProfile__GetIntAttribute    = 0x0; // 0x18079CEC4
        std::uint64_t PlayerProfile__GetStringAttribute = 0x0; // 0x18079F890
        std::uint64_t PlayerProfile__SetFloatAttribute  = 0x0; // 0x18079C328
        std::uint64_t PlayerProfile__SetIntAttribute    = 0x0; // 0x18079D548
        std::uint64_t PlayerProfile__SetStringAttribute = 0x0; // 0x180F69380

        // `C_UIMenuBase::OnFlashConfirm(const CryStringT<char> &id, bool
        // confirmed)`, bound to the movie's OnConfirm event.
        std::uint64_t C_UIMenuBase__OnFlashConfirm = 0x0; // 0x182BA80D8

        // `E_ConfirmationId::ToString`. 28 values, Invalid=0.
        std::uint64_t E_ConfirmationId__ToString = 0x0; // 0x182BAAD2C

        // Human soul queries and synchronous storage maintenance.
        std::uint64_t C_Soul__GetState                         = 0x0;
        std::uint64_t C_Soul__GetStateMax                      = 0x0;
        std::uint64_t C_Soul__GetStat                          = 0x0;
        std::uint64_t C_Soul__GetRelativeStat                  = 0x0;
        std::uint64_t C_Soul__GetDerivedStat                   = 0x0;
        std::uint64_t C_Soul__GetSkill                         = 0x0;
        std::uint64_t C_Soul__GetRelativeSkill                 = 0x0;
        std::uint64_t C_SoulStateStorage__CopyFrom             = 0x0;
        std::uint64_t C_SoulPropertyEvents__NotifyStateChanged = 0x0;
        std::uint64_t C_SoulPropertyNotifier__NotifyProperty   = 0x0; // 0x18064A6C4
        std::uint64_t C_RagdollManager__Rephysicalize          = 0x0; // 0x1808AB22C
        std::uint64_t C_RagdollManager__SetPhysicsProfile      = 0x0;

        // The two ways this mod moves a body between limp and upright, both
        // reached by address: neither is a slot of the ragdoll table.
        std::uint64_t C_RagdollManager__RequestStandUp         = 0x0; // 0x1828792D0
        std::uint64_t C_ActorStateExpansion__RequestDeadAction = 0x0; // 0x1808550F0

        // The lifecycle of the two body actions this mod drives. Hooked
        // rather than called: entering and leaving these is what actually
        // happened to a body, where the Request* calls above only report that
        // an action object was allocated.
        std::uint64_t C_ActorActionDead__Enter        = 0x0; // 0x180E78768
        std::uint64_t C_Human__DropHandItems          = 0x0; // 0x180E797A4
        std::uint64_t C_ActorActionUnconscious__Enter = 0x0; // 0x182898668
        std::uint64_t C_ActorActionUnconscious__Exit  = 0x0; // 0x1828994E4

        // The minigame action's Exit: a minigame ended because another actor
        // action replaced its own reaches the minigame's Cancel from here.
        std::uint64_t C_ActorActionMinigame__Exit = 0x0; // 0x1819C7EB4

        // The engine's own death edge, and the only one: it publishes module
        // message 37, calls HandleDeath(true), and only then considers a
        // game-over. A death is reported from here rather than from
        // RaiseGameOver, which is a presentation consequence gated on the
        // dying soul's player flag (soul+0xCF0 & 0x10) and latched once per
        // session.
        std::uint64_t C_Soul__DispatchSoulDeath    = 0x0; // 0x181FDF720
        std::uint64_t C_InventorySoul__ApplyOutfit = 0x0; // 0x181FE1DB0

        // Intercept local death at RaiseGameOver before UI and save-lock side
        // effects.
        std::uint64_t RaiseGameOver    = 0x0; // 0x182E1919C
        std::uint64_t C_Player__Revive = 0x0; // 0x180BC67E4
        // The tail of C_Actor::Physicalize that hands the template's inertia,
        // acceleration inertia and impulse recovery time to the actor's
        // animated character; takes C_Actor+0x268 and the three floats.
        std::uint64_t C_Actor__ApplyInertiaToAnimatedCharacter = 0x0; // 0x1808ABF14

        // C_Player::HandleDeath's player_fx/player_death material effect. Its
        // flowgraph plays special_player_death, a looping FMOD event that
        // holds bus:/dieg at silence and that nothing native stops.
        std::uint64_t C_PlayerMaterialEffects__PlayDeathEffect = 0x0; // 0x182AA9530

        std::uint64_t C_SoulStateStorage__SetState = 0x0; // 0x18046F6CC
        std::uint64_t C_Soul__SetStateWithCause    = 0x0; // 0x18046EA68

        // `const CryStringT<char> &C_Soul::ResolveBodyDescriptionPart(unsigned char part) const`:
        // look-as (+0xC68), barber (+0xAE0), then the soul's own (+0x430), first non-empty wins.
        // Borrows, where slot 162 copies five refcounted CryStrings the mod may not free.
        std::uint64_t C_Soul__ResolveBodyDescriptionPart = 0x0; // 0x181FE0C90

        // --- Weapons ------------------------------------------------------
        //
        // A KCD2 weapon is an ordinary item whose class descriptor names a
        // `weapon_class.xml` row, and `C_ItemAttachmentManager` -- one per
        // human, at `C_Human+0x9D0`, reached through `C_Actor` slot 191 -- is
        // the eight-slot table saying which item belongs where on the body.
        // Drawing is attaching, so all of these move items between the
        // holster and the hand rather than playing anything.

        // The invalid-item marker the manager fills its empty slots with,
        // read RIP-relative out of the first instruction of HasWeaponInSet.
        std::uint64_t g_InvalidItemWUID = 0x0; // 0x185325B60

        // `C_ItemAttachmentManager::DrawWeapons()`. Attaches both hands of
        // the selected set at once; this is what the DrawWeapon script
        // binding calls.
        std::uint64_t C_ItemAttachmentManager__DrawWeapons = 0x0; // 0x182AA24A0

        // `C_ItemAttachmentManager::HolsterWeapons()`, its matching half.
        std::uint64_t C_ItemAttachmentManager__HolsterWeapons = 0x0; // 0x1808EE994

        // `C_ItemAttachmentManager::SetSelectedWeaponSet(int)`. Writes
        // nothing while the hands are full, and rejects the wildcard 3.
        std::uint64_t C_ItemAttachmentManager__SetSelectedWeaponSet = 0x0; // 0x1808EEA04

        // `C_ItemAttachmentManager::HasWeaponInSet(int) const`.
        std::uint64_t C_ItemAttachmentManager__HasWeaponInSet = 0x0; // 0x182AA79A8

        // `C_ItemAttachmentManager::AreHandsEmpty() const`.
        std::uint64_t C_ItemAttachmentManager__AreHandsEmpty = 0x0; // 0x1808FE304

        // `C_ItemAttachmentManager::IsWeaponDrawn(bool, int set, unsigned
        // itemClassId) const`. Set 3 means either hand of any set.
        std::uint64_t C_ItemAttachmentManager__IsWeaponDrawn = 0x0; // 0x1808C69B4

        // `C_ItemAttachmentManager::AttachItemById(WUID, unsigned kind)`,
        // the half the draw animation's own anim events reach.
        std::uint64_t C_ItemAttachmentManager__AttachItemById = 0x0; // 0x1808CEA48

        // `C_ItemAttachmentManager::DetachItemById(WUID)`.
        std::uint64_t C_ItemAttachmentManager__DetachItemById = 0x0; // 0x1808CF158

        // --- Animated weapon draw and holster ------------------------------
        //
        // `DrawWeapons` is by design the un-animated half of drawing. The
        // engine's own decision function below takes an `animated` flag and,
        // when it is set, builds an S_ActorAnimDrawRequest or an
        // S_ActorAnimHolsterRequest on the actor's item-handling animation
        // expansion and queues it as an actor action. That is where the draw
        // and holster animations live.

        // `wh::entitymodule::DrawOrHolsterItem(C_Item **pair, S_ActionRequestResult
        // *out, C_Item *main, int qamSlot, C_Actor *actor, bool animated,
        // std::function *callback)`. A free function, so it is called through
        // `hook::call`.
        std::uint64_t WH__DrawOrHolsterItem = 0x0; // 0x1808ED3D8

        // `wh::entitymodule::BuildDrawHandPair(C_Item **pair, WUID out[2])`.
        // Places each item at the hand its own weapon class names, leaving
        // the rest as the invalid marker. A free function.
        std::uint64_t WH__BuildDrawHandPair = 0x0; // 0x1808C7D04

        // `wh::entitymodule::IsItemDrawn(C_Actor *, WUID)`. Reads the actor's
        // own hand store, the one SetHandItem writes, so it answers "is it
        // out" rather than "is it equipped".
        std::uint64_t WH__IsItemDrawn = 0x0; // 0x1809D3270

        // --- Stashes and locks ---------------------------------------------
        //
        // A lock has no C++ state. It lives on the entity's Lua script table,
        // so every accessor below reaches it through GetEntityScriptTable and
        // answers a default when the entity has no script proxy.

        // `C_LockBase::IsLocked() const`, the runtime `entity.bLocked` the
        // game itself tests. The signature pins the entity load from +0x38.
        std::uint64_t C_LockBase__IsLocked = 0x0; // 0x180CC5778

        // `C_LockBase::GetPropertiesLocked() const`, the design-time
        // `Properties.Lock.bLocked` the level author set.
        std::uint64_t C_LockBase__GetPropertiesLocked = 0x0; // 0x180CC55C8

        // `C_LockBase::SetLocked(bool, void *sink)`. Calls the entity's own
        // Lua `Lock` or `Unlock`, and is a no-op when the runtime flag already
        // matches. Per-client by construction, which is what makes a
        // per-player lock possible without inventing one.
        std::uint64_t C_LockBase__SetLocked = 0x0; // 0x180CC568C

        // `C_LockBase::CanUnlockWithDynamicKey() const`, reading
        // `Properties.Lock.bCanUnlockWithDynamicKey` and defaulting to true.
        std::uint64_t C_LockBase__CanUnlockWithDynamicKey = 0x0; // 0x180CC550C

        // `C_LockBase::CanUnlock(const CryGUID &) const`, vtable slot 30 as the
        // base implements it: does the local player's soul inventory hold an
        // item of that class. No dynamic key, and `this` is never read.
        std::uint64_t C_LockBase__CanUnlock = 0x0; // 0x180CC59F4

        // `C_Stash::CanUnlock(const CryGUID &) const`, the override: the base
        // answer, or the dynamic home/shop key when the stash allows one. What
        // the Lua `lockBase:PlayerHoldsKey` binding reaches for a stash, and so
        // what decides whether "unlock and open" is offered.
        std::uint64_t C_Stash__CanUnlock                              = 0x0; // 0x182AC08E8
        std::uint64_t C_ScriptBindEntityModule__HasPlayerVisibleItems = 0x0; // 0x181F27830
        std::uint64_t C_HerbGathering__Start                          = 0x0; // 0x182E97270
        std::uint64_t C_HerbGathering__AwardGatheredHerbs             = 0x0; // 0x182E88DB8
        std::uint64_t C_RespawnManager__HarvestArea                   = 0x0; // 0x182ADDCF0
        std::uint64_t C_RespawnManager__RestoreHarvestedArea          = 0x0; // 0x182ADEAAC
        std::uint64_t C_RespawnManager__EraseHarvestClaim             = 0x0; // 0x182ADF3C8
        std::uint64_t C_RespawnManager__HarvestSelected               = 0x0; // 0x182ADDFA4
        std::uint64_t C_RespawnManager__QuerySources                  = 0x0; // 0x182ADDA1C

        // `C_PlayerInteractor::AddLuaAction(record *, E_InteractionType,
        // HSCRIPTFUNCTION)`. Every prompt an entity's Lua GetActions returns
        // is registered here, through `C_ScriptBindPlayer::AddLuaActions`. The
        // only place the interaction type and the handler are in hand together.
        std::uint64_t C_PlayerInteractor__AddLuaAction = 0x0; // 0x1808E8A24

        // `CScriptBindGame::SetPrefabPhaseById(IFunctionHandler *, ScriptHandle,
        // const char *, int)`: `Game.SetPrefabPhaseById`. The only route by
        // which so_fireplace.xml changes a kettle cauldron's look.
        std::uint64_t CScriptBindGame__SetPrefabPhaseById = 0x0; // 0x1809E5584

        // `wh::entitymodule::InvokeLuaActionFunc(void *, S_LuaActionInvocation *)`,
        // a free function: the pressed prompt's handler run as
        // `func(entity, player, 0)`. Its only caller is the interaction
        // callback's `_Do_call`; returning early runs nothing else.
        std::uint64_t entitymodule__InvokeLuaActionFunc = 0x0; // 0x180B29258

        // `wh::entitymodule::FindInteractionType(E_InteractionType *, const char *)`,
        // a free function returning through RCX: the interaction filter's
        // name-to-id look-up, which never assigns an id. The native answer to
        // what the `inr_*` Lua globals only mirror.
        std::uint64_t entitymodule__FindInteractionType = 0x0; // 0x1823D5E64

        // Lua's native lock notifications, raised by every Lock() and Unlock()
        // whoever calls them -- containers' among them. Observation points
        // only: authoritative corrections go back through the entity's own Lua.
        std::uint64_t C_ScriptBindLockBase__ReportLocked   = 0x0; // 0x180CC58D8
        std::uint64_t C_ScriptBindLockBase__ReportUnlocked = 0x0; // 0x180CC4894

        // The native lockpick-success callback, reached after SetLocked(false)
        // and the C_LockBase +0x60 signal. It then calls entity:OnLockpicked().
        std::uint64_t C_LockBase__OnLockpicked = 0x0; // 0x1819B8344

        // `C_AnimDoor::Init(IGameObject *)`. After the original attaches the
        // native helpers, the outer entity table still inherits AnimDoor.lua's
        // methods. This is the stable edge for rooting the real Lua functions.
        std::uint64_t C_AnimDoor__Init = 0x0; // 0x18133E8F0

        // Raised by AnimDoor.lua's DoPlayAnimation, the one function that
        // changes a door's direction, before it selects and starts the clip.
        // Every transition passes here, whoever or whatever caused it.
        std::uint64_t C_ScriptBindAnimDoor__ReportManipulationStart = 0x0; // 0x1804CFA5C

        // AnimDoor.lua calls GetObjectAnimName on every transition, before it
        // starts the object's animation and, on a player's use, the actor's.
        // Custom-animation paths bypass this selector.
        std::uint64_t C_ScriptBindAnimDoor__GetObjectAnimName = 0x0; // 0x1804CF224

        // Lua's `entity.animDoor:IsInteractive()`: false while C_AnimDoor
        // +0xB0 is non-zero, the native disable a quest's
        // C_DisableDoorInteractivity drives. AnimDoor.lua asks it from
        // GetActions, which the game rules' IsUsable calls every interaction
        // scan, and from OnUsed before a use unlocks or moves anything, so a
        // false here takes the prompt away and refuses the use.
        std::uint64_t C_ScriptBindAnimDoor__IsInteractive = 0x0; // 0x1804CFA9C

        // `CScriptSystem::PopStackValue(ScriptAnyValue &)`. Converts the top
        // Lua stack value and consumes it. EndCall leaves the pcall error
        // there, so this retrieves it without reaching into CScriptSystem or
        // lua_State by offset.
        std::uint64_t CScriptSystem__PopStackValue = 0x0; // 0x180A29F38

        // C_Actor slot 180 (+0x5A0), used by AnimDoor.lua for the player's
        // native interactive action once C_AnimDoor has selected its name.
        std::uint64_t C_Actor__StartInteractiveActionByName = 0x0; // 0x180702288

        // `CActionInteractive::CActionInteractive(...)`. The constructor is
        // the exact point at which a submitted actor/target pair gains its
        // concrete IAction identity; Enter and Exit can then be correlated by
        // pointer rather than by a heuristic entity-id match.
        std::uint64_t CActionInteractive__Ctor = 0x0; // 0x180F473A0

        // `CActionInteractive::Enter()`. Queueing the action is asynchronous;
        // this is the first edge that proves the controller actually activated
        // it and began driving the actor toward its interactive fragment.
        std::uint64_t CActionInteractive__Enter = 0x0; // 0x18113AACC

        // `CActionInteractive::Exit()`. This concrete override is distinct
        // from the COMDAT-folded IAction Fail/Exit body and is therefore the
        // first unambiguous terminal edge for an interactive action.
        std::uint64_t CActionInteractive__Exit = 0x0; // 0x1807027FC

        // `wh::entitymodule::FindStashExtension(EntityId)`. Queries the game
        // object for Stash, StashCorpse, Nest, CartStash and DestroStash in
        // turn, so it answers for every stash variant. A free function.
        std::uint64_t entitymodule__FindStashExtension = 0x0; // 0x180CC4FE8

        // `C_Stash::GetMasterInventoryWuid() const`, the linked stash whose
        // inventory this one shares. Returns through a hidden pointer even
        // though a WUID is eight bytes.
        std::uint64_t C_Stash__GetMasterInventoryWuid = 0x0; // 0x182ABC580

        // `C_Stash::RestockNow()`. Reconciles the inventory against the preset
        // item list, which REMOVES everything the list does not name -- so it
        // is also the detour point for a stash whose contents a mod owns.
        std::uint64_t C_Stash__RestockNow = 0x0; // 0x180A2E7D4

        // `C_Stash::DumpItemsToWorld(const Vec3 &)`, the native spill.
        std::uint64_t C_Stash__DumpItemsToWorld = 0x0; // 0x182AB9E34

        // `C_Stash::OnOpened(bool byPlayer)` and `C_Stash::OnClosed()`, the
        // native per-stash open and close events. Their only callers are
        // C_ScriptBindStash::ReportOpened and ReportClosed, so they fire
        // exactly when the stash's own Lua says the transfer UI opened and
        // closed, already bracketing the robbed-value bookkeeping.
        std::uint64_t C_Stash__OnOpened = 0x0; // 0x182AC1284
        std::uint64_t C_Stash__OnClosed = 0x0; // 0x182AC11DC

        // `C_Stash::ScheduledUpdate(int)`. The schedule manager calls it on the
        // I_ScheduleUpdatable subobject at C_Stash+0x70, which the body's first
        // instruction walks back from. It reaches the same inventory
        // reconciliation as RestockNow without going through it, so a stash
        // whose contents a mod owns has to be kept out of both.
        std::uint64_t C_Stash__ScheduledUpdate = 0x0; // 0x1808F2F30

        // The soul twins of the two above. `C_InventorySoul::ScheduledUpdate(int)`
        // is the I_ScheduleUpdatable subobject at C_InventorySoul+0x10, which
        // the schedule manager and C_SkipTime tick; `FullyRestock(bool, bool)`
        // is the forced one behind the xgen `FullyRestock`. Both rebuild the
        // soul's rolled preset list and restock against it (0x181F1C0C0),
        // which copies the entry of every preset-created item without checking
        // the lookup found one -- a soul whose roll has changed since its items
        // were made faults there.
        std::uint64_t C_InventorySoul__ScheduledUpdate = 0x0; // 0x1808F2E88
        std::uint64_t C_InventorySoul__FullyRestock    = 0x0; // 0x181FE0BF0

        // The two C_ItemSlot property-load virtuals, primary vtable slots 33
        // and 34. Both read the same ItemSlot.lua properties into the same
        // fields and differ only in reaching the script table through its
        // vtable rather than the inlined accessors. The "bSpawnOnStart"
        // literal is referenced by these two and nothing else, so together
        // they are every write of the spawn flags -- detour both.
        std::uint64_t C_ItemSlot__LoadScriptProperties            = 0x0; // 0x181EEE490
        std::uint64_t C_ItemSlot__LoadScriptPropertiesVirtualCall = 0x0; // 0x181EEE7D0

        // Native missile actions and projectile launch.
        std::uint64_t C_Soul__ApplyModifiers                          = 0x0; // 0x180649F1C
        std::uint64_t C_RPGCombat__HasShootingStamina                 = 0x0; // 0x1815F5F80
        std::uint64_t C_EventFactory__CreateMissileHitEvent           = 0x0; // 0x182D19080
        std::uint64_t C_EffectVector__EmplaceBack                     = 0x0; // 0x18057F3F0
        std::uint64_t C_Item__EnsureRuntimeData                       = 0x0; // 0x1804694C0
        std::uint64_t C_Item__SetQuality                              = 0x0; // 0x182A6B10C
        std::uint64_t C_ItemHolder__CloneItem                         = 0x0; // 0x1808F0FE0
        std::uint64_t C_WeaponEquipRuntimeData__SetPoison             = 0x0; // 0x18118C604
        std::uint64_t C_EventManager__ProcessEvent                    = 0x0; // 0x1809DB51C
        std::uint64_t C_ActorShootingExpansion__MarkWeaponFired       = 0x0; // 0x182ACDBE0
        std::uint64_t C_CombatSoul__ProcessMissileHit                 = 0x0; // 0x182D291D0
        std::uint64_t S_MissileHitData__Constructor                   = 0x0; // 0x182762908
        std::uint64_t S_SoulEntityIdentity__Constructor               = 0x0; // 0x180D571E4
        std::uint64_t C_ItemAttachmentManager__FindMissileDefenseItem = 0x0; // 0x182AA7398
        std::uint64_t C_ActorShootingExpansion__RefreshAnimationTags  = 0x0; // 0x180C5F484
        std::uint64_t C_MissileWeaponRuntimeData__SetCocked           = 0x0; // 0x181741C80
        std::uint64_t C_MissileWeaponRuntimeData__LoadAmmunition      = 0x0; // 0x182A8A0C0
        std::uint64_t C_MissileWeaponRuntimeData__UnloadAmmunition    = 0x0; // 0x182A8B72C
        std::uint64_t C_ShootingSystem__FireWeapon                    = 0x0; // 0x1828999C8
        std::uint64_t CArrow__ProcessCollision                        = 0x0; // 0x182ACBFA8
        std::uint64_t CProjectile__Destructor                         = 0x0; // 0x182A9F9F4
        std::uint64_t CProjectile__UpdateWhiz                         = 0x0; // 0x182AAFAAC
        std::uint64_t CProjectile__TrailEffect                        = 0x0; // 0x182AAE044
        std::uint64_t C_Decoy__ProcessCollision                       = 0x0; // 0x182ACC6B8
        std::uint64_t CProjectile__Destroy                            = 0x0; // 0x182AA1A9C
        std::uint64_t CArrow__SpawnRecoverableItem                    = 0x0; // 0x182ACE094
        std::uint64_t C_Item__SpawnPickableImmediate                  = 0x0; // 0x18070814C
        std::uint64_t C_ActionActor__GetShootingExpansion             = 0x0; // 0x1807f3310
        std::uint64_t C_ActionActor__GetActorStateExpansion           = 0x0; // 0x1806823E0
        std::uint64_t C_ActorStateExpansion__RequestJump              = 0x0; // 0x180CF8600
        std::uint64_t C_Actor__IsJumping                              = 0x0; // 0x181E7D660

        // --- Traversal: ladders and ledges -------------------------------

        // Posts actor action type 0x22 for a ladder entity id. Non-virtual.
        std::uint64_t C_ActorStateExpansion__RequestLadderAction = 0x0; // 0x180F49BF0

        // Hooked, not called: every way onto a ladder ends in this action.
        std::uint64_t C_ActorActionLadder__Enter = 0x0; // 0x180F49D5C
        std::uint64_t C_ActorActionLadder__Exit  = 0x0; // 0x18040A87C
        // Hooked: the ladder's traffic check every climb step asks.
        std::uint64_t C_LadderTraffic__RegulateClimb = 0x0; // 0x1811D4C54

        // --- Carrying a body -----------------------------------------------

        // Builds the actor's C_ActorCarryCorpse on first use. Non-virtual.
        std::uint64_t C_Actor__GetCarryCorpse = 0x0; // 0x1803B6F84

        // Unbinds the carried body; what the carry action's Exit runs.
        std::uint64_t C_ActorCarryCorpse__EndCarrying = 0x0; // 0x18285F964

        // Hooked, not called: every carry starts and ends in this action.
        std::uint64_t C_ActorActionCarryCorpse__Enter = 0x0; // 0x182879F54
        std::uint64_t C_ActorActionCarryCorpse__Exit  = 0x0; // 0x18287A370

        // --- Carrying an item ----------------------------------------------

        // Builds and queues a C_ActorActionCarryItem. Non-virtual.
        std::uint64_t C_ActionActor__RequestCarryItem = 0x0; // 0x181391738

        // Hooked, not called: every item carry starts and ends in this action.
        std::uint64_t C_ActorActionCarryItem__Enter = 0x0; // 0x181083478
        std::uint64_t C_ActorActionCarryItem__Exit  = 0x0; // 0x181083644

        // The phase byte's one writer. Non-virtual.
        std::uint64_t C_ActorActionCarryItem__SetCarryPhase = 0x0; // 0x1810840FC

        // Switches the global carry_item / carry_item_manipulation action
        // filters for a player-controlled carrier. Hooked, not called.
        std::uint64_t C_ActorActionCarryItem__SetCarryInputMode = 0x0; // 0x1810841EC

        // The carry's per-frame update and the end of its pick-up, which
        // reach the global action sets for any carrier. Hooked, not called.
        std::uint64_t C_ActorActionCarryItem__Update             = 0x0; // 0x18091030C
        std::uint64_t C_ActorActionCarryItem__CompleteItemPickup = 0x0; // 0x180D7C2D4

        // The vault's two ends in C_ActorStateMovement::Ledge (events 123 and
        // 4), on the state's embedded ledge block at +0x2B0. Both switch the
        // global "ledge" action map for a player-controlled vaulter.
        std::uint64_t C_ActorStateMovement__BeginLedge = 0x0; // 0x180F43600
        std::uint64_t C_ActorStateMovement__EndLedge   = 0x0; // 0x180F42F30

        // Switches the global "crouch" action map for a player-controlled
        // croucher, from TryEnterCrouch and Exit.
        std::uint64_t C_ActorActionCrouch__SetPlayerCrouchInputEnabled = 0x0; // 0x18178CF4C

        // A paired animation's slave side switches the global "no_input"
        // action map on and off for a player-controlled slave.
        std::uint64_t ActorActionAnimSlave__Enter = 0x0; // 0x182879F00
        std::uint64_t ActorActionAnimSlave__Exit  = 0x0; // 0x18287A308

        // C_ActionSets slot 9 through its secondary-table adjustor: installs
        // a callback on an action of a set.
        std::uint64_t I_ActionSets__SetActionCallback = 0x0; // 0x18085A96C

        // What the game's put_item interaction runs. Non-virtual.
        std::uint64_t C_ActorActionCarryItem__BeginPutDownAnimation = 0x0; // 0x18287D078

        // The pick-up's start, from Enter: queues the pick-up fragment and
        // either aims it at the item's holder entity (TargetPos) or, for an
        // item loose in the world, moves the item in front of the carrier.
        std::uint64_t C_ActorActionCarryItem__BeginItemPickupAnimation = 0x0; // 0x181083914

        // IAction+0x68's parameter map: (this, lower-case CRC32 name, const QuatT &).
        std::uint64_t CMannequinParams__SetParam = 0x0; // 0x1806825C4
        // Its reader: (this, lower-case CRC32 name, QuatT &), false when absent.
        std::uint64_t CMannequinParams__GetParam = 0x0; // 0x18068430C

        // The slot every lockpicking action shares and no other action has.
        std::uint64_t I_LockPickingAction__OnFragmentStarted = 0x0; // 0x180897C60

        // Ends any actor action through its queue: (this, result).
        std::uint64_t C_Action__Finish = 0x0; // 0x1806C3A90
        std::uint64_t C_ChatFollowManager__StartFollowing = 0x0; // 0x182AAD91C
        std::uint64_t C_ChatFollowManager__IsFollowTargetEligible = 0x0; // 0x18082E974
        std::uint64_t C_ChatFollowManager__MeetsFollowDistanceRequirement = 0x0; // 0x182AA83F4
        std::uint64_t ClassifyFollowTarget = 0x0; // 0x18129DA18

        // The holder-agnostic item lookup by WUID.
        std::uint64_t C_ItemManager__FindItem = 0x0; // 0x1823D09E0

        // The complete vault. Free function; the actor arrives in RDX.
        std::uint64_t TryLedgeGrab                               = 0x0; // 0x180831B20
        std::uint64_t C_ActorShootingExpansion__SetAmmunition    = 0x0; // 0x180940d9c
        std::uint64_t C_ActorShootingExpansion__Reload           = 0x0; // 0x1818468bc
        std::uint64_t C_ActorShootingExpansion__Charge           = 0x0; // 0x182ace880
        std::uint64_t C_ActorShootingExpansion__CancelCharging   = 0x0; // 0x182acb994
        std::uint64_t C_ActorShootingExpansion__CancelReloading  = 0x0; // 0x182acb9b8
        std::uint64_t C_ActorShootingExpansion__Fire             = 0x0; // 0x182acbc28
        std::uint64_t C_ActorShootingExpansion__IsCharging       = 0x0; // 0x1814dbd20
        std::uint64_t C_ActorShootingExpansion__IsReloading      = 0x0; // 0x1813453d4
        std::uint64_t C_ActorShootingExpansion__IsFiring         = 0x0; // 0x180642c40
        std::uint64_t C_ActorShootingExpansion__CanFire          = 0x0; // 0x180a50d74
        std::uint64_t C_ActorShootingExpansion__CanReload        = 0x0; // 0x180c5fa7c
        std::uint64_t C_ActorShootingExpansion__CanCharge        = 0x0; // 0x182acb7fc
        std::uint64_t C_ShootingSystem__GetInstance              = 0x0; // 0x18189f3c0
        std::uint64_t C_ShootingSystem__SpawnAndLaunchProjectile = 0x0; // 0x18289f7a0
        std::uint64_t CWeaponSystem__SpawnAmmo                   = 0x0; // 0x18296043C
        std::uint64_t CProjectile__SProjectileDesc__Construct    = 0x0; // 0x182894158
        std::uint64_t C_Decoy__VTable                            = 0x0; // 0x184711C10
        std::uint64_t CProjectile__Launch                        = 0x0; // 0x182aa85a8

        // Missile state edges. The phase is the current sub-action of
        // C_ActorActionShootingMain: Enter writes it, Exit clears it, and
        // every one of the eight shooting sub-states reaches both, so the
        // pair is exhaustive. Main::Exit clears the expansion's main action
        // outright, which ends the phase whether or not a sub-action exited.
        std::uint64_t C_ActorActionShootingBase__Enter = 0x0; // 0x1828971FC
        std::uint64_t C_ActorActionShootingBase__Exit  = 0x0; // 0x182898D6C
        std::uint64_t C_ActorActionShootingMain__Exit  = 0x0; // 0x182898F08

        // Mid-reload refinements: the reload animation cocks the weapon and
        // loads a round without leaving the reloading sub-state, so neither
        // shows up on an Enter or an Exit.
        std::uint64_t C_ActorActionShootingReloading__CockWeapon           = 0x0; // 0x182895F30
        std::uint64_t C_ActorActionShootingReloading__LoadWeaponAmmunition = 0x0; // 0x18289C930

        // What a shooting sub-state does to its body: movement restriction
        // scope 15 and, on a player body, this machine's global
        // `no_horizontal_camera` filter. Run from the sub-states' Enter, Exit
        // and an actor-model callback.
        std::uint64_t C_ActorActionShootingBase__UpdateShootingRestrictions = 0x0; // 0x1828A16BC
        // CActionMapManager vtable slots 30/31 (enable, disable a filter for
        // one reason) both end here; `name`, then enable, reason and a flag.
        std::uint64_t CActionMapManager__ApplyFilterByReason = 0x0; // 0x1806169F0
        // A body's combat controller setting this machine's `no_inventory`
        // filter (reason 17); its own state is the byte at +0xF9.
        std::uint64_t C_CombatPlayerController__SetInventoryBlocked = 0x0; // 0x1806167E8

        // Bound by the expansion's constructor to the actor's handled-item
        // signal, and the only writer of the expansion's weapon.
        std::uint64_t C_ActorShootingExpansion__OnHandledItemChanged = 0x0; // 0x180952108

        // --- The dog companion -------------------------------------------

        // `C_SoulGroup::PossessAnimal(I_Soul *animal, bool insertFront)`.
        //
        // What makes an animal somebody's. It writes the master onto the
        // animal's own soul at +0x538 and pushes it into the per-category
        // deque, and it REFUSES an animal that already has a master -- so a
        // hand-over is release-then-possess. The game's own Lua binding
        // `C_ScriptBindPlayer::SetPlayerDog` is a caller, which is what fixes
        // the argument order.
        std::uint64_t C_SoulGroup__PossessAnimal = 0x0; // 0x180A2074C

        // `C_SoulGroup::ReleaseAnimal(I_Soul *animal)`, the other half.
        // Clears the master on the animal's soul and runs the engine's own
        // release callbacks inside the call.
        std::uint64_t C_SoulGroup__ReleaseAnimal = 0x0; // 0x180A1F6F4

        // `C_RiderPlayerControl::SetPlayerHorse(EntityId horse)`, the
        // singleplayer "this is my horse": release the old one, possess the
        // new one, strip its armour, move the old horse's inventory onto it.
        // Also reached by the mount event when the player has no horse, which
        // is why a replicated horse must be kept out of it.
        std::uint64_t C_RiderPlayerControl__SetPlayerHorse                 = 0x0; // 0x1817972A0
        std::uint64_t C_RiderPlayerControl__RegisterMountedHintCallbacks   = 0;   // 0x18192D370
        std::uint64_t C_RiderPlayerControl__UnregisterMountedHintCallbacks = 0;   // 0x18100CF80
        std::uint64_t C_RiderPlayerControl__UpdateMountedHintBindings      = 0;   // 0x180A260C8

        // The rider control's ride-state callback, and the three script
        // globals it ends in: horse_mounting (with the rider's entity id),
        // horse_mounted and horse_dismounting. Quests, UI and Lua read them as
        // the local player's. The last two are called without `this` in RCX;
        // they never read it.
        std::uint64_t C_RiderPlayerControl__OnHorseRideStateChanged   = 0x0; // 0x180618018
        // The rider input's update, run by the horse from the parallel entity
        // update for every rider, remote ones included. It has the local
        // player speak twice (the speeding and spur lines): each is a
        // `RequestPlayerMonolog(metarole)`, a `dialog:monologRequest` posted
        // through the AI messaging system's unlocked message pool.
        std::uint64_t C_RiderPlayerInput__Update                      = 0x0; // 0x180A4EAB4, I_HorseMoveAdapter slot 5
        std::uint64_t RequestPlayerMonolog                            = 0x0; // 0x18185C7E0
        // `PlayHorseFeedbackSound(feedback, sound)`: one of the ten horse
        // feedback triggers (table filled at 0x181ECA5D0 from the name lists
        // built at 0x180127C20) on the horse's audio proxy, at most once per
        // 0.5 s per sound. `ProcessSpurInput` alone plays sound 9,
        // `special_player_spurs_use` -- the rider's own 2D cue, culled at
        // 65535 m -- on every spur.
        std::uint64_t PlayHorseFeedbackSound                          = 0x0; // 0x180CFA924
        // The rider input's half of the ride-state callback: it aligns the
        // input to the horse `C_Player::GetRiddenHorse` resolves, and
        // dereferences it unchecked on states 1 and 3.
        std::uint64_t C_RiderPlayerInput__OnHorseRideStateChanged     = 0x0; // 0x180618200
        std::uint64_t C_Player__GetRiddenHorse                        = 0x0; // 0x180A4F980
        std::uint64_t ShouldHorseThrowOffRider                        = 0x0; // 0x180CF8524, asked by HorseRuntimeData::Update
        // `UpdateHorseCollisionAvoidance(state, frameTime, skipAvoidance)`,
        // run by `HorseRuntimeData::Update` from the parallel entity update.
        // While a rider input is active it queues probe rays on the shared
        // deferred ray queue below.
        std::uint64_t UpdateHorseCollisionAvoidance = 0x0; // 0x1806CF09C
        // Three more of `HorseRuntimeData::Update`'s reactions, all on the
        // runtime itself: the fast stop in front of an obstacle, which sets
        // ride state 6, the throw-off, which asks the rider sync for
        // state 7 through `C_RiderSync::RequestRideState7To9`, and the gait
        // transitions -- idle2move, which holds the speed at zero for 0.1 s
        // and sets ride state 4, and move2idle, which sets ride state 5.
        // Each starts the horse's own reaction action first, through
        // `TryStartHorseReactionAction(runtime, type)`, a free function.
        std::uint64_t C_HorseDirector__UpdateFastStopReaction        = 0x0; // 0x1806CF030
        std::uint64_t UpdateHorseThrowOffRiderState                  = 0x0; // 0x180CF82C4
        std::uint64_t TryStartHorseReactionAction                    = 0x0; // 0x180CF97C8
        std::uint64_t C_HorseDirector__UpdateGaitTransitionReactions = 0x0; // 0x1806CFB0C
        // Two more passes of `HorseRuntimeData::Update`, in its order:
        // `UpdateHorseMovementSpeed(speedState = runtime+0x148, frameTime)`
        // moves the runtime speed (+0x110) toward the commanded gait's
        // speed at 10 m/s^2 (8 down, 26 on a fast stop), and
        // `ApplyRuntimePaceAndDelta(runtime, request)`, from
        // `BuildAndSubmitMovementRequest`, puts the pace of the style that
        // speed falls in (`GetMoveStyleForRuntimeSpeed180A5169C`) on the
        // request as its pseudo speed, with the delta move (0, speed, 0).
        // The pseudo speed is what the MoveSpeed tag is resolved from.
        std::uint64_t UpdateHorseMovementSpeed                  = 0x0; // 0x1806CEC98
        std::uint64_t HorseRuntimeData__ApplyRuntimePaceAndDelta = 0x0; // 0x1806CF3D4
        // The player-only exhaustion reaction of the same update, which
        // pulls a horse up on its own soul's stamina.
        std::uint64_t C_HorseDirector__UpdateExhaustedReaction = 0x0; // 0x181381FE8

        // The deferred ray queue at `C_WHGameModules+0x40` (CryEngine's
        // `DeferredActionQueue`): queue with and without a skip list, cancel
        // and the per-frame update. None of them lock, yet entity updates on
        // `JobWorker_NN` reach the queue.
        std::uint64_t DeferredRayQueue__Queue       = 0x0; // 0x1805305F4
        std::uint64_t DeferredRayQueue__QueueNoSkip = 0x0; // 0x180531DC0
        std::uint64_t DeferredRayQueue__Cancel      = 0x0; // 0x1803ECF9C
        std::uint64_t DeferredRayQueue__Update      = 0x0; // 0x180531BA4
        std::uint64_t C_Actor__SubmitCharacterMoveRequest             = 0x0; // 0x1809D2134
        std::uint64_t C_CombatPlayerInput__FilterMovementRequest      = 0x0; // 0x180830BCC
        std::uint64_t C_CombatModule__GetLocalPlayerCameraManager     = 0x0; // 0x180B26BA4
        std::uint64_t C_CombatAlignmentManager__ProcessClipEvents     = 0x0; // 0x180A70F20, slot 6
        std::uint64_t C_CombatAlignmentManager__UpdateSolverTargets   = 0x0; // 0x1809E2D08, slot 7
        std::uint64_t C_AlignmentManager__SetPositionSolver           = 0x0; // 0x180A72B70, I_AlignmentManager slot 6
        std::uint64_t C_AlignmentManager__SetRotationSolver           = 0x0; // 0x180A728E4, I_AlignmentManager slot 20
        std::uint64_t C_CombatProceduralClipAnimateCamera__IsOwnerPlayerControlled = 0x0; // 0x180C50454
        std::uint64_t C_ProceduralClipCameraBoneAnimated__OnEnter      = 0x0; // 0x18286B5AC
        std::uint64_t C_ProceduralClipCameraBoneAnimated__OnExit       = 0x0; // 0x18286B71C
        std::uint64_t C_ProceduralClipCameraBoneAnimated__Update       = 0x0; // 0x18286D02C
        std::uint64_t C_ProceduralClipAnimateCamera__OnEnter           = 0x0; // 0x1812EC87C, clip "AnimateCamera"
        std::uint64_t C_ProceduralClipAnimateCamera__OnExit            = 0x0; // 0x1812EC8D8
        std::uint64_t CProceduralClipCameraNear__OnEnter               = 0x0; // 0x180897B28, clip "CameraNear"
        std::uint64_t CProceduralClipCameraNear__OnExit                = 0x0; // 0x180897B68
        std::uint64_t C_CombatActorMercyKill__RequestMercyKill         = 0x0; // 0x18275ED74
        std::uint64_t C_CombatActorMercyKill__OnMercyKillActionFinished = 0x0; // 0x18275E598
        std::uint64_t C_RiderPlayerControl__SetHorseMountingGlobal    = 0x0; // 0x1806182B4
        std::uint64_t C_RiderPlayerControl__SetHorseMountedGlobal     = 0x0; // 0x180618278
        std::uint64_t C_RiderPlayerControl__SetHorseDismountingGlobal = 0x0; // 0x18061823C

        // `C_ActorMovementController::RequestMovement(CMovementRequest &)`,
        // which merges a request into the controller's own and always answers
        // true. Every horse reaches it every frame from its runtime
        // (`HorseRuntimeData::BuildAndSubmitMovementRequest`), stating a pace
        // of zero while nothing feeds its runtime speed -- so a horse copy the
        // mod walks on a request of its own has to be shielded from the
        // animal's own.
        std::uint64_t C_ActorMovementController__RequestMovement = 0x0; // 0x180837E7C
        std::uint64_t C_ActorMovementController__UpdateMovementState = 0x0; // 0x1806412AC, vslot 16
        std::uint64_t OnHorseRiderChanged = 0x0; // 0x180A1C770, (context, old rider sync, new rider sync)

        // The controller's start / stop / turn transitions switched together
        // (three slots of the object at controller+0x300). Combat mode turns
        // them on and off on entry and exit. A remote player's body keeps
        // them off because their root motion would move it away from the
        // replicated pose.
        std::uint64_t C_ActorMovementController__SetMoveTransitions = 0x0; // 0x1808FFBC8

        // `C_DogCompanion::UpdateObjectivePlanner()`, the sole entry point
        // that scores every dog objective and switches the running one.
        //
        // It reads the current objective's priority, runs a visitor over the
        // fixed objective vector and the queued requests, and starts the
        // winner. Detouring it is how a replicated dog that this client does
        // not own is made inert: possession, animation, physics, audio and
        // NPC perception all keep working, and only the deciding stops.
        //
        // Not optional. Two objectives -- Teleport and Runaway's return --
        // write the dog's transform straight through actor virtuals, so a
        // wire-driven body cannot merely be corrected against them.
        std::uint64_t C_DogCompanion__UpdateObjectivePlanner = 0x0; // 0x1809F6CD8

        // `wh::xgenaimodule::activitysystem::C_DogCompanion::GetObjective(E_DogObjective)`.
        //
        // The companion's instance of one objective, from the map at
        // `+0x1468`. A key that is absent, disabled or out of order gets a
        // fixed fallback entry rather than null, so the same pointer answers
        // for many keys -- which is what makes the running objective's own id
        // findable by elimination, an objective carrying no key of its own.
        std::uint64_t C_DogCompanion__GetObjective = 0x0; // 0x1809F6B90

        // `C_WaitDogObjective::SelectNextWaitBehavior()`.
        //
        // The sit/lie-down chooser. It picks an entry from the objective's
        // configured wait set, then `BeginWaitBehavior` starts it -- which is
        // what queues the `C_DogActionAnim` that is the posture. Hooked only
        // to see whether it runs, and which of the three behaviour types it
        // lands on.
        std::uint64_t C_WaitDogObjective__SelectNextWaitBehavior = 0x0; // 0x1809F40F0

        // `wh::xgenaimodule::activitysystem::C_DogCompanion::DeactivateRunningObjective()`.
        //
        // The first half of the planner's own switch: it drops the running
        // objective from the active list and calls
        // `C_ActivityObjective::InterruptImpl`. The second half is the new
        // objective's vtable slot 2. Suppressing the planner takes both away
        // from a replicated dog, which is why it has no objective at all --
        // and therefore none of the actions that make it sit, lie or bark.
        std::uint64_t C_DogCompanion__DeactivateRunningObjective = 0x0; // 0x1809F4CA8

        // --- The dog bond, which is what petting actually is ---------------
        //
        // `C_Dog::Bond` (`0x1829DF430`) is the whole of it: it builds a
        // `C_DogExternalActionSyncAnimMaster` against the dog's own action
        // queue, fills it with the *local* player's entity id and a variant
        // rolled from the game's LCG, and queues it. The action's `Enter`
        // then puts the matching `syncAnimMaster` half on the person. The
        // objective planner is not involved at any point -- the running
        // objective simply stops while the animal is busy.
        //
        // The pieces below are what it takes to do the same thing with the
        // partner supplied instead of resolved, which is what a client that
        // does not own the dog has to do.

        // The action's constructor, `(slot, dogActionActor)`.
        std::uint64_t C_DogExternalActionSyncAnimMaster__Construct = 0x0; // 0x1829D54F4

        // The praise clip for one dog, out of its own animation database.
        std::uint64_t GetDogBondClipId = 0x0; // 0x1829E12B0

        // The partner half of the same pair, which is global.
        std::uint64_t GetDogBondPartnerClipId = 0x0; // 0x1829E12FC

        // `C_ActionActor::QueueAction(I_Action **)`, the surviving symbol at
        // `0x18039D1B8` -- vtable slot 4 of every actor's action queue, and
        // the one place an action of any kind is posted to a body.
        //
        // `I_Action` vtable slot 14 answers with its `E_ActorActionType`,
        // which `C_ActionActor::FindRiderActionPredicate` (`0x180856E68`) is
        // the proof of: it compares exactly that against the type it was
        // asked for.
        std::uint64_t C_ActionActor__QueueAction = 0x0; // 0x18039D1B8

        // `wh::xgenaimodule::activitysystem::SetPlayerDogMode(E_DogCompanionMode)`.
        //
        // The game's own reflected mode setter, shared with the console's
        // `wh_ai_Dog_SetMode` and with C_ScriptBindXGenAIModule. It resolves
        // the LOCAL player's primary companion of category 1, so it reaches
        // exactly the dog this client owns and nothing else -- which is the
        // only dog whose mode this client has any business writing.
        std::uint64_t SetPlayerDogMode = 0x0; // 0x183303BB8

        // The dog activity-owner resolver, `0x1808ADD68`.
        //
        // Takes the address of a soul's WUID in RDX -- its first integer
        // argument is dead -- asks the NPC manager for the AI object behind
        // it, and returns that object's kind-8 sub-system entry. Both
        // `C_DogInteractionManager::GetDogActor` and the reflected
        // `SetPlayerDogMode` are this call and nothing else.
        //
        // Called rather than reimplemented: the walk inside it is three
        // offsets deep, and the object it lands on is the one whose `+0x18`
        // every dog objective path calls through. The entity-side `C_Dog` is
        // NOT that object.
        std::uint64_t ResolveDogActivityOwner = 0x0; // 0x1808ADD68

        // The NPC registry, `wh::xgenaimodule::C_NPCManager`. Holds the AI
        // half of every actor that passed its spawn sink, a KCDC-spawned body
        // included.
        std::uint64_t C_NPCManager__Instance = 0x0; // 0x185494490

        // The brain-template registry the record initialiser looks a soul's
        // brain GUID up in.
        std::uint64_t AIBrainTemplateRegistry = 0x0; // 0x185493118

        // `BuildBrainForGuid(registry, const CryGUID *, C_NPC *, 0, 0)`.
        //
        // Looks the template up and walks its subbrain list through
        // CreateSubbrainByName18041A4DC. This is the one call that can give a
        // spawned dog the `companion_playerDog` brain -- and with it the
        // type-8 `DogCompanion` subbrain, the companion object, and a mind.
        std::uint64_t BuildBrainForGuid = 0x0; // 0x180419508

        // `C_DogInteractionManager::QueueDogObjective(objective, context,
        // callback, commandSource)`.
        //
        // The single choke point every dog command passes through: call,
        // contextual order, feed, item search, distract, pet, and the bark a
        // mode change acknowledges. Hooked only to say what the game asked the
        // dog for, because a command that then takes the process down leaves
        // nothing else behind -- the game's fatal trap at 0x181A72980 is a
        // deliberate null write shared by terminate, purecall and abort, so a
        // minidump taken there names no useful frame.
        std::uint64_t C_DogInteractionManager__QueueDogObjective = 0x0; // 0x182A01A70

        // --- Native audio ------------------------------------------------

        // Holder of the game's single `CWh_FmodStudioWrapper` -- Warhorse's
        // fork of CryEngine's `CryAudio::Impl::Fmod::CImpl`. It carries the
        // FMOD Studio system, its core system, and the listener attributes the
        // game last pushed, which is everything voice needs to play through
        // the game's own mixer instead of a device of its own.
        //
        // Reach it through `SDK::wh::CWh_FmodStudioWrapper::GetInstance`, and
        // only once `IsReady()`: the wrapper is constructed well before
        // `InitializeFmodStudio` fills it in.
        std::uint64_t CWh_FmodStudioWrapper__Instance = 0x0; // 0x18547E4D8

        // --- Quest graph -------------------------------------------------

        // Counterpart of `CryActionAllocate` above: the game's free. Below a
        // page `std::_Allocate<16>` and `std::_Deallocate<16>` reduce to
        // exactly this pair, so a buffer the mod takes from `CryActionAllocate`
        // is one the game can free from inside a std::vector.
        std::uint64_t FreeActionStorage = 0x0; // 0x181AB5160

        // `C_SharedResource`'s reference counting, which is also its
        // registration: the zero-to-one AddRef is what enters a node or a port
        // into the concept resource registry and stamps its real id, and the
        // one-to-zero Release is what takes it back out.
        std::uint64_t C_SharedResource__AddRef  = 0x0; // 0x181E38F20
        std::uint64_t C_SharedResource__Release = 0x0; // 0x181E39320

        // Answers the registry interface itself, two fixed hops inside the
        // concept module. Its slots are spelled by
        // `SDK::wh::conceptmodule::I_ResourceRegistry`.
        std::uint64_t ConceptResourceRegistry__Get = 0x0; // 0x180646DD4

        // The default constructors the reflected node factories call. Neither
        // allocates: allocate with `CryActionAllocate`, construct, then AddRef,
        // which is the sequence at 0x180DC48D0 and 0x180ACF770.
        std::uint64_t C_Quest__Construct     = 0x0; // 0x180DC4B80
        std::uint64_t C_Objective__Construct = 0x0; // 0x180ACF804

        // `C_ConstantPort::Create(_smart_ptr<C_ConstantPort> &, const
        // CryStringT<char> &name, std::string &value)`. Registers the port and
        // MOVES the value string into game-owned storage, so the string has to
        // be `SDK::StdString` rather than the mod's own `std::string`.
        std::uint64_t C_ConstantPort__Create = 0x0; // 0x1806B250C

        // A node's own port list. `AddPort` binds the port's owner and appends
        // it, consuming one reference; `FindPortByName` is the lookup half of
        // port-link resolution. KCDC binds its constant ports through the
        // reference cache instead, so neither is on its path -- they are the
        // way a node's ports are reached at all.
        std::uint64_t C_Node__AddPort        = 0x0; // 0x180697390
        std::uint64_t C_Node__FindPortByName = 0x0; // 0x180699818

        // `MakeVariant(rttr::variant &, const E_QuestProgress &)`: builds a
        // variant of the enum's own type and move-assigns it, destroying what
        // the destination held. A progress port has to answer one of these --
        // every reader goes through `variant::to_uint32`, which converts a
        // typed enum to its underlying value and cannot parse a string into
        // one. The template is instantiated once per enum and the copies are
        // byte-identical apart from the `lea` that loads the policy, which is
        // why that displacement is spelled out in the signature.
        std::uint64_t MakeQuestProgressVariant = 0x0; // 0x1816A4FA8

        // `C_Quest::GetLevelId`, which the journal row and the quest-marker
        // refresh both read. It resolves the quest's parent-node handle at
        // C_Node+0x1C, so a node with no parent answers -1.
        std::uint64_t C_Quest__GetLevelId = 0x0; // 0x180DC864C

        // The level the player is in, as `C_Quest::GetLevelId` answers for a
        // quest that has one. A quest built at runtime has no parent node to
        // resolve, so its level reads -1 and the journal files it under
        // "other region"; answering this instead files it where the player is.
        std::uint64_t GetCurrentLevelId = 0x0; // 0x1805550F4

        // `C_LogBase::C_LogBase`. Installs the base vtable, the two empty
        // strings and the embedded C_LocalizedString. The reflected factory
        // around it allocates 0x30 and zeroes it first, which is all a log
        // built this side has to reproduce.
        std::uint64_t C_LogBase__Construct = 0x0; // 0x181F43240

        // The two choke points for tracked-quest state, both
        // `(C_UIQuestLog *, C_Quest *)`. Every path in or out arrives at one
        // of them, and neither is edge-triggered.
        std::uint64_t C_UIQuestLog__BeginTrackingQuest = 0x0; // 0x180DC8B44
        std::uint64_t C_UIQuestLog__EndTrackingQuest   = 0x0; // 0x180DC8EA4

        // The slot a tracked quest holds, or -1: the game's own answer to
        // whether a quest is being followed.
        std::uint64_t LookupQuestTrackingId = 0x0; // 0x180DC5120

        // `wh::guimodule::AssignTrackingSlot(table, const CryStringT &name,
        // E_QuestType)`: writes the name into a free slot of its type's band
        // and answers the slot, or -1 when the band is full. The journal's
        // toggle and the auto-track both call it before BeginTrackingQuest,
        // which subscribes the quest but writes no name.
        std::uint64_t AssignQuestTrackingSlot = 0x0; // 0x180DC8E10

        // `C_UIQuestLog::CollectQuestMarkers(std::vector<S_QuestMarkerEntry> &)`,
        // sret in RDX: every map quest marker, one per marker of each active
        // or done objective of a followed quest. Its only caller is
        // `RefreshQuestMarkers`, so it is where a server quest's objective
        // positions join the game's own.
        std::uint64_t C_UIQuestLog__CollectQuestMarkers = 0x0; // 0x180DC5E68

        // `C_UIHudEvents::InvokeQuestFlashCommand(command, args...)`: sends a
        // named quest command to the HUD's Flash element. The pattern runs
        // past 0x1808ECB60, identical up to its first argument load (a float
        // there, an int here).
        std::uint64_t C_UIHudEvents__InvokeQuestFlashCommand = 0x0; // 0x180DC6D94

        // `RefreshQuestMarkers(C_UIMap *)`: collect, diff against the map's
        // copy at +0x680, push added rows and remove dropped ones.
        std::uint64_t RefreshQuestMarkers = 0x0; // 0x180DC577C

        // `std::vector<S_QuestMarkerEntry>::_Emplace_reallocate`, which grows
        // a marker list on the game's heap. Resolved through its call in the
        // per-objective collector (0x181F459A9): the body is a template shared
        // in shape with every other 0x38-byte vector.
        std::uint64_t std_vector_S_QuestMarkerEntry__Emplace_reallocate = 0x0; // 0x180DC8068

        // `C_Log`'s vtable. `C_LogBase` is abstract -- slots 2 and 3 are
        // `_purecall` -- and the journal calls both while building a row, so a
        // log left carrying the base's vtable aborts the process the moment an
        // objective is drawn. `C_Log` adds no members: its factory allocates
        // the same 0x30, runs the base constructor and overwrites the vtable
        // pointer, which is the whole of the derived object.
        std::uint64_t C_Log__VTable = 0x0; // 0x183ED62B8

        // `C_Quest::GetProgress`, the game's own read of the progress port at
        // quest+0xB0. It is what `CollectActiveQuests` filters on and what
        // fills the journal row's State, so it is also the way the mod checks
        // that a port it bound reads back as the value it wrote.
        std::uint64_t C_Quest__GetProgress = 0x0; // 0x1823D5EF0

        // The two ways anything reads the quest set. Both walk the same weak
        // handle vector at `C_NodeManager+0x48`, and between them they are
        // everything the journal, the diary and the quest markers see -- which
        // is what makes them the seam a server-authored quest arrives through.
        std::uint64_t C_QuestManager__CollectActiveQuests = 0x0; // 0x180DC4AD0
        std::uint64_t C_QuestManager__FindQuestByName     = 0x0; // 0x180DC4988

        // `C_RPGInventory::GetShopItemPrice`, slot 6 of the RPG rules object.
        // The only thing the trade screen asks for a price, for either side
        // of the counter, which is what makes it the one place a server's
        // price list can replace the game's.
        std::uint64_t C_RPGInventory__GetShopItemPrice = 0x0; // 0x180566410

        // `C_ShoppingTransaction::ApplyTransaction`, slot 10 of
        // I_ShopTransaction: money first, then every basket item, then the
        // RPG event. The single point a completed trade goes through.
        std::uint64_t C_ShoppingTransaction__ApplyTransaction = 0x0; // 0x1808D5150

        // `wh::shopmodule::GetShopSaleRefusal(self, item, row)`: whether the
        // shop takes one of the player's items, 0 meaning yes. The filter the
        // trade screen keeps on the player's column, and its only caller.
        std::uint64_t shopmodule__GetShopSaleRefusal = 0x0; // 0x181499EB8

        // `wh::shopmodule::TransferMoneyWithKeeper(unused, keeperSoul, amount)`.
        // Moves money straight between the player's purse and the keeper's
        // actor, with no transaction: the fallback `Shop.AcceptTransaction`
        // and `Shop.DoMoneyTransaction` take when no open deal is found.
        std::uint64_t shopmodule__TransferMoneyWithKeeper = 0x0; // 0x182EFECEC

        // The native Talk entry on an NPC. `BasicAIActions:GetActions` offers
        // it when `C_Human::CanTalk` (C_Actor slot 163) says yes, and pressing
        // it runs `C_Actor::RequestDialog` (slot 162) on the NPC with the
        // player as target -- so the pair is the gate and the door of the
        // game's own E-prompt conversation.
        std::uint64_t C_Human__CanTalk       = 0x0; // 0x18082A410
        std::uint64_t C_Actor__RequestDialog = 0x0; // 0x18093AE58

        // A native conversation stands a "twin" in for every participant:
        // `CreateTwin` spawns a copy that shares the original's soul and so
        // lands in its second actor handle. `CreateAndPlaceTwins` makes them
        // all and then places each one through `PlaceTwin`, answering false
        // -- and the conversation is abandoned -- when a twin could not be
        // made. Its placing pass runs even then, and `PlaceTwin` dereferences
        // the missing twin.
        std::uint64_t dialogmodule__CreateAndPlaceTwins = 0x0; // 0x180C89AE4
        std::uint64_t dialogmodule__PlaceTwin           = 0x0; // 0x180C8AEB0

        // `C_ScriptBindHuman::CanBeRobbed`, the Lua bind the same menu gates
        // its Rob entry on. The answer is a cached pickpocket state with no
        // native query above it, so the bind itself is the seam.
        std::uint64_t C_ScriptBindHuman__CanBeRobbed = 0x0; // 0x1804CECE8

        // `C_ShopModule::GetOrCreateTransaction(u64 shopWuid)`. Binds the deal
        // to the local player and the shop on both its paths.
        std::uint64_t C_ShopModule__GetOrCreateTransaction = 0x0; // 0x180C47C10

        // `I_ShopTransaction::Close()`: hands the basket back, zeroes both
        // parties and clears the open flag.
        std::uint64_t I_ShopTransaction__Close = 0x0; // 0x182048070

        // `C_UIShop::Close()`. Takes the trade screen down and unblocks
        // E_UIState::Shop -- the same path the screen's own close runs.
        std::uint64_t C_UIShop__Close = 0x0; // 0x1808C2360

        // The pickpocketing minigame, from the thief's side. `Start` binds the
        // victim and begins the charge; `OpenLootUI` builds the 11-tile grid
        // out of the victim soul's inventory once the charge is full;
        // `ResolvePickpocketingOutcome` is the one place every ending passes
        // through -- loot screen closed, caught while charging, interrupted --
        // carrying the WUIDs taken; `Finish` (vtable slot 5) is the native end
        // that closes the screen without resolving anything.
        std::uint64_t C_Pickpocketing__Start                       = 0x0; // 0x182EB0F74
        std::uint64_t C_Pickpocketing__OpenLootUI                  = 0x0; // 0x182EB1D84
        std::uint64_t C_Pickpocketing__ResolvePickpocketingOutcome = 0x0; // 0x182EAF5C8
        std::uint64_t C_Pickpocketing__Finish                      = 0x0; // 0x182EB2614

        // `C_Minigame` vtable slot 24: sends an AI message to the minigame's
        // interaction target. Pickpocketing sends `minigame:pickpocket` through
        // it, which is what runs the victim NPC's crime reaction.
        std::uint64_t C_Minigame__SendMessageToInteractionTarget = 0x0; // 0x182E96F40

        // `C_StealItemsEffect::Apply`: the one place a successful pickpocket
        // moves the taken items into the thief's inventory.
        std::uint64_t C_StealItemsEffect__Apply = 0x0; // 0x182CA9AFC

        // `wh::entitymodule::GetPickpocketLootGroup(item, actor)`: 2 leaves an
        // item out of the loot grid (equipped or drawn), otherwise its bag
        // group. Also what keeps a human's pouch/satchel flags current.
        std::uint64_t entitymodule__GetPickpocketLootGroup = 0x0; // 0x1809D14FC

        // The dice minigame. `StartGameWithTargetScore` starts a game on a
        // table against any actor; `InitializeDiceTableScene` resolves the
        // table's seat and badge tag points and `ResetDiceTableTransforms`
        // snaps both actors onto them. Every move a seat makes goes through
        // the game model's current state: `RequestSetDieHeld` and
        // `RequestScoreSelectedDice` are the model's entries, and the input
        // callbacks `OnHoldDieRequested`/`OnScoreRequested`/`OnPassRequested`
        // are what the local player's keys run. `RollDice` is the one place
        // faces are decided, `TransitionToRollingState` hands a seat its
        // throw and `TransitionToEndingState` ends the game. The opponent
        // seat's AI is asked in `C_SimpleDiceAI::TryHandleGameState`, and a
        // seat's badge exists only once `BindBackingItemById` binds its item.
        // `Finish` (slot 5) is the native end of the whole session.
        std::uint64_t C_Dice__StartGameWithTargetScore           = 0x0; // 0x182E97D0C
        std::uint64_t C_DieSet__UpdateDieNavigationCameraTransform = 0x0; // 0x182E89A8C
        std::uint64_t C_Dice__InitializeDiceTableScene           = 0x0; // 0x182E8C3E8
        std::uint64_t C_Dice__ResetDiceTableTransforms           = 0x0; // 0x182E92CBC
        std::uint64_t C_Dice__OnHoldDieRequested                 = 0x0; // 0x182E8FA84
        std::uint64_t C_Dice__OnScoreRequested                   = 0x0; // 0x182E91B3C
        std::uint64_t C_Dice__OnPassRequested                    = 0x0; // 0x182E919E0
        std::uint64_t C_UIDice__ShowDiceScore                      = 0x0; // 0x182BBB5A4, secondary interface +0x58
        std::uint64_t C_Dice__Finish                             = 0x0; // 0x182E98638

        // `InitializeDiceSession` seats both actors once the entry delay is
        // over, and ends by turning the opponent's C_NPC to look at the first
        // seat without checking there is one. The two members and the dice
        // generator's state are read off its own body.
        std::uint64_t C_Dice__InitializeDiceSession              = 0x0; // 0x182E93D30
        std::uint64_t C_Dice__SetDiceActorsInteractionMode       = 0x0; // 0x182E8D20C
        std::uint64_t C_Dice__InstallPlayerAction                = 0x0; // 0x182E97D68
        std::uint64_t playermodule__s_diceLcgState               = 0x0; // 0x18493C870
        std::uint64_t C_DiceGameModel__RequestSetDieHeld         = 0x0; // 0x182E97F64
        std::uint64_t C_DiceGameModel__RequestScoreSelectedDice  = 0x0; // 0x182E9848C
        std::uint64_t C_DiceGamePlayerModel__RollDice            = 0x0; // 0x182E99004
        std::uint64_t playermodule__TransitionToRollingState     = 0x0; // 0x182E7671C
        std::uint64_t playermodule__TransitionToEndingState      = 0x0; // 0x182E76438
        std::uint64_t C_SimpleDiceAI__TryHandleGameState         = 0x0; // 0x182E9189C
        std::uint64_t C_DiceBadge__BindBackingItemById           = 0x0; // 0x182E94B54

        // `ForceMetaroleDialog(actor, line)`: makes an actor say one of its
        // metarole barks. The dice table drives the opponent's commentary
        // through it (`KOSTKY_*`).
        std::uint64_t ForceMetaroleDialog = 0x0; // 0x18051A948

        // Progression. Every skill and stat change is one XP effect applied
        // by `ProcessEvent` (the two `Apply`s, effect slot 3); every perk add
        // is `C_PerkList::AddPerk`, and the perk screen's learn starts at
        // `C_RPGInventory::ApplyInventoryPerk`, which builds the event whose
        // `C_LearnPerkEffect` spends the point through
        // `C_PerkLearningState::LearnPerk`. The four creators build an XP effect on
        // an event, scaled (gameplay) or unscaled (level advances); the event
        // they hang on comes from the folded single-WUID factory the Lua XP
        // bindings use.
        std::uint64_t C_SkillXPEffect__Apply                    = 0x0; // 0x180C65588
        std::uint64_t C_StatXPEffect__Apply                     = 0x0; // 0x180C653E4
        std::uint64_t C_PerkLearningState__LearnPerk            = 0x0; // 0x182CC7EE0
        std::uint64_t C_PerkList__AddPerk                       = 0x0; // 0x18046B4A4
        std::uint64_t C_PerkList__RemovePerk                    = 0x0; // 0x180A2043C
        std::uint64_t C_PerkList__Respec                        = 0x0; // 0x1811217D8
        std::uint64_t C_PerkList__RefreshAvailablePerks         = 0x0; // 0x1804688C0
        std::uint64_t C_PerkList__OnEvent                       = 0x0; // 0x1809DB228, its I_EventListener slot: where a level becomes perk points
        std::uint64_t C_Soul__HasPerk                           = 0x0; // 0x1804D2760, primary vtable slot 115
        std::uint64_t PerkScreenState                           = 0x0; // 0x18119F604, what the perk screen draws a perk as
        std::uint64_t C_RPGInventory__Instance                  = 0x0; // 0x18492EC88, the stateless rules object's global
        std::uint64_t C_RPGInventory__ApplyInventoryPerk        = 0x0; // 0x1811A0A64, slot 38: the perk screen's learn
        std::uint64_t C_RPGInventory__ApplyRepairKit            = 0x0; // 0x182CFE688, slot 23: soul, kit, equipment
        std::uint64_t rpgmodule__CreateSkillXPEffect            = 0x0; // 0x180C65A20
        std::uint64_t rpgmodule__CreateSkillXPEffectUnscaled    = 0x0; // 0x1816849B4
        std::uint64_t rpgmodule__CreateStatXPEffect             = 0x0; // 0x180C66EF0
        std::uint64_t rpgmodule__CreateStatXPEffectUnscaled     = 0x0; // 0x180FA3218
        std::uint64_t rpgmodule__FindBuffDefinition             = 0x0; // 0x1808F81F8
        std::uint64_t rpgmodule__NotifySkillChanged             = 0x0; // 0x18046A9A0
        std::uint64_t C_EventFactory__CreateAlchemyStartedEvent = 0x0; // 0x1809DC2EC

        // Server inventory authority. The four natives that write an item's
        // health or quality, add it to a holder, or take it out are where the
        // projection learns the game changed the local inventory by itself.
        std::uint64_t C_Item__SetHealthCore                             = 0x0; // 0x180470078
        std::uint64_t C_Item__SetQualityCore                            = 0x0; // 0x1804699EC
        std::uint64_t C_ItemHolder__AddItem                             = 0x0; // 0x1804661A0
        std::uint64_t C_Item__CanMergeStackWith                         = 0x0; // 0x181F0DA90
        std::uint64_t C_Actor__UseItem                                  = 0x0; // 0x181E7E2C0
        std::uint64_t C_RPGInventory__ApplyFoodConsumption              = 0;   // 0x182CFDCD4, slot 27
        std::uint64_t C_RPGInventory__CalculateItemDecayRaw             = 0x0; // 0x181284168
        std::uint64_t C_RPGInventory__PrepareItems                      = 0x0; // 0x182CFC2AC, the body DryHerbs (slot 31) and PrepareFood (slot 30) share
        std::uint64_t C_ActorActionShootingMain__TryDischargeRaw        = 0x0; // 0x18289A17C
        std::uint64_t C_EquippableItemRuntimeData__AddOnEquipBuffRaw    = 0x0; // 0x182A68984
        std::uint64_t C_EquippableItemRuntimeData__RemoveOnEquipBuffRaw = 0x0; // 0x182A6AC38
        std::uint64_t C_UIInventoryBase__TransferItems                  = 0x0; // 0x1819B157C

        // Carts: the setup pass, the path the movement follows, the seat and
        // mount natives, and the one call that seats the local player.
        std::uint64_t C_Cart__SetupCartOnReset                 = 0x0; // 0x180F8F588
        std::uint64_t C_Cart__ConfirmMoveStart                 = 0x0; // 0x1828D5D08
        std::uint64_t C_Cart__SetDriver                        = 0x0; // 0x1828DC678
        std::uint64_t C_Cart__CanOccupyPosition                = 0x0; // 0x1828D4B40, I_Cart slot 44, hooked
        std::uint64_t FindCartByEntityId                       = 0x0; // 0x1828F4444
        std::uint64_t SetCartPath                              = 0x0; // 0x180F8ED44
        std::uint64_t MakeCartPathNode                         = 0x0; // 0x1828F078C
        std::uint64_t C_CartPath__AppendNode                   = 0x0; // 0x1828F0E98
        std::uint64_t C_CartPath__BuildFromPathEntity          = 0x0; // 0x1828F974C, hooked
        std::uint64_t SetCartActorState                        = 0x0; // 0x1828DCA58, hooked
        std::uint64_t C_CartPlayerControl__MountPlayer         = 0x0; // 0x1828F8A38, hooked
        std::uint64_t C_CartPlayerControl__CanPlayerMount      = 0x0; // 0x1828F1B50
        std::uint64_t C_DisablePlayerCartDismount__SetDismountDisabled = 0x0; // 0x18295D79C
        std::uint64_t C_CartPlayerControl__OnSeated            = 0x0; // 0x1828F8E0C, hooked
        std::uint64_t C_CartMoveContext__Update                = 0x0; // 0x1828D5824
        std::uint64_t C_CartMoveContext__Stop                  = 0x0; // 0x1828DD950
        std::uint64_t C_ActorStateExpansion__RequestCartAction = 0x0; // 0x18289FAB8, slot 17, hooked
        std::uint64_t C_ActorActionCart__RequestDismount       = 0x0; // 0x1828D7344, slot 37
        std::uint64_t ApplyCartReinsModifier                   = 0x0; // 0x1828FA228, hooked
        std::uint64_t DriveHorseToHarness                      = 0x0; // 0x1828E15F8
        std::uint64_t RollCartWheel                            = 0x0; // 0x1828FA3C8
        std::uint64_t AdvanceCartPathCursor                    = 0x0; // 0x1828F1624, hooked

        // The `AnimateCamera` clip's camera requests on its context
        // (`ProceduralContextACCM`): taken on entry, given back on exit.
        std::uint64_t C_ProceduralContextAnimateCameraControlMethod__Request = 0x0; // 0x1812EC934
        std::uint64_t C_ProceduralContextAnimateCameraControlMethod__Release = 0x0; // 0x1812ECA34

        // Women's bodies: the game's files extended as they load (one
        // Mannequin vocabulary for every human, her fragments behind a
        // `female` tag, her skeleton including the male clip list), every
        // woman's controller built on it, male clips' root motion on her root
        // joint, and the local body rebuilt where no animation job can be
        // reading it.
        std::uint64_t CXmlUtils__LoadXmlFromFile                          = 0x0; // 0x1809792B4, IXmlUtils slot 1, hooked
        std::uint64_t CAnimatedCharacter__InitializeMannequin             = 0x0; // 0x18093F108, hooked
        std::uint64_t GlobalAnimationHeaderCAF__GetControllerByJointCRC32 = 0x0; // 0x180844184, hooked
        std::uint64_t CharacterManager__SyncAllAnimations                 = 0x0; // 0x1804BE0B0, ICharacterManager slot 34, hooked
        std::uint64_t CAnimationManager__CreateGAH_CAF = 0x0; // 0x180B14D68
        std::uint64_t CAnimationSet__LoadFileCAF = 0x0; // 0x18083F874
        std::uint64_t CTexture__ReloadFile = 0x0; // 0x182518E20
        std::uint64_t C_ObjectDatabaseManager__Instance        = 0x0; // 0x180EF4F8C, the magic-static accessor
        std::uint64_t C_ObjectDatabaseManager__ReloadDatabases = 0x0; // 0x180EF4048
        std::uint64_t C_SchedulerDatabase__VTable             = 0x0; // 0x1840082E0
        std::uint64_t S_ModInfo__Construct                     = 0x0; // 0x182434BD8
        std::uint64_t CLocalizedStringsManager__LoadExcelXmlSpreadsheet = 0x0; // 0x18071AEC0
        std::uint64_t CRenderer__EF_ReloadFile = 0x0; // 0x1824F1FA8, IRenderer slot 136
        std::uint64_t CWh_FmodStudioWrapper__RegisterBank = 0x0; // 0x180B403E8
        std::uint64_t FSW_BANK__LoadMetadataAsync = 0x0; // 0x180B40850
        std::uint64_t FSW_BANK__RequestMetadataUnload = 0x0; // 0x18256D980
        std::uint64_t FSW_B_EVENT_INSTANCE__Start = 0x0; // 0x1803AF620, hooked
        std::uint64_t FSW_B_EVENT_INSTANCE__Update3DAttributes = 0x0; // 0x1804823C4, hooked
        std::uint64_t SAudioManagerRequestData512__Ctor = 0x0; // 0x181174690
        std::uint64_t C_ActorEquipmentHandler__ReattachToBody             = 0x0; // 0x1808D353C

        // A woman's hands on the man's joint frames, applied once as each
        // thing that carries them loads: her skeleton's bones, each of her
        // databases' clips (scanned, then converted before a clip counts as
        // loaded), her aim and look poses, and the copies a database's clips
        // were given, freed with it.
        std::uint64_t SkelLoader_Helpers__InitializeBones                 = 0x0; // 0x180B316F4, hooked
        std::uint64_t CDefaultSkeleton__LoadNewSKEL                       = 0x0; // 0x18051BFE8, hooked
        std::uint64_t CInternalDatabaseInfo__ReadController905            = 0x0; // 0x180766210, hooked
        std::uint64_t GlobalAnimationHeaderCAF__InitControllerLookup      = 0x0; // 0x180B152C8, hooked
        std::uint64_t CInternalDatabaseInfo__Destructor                   = 0x0; // 0x180956E44, hooked
        std::uint64_t CityHash64Lowercase                                 = 0x0; // 0x1806A3014

        // Shared character mesh upload, including streamed skin LODs.
        std::uint64_t CModelMesh__CreateRenderMesh = 0x0; // 0x18051E954, hooked

        // Editor layer draw filtering before native per-node cache mutation.
        std::uint64_t CObjManager__RenderObject         = 0x0; // 0x1806583AC, hooked
        std::uint64_t CObjManager__RenderObjectWithMask = 0x0; // 0x180658CBC, hooked
        std::uint64_t CObjManager__RenderBrush          = 0x0; // 0x180433470, hooked
        std::uint64_t CObjManager__RenderVegetation     = 0x0; // 0x180431B10, hooked
        std::uint64_t CObjManager__RenderObjectShadows  = 0x0; // 0x180433960, hooked
        std::uint64_t CMergedMeshRenderNode__Render     = 0x0; // 0x18078DE60, hooked
        std::uint64_t CParticleEmitter__Render          = 0x0; // 0x18065A0B8, hooked
        std::uint64_t CDeformableNode__Render           = 0x0; // 0x1834ED24C, hooked

        // Resolves patterns during startup, before engine hooks run.
        static void Init();
    };

    extern Patterns gPatterns;
} // namespace SDK
