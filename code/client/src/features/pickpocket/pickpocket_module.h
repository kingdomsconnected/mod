#pragma once

#include "core/feature_registry.h"

#include "shared/features/pickpocket/pickpocket_rpc.h"

#include "sdk/types.h"
#include "sdk/wh/entitymodule/c_inventory.h"
#include "sdk/wh/entitymodule/c_item.h"
#include "sdk/wh/framework/wuid.h"
#include "sdk/wh/playermodule/c_minigame.h"
#include "sdk/wh/playermodule/c_pickpocketing.h"
#include "sdk/wh/rpgmodule/c_steal_items_effect.h"

#include <mafianet/get_time.h>

#include <atomic>
#include <cstdint>
#include <unordered_set>
#include <vector>

namespace KCDC::Features::Pickpocket {
    /**
     * Picking another player's pocket, on the game's own minigame.
     *
     * The local player robs a remote player's body with the native Rob entry,
     * charge, rosette and loot grid, and nothing about that changes. What
     * changes is where the victim's pockets come from and where the loot goes,
     * and that nobody's AI answers for the victim:
     *
     * - **Pockets.** A remote body's inventory holds only what its owner wears,
     *   so the grid would be empty. When the charge begins the server is asked
     *   for the victim's pockets -- their own client samples them -- and they
     *   are created into the body's soul inventory, which is exactly what
     *   `OpenLootUI` builds the grid from. The loot screen is held back until
     *   they are there, and while it is built the game's own loot filter
     *   leaves out everything else the body holds -- the shadow items the
     *   copy keeps for its hands and its bow are presentation, not pockets.
     *
     * - **The loot.** `C_StealItemsEffect::Apply` is where a successful
     *   pickpocket moves items to the thief. Out of a remote body it never
     *   runs: `ResolvePickpocketingOutcome` reports what the grid took, and the
     *   server takes it from the victim and grants the thief what really came
     *   out -- the vendor's order, so a claim can never mint items. What was
     *   created into the body is deleted again once the attempt is over.
     *
     * - **The victim's reaction.** The minigame tells its target about the
     *   outcome with an AI message, and that message is what starts an NPC's
     *   crime reaction -- the self-help dialogue that crashed the robber's
     *   client when the target was a player's body. A remote body is a
     *   person, not an NPC: the message is refused for it, and what being
     *   robbed means is the gamemode's, through the server's events.
     *
     * Detection stays native. The success chance the charge is held against
     * is `C_RPGMinigames::CalculatePickpocketingSuccessProbability`, fed by
     * the victim body's facing and both souls' stats -- and on this machine
     * those are the victim's own, replicated. Turning to face the thief is
     * what catches them, exactly as it does against an NPC.
     *
     * Game-thread only: the detours, the RPC handlers and the tick all run
     * there. The one exception is the loot filter, which the engine also
     * calls when an item is added to any body; it reads a single atomic flag
     * before anything else.
     */
    class PickpocketModule final: public KCDC::Core::IFeatureService {
      public:
        static PickpocketModule &Get();

        /** Installs the detours. Called once, at boot. */
        static void Install();

        void Update() override;
        void Reset() override;

        /** Raised on the loading thread; the tick drops the attempt without touching the minigame. */
        void NativeContextChanged() override;

        /** The native Start ran. Opens an attempt when the local player started it against a remote player's body. */
        void OnStarted(SDK::wh::playermodule::C_Pickpocketing *minigame);

        /** Whether the loot grid may be built now: not before this attempt's pockets are in the victim's body. */
        bool MayOpenLoot(const SDK::wh::playermodule::C_Pickpocketing *minigame) const;

        /** Brackets the native build of this attempt's grid, so the filter below knows when it applies. */
        void BeginLootGrid(const SDK::wh::playermodule::C_Pickpocketing *minigame);
        void EndLootGrid();

        /** While this attempt's grid is being built, whether an item must stay out of it: everything the stock did not create. */
        bool HidesFromLootGrid(const SDK::wh::entitymodule::C_Item *item) const;

        /** Every ending passes here, before the native resolves it; reports this attempt's outcome to the server. */
        void OnResolving(const SDK::wh::playermodule::C_Pickpocketing *minigame, bool success, const SDK::StackVector<SDK::wh::framework::WUID, 11> *taken, std::uint8_t reason);

        /** Whether a minigame's AI message must not reach its target: a remote player's body has no mind to answer it. */
        bool RefusesMessage(const SDK::wh::playermodule::C_Minigame *minigame) const;

        /** Whether a steal effect takes out of a remote player's body, and so must not move anything. */
        bool RefusesTransfer(const SDK::wh::rpgmodule::C_StealItemsEffect *effect) const;

        /**
         * A remote body is about to go. An attempt against it is ended the
         * native way first, while the body the loot screen reads is still
         * there, and the server is told it was abandoned.
         */
        void ForgetVictim(SDK::EntityId entityId);

      private:
        enum class Phase : std::uint8_t {
            Idle,
            /** The charge began; the server has the request and has not answered. */
            AwaitingVerdict,
            /** The pockets are in the victim's body; the grid may open. */
            Stocked,
            /** The outcome went to the server; the created items go once the minigame has let go. */
            Resolved,
        };

        void InstallRPCs();
        void OnVerdict(const Shared::RPC::PickpocketVerdict &verdict);
        /** Creates the stock into the victim body's soul inventory and remembers exactly which items that made. */
        bool Stock(const std::vector<Shared::RPC::PickpocketItem> &items);

        /**
         * Deletes every created item still loose in the victim body's
         * inventory. One the copy's hand logic has since equipped -- it
         * reuses items by class -- is in use and stays.
         */
        void Unstock();

        /** Ends the native minigame without resolving it, tells the server when it knew of the attempt, and forgets it. */
        void Abandon(bool tellServer);

        void SendResult(Shared::RPC::PickpocketOutcome outcome, const std::vector<Shared::RPC::PickpocketItem> &taken) const;
        void Clear();

        /** The victim body's soul inventory, or null once the body or its soul has gone. */
        SDK::wh::entitymodule::C_Inventory *VictimInventory() const;

        Phase _phase                                      = Phase::Idle;
        SDK::wh::playermodule::C_Pickpocketing *_minigame = nullptr;
        SDK::EntityId _victimEntityId                     = 0;
        std::uint64_t _victimNetworkId                    = 0;
        std::uint32_t _requestId                          = 0;
        std::uint32_t _nextRequestId                      = 1;

        /** The WUIDs `Stock` created, so nothing the body already carried is ever deleted. */
        std::unordered_set<std::uint64_t> _stocked;

        /** When waiting on the server gives up. */
        MafiaNet::Time _deadline = 0;

        bool _rpcsInstalled = false;

        /** True only while `OpenLootUI` builds this attempt's grid. Atomic: the filter it gates also runs off the game thread. */
        std::atomic<bool> _buildingGrid = false;

        /** Set from NativeContextChanged, acted on by the tick. */
        std::atomic<bool> _nativeContextLost = false;
    };
} // namespace KCDC::Features::Pickpocket
