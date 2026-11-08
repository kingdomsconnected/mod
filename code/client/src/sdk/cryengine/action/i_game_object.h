#pragma once

#include "../../types.h"
#include "../system/cry_name.h"
#include "i_game_object_system.h"

#include <cstddef>

namespace SDK {
    class ICrySizer;
    class IGameObjectExtension;
    struct IGameObjectProfileManager;
    class IGameObjectView;
    class IMovementController;
    class INetChannel;
    class IScriptTable;
    class IWorldQuery;
    struct SGameObjectEvent;

    using NetworkAspectType = std::uint32_t;

    struct SmartScriptTable {
        IScriptTable *table;
    };

    enum class EBindToNetworkMode : int {
    };
    enum class EEntityAspects : std::uint32_t {
        /** The body's physics profile; `C_Actor` routes it to `C_RagdollManager::SetPhysicsProfile`. */
        Physics = 8,

        /**
         * Whether the actor updates at all.
         *
         * `C_Actor`'s profile manager (0x18087ACA0) handles exactly two
         * aspects: `Physics`, and this one, which it hands to the setter at
         * 0x180879768. That writes `C_Actor + 0x9B0` and calls
         * `C_Actor::RefreshUpdateSlot` (0x181E7DFE0), whose predicate is
         *
         *     (actor[0x9B2] & 0x40) == 0   // not put away
         *  && actor[0x9B0] == 0            // this profile
         *
         * and which enables or disables the game object's update slot on the
         * answer. **`C_Actor`'s constructor initialises `+0x9B0` to 3**, so an
         * actor does not update until something sets this profile to zero.
         *
         * Everything an actor does for itself hangs off that slot: its action
         * queue, and therefore every action's `Update`, and therefore the
         * `Exit` that ends one. A body left at the constructor's value renders
         * and can be posed, but never finishes an action it was given.
         *
         * **Do not set this from the mod.** Nothing in the shipped game does:
         * the writer behind this aspect (0x180879768) has one code caller,
         * `C_Actor`'s own profile manager, and the only aspect the engine routes
         * into that manager is `Physics`. Both ways of forcing it crash --
         * through `IGameObject::SetAspectProfile`, which indexes its network
         * aspect table with bit 29 and lands on the PGO build's unreachable stub
         * at 0x181A72980 (a store to address zero), and through the actor's
         * profile manager, which reaches `RefreshUpdateSlot` and re-enters the
         * Mannequin action controller mid-PostUpdate, dying in
         * `CActionScope::UpdateSequencers`'s slot-37 call.
         */
        Update = 0x20000000,
    };

    /** Profiles of `EEntityAspects::Update`. Only zero updates. */
    enum class EActorUpdateProfile : std::uint8_t {
        /** The actor updates: its action queue ticks and its actions can finish. */
        Full = 0,
        /** Set for a body being blended into a ragdoll; see 0x180879768. */
        Ragdollish = 1,
        /** Passed on to the entity as "dormant" (actor slot 63 with true). */
        Dormant = 2,
        /** What `C_Actor::C_Actor` leaves behind, and what never updates. */
        Constructed = 3,
    };

    /**
     * When a game object receives pre-physics updates, which is where an actor
     * does its movement work.
     *
     * `CGameObject::EvaluateUpdateActivation` reads the rule at 0x1807FF6A0:
     * `Always` answers true outright, `WhenAIActivated` answers
     * `hasAI ? ShouldUpdateAI() : true`, and anything else false.
     */
    enum class EPrePhysicsUpdate : int {
        Never           = 0,
        Always          = 1,
        WhenAIActivated = 2,
    };

    /**
     * What makes a game object's AI count as activated.
     *
     * The switch in `CGameObject::ShouldUpdateAI` (0x1805D4317), over bits 3-4
     * of the flags at CGameObject+0xB0, written by `SetAIActivation`.
     */
    enum class EGameObjectAIActivationMode : int {
        Never            = 0,
        Always           = 1,
        VisibleOrInRange = 2,
    };
    enum class EAutoDisablePhysicsMode : int {
    };
    enum class EUpdateEnableCondition : int {
    };

    class IActionListener {
      public:
        virtual ~IActionListener()                                                     = default;
        virtual void OnAction(const CCryName &action, int activationMode, float value) = 0;
        virtual void AfterAction()                                                     = 0;
    };

    /**
     * IGameObject subobject implemented by CGameObject at vtable 0x183A5DCD8.
     * CGameObject's separate 25-slot IEntityProxy vtable is at 0x183A5DC08.
     * KCD2 shuffles and extends this interface, so unresolved slots stay opaque.
     */
    class IGameObject: public IActionListener {
      public:
        virtual bool BindToNetwork(EBindToNetworkMode mode = {})                                                               = 0; // +0x018
        virtual bool BindToNetworkWithParent(EBindToNetworkMode mode, EntityId parentId)                                       = 0;
        virtual void ChangedNetworkState(NetworkAspectType aspects)                                                            = 0;
        virtual void EnableAspect(NetworkAspectType aspects, bool enable)                                                      = 0;
        virtual void EnableDelegatableAspect(NetworkAspectType aspects, bool enable)                                           = 0;
        virtual void DontSyncPhysics()                                                                                         = 0;
        // Two names where CryAction has one overloaded `QueryExtension`: MSVC
        // lays same-name virtuals out in reverse declaration order, which
        // would swap these two slots. The binary has the id lookup at +0x48
        // (0x180453FA4) and the name lookup, which resolves the id and calls
        // it, at +0x50 (0x180453F6C).
        virtual IGameObjectExtension *QueryExtensionById(IGameObjectSystem::ExtensionID id) const                              = 0; // +0x048
        virtual IGameObjectExtension *QueryExtensionByName(const char *extension) const                                        = 0; // +0x050
        virtual bool SetExtensionParams(const char *extension, SmartScriptTable params)                                        = 0;
        virtual bool GetExtensionParams(const char *extension, SmartScriptTable params)                                        = 0;
        virtual void SendEvent(const SGameObjectEvent &event)                                                                  = 0;
        virtual void ForceUpdate(bool force)                                                                                   = 0;
        virtual void ForceUpdateExtension(IGameObjectExtension *extension, int slot)                                           = 0;
        virtual ChannelId GetChannelId() const                                                                                 = 0; // +0x080, C_Actor::IsPlayerControlled
        virtual void SetChannelId(ChannelId channelId)                                                                         = 0;
        virtual INetChannel *GetNetChannel() const                                                                             = 0;
        virtual void FullSerialize(TSerialize serialize)                                                                       = 0;
        virtual bool NetSerialize(TSerialize serialize, EEntityAspects aspect, std::uint8_t profile, int flags)                = 0;
        virtual void PostSerialize()                                                                                           = 0;
        virtual bool IsProbablyVisible()                                                                                       = 0;
        virtual bool IsProbablyDistant()                                                                                       = 0;
        virtual bool SetAspectProfile(EEntityAspects aspect, std::uint8_t profile, bool fromNetwork = false)                   = 0;
        virtual std::uint8_t GetAspectProfile(EEntityAspects aspect)                                                           = 0;
        virtual void Opaque26()                                                                                                = 0;
        virtual IGameObjectExtension *GetExtensionWithRMIBase(const void *base)                                                = 0;
        virtual void EnablePrePhysicsUpdate(EPrePhysicsUpdate updateRule)                                                      = 0;
        virtual void SetNetworkParent(EntityId id)                                                                             = 0;
        virtual void Pulse(std::uint32_t pulse)                                                                                = 0;
        virtual void RegisterAsPredicted()                                                                                     = 0;
        virtual void RegisterAsValidated(IGameObject *gameObject, int predictionHandle)                                        = 0;
        virtual int GetPredictionHandle()                                                                                      = 0;
        virtual void Opaque34()                                                                                                = 0;
        virtual void RegisterExtForEvents(IGameObjectExtension *extension, const int *events, int eventCount)                  = 0;
        virtual void UnRegisterExtForEvents(IGameObjectExtension *extension, const int *events, int eventCount)                = 0;
        virtual void EnablePhysicsEvent(bool enable, int events)                                                               = 0;
        virtual bool WantsPhysicsEvent(int events)                                                                             = 0;
        virtual void AttachDistanceChecker()                                                                                   = 0;
        virtual bool SetAIActivation(EGameObjectAIActivationMode mode)                                                         = 0;
        virtual void SetAutoDisablePhysicsMode(EAutoDisablePhysicsMode mode)                                                   = 0;
        virtual bool ShouldUpdate()                                                                                            = 0;
        virtual void RequestRemoteUpdate(NetworkAspectType aspectMask)                                                         = 0;
        virtual bool CaptureView(IGameObjectView *view)                                                                        = 0;
        virtual void ReleaseView(IGameObjectView *view)                                                                        = 0;
        virtual bool CaptureActions(IActionListener *listener)                                                                 = 0;
        virtual void ReleaseActions(IActionListener *listener)                                                                 = 0;
        virtual bool CaptureProfileManager(IGameObjectProfileManager *profileManager)                                          = 0;
        virtual void ReleaseProfileManager(IGameObjectProfileManager *profileManager)                                          = 0;
        virtual void EnableUpdateSlot(IGameObjectExtension *extension, int slot)                                               = 0;
        virtual void DisableUpdateSlot(IGameObjectExtension *extension, int slot)                                              = 0;
        virtual std::uint8_t GetUpdateSlotEnables(IGameObjectExtension *extension, int slot)                                   = 0;
        virtual void EnablePostUpdates(IGameObjectExtension *extension)                                                        = 0;
        virtual void DisablePostUpdates(IGameObjectExtension *extension)                                                       = 0;
        virtual void SetUpdateSlotEnableCondition(IGameObjectExtension *extension, int slot, EUpdateEnableCondition condition) = 0;
        virtual void PostUpdate(float frameTime)                                                                               = 0;
        virtual IWorldQuery *GetWorldQuery()                                                                                   = 0;
        virtual bool IsJustExchanging()                                                                                        = 0;
        virtual void GetMemoryUsage(ICrySizer *sizer) const                                                                    = 0;
        virtual void *GetUserData() const                                                                                      = 0;
        virtual void SetUserData(void *userData)                                                                               = 0;

      protected:
        enum class EChangeExtension : int {
            Activate,
            Deactivate,
            Acquire,
            Release,
        };

        virtual IGameObjectExtension *ChangeExtension(const char *extension, EChangeExtension change) = 0;
        virtual void Opaque63()                                                                       = 0; // DoInvokeRMI; private body type intentionally not exposed.

      public:
        /** IGameObject::m_entityId, read by CAIProxy::GetActor 0x1806444A8. */
        EntityId GetEntityId() const {
            return m_entityId;
        }

      private:
        EntityId m_entityId; // 0x08

        static void AssertLayout() {
            static_assert(offsetof(IGameObject, m_entityId) == 0x08, "IGameObject::m_entityId offset mismatch");
        }
    };

    static_assert(sizeof(SmartScriptTable) == 0x8, "SmartScriptTable size mismatch");
} // namespace SDK
