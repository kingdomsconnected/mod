#pragma once

#include "core/feature_registry.h"

#include "door.h"

#include "shared/features/door/door_policy.h"
#include "shared/features/door/door_rpc.h"

#include "sdk/cryengine/entitysystem/i_entity.h"
#include "sdk/cryengine/entitysystem/i_entity_system.h"
#include "sdk/cryengine/scriptsystem/i_script_system.h"
#include "sdk/cryengine/scriptsystem/i_script_table.h"
#include "sdk/types.h"
#include "sdk/wh/entitymodule/c_actor.h"
#include "sdk/wh/entitymodule/c_anim_door.h"
#include "sdk/wh/entitymodule/c_player_interactor.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace KCDC::Features::Door {
    /**
     * The client half of door synchronization.
     *
     * Puts what the server says onto each native door -- when the level
     * builds it, and whenever the server changes it. The server knows every
     * door from the level's own data, so this never tells it one exists.
     *
     * An interaction is separate from that state: it arrives only for players
     * close enough to watch, and is replayed by handing the action to the
     * engine's own pipeline rather than rebuilding it here.
     */
    class DoorService final: public KCDC::Core::IFeatureService {
      public:
        /** Preview may unlock local doors and uses the same native swing as replicated doors. */
        Shared::Game::DoorPassage PreparePreviewPassage(std::uint64_t guid, SDK::EntityId actorId);

        /** Registers the door replica and its RPCs. Call once at startup. */
        static void Install();

        /** Applies replicated state, reports local edges, answers queries. */
        void Update() override;

        /** A hung door's AnimDoor to remove on the next tick; callable from the peer pump. */
        void QueueDespawn(SDK::EntityId entityId);

        /** Drops everything belonging to the session that just ended. */
        void Reset() override;

        /** Loading-thread edge: the native world this was bound to is gone. */
        void NativeContextChanged() override;

        // --- Local engine edges, raised by this feature's own hooks --------

        /**
         * The side a door's next clip is being chosen for. Every door
         * transition asks, whoever caused it; only the local player's own
         * use goes on to start an actor action on the door.
         */
        void NotifyAccessSide(SDK::EntityId doorEntityId, bool accessedFromFront);

        /**
         * An actor started an interactive action. The local player's on a
         * door is the one way a player works a door, and what makes the
         * transition this client's to report.
         */
        void NotifyActorAction(SDK::wh::entitymodule::C_Actor *actor, const char *actionName, SDK::EntityId objectEntityId);

        /** A successful lockpick made the door durably unlocked. */
        void NotifyLockpicked(SDK::EntityId doorEntityId);

        /**
         * Whether the server lets players work this door. AnimDoor.lua asks
         * the native IsInteractive every interaction scan, before it offers a
         * prompt and before a use moves anything, so false takes the prompt
         * away. Game thread.
         */
        bool IsUsable(SDK::EntityId doorEntityId);

        /**
         * The interaction gate is registering one of the local player's
         * prompts. A door's four -- open, close, unlock, lockpick -- are
         * remembered with their Lua handler, so a press can be told apart.
         */
        void OnActionRegistering(const SDK::wh::entitymodule::C_PlayerInteractor &interactor, SDK::wh::entitymodule::E_InteractionType interactionType, SDK::HScriptFunction function);

        /**
         * A pressed prompt is about to run its Lua handler. For a door's,
         * every change the press makes is put to the client's `doorInteract`
         * handlers first; true when one refused, and then nothing of the
         * press runs. Game thread.
         */
        bool InterceptInteraction(const SDK::wh::entitymodule::S_LuaActionInvocation &invocation);

        /**
         * A door's direction or lock bit is changing natively, whoever asked.
         * The local player's use and a lockpick claim theirs in the same Lua
         * call; any other the next tick puts back to the server's state. A
         * door being built can raise one from C_AnimDoor::Init's thread,
         * which need not be the game thread.
         */
        void NotifyNativeEdge(SDK::EntityId entityId);

        /**
         * A door has just been built by the level. Roots AnimDoor.lua's
         * methods while the class is loaded and records the door for the
         * next tick, which tells it what the server says.
         */
        void NotifyDoorInitialized(SDK::wh::entitymodule::C_AnimDoor *door);

        /** Plays one remote interaction, once its actor fence has passed. */
        void ApplyRemoteInteraction(const Shared::RPC::DoorInteraction &interaction);

        /**
         * Replaces the local arguments Lua supplied while an authoritative
         * transition is being applied. Outside that narrow call scope it is a
         * no-op, so ordinary game animation selection remains native.
         */
        void PrepareObjectAnimation(SDK::EntityId doorEntityId, SDK::EntityId &userEntityId, bool opening, bool &accessedFromFront);

      private:
        struct LocalActorFence {
            std::uint32_t incarnation    = 0;
            std::uint8_t stateEpoch      = 0;
            std::uint32_t combatRevision = 0;

            explicit operator bool() const {
                return incarnation != 0 && combatRevision != 0;
            }
        };

        struct PendingLocalTransition {
            SDK::EntityGUID doorGuid                 = 0;
            std::uint64_t presentationSequence       = 0;
            std::uint64_t startedAt                  = 0;
            Shared::RPC::DoorActorAction actorAction = Shared::RPC::DoorActorAction::None;
            LocalActorFence actorFence;
            bool fromFront = false;
        };

        struct AccessSide {
            SDK::EntityId doorEntityId = 0;
            bool fromFront             = false;
        };

        /** Network thread: an operator asked which door this player is at. */
        void QueueNearestQuery(const Shared::RPC::DoorNearestQuery &query);
        void AnswerNearestQueries();

        void ObserveNativeContext(const std::string &level);
        void DrainInitializedDoors();

        /** Tells every replicated door what the server says, where it differs. */
        void ApplyReplicatedState();
        bool ApplyDurable(Door &door);
        bool ApplyNativeState(std::uint64_t guid, bool open, bool locked, bool fromFront, SDK::EntityId userId, const Door *replica);

        /**
         * Marks every door the world moved since the last tick, and every
         * door whose report was lost, to be put back.
         */
        void DrainNativeEdges();

        /** The native edges this client's own report accounts for. */
        void ClaimNativeEdges(SDK::EntityId doorEntityId);

        void SendPendingTransitions();
        bool SendPendingTransition(SDK::EntityId doorEntityId);

        /** Drops a transition that will never be reported, and puts its door back. */
        void DiscardPendingTransition(SDK::EntityId doorEntityId, const char *reason);

        bool CaptureLocalActorFence(LocalActorFence &actorFence, const char **failureReason = nullptr) const;
        bool SendLocalInteraction(SDK::EntityGUID doorGuid, Shared::RPC::DoorEvent event, bool open, bool locked, bool fromFront, Shared::RPC::DoorActorAction actorAction, const std::string &objectAnimation, const LocalActorFence *actorFence = nullptr,
            std::uint64_t presentationSequence = 0);

        /** The replicated door for one level GUID, or null. */
        Door *Find(SDK::EntityGUID doorGuid) const;

        /** Rebuilds the GUID index for this tick. */
        void RefreshReplicaIndex();

        bool ReplayLockpicked(SDK::IEntity *entity);
        bool EnsureDoorDispatcher(SDK::IScriptTable *entityScript, std::string &errorMessage, bool preferGlobalEnvironment = false);

        /** Looks the door prompts' interaction types up by name, once; false when the filter lacks one. */
        bool ResolveInteractionTypes();

        /** AnimDoor:IsOnKeySide for the local player, by the rule the server's verdict reads. */
        bool LocalPlayerOnKeySide(SDK::IEntity *entity, const Door *replica) const;

        std::mutex _incomingMutex;
        std::vector<Shared::RPC::DoorNearestQuery> _incomingNearestQueries;
        std::atomic<bool> _acceptIncoming {false};
        std::atomic<bool> _captureLocalEvents {false};

        // C_AnimDoor::Init can run on a loading thread, so it only records a
        // GUID; the game thread touches everything else.
        std::mutex _initializedDoorsMutex;
        std::vector<SDK::EntityGUID> _initializedDoors;

        /** Rebuilt each tick, so it never outlives the replicas in it. */
        std::unordered_map<SDK::EntityGUID, Door *> _replicaIndex;

        /** Hung doors whose replica went, for the tick to take down. */
        std::mutex _despawnMutex;
        std::vector<SDK::EntityId> _pendingDespawns;

        std::unordered_map<SDK::EntityId, PendingLocalTransition> _pendingLocalTransitions;

        /** Valid only between a door's clip selection and its actor action. */
        AccessSide _accessSide;

        // Native edges nobody has claimed yet, locked for the same reason as
        // the initialized doors above.
        std::mutex _nativeEdgesMutex;
        std::vector<SDK::EntityId> _nativeEdges;

        // Reports that will never reach the server. Raised before this tick's
        // index is built, where a replica pointer from the last one may
        // already be gone.
        std::vector<SDK::EntityGUID> _strayedDoors;

        // Incremented from the loading thread and consumed on the game thread.
        std::atomic<std::uint64_t> _nativeContext {1};
        std::uint64_t _observedContext   = 0;
        std::uint32_t _observedLevelHash = 0;
        std::string _observedLevel;

        // Valid only while a Lua DoPlayAnimation call is on this thread.
        std::uint32_t _applyDepth                   = 0;
        SDK::EntityId _animationDoorEntityId        = 0;
        SDK::EntityId _animationUserEntityId        = 0;
        bool _animationOpening                      = false;
        bool _animationAccessedFromFront            = false;
        SDK::IScriptSystem *_dispatcherScriptSystem = nullptr;
        SDK::HScriptFunction _dispatcherFunction    = 0;
        std::uint64_t _dispatcherContext            = 0;

        /** A door prompt the interactor registered, and the Lua handler its press runs. */
        struct RegisteredPrompt {
            SDK::HScriptFunction function = 0;
            SDK::wh::entitymodule::E_InteractionType interactionType {};
        };

        // The prompts of the door the interactor last registered them for;
        // a press on anything else is not a door's.
        SDK::EntityId _promptTargetEntityId = 0;
        std::vector<RegisteredPrompt> _promptHandlers;
        SDK::wh::entitymodule::E_InteractionType _openType {};
        SDK::wh::entitymodule::E_InteractionType _closeType {};
        SDK::wh::entitymodule::E_InteractionType _unlockType {};
        SDK::wh::entitymodule::E_InteractionType _lockpickType {};
        bool _interactionTypesLooked = false;
        bool _interactionTypesFound  = false;
    };
} // namespace KCDC::Features::Door
