#pragma once

#include <utils/safe_win32.h>

#include "shared/features/stash/stash_rpc.h"

#include "core/feature_registry.h"
#include "sdk/cryengine/scriptsystem/i_script_system.h"
#include "sdk/types.h"
#include "sdk/wh/entitymodule/c_inventory.h"
#include "sdk/wh/entitymodule/c_item.h"
#include "sdk/wh/entitymodule/c_item_holder.h"
#include "sdk/wh/entitymodule/c_player_interactor.h"
#include "sdk/wh/entitymodule/c_stash.h"
#include "stash.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>
#include <vector>

namespace KCDC::Features::Stash {
    /**
     * Client owner for the containers the server replicates.
     *
     * A `Stash` is one container; what spans all of them lives here -- the
     * replicated type and its RPCs, the per-frame sweep, the native open and
     * close edges the stash's own Lua raises, and the gate every open, unlock
     * and lockpick passes.
     *
     * The gate. A prompt's Lua handler is registered through
     * `C_PlayerInteractor::AddLuaAction`, which is the only place its
     * interaction type is in hand, and run by `InvokeLuaActionFunc` when the
     * player presses it. For a replicated container this holds the press,
     * asks the server, and runs the same handler again only when the server
     * allows it -- so a refused open never shows the transfer screen, and a
     * refused lockpick never starts the minigame.
     *
     * The restock refusal is not here: it applies to every stash in the world
     * rather than to the replicated ones, so it needs nothing from this
     * service and lives in `stash_hooks.cpp`.
     */
    class StashService final: public KCDC::Core::IFeatureService {
      public:
        /** Registers the contents and verdict RPCs. Once at startup. */
        static void Install();
        void Update() override;
        void Reset() override;
        void NativeContextChanged() override;
        void OnNativeOpened(SDK::wh::entitymodule::C_Stash *nativeStash, bool byPlayer);
        void OnNativeClosed(SDK::wh::entitymodule::C_Stash *nativeStash);

        /**
         * A prompt is about to be registered. Remembers what kind it is, by its
         * handler, and disables a lockpick the server says the container does
         * not offer -- on the record the interactor copies, before it does.
         */
        void OnActionRegistering(const SDK::wh::entitymodule::C_PlayerInteractor &interactor, SDK::wh::entitymodule::S_InteractorActionRecord &record, SDK::wh::entitymodule::E_InteractionType interactionType, SDK::HScriptFunction function);

        /**
         * A prompt was pressed. True when it was held for the server: the
         * caller then runs nothing. False lets the game run it now -- anything
         * that is not a replicated container, a close, or the gate's own replay.
         */
        bool InterceptInteraction(const SDK::wh::entitymodule::S_LuaActionInvocation &invocation);

        /**
         * Which key a replicated container asks for. True with `answer` set
         * when this service decides, false to let the game's own
         * `C_Stash::CanUnlock` answer.
         */
        bool AnswerCanUnlock(const SDK::wh::entitymodule::C_Stash *nativeStash, const SDK::CryGUID &levelKey, bool &answer) const;

        /** The container's own Lua unlocked it; reported when a granted key turned it. */
        void NotifyUnlocked(SDK::EntityId entityId);

        /** A lockpick succeeded on it; reported when the server granted the pick. */
        void NotifyLockpicked(SDK::EntityId entityId);

        /**
         * The transfer screen moves `items` into `destination`. True when this
         * decided it: a move between the local player and a replicated
         * container is asked of the server, any other is refused.
         */
        bool OnTransferRequested(SDK::wh::entitymodule::C_Inventory *destination, const SDK::StdVector<SDK::wh::entitymodule::C_Item *> &items, std::uint32_t amount);

        /** Any thread: the game changed `holder`; a container showing the server's rows there applies them again. */
        void NotifyNativeChange(const SDK::wh::entitymodule::C_ItemHolder *holder);

        /** Any thread: whether a container's projection holds this stack. */
        bool ManagesNativeItem(const SDK::wh::entitymodule::C_Item *item) const;

        /**
         * What `EntityModule.HasPlayerVisibleItems` answers for the inventory
         * a replicated container opens -- true for an empty one, as the open
         * prompt reads it -- from the server's word, not the native inventory
         * only its holder is shown. False to let the game answer.
         */
        bool AnswerHasVisibleItems(std::uint64_t inventoryWuid, bool &answer) const;

        /** Names the next container this session builds; unique while it lasts. */
        std::uint32_t NextSpawnOrdinal() {
            return _nextSpawnOrdinal++;
        }

      private:
        /** A press held for the server, with its own reference to the handler. */
        struct PendingInteraction {
            std::uint64_t stashNetworkId     = 0;
            SDK::EntityId entityId           = 0;
            SDK::HScriptFunction function    = 0;
            SDK::IScriptSystem *scriptSystem = nullptr;
            Shared::RPC::StashAction action  = Shared::RPC::StashAction::Open;
            std::uint64_t expiresAt          = 0;
        };

        /** The replica bound to a native container, or null. */
        Stash *FindReplica(const SDK::wh::entitymodule::C_Stash *nativeStash) const;
        Stash *FindReplica(SDK::EntityId entityId) const;

        /** The replica whose contents a container opens: its linked master, or itself. */
        Stash *ContentsReplica(Stash *stash) const;

        /** Rebuilds the native-to-replica index for this tick. */
        void RefreshIndex();

        /** Looks up the interaction types the gate recognises, once the interaction filter has loaded them. */
        bool ResolveInteractionTypes();

        /** Network thread: the server answered a held press. */
        void QueueVerdict(const Shared::RPC::StashInteractVerdict &verdict);
        void ApplyVerdicts();
        void ExpirePending();

        /** Runs a held handler as the game would have, with the gate out of the way. */
        void Replay(const PendingInteraction &pending);
        static void ReleasePending(const PendingInteraction &pending);

        /** Network ids, never replica pointers: a replica can go between two ticks. */
        struct BoundStash {
            const SDK::wh::entitymodule::C_Stash *nativeStash = nullptr;
            SDK::wh::entitymodule::C_Inventory *inventory     = nullptr;
            SDK::EntityId entityId                            = 0;
            std::uint64_t networkId                           = 0;
        };

        enum class IncomingMessageKind {
            Snapshot,
            VirtualOpen,
            Close,
        };

        struct IncomingMessage {
            IncomingMessageKind kind;
            std::uint64_t stashNetworkId = 0;
            nlohmann::json snapshot;
        };

        /** Network thread: parse a snapshot once, before taking the queue lock. */
        void QueueSnapshot(const std::string &body, IncomingMessageKind kind);

        /** Network thread: the server took a view away. */
        void QueueServerClose(std::uint64_t stashNetworkId);

        /**
         * The containers bound this tick. A flat list that keeps its capacity:
         * it is rebuilt every tick and searched only by the hooks, a few times
         * per prompt, over at most the interest budget's worth of entries.
         */
        std::vector<BoundStash> _bound;

        /** What kind of prompt each registered container handler is. Handles are registry indices, so a reused one is overwritten. */
        std::unordered_map<SDK::HScriptFunction, SDK::wh::entitymodule::E_InteractionType> _actionTypes;

        SDK::wh::entitymodule::E_InteractionType _openType     = SDK::wh::entitymodule::E_InteractionType::None;
        SDK::wh::entitymodule::E_InteractionType _unlockType   = SDK::wh::entitymodule::E_InteractionType::None;
        SDK::wh::entitymodule::E_InteractionType _lockpickType = SDK::wh::entitymodule::E_InteractionType::None;

        std::unordered_map<std::uint32_t, PendingInteraction> _pending;
        std::uint32_t _nextRequestId = 1;

        std::mutex _incomingMutex;
        std::vector<Shared::RPC::StashInteractVerdict> _incomingVerdicts;

        /** Set while a held handler runs again, so the gate lets it through and its unlock is reported. */
        std::uint32_t _replayDepth             = 0;
        SDK::EntityId _replayEntityId          = 0;
        Shared::RPC::StashAction _replayAction = Shared::RPC::StashAction::Open;

        /** Containers this client was granted a lockpick on, until when. */
        std::unordered_map<SDK::EntityId, std::uint64_t> _lockpickGrants;

        std::uint32_t _nextSpawnOrdinal = 1;

        /** Snapshots, virtual opens and closes in network arrival order. */
        std::vector<IncomingMessage> _incoming;
        std::atomic<bool> _nativeContextChanged = false;

        /** Open RPCs can precede replication construction. Bounded and expired. */
        struct PendingVirtualOpen {
            nlohmann::json snapshot;
            std::uint64_t expiresAt = 0;
        };
        std::unordered_map<std::uint64_t, PendingVirtualOpen> _virtualOpens;

        /**
         * The native inventories showing the server's rows, published by the
         * game thread for the item hooks that run on worker threads, and the
         * ones the game changed since the last update.
         */
        mutable std::mutex _projectedMutex;
        std::vector<const SDK::wh::entitymodule::C_ItemHolder *> _projected;
        std::vector<const SDK::wh::entitymodule::C_ItemHolder *> _changedHolders;
    };
} // namespace KCDC::Features::Stash
