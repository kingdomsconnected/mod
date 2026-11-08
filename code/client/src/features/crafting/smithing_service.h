#pragma once

#include <utils/safe_win32.h>

#include "core/feature_registry.h"
#include "sdk/wh/entitymodule/c_actor.h"
#include "sdk/wh/playermodule/c_blacksmithing.h"
#include "sdk/wh/playermodule/c_blacksmithing_slot_tool_attachment.h"
#include "sdk/wh/playermodule/c_minigame.h"
#include "shared/features/crafting/smithing_rpc.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace KCDC::Features::Crafting {
    /**
     * The local player's native smithing, with the server holding the
     * inventory.
     *
     * The game's minigame runs as in single player -- the forge, the anvil,
     * every strike -- except for the two places it touches the inventory.
     * Pressing Craft is held and asked of the server, which takes the
     * materials; the press is replayed once the server agrees, and the
     * recipe screen stays open when it does not. When the workpiece is over,
     * the game's own debit and its product, finishing XP and lockpicks are
     * suppressed, and the workpiece quality it reached is reported instead:
     * the server grants what it makes, through the inventory.
     */
    class SmithingService final: public KCDC::Core::IFeatureService {
      public:
        bool IsWorking() const {
            return _phase == Phase::Working;
        }
        void Update() override;
        void Reset() override;
        void NativeContextChanged() override;
        /** Network thread: queued until the next Update. */
        void OnVerdict(const KCDC::Shared::RPC::SmithingVerdict &verdict);
        void OnClosed(const KCDC::Shared::RPC::SmithingClosed &closed);

        /** The Craft callback. True lets the native run: without authority, or as the approved replay. */
        bool AcceptingRecipe(SDK::wh::playermodule::C_Blacksmithing *minigame);
        /** Native Cancel (slot 6): a normal exit request, unless the action was replaced first. */
        void Cancelling(SDK::wh::playermodule::C_Blacksmithing *minigame);
        /** The minigame's actor action was replaced; its Cancel that follows is no exit request. */
        void ActionReplaced(const SDK::wh::playermodule::C_Minigame *minigame);
        /** `FinishWorkpiece` is about to run; reports the workpiece this ends. */
        void FinishingWorkpiece(SDK::wh::playermodule::C_Blacksmithing *minigame, bool success);
        void FinishedWorkpiece(SDK::wh::playermodule::C_Blacksmithing *minigame);
        /** True where the native material debit must not run: the server settles materials. */
        bool SuppressNativeSettlement(SDK::wh::playermodule::C_Blacksmithing *minigame) const;
        /** True where RPG slot 73 must not grant: inside the local player's successful finish. */
        bool SuppressNativeOutcome() const;
        /**
         * A smithery tool coming to the local player's hand. It is the
         * smithery's, carried through the native inventory for as long as it
         * is held: lent to the projection, so the server's rows neither take
         * it nor count it. True lets the transfer through the authority.
         */
        bool LendingTool(const SDK::wh::playermodule::C_BlacksmithingSlotToolAttachment *tool, const SDK::wh::entitymodule::C_Actor *actor);
        /** The same tool going back; true lets the transfer through. */
        bool ReturningTool(const SDK::wh::playermodule::C_BlacksmithingSlotToolAttachment *tool) const;
        void ReturnedTool(const SDK::wh::playermodule::C_BlacksmithingSlotToolAttachment *tool);

      private:
        enum class Phase : std::uint8_t {
            Idle,
            // A Craft press is with the server.
            Awaiting,
            // The server took the materials; the press is replayed next frame.
            Approved,
            // The workpiece is on the anvil.
            Working,
            // The server ended the workpiece; the native minigame is to be cancelled.
            Closing
        };

        bool Owns(const SDK::wh::playermodule::C_Blacksmithing *minigame) const;
        static bool ReadSelectedRecipe(std::string &recipe);
        void Report(std::uint32_t session, KCDC::Shared::RPC::SmithingOutcome outcome, float quality);
        void Refused(KCDC::Shared::RPC::SmithingCode code) const;
        void ApplyVerdict(const KCDC::Shared::RPC::SmithingVerdict &verdict);
        void Clear();
        void PublishProjectionHold();
        /** How an unsettled workpiece ended without a result. */
        KCDC::Shared::RPC::SmithingOutcome UnfinishedOutcome() const;

        std::mutex _incomingMutex;
        std::vector<KCDC::Shared::RPC::SmithingVerdict> _verdicts;
        std::vector<KCDC::Shared::RPC::SmithingClosed> _closures;
        std::atomic<bool> _contextChanged {false};

        Phase _phase                                      = Phase::Idle;
        SDK::wh::playermodule::C_Blacksmithing *_minigame = nullptr;
        // The smithery the press was made at; the replay must land at the same one.
        SDK::EntityId _station = 0;
        std::string _recipe;
        std::uint32_t _request    = 0;
        std::uint32_t _session    = 0;
        std::uint64_t _deadlineMs = 0;
        bool _replaying           = false;
        // Set for the length of the local player's successful FinishWorkpiece.
        bool _finishingSuccess = false;
        bool _cancelRequested  = false;
        bool _actionReplaced   = false;
    };
} // namespace KCDC::Features::Crafting
