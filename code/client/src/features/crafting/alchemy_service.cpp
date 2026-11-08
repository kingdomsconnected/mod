#include "alchemy_service.h"

#include "core/application.h"
#include "core/boot/boot_state.h"
#include "core/local_events.h"
#include "core/obfuscation.h"
#include "features/player/player_inventory_service.h"
#include "common/module_support.h"
#include "sdk/cryengine/entitysystem/i_entity_class_registry.h"
#include "sdk/cryengine/entitysystem/i_entity_iterator.h"
#include "sdk/cryengine/entitysystem/i_entity_proxy.h"
#include "sdk/cryengine/entitysystem/i_entity_system.h"
#include "sdk/wh/rpgmodule/c_inventory_soul.h"
#include "shared/features/crafting/alchemy_native_ids.h"
#include "shared/features/crafting/alchemy_protocol.h"

#include <logging/logger.h>
#include <mafianet/get_time.h>

#include <algorithm>
#include <cstring>
#include <limits>
#include <utility>

namespace KCDC::Features::Crafting {
    namespace {
        constexpr const char *kLogger = "Alchemy";
        constexpr std::uint64_t kEntryDeadlineMilliseconds    = 15000;
        constexpr std::uint64_t kStationDiscoveryMilliseconds = 1000;
        // Transport/native liveness watchdog, never a gameplay duration or
        // evidence that an animation has completed.
        constexpr std::uint64_t kActionDeadlineMilliseconds = 120000;
        // A table close the server never answers must not hold the table
        // interaction forever; the server expires its side on its own.
        constexpr std::uint64_t kCancelDeadlineMilliseconds = 15000;
        // The server paces steps on its own clock and says only "too early".
        constexpr std::uint64_t kTooEarlyRetryMilliseconds = 250;
        constexpr std::size_t kMaximumQueuedReplies        = 64;

        std::uint64_t MonotonicMilliseconds() {
            // GetTimeMS is a 32-bit wrapping counter. Entry identities and
            // deadlines use the library's full-width monotonic clock.
            constexpr std::uint64_t kMicrosecondsPerMillisecond = 1000;
            return MafiaNet::GetTimeUS() / kMicrosecondsPerMillisecond;
        }

        /** Whether every native table holder is empty: nothing brewing, nothing left behind. */
        bool NativeTableEmpty(const SDK::wh::playermodule::C_Alchemy &alchemy) {
            for (std::int32_t index = 0; index < static_cast<std::int32_t>(SDK::wh::playermodule::AlchemyItemType::None); ++index) {
                const auto *group = alchemy.GetResources(static_cast<SDK::wh::playermodule::AlchemyItemType>(index));
                if (group == nullptr || !group->IsEmpty()) {
                    return false;
                }
            }
            return true;
        }
    } // namespace

    void AlchemyService::OnServerResponse(std::string body) {
        std::scoped_lock lock(_incomingMutex);
        if (_incoming.size() < kMaximumQueuedReplies) {
            _incoming.push_back(std::move(body));
        }
    }

    SDK::IScriptTable *AlchemyService::CurrentClass() const {
        return KCDC::Common::ClassScriptTable(std::string(KCDC_HIDE("AlchemyTable")).c_str());
    }

    bool AlchemyService::CurrentIdentity(AlchemyEntryIdentity &identity) const {
        auto *application = KCDC::Core::gApplication.get();
        if (application == nullptr || _contextChanged.load()) {
            return false;
        }
        auto *actor   = KCDC::Common::LocalPlayer();
        auto *body    = KCDC::Common::LocalHumanActor();
        auto *station = KCDC::Common::ResolveEntity(_station);
        if (actor == nullptr || body == nullptr || actor->GetId() != _actor || reinterpret_cast<std::uintptr_t>(body) != _bodyAddress || station == nullptr || station->GetGuid() != _stationGuid) {
            return false;
        }
        identity = {application->GetCurrentLevelName(), _actor, _station, _stationGuid, _nativeGeneration, _bodyGeneration, _callbackGeneration};
        return identity.Valid();
    }

    SDK::wh::playermodule::C_Alchemy *AlchemyService::CurrentNative() const {
        AlchemyEntryIdentity identity;
        return _native != nullptr && CurrentIdentity(identity) && _native->GetActor() == KCDC::Common::LocalHumanActor() && _native->GetStationEntityId() == _station ? _native : nullptr;
    }

    bool AlchemyService::PendingTerminal() const {
        return _state.is_object() && _state.value("pendingTerminal", false);
    }

    SDK::wh::framework::I_Action *AlchemyService::CurrentLoopAction() const {
        auto *native = CurrentNative();
        auto *action = native != nullptr ? native->GetInteractionAction() : nullptr;
        return action != nullptr && reinterpret_cast<std::uintptr_t>(action) == _loopActionAddress ? action : nullptr;
    }

    bool AlchemyService::OwnsLoop(SDK::wh::framework::I_Action *action, LoopKind kind) const {
        return action != nullptr && kind != LoopKind::None && _loopKind == kind && action == CurrentLoopAction() && !_nativeShutdown && _entry.GetPhase() == AlchemyEntryContinuation::Phase::Active && _projectedCharacter.Valid() && _fault.empty();
    }

    bool AlchemyService::LoopIsIdle() const {
        auto *action = CurrentLoopAction();
        if (!OwnsLoop(action, _loopKind)) {
            return false;
        }
        if (_loopKind == LoopKind::Bellows) {
            return static_cast<SDK::wh::playermodule::C_UseBellowsAction *>(action)->GetPhase() == SDK::wh::playermodule::C_UseBellowsAction::Phase::Idle;
        }
        return static_cast<SDK::wh::playermodule::C_UseRetortAction *>(action)->GetPhase() == SDK::wh::playermodule::C_UseRetortAction::Phase::Idle;
    }

    void AlchemyService::ClearLoop() {
        _loopKind                = LoopKind::None;
        _loopActionAddress       = 0;
        _mortarStartAddress      = 0;
        _loopPulseRequested      = false;
        _loopReturnRequested     = false;
        _pulseRequest            = false;
        _approvedLoop            = false;
        _retortFinishUsed        = false;
        _requiredPulses          = 0;
        _expectedCompletedPulses = 0;
    }

    bool AlchemyService::BellowsPumping(SDK::wh::playermodule::C_UseBellowsAction *action) {
        if (!OwnsLoop(action, LoopKind::Bellows)) {
            Fault(std::string(KCDC_HIDE("Bellows pulse lost its accepted action owner")));
            return false;
        }
        if (action->GetPhase() != SDK::wh::playermodule::C_UseBellowsAction::Phase::Idle) {
            return false; // Retail Pump is also a no-op outside Idle.
        }
        if (_approvedLoop) {
            _approvedLoop       = false;
            _loopPulseRequested = false;
            return _pulseRequest && !_returnCurrent && _actionPhase == ActionPhase::Running;
        }
        _loopPulseRequested = true;
        return false;
    }

    bool AlchemyService::BellowsReturning(SDK::wh::playermodule::C_UseBellowsAction *action) {
        if (!OwnsLoop(action, LoopKind::Bellows)) {
            return false;
        }
        if (_approvedLoop) {
            _approvedLoop = false;
            return _returnCurrent && !_pulseRequest && _actionPhase == ActionPhase::Running && action->GetPhase() == SDK::wh::playermodule::C_UseBellowsAction::Phase::Idle;
        }
        if (_actionPhase == ActionPhase::Idle && action->GetPhase() == SDK::wh::playermodule::C_UseBellowsAction::Phase::Idle) {
            _loopReturnRequested = true;
        }
        return false;
    }

    bool AlchemyService::RetortPumping(SDK::wh::playermodule::C_UseRetortAction *action) {
        if (!OwnsLoop(action, LoopKind::Retort)) {
            Fault(std::string(KCDC_HIDE("Distillation pulse lost its accepted action owner")));
            return false;
        }
        if (action->GetPhase() != SDK::wh::playermodule::C_UseRetortAction::Phase::Idle) {
            return false;
        }
        if (_approvedLoop) {
            _approvedLoop       = false;
            _loopPulseRequested = false;
            return _pulseRequest && !_returnCurrent && _actionPhase == ActionPhase::Running && (_requiredPulses == 0 || action->GetCompletedPulses() + 1 == _expectedCompletedPulses);
        }
        _loopPulseRequested = true;
        return false;
    }

    bool AlchemyService::RetortRequiredPulses(SDK::wh::playermodule::C_UseRetortAction *action, std::uint32_t &value) {
        if (!OwnsLoop(action, LoopKind::Retort) || _requiredPulses == 0) {
            return false;
        }
        value = _requiredPulses;
        return true;
    }

    bool AlchemyService::RetortFinishing(SDK::wh::playermodule::C_UseRetortAction *action) {
        if (!OwnsLoop(action, LoopKind::Retort) || _actionPhase != ActionPhase::Running || !_pulseRequest || _retortFinishUsed || action->GetPhase() != SDK::wh::playermodule::C_UseRetortAction::Phase::Idle || !PendingTerminal()
            || (_requiredPulses != 0 && (action->GetCompletedPulses() != _requiredPulses || _expectedCompletedPulses != _requiredPulses))) {
            Fault(std::string(KCDC_HIDE("Distillation finish has no exact accepted final pulse")));
            return false;
        }
        _retortFinishUsed = true;
        return true;
    }

    bool AlchemyService::RequestLoop(bool returnCurrent) {
        if (!LoopIsIdle() || _actionPhase != ActionPhase::Idle || !_pendingRequest.is_null() || (returnCurrent && _loopKind != LoopKind::Bellows)) {
            return false;
        }
        _entryReadyRequest     = false;
        _returnCurrent         = returnCurrent;
        _pulseRequest          = !returnCurrent;
        _selection             = returnCurrent ? SDK::wh::playermodule::AlchemyItemType::None : SDK::wh::playermodule::AlchemyItemType::Bellows;
        _heldBefore            = CurrentNative()->GetHeldObjectKind();
        _actionSource          = 0;
        _nativeOutcomeObserved = false;
        _nativeResetObserved   = false;
        _actionItemId.clear();
        _actionSequence = _confirmedSequence + 1;
        Ask({{"request", NewRequestId()}, {"op", returnCurrent ? "return" : "pulse"}, {"session", Session()}, {"sequence", std::to_string(_actionSequence)}});
        _actionPhase         = ActionPhase::AwaitingApproval;
        _actionDeadline      = MonotonicMilliseconds() + kActionDeadlineMilliseconds;
        _loopPulseRequested  = false;
        _loopReturnRequested = false;
        return true;
    }

    void AlchemyService::MortarStarted(SDK::wh::playermodule::C_UseMortarAction *action, bool accepted) {
        auto *native = CurrentNative();
        if (!accepted || action == nullptr || native == nullptr || action->GetAlchemy() != native || !IsNativeScope() || native->HasInteractionAction() || _mortarStartAddress != 0 || _entry.GetPhase() != AlchemyEntryContinuation::Phase::Active
            || _actionPhase != ActionPhase::Running || _heldBefore != SDK::wh::playermodule::AlchemyItemType::None || (_selection != SDK::wh::playermodule::AlchemyItemType::Mortar && _selection != SDK::wh::playermodule::AlchemyItemType::Pestle) || _returnCurrent || _pulseRequest
            || _entryReadyRequest || action->GetPhase() != SDK::wh::playermodule::C_UseMortarAction::Phase::Picking || action->GetCompletedUses() != 0 || !_projectedCharacter.Valid() || !_fault.empty()) {
            Fault(std::string(KCDC_HIDE("Mortar start does not match the approved milling action")));
            return;
        }
        // Start has only queued the pick animation. Replace its Lua-derived
        // count before any accepted milling callback can run.
        action->SetRequiredUses(KCDC::Shared::Crafting::AlchemyNative::kMortarUseCount);
        // C_ActionDirector installs its slot only after Start returns true
        // (0x1809CCD71 before 0x1809CCD83); Selected proves this is the
        // action that got installed.
        _mortarStartAddress = reinterpret_cast<std::uintptr_t>(static_cast<SDK::wh::framework::I_Action *>(action));
    }

    void AlchemyService::Ask(nlohmann::json request) {
        _pendingRequest = std::move(request);
        _requestSent    = false;
        _nextSend       = 0;
    }

    void AlchemyService::Send(const nlohmann::json &request) {
        const auto id = request.value("request", std::string());
        if (id != _lastLoggedRequest) {
            _lastLoggedRequest = id;
            Framework::Logging::GetLogger(kLogger)->debug("Asking the server to {} ({})", request.value("op", std::string()), id);
        }
        if (KCDC::Core::gApplication != nullptr) {
            KCDC::Core::gApplication->GetLocalEvents().AlchemyIntent(request.dump());
        }
    }

    std::string AlchemyService::NewRequestId() {
        return std::to_string(++_operation);
    }

    std::string AlchemyService::CallbackGeneration() const {
        return std::to_string(_callbackGeneration);
    }

    void AlchemyService::Fault(std::string reason) {
        // Called from a detour: defer cancellation until the native stack
        // has unwound. Preserve the first failure for diagnosis.
        if (_fault.empty()) {
            _fault = std::move(reason);
        }
    }

    bool AlchemyService::BindCharacter(const nlohmann::json &response) {
        const auto found = response.find("character");
        if (found == response.end() || found->is_null()) {
            return true;
        }
        KCDC::Shared::Crafting::AlchemyCharacterView candidate;
        std::string error;
        if (!candidate.Bind(*found, error)) {
            Retire(std::string(KCDC_HIDE("Alchemy character projection is invalid")));
            return false;
        }
        _projectedCharacter = std::move(candidate);
        return true;
    }

    void AlchemyService::DiscoverStations(SDK::IScriptTable &currentClass) {
        auto *entities = SDK::IEntitySystem::GetInstance();
        SDK::IEntityItPtr iterator(entities != nullptr ? entities->GetEntityIterator() : nullptr);
        if (!iterator) {
            return;
        }
        std::string error;
        while (auto *entity = iterator->Next()) {
            auto *type = entity->GetClass();
            if (type == nullptr || std::strcmp(type->GetName(), std::string(KCDC_HIDE("AlchemyTable")).c_str()) != 0) {
                continue;
            }
            if (auto *table = KCDC::Common::EntityScriptTable(entity); table != nullptr && !_script.Install(currentClass, *table, error)) {
                Retire(error);
                break;
            }
        }
    }

    void AlchemyService::Retire(const std::string &reason, const char *endReason) {
        const bool wasRetiring = _entry.GetPhase() == AlchemyEntryContinuation::Phase::Retiring;
        if (reason != _lastRetireReason) {
            _lastRetireReason = reason;
            Framework::Logging::GetLogger(kLogger)->warn("Alchemy retired in phase {}: {}", static_cast<int>(_entry.GetPhase()), reason);
        }
        _entry.Retire(reason);
        _approvedSelection  = false;
        _approvedEntryReady = false;
        _approvedLoop       = false;
        if (wasRetiring || _entry.GetPhase() != AlchemyEntryContinuation::Phase::Retiring) {
            return;
        }
        // Close the table on the server: it refunds what the rule returns and
        // releases the table. Refused or accepted, the answer ends the visit.
        _cancelDeadline         = MonotonicMilliseconds() + kCancelDeadlineMilliseconds;
        const auto cancellation = _entry.Cancellation();
        if (cancellation.startRequest.empty() || _tableClosed) {
            return;
        }
        nlohmann::json cancel = {{"request", NewRequestId()}, {"op", "cancel"}, {"startRequest", cancellation.startRequest}, {"reason", endReason}};
        if (!cancellation.session.empty()) {
            cancel["session"] = cancellation.session;
        }
        if (!cancellation.continuationOf.empty()) {
            cancel["continuationOf"] = cancellation.continuationOf;
        }
        Ask(std::move(cancel));
    }

    void AlchemyService::ApplyPush(const nlohmann::json &response) {
        const auto operation = response.value("op", std::string());
        if (operation == "character") {
            // A script changed this player's recipe knowledge.
            KCDC::Shared::Crafting::AlchemyCharacterView candidate;
            std::string error;
            if (response.contains("character") && candidate.Bind(response.at("character"), error)) {
                _projectedCharacter = std::move(candidate);
            }
            else {
                Framework::Logging::GetLogger(kLogger)->warn("Ignoring an invalid alchemy character push");
            }
            return;
        }
        if (operation != "closed") {
            return;
        }
        // The server ended the session itself: a script cancel, expiry. The
        // table is already released there, so native cleanup needs no close.
        const auto phase   = _entry.GetPhase();
        const auto session = response.value("session", std::string());
        if (phase == AlchemyEntryContinuation::Phase::Idle || phase == AlchemyEntryContinuation::Phase::Retired || session.empty() || (session != _tableSession && session != Session())) {
            return;
        }
        _tableClosed = true;
        Retire(std::string(KCDC_HIDE("Server closed the alchemy table")));
    }

    void AlchemyService::Apply(const nlohmann::json &response) {
        if (!response.is_object() || !response.contains("request") || !response.at("request").is_string() || !response.contains("ok") || !response.at("ok").is_boolean()) {
            return;
        }
        // Every answer names the inventory revision its change landed in;
        // the projection waits for it before it touches the bags again.
        if (const auto revision = response.find("inventoryRevision"); revision != response.end() && revision->is_number_unsigned()) {
            _heldRevision = std::max(_heldRevision, revision->get<std::uint64_t>());
        }
        const auto request = response.at("request").get<std::string>();
        if (request.empty()) {
            ApplyPush(response);
            return;
        }
        if (!_pendingRequest.is_object() || request != _pendingRequest.value("request", "")) {
            return;
        }
        const auto operation = _pendingRequest.value("op", std::string());
        const bool ok        = response.at("ok").get<bool>();
        const auto code      = response.value("code", std::string());
        const auto verdict   = request + ":" + operation + ":" + (ok ? std::string("ok") : code);
        if (verdict != _lastLoggedVerdict) {
            _lastLoggedVerdict = verdict;
            Framework::Logging::GetLogger(kLogger)->debug("Server answered {}:{} ({})", operation, ok ? std::string("ok") : code, request);
        }
        if (!ok && code == "alchemyActionTooEarly") {
            // The server paces steps on its own clock. Ask again shortly;
            // the action watchdog still bounds how long this may go on.
            _requestSent = false;
            _nextSend    = MonotonicMilliseconds() + kTooEarlyRetryMilliseconds;
            return;
        }
        _pendingRequest = nullptr;
        _requestSent    = false;
        if (operation == "cancel") {
            // Accepted or refused, the server holds no table for this visit.
            _tableClosed = true;
            return;
        }
        const auto state = response.find("state");
        if (operation == "complete") {
            // A script veto still ends the batch, as failed: the terminal
            // state is what counts, not the verdict.
            if (_actionPhase != ActionPhase::AwaitingCompletion || state == response.end() || !state->is_object() || !state->value("terminal", false)) {
                Retire(ok ? std::string(KCDC_HIDE("Alchemy completion did not end the batch")) : code);
                return;
            }
            if (!BindCharacter(response)) {
                return;
            }
            _state       = *state;
            _actionPhase = ActionPhase::Completed;
            return;
        }
        if (!ok) {
            // A refused first start leaves nothing held on the server; a
            // refused next batch still has its predecessor's table to close.
            if (operation == "start" && !_entry.IsBatchContinuation()) {
                _tableClosed = true;
            }
            Retire(code.empty() ? std::string(KCDC_HIDE("Alchemy request refused")) : code);
            return;
        }
        if (!BindCharacter(response)) {
            return;
        }
        std::uint64_t sequence = 0;
        if (state == response.end() || !state->is_object() || !KCDC::Shared::Crafting::IsToken(response, "session", 160) || !KCDC::Shared::Crafting::ReadIdentity(response, "sequence", sequence)) {
            Retire(std::string(KCDC_HIDE("Alchemy response omitted its session state")));
            return;
        }
        const auto session = response.at("session").get<std::string>();
        if (operation == "start") {
            AlchemyEntryIdentity identity;
            std::string error;
            if (!CurrentIdentity(identity) || !_entry.Approve({request, session, _ticket}, identity, MonotonicMilliseconds(), error)) {
                Retire(error);
                return;
            }
            if (!_projectedCharacter.Valid()) {
                Retire(std::string(KCDC_HIDE("Alchemy admission carried no character")));
                return;
            }
            _state             = *state;
            _tableSession      = session;
            _confirmedSequence = sequence;
            return;
        }
        // action, return, pulse, entryready
        if (_actionPhase != ActionPhase::AwaitingApproval || session != Session() || sequence != _actionSequence) {
            Retire(std::string(KCDC_HIDE("Alchemy action approval changed its session or sequence")));
            return;
        }
        const bool retort = _loopKind == LoopKind::Retort || (_selection == SDK::wh::playermodule::AlchemyItemType::Retort && _heldBefore == SDK::wh::playermodule::AlchemyItemType::PotionFlask);
        if (const auto distillation = state->find("distillation"); retort && distillation != state->end()) {
            // Optional: when the server names its pulse count the native
            // count follows it, otherwise the game's own count stands and
            // only pendingTerminal decides the final pulse.
            if (!distillation->is_object() || !distillation->contains("requiredPulses") || !distillation->at("requiredPulses").is_number_unsigned() || !distillation->contains("completedPulses") || !distillation->at("completedPulses").is_number_unsigned()) {
                Retire(std::string(KCDC_HIDE("Distillation approval carried a malformed pulse count")));
                return;
            }
            const auto required  = distillation->at("requiredPulses").get<std::uint64_t>();
            const auto completed = distillation->at("completedPulses").get<std::uint64_t>();
            if (required < KCDC::Shared::Crafting::AlchemyNative::kMinimumDistillationPumps || required > KCDC::Shared::Crafting::AlchemyNative::kMaximumDistillationPumps || completed > required || (_requiredPulses != 0 && required != _requiredPulses)
                || completed != (_pulseRequest ? static_cast<std::uint64_t>(_expectedCompletedPulses) + 1 : 0) || state->value("pendingTerminal", false) != (completed == required)) {
                Retire(std::string(KCDC_HIDE("Distillation approval changed its exact pulse progression")));
                return;
            }
            _requiredPulses          = static_cast<std::uint32_t>(required);
            _expectedCompletedPulses = static_cast<std::uint32_t>(completed);
        }
        _state             = *state;
        _confirmedSequence = sequence;
        _actionPhase       = ActionPhase::Approved;
    }

    bool AlchemyService::ResolveIngredient(SDK::wh::playermodule::AlchemyItemType target, std::uint64_t &nativeId, std::string &itemId) {
        auto *native         = CurrentNative();
        auto *actor          = KCDC::Common::LocalHumanActor();
        auto *runtime        = actor != nullptr ? actor->GetInventorySoulRuntime() : nullptr;
        auto *inventory      = runtime != nullptr && runtime->IsInitialisedFor(actor->GetSoul()) ? runtime->GetInventory() : nullptr;
        const auto *assigned = native != nullptr ? native->GetAssignedHerb(target) : nullptr;
        if (native == nullptr || inventory == nullptr) {
            return false;
        }
        // The factory checks class availability, but accepted Take resolves
        // the UI-selected exact WUID from +0x108 (0x1808D4306). Preserve
        // that distinction when equal classes have different properties.
        auto *item = inventory->FindItemById(native->GetAssignedIngredientId(target));
        if (item == nullptr) {
            return false;
        }
        const auto itemClass = item->InvokeGetItemClassRaw();
        if (assigned != nullptr && (itemClass.m_low != assigned->m_low || itemClass.m_high != assigned->m_high)) {
            return false;
        }
        // The server row the projection laid this stack over; the server
        // takes the unit out of that row when it accepts the take.
        const auto units = KCDC::Core::gApplication->GetPlayerInventoryService().UnitsOf(item->GetId());
        if (units.empty() || units.front().id.empty()) {
            return false;
        }
        nativeId = item->GetId();
        itemId   = units.front().id;
        return true;
    }

    bool AlchemyService::Selecting(SDK::wh::playermodule::C_Alchemy *minigame, SDK::wh::playermodule::AlchemyItemType target) {
        if (minigame != CurrentNative() || !_projectedCharacter.Valid() || !_fault.empty() || _nativeShutdown || _entry.GetPhase() != AlchemyEntryContinuation::Phase::Active) {
            return false;
        }
        if (_approvedSelection) {
            // Consume before original SelectObject can register callbacks.
            _approvedSelection = false;
            return _actionPhase == ActionPhase::Running && target == _selection;
        }
        return RequestSelection(minigame, target, false);
    }

    bool AlchemyService::EntryReady(SDK::wh::playermodule::C_Alchemy *minigame) {
        if (minigame != CurrentNative() || _nativeShutdown || !_projectedCharacter.Valid() || !_fault.empty()) {
            Fault(std::string(KCDC_HIDE("Alchemy entry completion lost its accepted native owner")));
            return false;
        }
        if (_approvedEntryReady) {
            _approvedEntryReady = false;
            if (!_entryReadyRequest || _actionPhase != ActionPhase::Running || !_nativeEntryReadyObserved || _nativeEntryReadyConsumed) {
                return false;
            }
            // Consume before Started, idle-action registration, or any nested
            // callback can run.
            _nativeEntryReadyConsumed = true;
            return true;
        }
        _nativeEntryReadyObserved = true;
        return false;
    }

    bool AlchemyService::Returning(SDK::wh::playermodule::C_Alchemy *minigame) {
        if (minigame == CurrentNative() && _loopKind == LoopKind::Bellows && LoopIsIdle()) {
            _loopReturnRequested = true;
            return false;
        }
        // An accepted return is replayed from Update.
        RequestSelection(minigame, SDK::wh::playermodule::AlchemyItemType::None, true);
        return false;
    }

    bool AlchemyService::RequestSelection(SDK::wh::playermodule::C_Alchemy *minigame, SDK::wh::playermodule::AlchemyItemType target, bool returnCurrent) {
        if (minigame != CurrentNative() || !_projectedCharacter.Valid() || !_fault.empty() || _nativeShutdown || _entry.GetPhase() != AlchemyEntryContinuation::Phase::Active || _actionPhase != ActionPhase::Idle || minigame->HasInteractionAction() || !minigame->CanSelectObject()
            || (returnCurrent ? target != SDK::wh::playermodule::AlchemyItemType::None || minigame->GetHeldObjectKind() == SDK::wh::playermodule::AlchemyItemType::None : target >= SDK::wh::playermodule::AlchemyItemType::None)) {
            return false;
        }
        _entryReadyRequest     = false;
        _pulseRequest          = false;
        _mortarStartAddress    = 0;
        _returnCurrent         = returnCurrent;
        _selection             = target;
        _heldBefore            = minigame->GetHeldObjectKind();
        _actionSource          = 0;
        _nativeOutcomeObserved = false;
        _nativeResetObserved   = false;
        _constructedNativeId   = 0;
        _actionItemId.clear();
        if (_heldBefore == SDK::wh::playermodule::AlchemyItemType::None && target >= SDK::wh::playermodule::AlchemyItemType::Herb1 && target <= SDK::wh::playermodule::AlchemyItemType::SpecialIngredient3 && !ResolveIngredient(target, _actionSource, _actionItemId)) {
            return false;
        }
        _actionSequence        = _confirmedSequence + 1;
        nlohmann::json request = {{"request", NewRequestId()}, {"op", returnCurrent ? "return" : "action"}, {"session", Session()}, {"sequence", std::to_string(_actionSequence)}};
        if (!returnCurrent) {
            request["target"] = static_cast<std::uint8_t>(target);
        }
        if (!_actionItemId.empty()) {
            request["itemId"] = _actionItemId;
        }
        Ask(std::move(request));
        _actionPhase    = ActionPhase::AwaitingApproval;
        _actionDeadline = MonotonicMilliseconds() + kActionDeadlineMilliseconds;
        return false;
    }

    void AlchemyService::Selected(SDK::wh::playermodule::C_Alchemy *minigame, SDK::wh::playermodule::AlchemyItemType target) {
        if (_actionPhase != ActionPhase::Running || minigame != CurrentNative() || target != _selection || _approvedSelection || !minigame->HasInteractionAction()) {
            Retire(std::string(KCDC_HIDE("Native alchemy did not queue the approved action")));
            return;
        }
        const bool milling = _heldBefore == SDK::wh::playermodule::AlchemyItemType::None && (target == SDK::wh::playermodule::AlchemyItemType::Mortar || target == SDK::wh::playermodule::AlchemyItemType::Pestle) && !PendingTerminal();
        if ((milling && (_mortarStartAddress == 0 || reinterpret_cast<std::uintptr_t>(minigame->GetInteractionAction()) != _mortarStartAddress)) || (!milling && _mortarStartAddress != 0)) {
            Retire(std::string(KCDC_HIDE("Mortar start did not install its exact accepted action")));
            return;
        }
        _mortarStartAddress = 0;
        if ((_heldBefore == SDK::wh::playermodule::AlchemyItemType::None && target == SDK::wh::playermodule::AlchemyItemType::Bellows) || (_heldBefore == SDK::wh::playermodule::AlchemyItemType::PotionFlask && target == SDK::wh::playermodule::AlchemyItemType::Retort)) {
            if (_loopKind != LoopKind::None) {
                Retire(std::string(KCDC_HIDE("Alchemy attempted to replace a live persistent action")));
                return;
            }
            // The accepted factory branch proves the concrete class. Keep
            // only its identity; every later read reacquires director slot1.
            _loopKind          = target == SDK::wh::playermodule::AlchemyItemType::Bellows ? LoopKind::Bellows : LoopKind::Retort;
            _loopActionAddress = reinterpret_cast<std::uintptr_t>(minigame->GetInteractionAction());
        }
    }

    void AlchemyService::ResourceConstructing(SDK::wh::playermodule::C_AlchemyResource *resource, SDK::wh::entitymodule::C_Item *source) {
        if (!IsNativeScope() || _actionPhase != ActionPhase::Running || _actionSource == 0 || source == nullptr || source->GetId() != _actionSource) {
            return;
        }
        if (_constructingResource != nullptr || _constructedNativeId != 0) {
            Fault(std::string(KCDC_HIDE("Alchemy attempted a second ingredient construction")));
            return;
        }
        _constructingResource = resource;
    }

    void AlchemyService::ResourceConstructed(SDK::wh::playermodule::C_AlchemyResource *resource, SDK::wh::entitymodule::C_Item *source) {
        if (resource != _constructingResource) {
            return;
        }
        _constructingResource = nullptr;
        // The server took exactly one unit; the table must hold exactly one.
        auto *item = resource->GetItem();
        if (item == nullptr || item->GetId() == 0 || item->GetAmount() != 1 || item->GetOwner() != resource->GetItemWrapper()) {
            Fault(std::string(KCDC_HIDE("Alchemy ingredient constructor did not transfer its exact unit")));
        }
        else {
            _constructedNativeId = item->GetId();
        }
    }

    bool AlchemyService::BeginScriptCall(std::uint64_t actor, std::uint64_t station) {
        std::string error;
        if (_contextChanged.load() || !_projectedCharacter.Valid() || !_fault.empty() || !_entry.BeginScriptCall(actor, station, error)) {
            return false;
        }
        ++_nativeScopeDepth;
        return true;
    }

    void AlchemyService::BaseResourceConstructed(SDK::wh::playermodule::C_AlchemyResource *resource, SDK::wh::playermodule::AlchemyItemType selection) {
        if (!IsNativeScope()) {
            return;
        }
        auto *item = resource != nullptr ? resource->GetItem() : nullptr;
        if (_actionPhase != ActionPhase::Running || selection != _selection || selection < SDK::wh::playermodule::AlchemyItemType::Glass1 || selection > SDK::wh::playermodule::AlchemyItemType::Glass4 || _actionSource != 0 || _constructedNativeId != 0 || resource == nullptr
            || !resource->IsBase() || resource->IsMilled() || resource->IsDistilled() || item == nullptr || item->GetId() == 0 || item->GetAmount() != 1 || item->GetOwner() != resource->GetItemWrapper()) {
            Fault(std::string(KCDC_HIDE("Alchemy base constructor does not match its accepted glass action")));
            return;
        }
        _constructedNativeId = item->GetId();
    }

    bool AlchemyService::EndScriptCall(int originalValueCount) {
        if (_nativeScopeDepth != 0) {
            --_nativeScopeDepth;
        }
        std::string error;
        const bool accepted = _entry.EndScriptCall(originalValueCount, error);
        if (!accepted) {
            Retire(error);
        }
        return accepted;
    }

    bool AlchemyService::Starting(SDK::wh::playermodule::C_Alchemy *minigame, SDK::EntityId station) {
        AlchemyEntryIdentity identity;
        std::string error;
        if (minigame == nullptr || station != _station || minigame->GetActor() != KCDC::Common::LocalHumanActor() || !CurrentIdentity(identity) || !_entry.ConsumeNativeStart(identity, MonotonicMilliseconds(), error)) {
            return false;
        }
        _native = minigame;
        return true;
    }

    void AlchemyService::Started(SDK::wh::playermodule::C_Alchemy *minigame, SDK::EntityId station, bool accepted) {
        if (minigame != _native || station != _station) {
            return;
        }
        std::string error;
        if (!_entry.NativeStartReturned(accepted, error) || !accepted) {
            Retire(error);
        }
    }

    void AlchemyService::Destroying(SDK::wh::playermodule::C_Alchemy *minigame) {
        if (_native == minigame) {
            // A refused Start can be destroyed without ever reaching the
            // normal Reset path. Observe its holders before the destructor
            // body; an empty table is sufficient, an occupied one is not.
            if (NativeTableEmpty(*minigame)) {
                _nativeResetObserved = true;
            }
            _nativeShutdown = true;
            Retire(std::string(KCDC_HIDE("Native alchemy instance ended")), _normalExitRequested ? "cancelled" : "interrupted");
        }
    }

    void AlchemyService::Destroyed(SDK::wh::playermodule::C_Alchemy *minigame) {
        if (_native == minigame) {
            _native = nullptr;
            ++_nativeGeneration;
        }
    }

    bool AlchemyService::EnterNativeScope(SDK::wh::playermodule::C_Alchemy *minigame) {
        if (minigame == nullptr || minigame != CurrentNative()) {
            return false;
        }
        ++_nativeScopeDepth;
        return true;
    }

    bool AlchemyService::EnterCleanupScope(SDK::wh::playermodule::C_Alchemy *minigame) {
        // The native cleanup detour itself establishes receiver lifetime.
        // Old-body cleanup must remain possible after the current actor,
        // station, or world has changed. This grants no action admission.
        if (minigame == nullptr || minigame != _native) {
            return false;
        }
        ++_nativeScopeDepth;
        ++_cleanupScopeDepth;
        return true;
    }

    void AlchemyService::LeaveCleanupScope() {
        if (_cleanupScopeDepth != 0) {
            --_cleanupScopeDepth;
            LeaveNativeScope();
        }
    }

    SDK::wh::playermodule::C_Alchemy *AlchemyService::CleanupNative() const {
        return _cleanupScopeDepth != 0 ? _native : nullptr;
    }

    bool AlchemyService::EnterActionScope(SDK::wh::playermodule::C_Alchemy *minigame, SDK::wh::framework::I_Action *action) {
        if (minigame == nullptr || minigame != CurrentNative() || action == nullptr || !minigame->OwnsInteractionAction(action) || _actionPhase != ActionPhase::Running || _entry.GetPhase() != AlchemyEntryContinuation::Phase::Active || _nativeShutdown || _entryReadyRequest
            || !_projectedCharacter.Valid() || !_fault.empty()) {
            Fault(std::string(KCDC_HIDE("Alchemy animation callback has no accepted live action")));
            return false;
        }
        ++_nativeScopeDepth;
        return true;
    }

    void AlchemyService::LeaveNativeScope() {
        if (_nativeScopeDepth != 0) {
            --_nativeScopeDepth;
        }
    }

    void AlchemyService::RequestingExit(SDK::wh::playermodule::C_Alchemy *minigame) {
        if (minigame == _native) {
            _normalExitRequested = true;
        }
    }

    void AlchemyService::ShuttingDown(SDK::wh::playermodule::C_Alchemy *minigame) {
        if (minigame == _native) {
            _nativeShutdown = true;
            Retire(std::string(KCDC_HIDE("Native alchemy session is closing")), _normalExitRequested ? "cancelled" : "interrupted");
        }
    }

    void AlchemyService::ShutDown(SDK::wh::playermodule::C_Alchemy *minigame) {
        if (minigame == _native && !minigame->HasFinished()) {
            Retire(std::string(KCDC_HIDE("Alchemy cleanup did not reach its ended state")));
        }
    }

    bool AlchemyService::SuppressNativeOutcome(SDK::wh::playermodule::C_Alchemy *minigame, SDK::wh::playermodule::AlchemyItemType container) {
        // The caller independently performs Reset and director completion.
        // Skip all native products, XP, knowledge and outcome quest effects;
        // the server's Complete is the sole reward source.
        if (minigame != CurrentNative() || !IsNativeScope() || _actionPhase != ActionPhase::Running || !PendingTerminal() || _nativeOutcomeObserved
            || (container != SDK::wh::playermodule::AlchemyItemType::PotionFlask && container != SDK::wh::playermodule::AlchemyItemType::Mortar)) {
            Fault(std::string(KCDC_HIDE("Native alchemy outcome has no accepted terminal action")));
            return true;
        }
        _nativeOutcomeObserved = true;
        return true;
    }

    bool AlchemyService::ProjectNativeStart(SDK::wh::rpgmodule::I_Soul *soul, float &value, bool &executeNative) {
        auto *actor = KCDC::Common::LocalHumanActor();
        if (!IsNativeScope() || actor == nullptr || soul != actor->GetSoul()) {
            return false;
        }
        executeNative = false;
        value         = 0.0F;
        if (!_projectedCharacter.Valid()) {
            Fault(std::string(KCDC_HIDE("Alchemy lifecycle has no server character inputs")));
            return true;
        }
        value = _projectedCharacter.SessionQualityBonus();
        if (_nativeLifecycleStarted || _nativeLifecycleFinished) {
            Fault(std::string(KCDC_HIDE("Native alchemy repeated its table-entry lifecycle event")));
            return true;
        }
        // These native events affect the local character, not inventory
        // production. Preserve them once; only the result bonus is supplied
        // by the server. EvaluateContainer remains the reward boundary.
        _nativeLifecycleStarted = true;
        executeNative           = true;
        return true;
    }

    bool AlchemyService::SuppressNativeFinish(SDK::wh::rpgmodule::I_Soul *soul) {
        auto *cleanup = CleanupNative();
        auto *actor   = cleanup != nullptr ? cleanup->GetActor() : KCDC::Common::LocalHumanActor();
        if (!IsNativeScope() || actor == nullptr || soul != actor->GetSoul()) {
            return false;
        }
        if (_nativeLifecycleFinished) {
            Fault(std::string(KCDC_HIDE("Native alchemy repeated its table-exit lifecycle event")));
            return true;
        }
        _nativeLifecycleFinished = true;
        return false;
    }

    void AlchemyService::ResetNative(SDK::wh::playermodule::C_Alchemy *minigame) {
        if (minigame != _native) {
            return;
        }
        _nativeResetObserved = NativeTableEmpty(*minigame);
        if (!_nativeResetObserved) {
            Fault(std::string(KCDC_HIDE("Native alchemy reset left a working resource alive")));
        }
    }

    void AlchemyService::NativeContextChanged() {
        _contextChanged.store(true);
    }

    void AlchemyService::ProgressRetirement(SDK::IScriptTable &currentClass, std::uint64_t now) {
        if (_entry.GetPhase() != AlchemyEntryContinuation::Phase::Retiring || !_entry.MayPerformNativeCleanup() || _nativeScopeDepth != 0) {
            return;
        }
        if (!_tableClosed) {
            if (now < _cancelDeadline) {
                return;
            }
            Framework::Logging::GetLogger(kLogger)->warn("The server did not answer the alchemy table close; leaving the table");
            _tableClosed = true;
        }
        auto *native = CurrentNative();
        if (native != nullptr && !_nativeShutdown && !native->HasFinished()) {
            native->Cancel();                  // Native slot6 reaches Shutdown and Reset.
        }
        native              = CurrentNative(); // Cancel may destroy or replace owner.
        const bool inactive = !_entry.NativeCleanupRequired() || (native != nullptr && native->HasFinished() && _nativeResetObserved) || (_native == nullptr && _nativeResetObserved);
        if (!inactive) {
            return;
        }
        std::string error;
        if (!_script.Retire(currentClass, true, error) || !_entry.CompleteRetirement(true, true, true, error)) {
            Fault(error);
            return;
        }
        // The next callback generation accepts a fresh table entry; no old
        // callback can revive this visit.
        ClearLoop();
        _projectedCharacter.Reset();
        _fault.clear();
        _state          = nullptr;
        _pendingRequest = nullptr;
        _requestSent    = false;
        _tableSession.clear();
        _actionPhase = ActionPhase::Idle;
        _native      = nullptr;
        ++_nativeGeneration;
        ++_callbackGeneration;
        _normalExitRequested   = false;
        _nativeShutdown        = false;
        _nativeResetObserved   = false;
        _nativeOutcomeObserved = false;
        _tableClosed           = false;
        _nextDiscovery         = 0;
    }

    bool AlchemyService::CompleteDisconnectedCleanup() {
        if (!_disconnectedCleanup) {
            return true;
        }
        if (_nativeScopeDepth != 0 || _entry.IsInsideInvocation()) {
            return false;
        }
        auto *native = CurrentNative();
        if (native != nullptr && !native->HasFinished()) {
            native->Cancel();
        }
        native              = CurrentNative();
        bool nativeInactive = !_entry.NativeCleanupRequired() || (_native == nullptr && _nativeResetObserved);
        if (native != nullptr && native->HasFinished()) {
            nativeInactive = NativeTableEmpty(*native);
        }
        if (!nativeInactive) {
            return false;
        }
        std::string error;
        auto *current          = CurrentClass();
        const bool noAdmission = _native == nullptr && (_entry.GetPhase() == AlchemyEntryContinuation::Phase::Idle || _entry.GetPhase() == AlchemyEntryContinuation::Phase::Retired || _entry.Operation().empty());
        if ((current == nullptr ? !noAdmission : !_script.Retire(*current, true, error)) || !_entry.CompleteDisconnectedRetirement(true, true, error)) {
            return false;
        }
        // The server dropped the session with the connection.
        ClearLoop();
        _native         = nullptr;
        _pendingRequest = nullptr;
        _requestSent    = false;
        _state          = nullptr;
        _tableSession.clear();
        _projectedCharacter.Reset();
        _fault.clear();
        _actionPhase              = ActionPhase::Idle;
        _confirmedSequence        = 0;
        _tableClosed              = false;
        _normalExitRequested      = false;
        _nativeShutdown           = false;
        _nativeResetObserved      = false;
        _nativeOutcomeObserved    = false;
        _nativeLifecycleStarted   = false;
        _nativeLifecycleFinished  = false;
        _nativeEntryReadyObserved = false;
        _nativeEntryReadyConsumed = false;
        _approvedEntryReady       = false;
        _entryReadyRequest        = false;
        _disconnectedCleanup      = false;
        ++_nativeGeneration;
        ++_callbackGeneration;
        _nextDiscovery = 0;
        return true;
    }

    void AlchemyService::ResetOnGameThread() {
        Retire(std::string(KCDC_HIDE("Alchemy connection ended")));
        _disconnectedCleanup = true;
        _pendingRequest      = nullptr;
        _requestSent         = false;
        _heldRevision        = 0;
        CompleteDisconnectedCleanup();
        {
            std::scoped_lock lock(_incomingMutex);
            _incoming.clear();
        }
    }

    void AlchemyService::Reset() {
        const auto gameThread = KCDC::Core::Boot::gBootState.gameThreadId.load(std::memory_order_relaxed);
        if (gameThread == 0 || gameThread != GetCurrentThreadId()) {
            _resetRequested.store(true);
            return;
        }
        ResetOnGameThread();
    }

    void AlchemyService::PublishProjectionHold() {
        // While a step is in flight the native table and the server's rows
        // are about to change together: projecting in between would put a
        // taken ingredient back, or take away the one about to be picked up.
        const bool inFlight = _entry.GetPhase() == AlchemyEntryContinuation::Phase::Active && (_actionPhase == ActionPhase::AwaitingApproval || _actionPhase == ActionPhase::Approved || _actionPhase == ActionPhase::Running);
        KCDC::Core::gApplication->GetPlayerInventoryService().HoldProjectionUntil(inFlight ? std::numeric_limits<std::uint64_t>::max() : _heldRevision);
    }

    void AlchemyService::Update() {
        if (KCDC::Core::gApplication == nullptr) {
            return;
        }
        if (_resetRequested.exchange(false)) {
            ResetOnGameThread();
        }
        if (_disconnectedCleanup && !CompleteDisconnectedCleanup()) {
            return;
        }
        UpdateSession();
        PublishProjectionHold();
    }

    void AlchemyService::UpdateSession() {
        auto &inventory        = KCDC::Core::gApplication->GetPlayerInventoryService();
        auto *body             = KCDC::Common::LocalHumanActor();
        const auto actor       = KCDC::Common::LocalActorEntityId();
        const auto bodyAddress = reinterpret_cast<std::uintptr_t>(body);
        if (!_fault.empty() && _nativeScopeDepth == 0) {
            // With no visit to retire, a fault must not lock the tables for good.
            if (_entry.GetPhase() == AlchemyEntryContinuation::Phase::Idle || _entry.GetPhase() == AlchemyEntryContinuation::Phase::Retired) {
                Framework::Logging::GetLogger(kLogger)->warn("Alchemy fault outside a table visit: {}", _fault);
                _fault.clear();
            }
            else {
                Retire(_fault);
            }
        }
        if (_contextChanged.exchange(false) || actor != _actor || bodyAddress != _bodyAddress) {
            Retire(std::string(KCDC_HIDE("Alchemy native context changed")), _normalExitRequested ? "cancelled" : "interrupted");
            _actor       = actor;
            _bodyAddress = bodyAddress;
            ++_bodyGeneration;
            ++_callbackGeneration;
            _nextDiscovery = 0;
        }
        if (!inventory.HasAuthority() || actor == 0 || body == nullptr) {
            return;
        }
        std::vector<std::string> incoming;
        {
            std::scoped_lock lock(_incomingMutex);
            incoming.swap(_incoming);
        }
        for (const auto &bodyText : incoming) {
            try {
                Apply(nlohmann::json::parse(bodyText));
            }
            catch (const nlohmann::json::exception &) {
                Retire(std::string(KCDC_HIDE("Invalid alchemy server response")));
            }
        }
        // Read after the responses: an approval applied above is stamped with
        // its own clock reading, and the entry refuses an earlier `now`.
        const auto now     = MonotonicMilliseconds();
        auto *currentClass = CurrentClass();
        if (currentClass == nullptr) {
            return;
        }
        // A failed Lua callback keeps its failure until the generation is
        // retired. Retrying Ensure would fail again before cleanup or the
        // cancel request could run. Finish retirement independently of entry.
        if (_entry.GetPhase() != AlchemyEntryContinuation::Phase::Retiring) {
            UpdateInteraction(*currentClass, actor, now);
        }
        ProgressRetirement(*currentClass, now);
        if (_pendingRequest.is_object() && !_requestSent && now >= _nextSend) {
            Send(_pendingRequest);
            _requestSent = true;
        }
    }

    void AlchemyService::UpdateInteraction(SDK::IScriptTable &currentClass, SDK::EntityId actor, std::uint64_t now) {
        std::string error;
        const std::string generation = CallbackGeneration();
        // Admission stays enabled: the wrapper defers table use to the
        // server instead of falling through to retail.
        if (!_script.Ensure(currentClass, generation, actor, true, error)) {
            Retire(error);
            return;
        }
        if (now >= _nextDiscovery) {
            _nextDiscovery = now + kStationDiscoveryMilliseconds;
            DiscoverStations(currentClass);
        }
        if (_entry.GetPhase() == AlchemyEntryContinuation::Phase::Idle || _entry.GetPhase() == AlchemyEntryContinuation::Phase::Retired) {
            AlchemyScriptRequest request;
            if (_script.ReadRequest(currentClass, request, error)) {
                auto *station = request.station <= std::numeric_limits<SDK::EntityId>::max() ? KCDC::Common::ResolveEntity(static_cast<SDK::EntityId>(request.station)) : nullptr;
                if (request.actor != actor || request.generation != generation || station == nullptr || station->GetGuid() == 0) {
                    Framework::Logging::GetLogger(kLogger)->debug("Station request mismatch: actor {} vs {}, generation '{}' vs '{}', station {} resolved {} guid {}", request.actor, actor, request.generation, generation, request.station, station != nullptr,
                        station != nullptr ? station->GetGuid() : 0);
                    Retire(std::string(KCDC_HIDE("Alchemy station request identity changed")));
                }
                else {
                    _station     = station->GetId();
                    _stationGuid = station->GetGuid();
                    _ticket      = request.ticket;
                    AlchemyEntryIdentity identity;
                    const auto operation = NewRequestId();
                    if (CurrentIdentity(identity) && _entry.Queue(identity, operation, request.ticket, now, now + kEntryDeadlineMilliseconds, error)) {
                        _tableClosed = false;
                        _tableSession.clear();
                        _confirmedSequence = 0;
                        Ask({{"request", operation}, {"op", "start"}, {"station", std::to_string(_stationGuid)}, {"ticket", _ticket}, {"callbackGeneration", std::to_string(_callbackGeneration)}});
                    }
                }
            }
        }
        if (_entry.GetPhase() == AlchemyEntryContinuation::Phase::Approved) {
            AlchemyEntryIdentity identity;
            auto *station     = KCDC::Common::EntityScriptTable(KCDC::Common::ResolveEntity(_station));
            auto *actorScript = KCDC::Common::EntityScriptTable(KCDC::Common::LocalPlayer());
            if (_entry.IsBatchContinuation()) {
                // The next batch at the same table: no native entry replay,
                // only proof that the finished batch left the table empty.
                auto *native = CurrentNative();
                if (native == nullptr || !native->CanSelectObject() || native->HasInteractionAction() || !NativeTableEmpty(*native) || !CurrentIdentity(identity) || !_entry.BeginBatch(identity, now, error)) {
                    Retire(error.empty() ? std::string(KCDC_HIDE("Alchemy next batch found the table busy")) : error);
                }
            }
            else if (station == nullptr || actorScript == nullptr || !CurrentIdentity(identity) || !_entry.BeginInvocation(identity, now, error)) {
                Retire(error);
            }
            else {
                _nativeLifecycleStarted   = false;
                _nativeLifecycleFinished  = false;
                _nativeEntryReadyObserved = false;
                _nativeEntryReadyConsumed = false;
                _approvedEntryReady       = false;
                _entryReadyRequest        = false;
                const bool completed      = _script.Replay(currentClass, *station, *actorScript, _ticket, error);
                if (!_entry.EndInvocation(completed, error)) {
                    Retire(error);
                }
                // The native start inside the replay stamped the entry with a
                // reading of its own (Starting); the rest of this tick is
                // later than that, not earlier.
                now = MonotonicMilliseconds();
            }
        }
        if (_entry.GetPhase() == AlchemyEntryContinuation::Phase::Active && _nativeEntryReadyObserved && !_nativeEntryReadyConsumed && _actionPhase == ActionPhase::Idle && _pendingRequest.is_null()) {
            auto *native = CurrentNative();
            if (native == nullptr || native->HasInteractionAction() || native->CanSelectObject()) {
                Retire(std::string(KCDC_HIDE("Alchemy entry animation completion changed before authorization")));
            }
            else {
                _entryReadyRequest = true;
                _actionSequence    = _confirmedSequence + 1;
                Ask({{"request", NewRequestId()}, {"op", "entryready"}, {"session", Session()}, {"sequence", std::to_string(_actionSequence)}});
                _actionPhase    = ActionPhase::AwaitingApproval;
                _actionDeadline = now + kActionDeadlineMilliseconds;
            }
        }
        if (_entry.GetPhase() == AlchemyEntryContinuation::Phase::Active && _actionPhase != ActionPhase::Idle && _actionPhase != ActionPhase::Completed && now >= _actionDeadline) {
            Retire(std::string(KCDC_HIDE("Alchemy action transport or native continuation expired")));
        }
        if (_entry.GetPhase() == AlchemyEntryContinuation::Phase::Active && _actionPhase == ActionPhase::Idle && (_loopPulseRequested || _loopReturnRequested)) {
            RequestLoop(!_loopPulseRequested && _loopReturnRequested);
        }
        if (_entry.GetPhase() == AlchemyEntryContinuation::Phase::Active && _actionPhase == ActionPhase::Approved) {
            auto *native = CurrentNative();
            AlchemyEntryIdentity identity;
            if (_entryReadyRequest) {
                if (native == nullptr || !CurrentIdentity(identity) || !_entry.Observe(identity, now, error) || native->CanSelectObject() || native->HasInteractionAction() || !_nativeEntryReadyObserved || _nativeEntryReadyConsumed) {
                    Retire(std::string(KCDC_HIDE("Alchemy entry readiness changed before native continuation")));
                }
                else {
                    _actionPhase        = ActionPhase::Running;
                    _approvedEntryReady = true;
                    native->OnEntryActionFinished();
                    if (_approvedEntryReady || !_nativeEntryReadyConsumed || !_nativeLifecycleStarted || !native->CanSelectObject()) {
                        Retire(std::string(KCDC_HIDE("Alchemy entry continuation did not complete exactly once")));
                    }
                    else {
                        _actionPhase = ActionPhase::Idle;
                    }
                }
            }
            else if (_pulseRequest || (_returnCurrent && _loopKind == LoopKind::Bellows)) {
                if (native == nullptr || !CurrentIdentity(identity) || !_entry.Observe(identity, now, error) || !LoopIsIdle() || native->GetHeldObjectKind() != _heldBefore) {
                    Retire(std::string(KCDC_HIDE("Alchemy persistent action changed before native continuation")));
                }
                else {
                    auto *action      = CurrentLoopAction();
                    _actionPhase      = ActionPhase::Running;
                    _approvedLoop     = true;
                    const bool scoped = EnterNativeScope(native);
                    if (!scoped) {
                        Retire(std::string(KCDC_HIDE("Alchemy persistent action lost its native scope")));
                    }
                    else {
                        if (_loopKind == LoopKind::Retort) {
                            static_cast<SDK::wh::playermodule::C_UseRetortAction *>(action)->Pump();
                        }
                        else if (_returnCurrent) {
                            static_cast<SDK::wh::playermodule::C_UseBellowsAction *>(action)->ReturnBellows();
                        }
                        else {
                            static_cast<SDK::wh::playermodule::C_UseBellowsAction *>(action)->Pump();
                        }
                        LeaveNativeScope();
                        if (_approvedLoop) {
                            Retire(std::string(KCDC_HIDE("Alchemy persistent action did not consume its one-use permission")));
                        }
                    }
                }
            }
            else if (native == nullptr || !CurrentIdentity(identity) || !_entry.Observe(identity, now, error) || !native->CanSelectObject() || native->HasInteractionAction() || native->GetHeldObjectKind() != _heldBefore
                     || (_actionSource != 0 && native->GetAssignedIngredientId(_selection) != _actionSource)) {
                Retire(std::string(KCDC_HIDE("Alchemy action context changed before native continuation")));
            }
            else {
                _actionPhase = ActionPhase::Running;
                if (_returnCurrent) {
                    const bool scoped = EnterNativeScope(native);
                    const bool queued = scoped && native->QueueReturnAction();
                    if (scoped) {
                        LeaveNativeScope();
                    }
                    if (!queued) {
                        Retire(std::string(KCDC_HIDE("Native alchemy did not queue the approved return action")));
                    }
                }
                else {
                    // The book stays open for as long as the player reads it.
                    if (_selection == SDK::wh::playermodule::AlchemyItemType::Book) {
                        _actionDeadline = std::numeric_limits<std::uint64_t>::max();
                    }
                    _approvedSelection = true;
                    native->SelectObject(_selection);
                    if (_approvedSelection) {
                        Retire(std::string(KCDC_HIDE("Alchemy action continuation did not consume its permission")));
                    }
                }
            }
        }
        if (_entry.GetPhase() == AlchemyEntryContinuation::Phase::Active && _actionPhase == ActionPhase::Running) {
            auto *native = CurrentNative();
            if (native == nullptr || now >= _actionDeadline) {
                Retire(std::string(KCDC_HIDE("Alchemy action did not finish in its live session")));
            }
            else if (!native->HasInteractionAction() || (!_returnCurrent && LoopIsIdle())) {
                if (_loopKind == LoopKind::Retort && _requiredPulses != 0 && LoopIsIdle() && static_cast<SDK::wh::playermodule::C_UseRetortAction *>(CurrentLoopAction())->GetCompletedPulses() != _expectedCompletedPulses) {
                    Retire(std::string(KCDC_HIDE("Distillation native pulse completion diverged from its approval")));
                    return;
                }
                if (PendingTerminal()) {
                    // The native outcome was suppressed and the table reset;
                    // only now may the server settle the batch.
                    if (!_nativeOutcomeObserved || !_nativeResetObserved || !NativeTableEmpty(*native)) {
                        Retire(std::string(KCDC_HIDE("Alchemy terminal animation did not complete its accepted cleanup")));
                    }
                    else {
                        Ask({{"request", NewRequestId()}, {"op", "complete"}, {"session", Session()}, {"sequence", std::to_string(_confirmedSequence)}});
                        _actionPhase    = ActionPhase::AwaitingCompletion;
                        _actionDeadline = now + kActionDeadlineMilliseconds;
                    }
                }
                else {
                    if (_returnCurrent && _loopKind == LoopKind::Bellows) {
                        if (native->HasInteractionAction() || native->GetHeldObjectKind() != SDK::wh::playermodule::AlchemyItemType::None) {
                            Retire(std::string(KCDC_HIDE("Bellows return did not retire its exact persistent action")));
                            return;
                        }
                        ClearLoop();
                    }
                    _actionPhase = ActionPhase::Idle;
                }
            }
        }
        if (_entry.GetPhase() == AlchemyEntryContinuation::Phase::Active && _actionPhase == ActionPhase::Completed) {
            // The batch is settled on the server; stay at the table and ask
            // for the next one under the same native interaction.
            auto *native = CurrentNative();
            AlchemyEntryIdentity identity;
            if (native == nullptr || _nativeShutdown || !_nativeResetObserved || !native->CanSelectObject() || native->HasInteractionAction() || !NativeTableEmpty(*native) || !CurrentIdentity(identity)) {
                Retire(std::string(KCDC_HIDE("Alchemy next batch lacks an empty finished table")));
            }
            else {
                const auto previousSession = Session();
                const auto operation       = NewRequestId();
                if (!_entry.QueueBatch(identity, previousSession, operation, now, now + kEntryDeadlineMilliseconds, error)) {
                    Retire(error);
                }
                else {
                    ClearLoop();
                    _actionPhase = ActionPhase::Idle;
                    _state       = nullptr;
                    Ask({{"request", operation}, {"op", "start"}, {"station", std::to_string(_stationGuid)}, {"ticket", _ticket}, {"callbackGeneration", std::to_string(_callbackGeneration)}, {"continuationOf", previousSession}});
                }
            }
        }
        if (_entry.GetPhase() != AlchemyEntryContinuation::Phase::Idle && _entry.GetPhase() != AlchemyEntryContinuation::Phase::Retired && _entry.GetPhase() != AlchemyEntryContinuation::Phase::Retiring) {
            AlchemyEntryIdentity identity;
            if (!CurrentIdentity(identity) || !_entry.Observe(identity, now, error)) {
                Retire(error);
            }
        }
    }
} // namespace KCDC::Features::Crafting
