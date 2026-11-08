#pragma once

#include <utils/safe_win32.h>

#include "alchemy_entry_continuation.h"
#include "core/feature_registry.h"
#include "native_alchemy_entry.h"
#include "sdk/cryengine/entitysystem/i_entity.h"
#include "sdk/wh/playermodule/c_alchemy.h"
#include "sdk/wh/playermodule/c_use_bellows_action.h"
#include "sdk/wh/playermodule/c_use_mortar_action.h"
#include "sdk/wh/playermodule/c_use_retort_action.h"
#include "shared/features/crafting/alchemy_character_view.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace KCDC::Features::Crafting {
    /**
     * Owns one connected player's native brewing session.
     *
     * The native minigame keeps running the game's own animations and
     * holders, but every step that changes anything is held at its detour,
     * asked of the server, and replayed only once the server has accepted
     * it. The game grants nothing itself: its outcome and exit events are
     * suppressed, and the server's inventory rows are the only reward. The
     * inventory projection is held while a step is in flight and until the
     * inventory revision the server named for it has arrived, so the table
     * move and the server's row change land together.
     */
    class AlchemyService final: public KCDC::Core::IFeatureService {
      public:
        void Update() override;
        void Reset() override;
        void NativeContextChanged() override;
        void OnServerResponse(std::string body);

        bool BeginScriptCall(std::uint64_t actor, std::uint64_t station);
        bool EndScriptCall(int originalValueCount);
        bool Starting(SDK::wh::playermodule::C_Alchemy *minigame, SDK::EntityId station);
        void Started(SDK::wh::playermodule::C_Alchemy *minigame, SDK::EntityId station, bool accepted);
        void Destroying(SDK::wh::playermodule::C_Alchemy *minigame);
        void Destroyed(SDK::wh::playermodule::C_Alchemy *minigame);
        bool Selecting(SDK::wh::playermodule::C_Alchemy *minigame, SDK::wh::playermodule::AlchemyItemType target);
        bool Returning(SDK::wh::playermodule::C_Alchemy *minigame);
        bool EntryReady(SDK::wh::playermodule::C_Alchemy *minigame);
        bool BellowsPumping(SDK::wh::playermodule::C_UseBellowsAction *action);
        bool BellowsReturning(SDK::wh::playermodule::C_UseBellowsAction *action);
        bool RetortPumping(SDK::wh::playermodule::C_UseRetortAction *action);
        bool RetortFinishing(SDK::wh::playermodule::C_UseRetortAction *action);
        bool RetortRequiredPulses(SDK::wh::playermodule::C_UseRetortAction *action, std::uint32_t &value);
        void MortarStarted(SDK::wh::playermodule::C_UseMortarAction *action, bool accepted);
        void Selected(SDK::wh::playermodule::C_Alchemy *minigame, SDK::wh::playermodule::AlchemyItemType target);
        void RequestingExit(SDK::wh::playermodule::C_Alchemy *minigame);
        void ShuttingDown(SDK::wh::playermodule::C_Alchemy *minigame);
        void ShutDown(SDK::wh::playermodule::C_Alchemy *minigame);

        bool SuppressNativeOutcome(SDK::wh::playermodule::C_Alchemy *minigame, SDK::wh::playermodule::AlchemyItemType container);
        bool ProjectNativeStart(SDK::wh::rpgmodule::I_Soul *soul, float &value, bool &executeNative);
        bool SuppressNativeFinish(SDK::wh::rpgmodule::I_Soul *soul);
        void ResetNative(SDK::wh::playermodule::C_Alchemy *minigame);

        /** Called from a detour: retires the session once the native stack has unwound. */
        void Fault(std::string reason);
        void ResourceConstructing(SDK::wh::playermodule::C_AlchemyResource *resource, SDK::wh::entitymodule::C_Item *source);
        void ResourceConstructed(SDK::wh::playermodule::C_AlchemyResource *resource, SDK::wh::entitymodule::C_Item *source);
        void BaseResourceConstructed(SDK::wh::playermodule::C_AlchemyResource *resource, SDK::wh::playermodule::AlchemyItemType selection);
        SDK::wh::playermodule::C_Alchemy *CurrentNative() const;
        bool IsNativeScope() const {
            return _nativeScopeDepth != 0;
        }
        const std::string &Session() const {
            return _entry.Approval().session;
        }
        /** The server's view of this player's alchemy character: entry bonus and recipe knowledge. */
        const KCDC::Shared::Crafting::AlchemyCharacterView &ProjectedCharacter() const {
            return _projectedCharacter;
        }
        bool EnterNativeScope(SDK::wh::playermodule::C_Alchemy *minigame);
        void LeaveNativeScope();
        bool EnterCleanupScope(SDK::wh::playermodule::C_Alchemy *minigame);
        void LeaveCleanupScope();
        bool EnterActionScope(SDK::wh::playermodule::C_Alchemy *minigame, SDK::wh::framework::I_Action *action);

      private:
        enum class ActionPhase : std::uint8_t {
            Idle,
            AwaitingApproval,
            Approved,
            Running,
            AwaitingCompletion,
            Completed
        };
        enum class LoopKind : std::uint8_t {
            None,
            Bellows,
            Retort
        };
        bool OwnsLoop(SDK::wh::framework::I_Action *action, LoopKind kind) const;
        SDK::wh::framework::I_Action *CurrentLoopAction() const;
        bool LoopIsIdle() const;
        bool RequestLoop(bool returnCurrent);
        void ClearLoop();
        bool CurrentIdentity(AlchemyEntryIdentity &identity) const;
        SDK::wh::playermodule::C_Alchemy *CleanupNative() const;
        bool PendingTerminal() const;
        void Ask(nlohmann::json request);
        void Send(const nlohmann::json &request);
        void Apply(const nlohmann::json &response);
        void ApplyPush(const nlohmann::json &response);
        bool BindCharacter(const nlohmann::json &response);
        void Retire(const std::string &reason, const char *endReason = "clientError");
        void DiscoverStations(SDK::IScriptTable &currentClass);
        void ProgressRetirement(SDK::IScriptTable &currentClass, std::uint64_t now);
        void UpdateSession();
        void UpdateInteraction(SDK::IScriptTable &currentClass, SDK::EntityId actor, std::uint64_t now);
        void PublishProjectionHold();
        void ResetOnGameThread();
        bool CompleteDisconnectedCleanup();
        bool ResolveIngredient(SDK::wh::playermodule::AlchemyItemType target, std::uint64_t &nativeId, std::string &itemId);
        bool RequestSelection(SDK::wh::playermodule::C_Alchemy *minigame, SDK::wh::playermodule::AlchemyItemType target, bool returnCurrent);
        std::string NewRequestId();
        std::string CallbackGeneration() const;
        SDK::IScriptTable *CurrentClass() const;
        std::mutex _incomingMutex;
        std::vector<std::string> _incoming;
        std::atomic<bool> _contextChanged {false};
        std::atomic<bool> _resetRequested {false};
        NativeAlchemyEntry _script;
        AlchemyEntryContinuation _entry;
        SDK::wh::playermodule::C_Alchemy *_native = nullptr;
        SDK::EntityId _actor                      = 0;
        std::uintptr_t _bodyAddress               = 0;
        std::uint64_t _bodyGeneration             = 1;
        std::uint64_t _nativeGeneration           = 1;
        std::uint64_t _callbackGeneration         = 1;
        std::uint64_t _operation                  = 0;
        std::uint64_t _nextSend                   = 0;
        std::uint64_t _nextDiscovery              = 0;
        std::uint64_t _cancelDeadline             = 0;
        std::uint32_t _ticket                     = 0;
        std::uint64_t _stationGuid                = 0;
        SDK::EntityId _station                    = 0;
        // The one request the server has not answered yet, sent once over the
        // ordered channel; only a too-early refusal sends it again.
        nlohmann::json _pendingRequest;
        bool _requestSent = false;
        // The server's working state of the current batch, as last accepted.
        nlohmann::json _state;
        // The session that holds the table, kept across a batch continuation.
        std::string _tableSession;
        // Highest inventory revision a response named; the projection waits for it.
        std::uint64_t _heldRevision = 0;
        KCDC::Shared::Crafting::AlchemyCharacterView _projectedCharacter;
        std::string _fault;
        std::string _lastLoggedRequest;
        std::string _lastLoggedVerdict;
        std::string _lastRetireReason;
        std::uint64_t _confirmedSequence                   = 0;
        ActionPhase _actionPhase                           = ActionPhase::Idle;
        std::uint64_t _actionSequence                      = 0;
        std::uint64_t _actionSource                        = 0;
        std::uint64_t _actionDeadline                      = 0;
        SDK::wh::playermodule::AlchemyItemType _heldBefore = SDK::wh::playermodule::AlchemyItemType::None;
        std::string _actionItemId;
        SDK::wh::playermodule::C_AlchemyResource *_constructingResource = nullptr; // Borrowed only while its original constructor is on stack.
        std::uint64_t _constructedNativeId                              = 0;
        bool _tableClosed                                               = false;
        bool _disconnectedCleanup                                       = false;
        bool _normalExitRequested                                       = false;
        bool _nativeShutdown                                            = false;
        bool _nativeResetObserved                                       = false;
        bool _nativeOutcomeObserved                                     = false;
        bool _nativeLifecycleStarted                                    = false;
        bool _nativeLifecycleFinished                                   = false;
        bool _nativeEntryReadyObserved                                  = false;
        bool _nativeEntryReadyConsumed                                  = false;
        bool _approvedEntryReady                                        = false;
        bool _entryReadyRequest                                         = false;
        LoopKind _loopKind                                              = LoopKind::None;
        std::uintptr_t _loopActionAddress                               = 0;
        std::uintptr_t _mortarStartAddress                              = 0;
        bool _loopPulseRequested                                        = false;
        bool _loopReturnRequested                                       = false;
        bool _pulseRequest                                              = false;
        bool _approvedLoop                                              = false;
        bool _retortFinishUsed                                          = false;
        std::uint32_t _requiredPulses                                   = 0;
        std::uint32_t _expectedCompletedPulses                          = 0;
        std::uint32_t _cleanupScopeDepth                                = 0;
        bool _approvedSelection                                         = false;
        bool _returnCurrent                                             = false;
        std::uint32_t _nativeScopeDepth                                 = 0;
        SDK::wh::playermodule::AlchemyItemType _selection               = SDK::wh::playermodule::AlchemyItemType::None;
    };
} // namespace KCDC::Features::Crafting
