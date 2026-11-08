#include "smithing_service.h"

#include "core/application.h"
#include "core/local_events.h"
#include "core/obfuscation.h"
#include "features/player/player_inventory_service.h"
#include "common/module_support.h"
#include "sdk/cryengine/entitysystem/i_entity.h"
#include "sdk/wh/c_wh_global_environment.h"

#include <logging/logger.h>
#include <mafianet/get_time.h>

namespace KCDC::Features::Crafting {
    namespace {
        constexpr const char *kLogger = "Smithing";
        // A verdict the server never sends must not hold the Craft button.
        constexpr std::uint64_t kVerdictDeadlineMs = 15000;

        std::uint64_t MonotonicMilliseconds() {
            return MafiaNet::GetTimeUS() / 1000;
        }
    } // namespace

    void SmithingService::OnVerdict(const KCDC::Shared::RPC::SmithingVerdict &verdict) {
        std::scoped_lock lock(_incomingMutex);
        _verdicts.push_back(verdict);
    }

    void SmithingService::OnClosed(const KCDC::Shared::RPC::SmithingClosed &closed) {
        std::scoped_lock lock(_incomingMutex);
        _closures.push_back(closed);
    }

    bool SmithingService::Owns(const SDK::wh::playermodule::C_Blacksmithing *minigame) const {
        const auto *actor = KCDC::Common::LocalHumanActor();
        return minigame != nullptr && actor != nullptr && minigame->GetActor() == actor;
    }

    bool SmithingService::ReadSelectedRecipe(std::string &recipe) {
        auto *environment = SDK::wh::C_GlobalEnvironment::GetInstance();
        auto *apse        = environment != nullptr ? environment->GetUIApse() : nullptr;
        if (apse == nullptr) {
            return false;
        }
        SDK::ReturnedCryString selected;
        apse->GetSelectedBlacksmithRecipe(selected.Slot());
        recipe = selected.c_str();
        return !recipe.empty() && recipe.size() <= KCDC::Shared::RPC::kMaxSmithingRecipeId;
    }

    bool SmithingService::AcceptingRecipe(SDK::wh::playermodule::C_Blacksmithing *minigame) {
        auto *application = KCDC::Core::gApplication.get();
        if (application == nullptr || !application->GetPlayerInventoryService().HasAuthority() || !Owns(minigame)) {
            return true;
        }
        if (_replaying) {
            _replaying = false;
            return true;
        }
        // One workpiece at a time; a second press while the first is asked is dropped.
        if (_phase != Phase::Idle) {
            application->GetPlayerInventoryService().RefuseUnsupportedAction("The server is still settling your last workpiece.");
            return false;
        }
        auto *station = KCDC::Common::ResolveEntity(minigame->GetStationEntityId());
        if (station == nullptr || station->GetGuid() == 0 || !ReadSelectedRecipe(_recipe)) {
            application->GetPlayerInventoryService().RefuseUnsupportedAction("This forge cannot be used here.");
            _recipe.clear();
            return false;
        }
        KCDC::Shared::RPC::SmithingBegin request;
        request.request = ++_request;
        request.station = station->GetGuid();
        request.recipe  = _recipe;
        application->GetLocalEvents().SmithingBegin(request);
        Framework::Logging::GetLogger(kLogger)->debug("Asking the server to begin {} ({})", _recipe, request.request);
        _minigame   = minigame;
        _station    = minigame->GetStationEntityId();
        _phase      = Phase::Awaiting;
        _deadlineMs = MonotonicMilliseconds() + kVerdictDeadlineMs;
        return false;
    }

    void SmithingService::Cancelling(SDK::wh::playermodule::C_Blacksmithing *minigame) {
        if (_phase == Phase::Working && minigame == _minigame && Owns(minigame)) {
            _cancelRequested = true;
        }
    }

    void SmithingService::ActionReplaced(const SDK::wh::playermodule::C_Minigame *minigame) {
        if (_phase == Phase::Working && minigame == _minigame && Owns(_minigame)) {
            _actionReplaced = true;
        }
    }

    KCDC::Shared::RPC::SmithingOutcome SmithingService::UnfinishedOutcome() const {
        return _cancelRequested && !_actionReplaced ? KCDC::Shared::RPC::SmithingOutcome::Cancelled : KCDC::Shared::RPC::SmithingOutcome::Interrupted;
    }

    void SmithingService::FinishingWorkpiece(SDK::wh::playermodule::C_Blacksmithing *minigame, bool success) {
        if (!Owns(minigame)) {
            return;
        }
        _finishingSuccess = success;
        if (_phase != Phase::Working || minigame != _minigame) {
            return;
        }
        const auto *state = minigame->GetState();
        Report(_session, success ? KCDC::Shared::RPC::SmithingOutcome::Success : _cancelRequested || _actionReplaced ? UnfinishedOutcome() : KCDC::Shared::RPC::SmithingOutcome::Failure, state != nullptr ? state->GetQuality() : 0.0F);
        Clear();
    }

    void SmithingService::FinishedWorkpiece(SDK::wh::playermodule::C_Blacksmithing *minigame) {
        if (Owns(minigame)) {
            _finishingSuccess = false;
        }
    }

    bool SmithingService::SuppressNativeSettlement(SDK::wh::playermodule::C_Blacksmithing *minigame) const {
        auto *application = KCDC::Core::gApplication.get();
        return application != nullptr && application->GetPlayerInventoryService().HasAuthority() && Owns(minigame);
    }

    bool SmithingService::SuppressNativeOutcome() const {
        auto *application = KCDC::Core::gApplication.get();
        return _finishingSuccess && application != nullptr && application->GetPlayerInventoryService().HasAuthority();
    }

    bool SmithingService::LendingTool(const SDK::wh::playermodule::C_BlacksmithingSlotToolAttachment *tool, const SDK::wh::entitymodule::C_Actor *actor) {
        auto *application = KCDC::Core::gApplication.get();
        const auto *local = KCDC::Common::LocalHumanActor();
        // An empty tool slot has no item to lend; the native transfer fails on its own.
        if (application == nullptr || !application->GetPlayerInventoryService().HasAuthority() || tool == nullptr || tool->IsAttached() || tool->GetToolItemWuid() == 0 || local == nullptr || actor != local) {
            return false;
        }
        application->GetPlayerInventoryService().LendNativeItem(tool->GetToolItemWuid());
        Framework::Logging::GetLogger(kLogger)->debug("Lent smithery tool {:#x}", tool->GetToolItemWuid());
        return true;
    }

    bool SmithingService::ReturningTool(const SDK::wh::playermodule::C_BlacksmithingSlotToolAttachment *tool) const {
        auto *application = KCDC::Core::gApplication.get();
        const auto *local = KCDC::Common::LocalHumanActor();
        return application != nullptr && application->GetPlayerInventoryService().HasAuthority() && tool != nullptr && tool->IsAttached() && tool->GetToolItemWuid() != 0 && local != nullptr && tool->GetActorEntityId() == local->GetEntityId();
    }

    void SmithingService::ReturnedTool(const SDK::wh::playermodule::C_BlacksmithingSlotToolAttachment *tool) {
        KCDC::Core::gApplication->GetPlayerInventoryService().ReturnNativeItem(tool->GetToolItemWuid());
        Framework::Logging::GetLogger(kLogger)->debug("Returned smithery tool {:#x}", tool->GetToolItemWuid());
    }

    void SmithingService::Report(std::uint32_t session, KCDC::Shared::RPC::SmithingOutcome outcome, float quality) {
        KCDC::Shared::RPC::SmithingFinish report;
        report.session = session;
        report.outcome = static_cast<std::uint8_t>(outcome);
        report.quality = quality;
        KCDC::Core::gApplication->GetLocalEvents().SmithingFinish(report);
        Framework::Logging::GetLogger(kLogger)->debug("Workpiece {} ended with outcome {} at quality {}", session, static_cast<int>(outcome), quality);
    }

    void SmithingService::Refused(KCDC::Shared::RPC::SmithingCode code) const {
        const char *message = nullptr;
        switch (code) {
        case KCDC::Shared::RPC::SmithingCode::StationBusy: message = "Someone else is working at this forge."; break;
        case KCDC::Shared::RPC::SmithingCode::MissingMaterials: message = "You do not have the materials for this recipe."; break;
        case KCDC::Shared::RPC::SmithingCode::RecipeNotKnown: message = "You have not learned this recipe."; break;
        case KCDC::Shared::RPC::SmithingCode::SkillTooLow: message = "Your craftsmanship is too low for this recipe."; break;
        default: message = "The server did not allow this workpiece."; break;
        }
        KCDC::Core::gApplication->GetPlayerInventoryService().RefuseUnsupportedAction(message);
    }

    void SmithingService::ApplyVerdict(const KCDC::Shared::RPC::SmithingVerdict &verdict) {
        const auto code = static_cast<KCDC::Shared::RPC::SmithingCode>(verdict.code);
        if (_phase != Phase::Awaiting || verdict.request != _request) {
            // An approval that outlived its press -- it came after the
            // deadline or a context change -- holds materials nobody works.
            if (code == KCDC::Shared::RPC::SmithingCode::Accepted && verdict.session != _session) {
                Report(verdict.session, KCDC::Shared::RPC::SmithingOutcome::Abandoned, 0.0F);
            }
            return;
        }
        Framework::Logging::GetLogger(kLogger)->debug("Server answered {} with code {}", verdict.request, verdict.code);
        if (code != KCDC::Shared::RPC::SmithingCode::Accepted) {
            Refused(code);
            Clear();
            return;
        }
        _session = verdict.session;
        _phase   = Phase::Approved;
    }

    void SmithingService::Clear() {
        _cancelRequested = false;
        _actionReplaced  = false;
        _phase           = Phase::Idle;
        _minigame        = nullptr;
        _station         = 0;
        _session         = 0;
        _recipe.clear();
        _replaying = false;
    }

    void SmithingService::NativeContextChanged() {
        _contextChanged.store(true);
    }

    void SmithingService::Reset() {
        // The server fails the workpiece with the connection; nothing to report.
        Clear();
        _finishingSuccess = false;
        std::scoped_lock lock(_incomingMutex);
        _verdicts.clear();
        _closures.clear();
    }

    void SmithingService::Update() {
        if (KCDC::Core::gApplication == nullptr) {
            return;
        }
        std::vector<KCDC::Shared::RPC::SmithingVerdict> verdicts;
        std::vector<KCDC::Shared::RPC::SmithingClosed> closures;
        {
            std::scoped_lock lock(_incomingMutex);
            verdicts.swap(_verdicts);
            closures.swap(_closures);
        }
        for (const auto &verdict : verdicts) {
            ApplyVerdict(verdict);
        }
        for (const auto &closed : closures) {
            if (closed.session == _session && (_phase == Phase::Approved || _phase == Phase::Working)) {
                _phase = Phase::Closing;
            }
        }
        // The body the minigame belongs to is gone, and the workpiece with it.
        if (_contextChanged.exchange(false) && _phase != Phase::Idle) {
            Framework::Logging::GetLogger(kLogger)->warn("Smithing native context changed; ending workpiece {}", _session);
            if (_phase == Phase::Approved) {
                Report(_session, KCDC::Shared::RPC::SmithingOutcome::Abandoned, 0.0F);
            }
            else if (_phase == Phase::Working) {
                Report(_session, UnfinishedOutcome(), 0.0F);
            }
            Clear();
        }
        switch (_phase) {
        case Phase::Awaiting:
            if (MonotonicMilliseconds() >= _deadlineMs) {
                Framework::Logging::GetLogger(kLogger)->warn("The server did not answer the smithing request {}", _request);
                Clear();
            }
            break;
        case Phase::Approved: {
            // The press is replayed only onto the minigame and recipe the
            // server approved; anything else was never worked.
            std::string selected;
            if (!Owns(_minigame) || !_minigame->IsActive() || _minigame->GetStationEntityId() != _station || !ReadSelectedRecipe(selected) || selected != _recipe) {
                Report(_session, KCDC::Shared::RPC::SmithingOutcome::Abandoned, 0.0F);
                Clear();
                break;
            }
            _phase     = Phase::Working;
            _replaying = true;
            _minigame->AcceptSelectedRecipe();
            if (_replaying) {
                // The detour never saw the replay: the game did not start the workpiece.
                Report(_session, KCDC::Shared::RPC::SmithingOutcome::Abandoned, 0.0F);
                Clear();
            }
            break;
        }
        case Phase::Working:
            // Shutdown (slot 5, 0x180B684B8) can end the minigame without
            // FinishWorkpiece; report teardown separately from a broken workpiece.
            if (!Owns(_minigame) || !_minigame->IsActive() || _minigame->HasFinished()) {
                Report(_session, UnfinishedOutcome(), 0.0F);
                Clear();
            }
            break;
        case Phase::Closing: {
            // Cancel reaches FinishWorkpiece(false), which reports nothing
            // now: the server already settled the workpiece.
            auto *minigame = _minigame;
            Clear();
            if (Owns(minigame) && minigame->IsActive()) {
                minigame->Cancel();
            }
            break;
        }
        default: break;
        }
        PublishProjectionHold();
    }

    void SmithingService::PublishProjectionHold() {
        // The server's take lands in the inventory before the replay has
        // started the workpiece: the recipe screen must not watch its
        // materials vanish in between.
        KCDC::Core::gApplication->GetPlayerInventoryService().PauseProjection(_phase == Phase::Awaiting || _phase == Phase::Approved);
    }
} // namespace KCDC::Features::Crafting
