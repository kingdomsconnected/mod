#include "c_combat_actor.h"

#include "c_combat_actor_action_manager.h"
#include "c_combat_actor_defense_actions.h"
#include "c_combat_actor_model.h"

#include "../../patterns.h"

#include <utils/hooking/hooking.h>

namespace SDK::wh::combatmodule {
    void C_CombatActorModel::SetCombatRequest(CombatRequest request, bool value) {
        hook::this_call<void>(gPatterns.C_ModelArrayPropertyBool__SetElement, &m_pCombatRequestsTable, static_cast<std::int32_t>(request), value);
    }

    void C_CombatActorModel::SetRiposteRequested(bool value) {
        hook::this_call<void>(gPatterns.C_ModelPropertyBool__SetValue, &m_riposteRequested, value);
    }

    bool C_CombatActor::IsFightingStanceUp() const {
        auto *model = GetModel();
        return model != nullptr && model->IsFightingStanceUp();
    }

    bool C_CombatActor::IsInFreeGuardStance() const {
        auto *model = GetModel();
        return model != nullptr && model->GetCombatActionType() == *reinterpret_cast<const std::int32_t *>(gPatterns.CombatActionTypeId__FreeGuard);
    }

    bool C_CombatActor::TryEnterCombatMode() {
        const CombatModeRequest request {};
        return hook::this_call<bool>(gPatterns.C_CombatActor__TryEnterCombatMode, this, &request);
    }

    void C_CombatActor::LeaveCombatModeIfActive() {
        hook::this_call<void>(gPatterns.C_CombatActor__LeaveCombatModeIfActive, this);
    }

    // The action type is the freeGuard row, read from the game's own global at
    // the moment of use exactly as C_CombatPlayerController::EnsureFightingStance
    // does. It is not decoration: EnterCombatMode writes it to the model, and
    // the attack factory reads it straight back out when it builds its query --
    // a stance entered with -1 leaves the fighter with no action type and the
    // factory with no row to match, so the swing is refused.
    bool C_CombatActor::RequestFightingStance(bool up) {
        const auto freeGuard = *reinterpret_cast<const std::int32_t *>(gPatterns.CombatActionTypeId__FreeGuard);

        const FightingStanceRequest request {up, freeGuard, false};
        return hook::this_call<bool>(gPatterns.C_CombatActor__RequestFightingStance, this, &request);
    }

    void C_CombatActor::RequestGuard(GuardRequest request, GuardRequestSource source) {
        hook::this_call<void>(gPatterns.C_CombatActor__RequestGuard, this, static_cast<std::uint8_t>(request), static_cast<std::uint8_t>(source));
    }

    // The engine passes a reason code through to the property's signal; 6 and
    // 8 are the two it uses and neither changes what is written, so the guard
    // path's 6 is what this states.
    void C_CombatActor::SetCombatZone(std::int32_t zoneId) {
        constexpr std::int32_t kZoneChangeReason = 6;
        hook::this_call<void>(gPatterns.C_CombatActor__SetCombatZone, this, static_cast<std::uint32_t>(zoneId), kZoneChangeReason, true);
    }

    bool C_CombatActor::RequestCombatZone(std::int32_t zoneId) {
        return hook::this_call<bool>(gPatterns.C_CombatActor__RequestCombatZone, this, static_cast<std::uint32_t>(zoneId));
    }

    bool C_CombatActor::TryApplyCombatZone(std::int32_t zoneId, std::int32_t guardSide) {
        return hook::this_call<bool>(gPatterns.C_CombatActor__TryApplyCombatZone, this, static_cast<std::uint32_t>(zoneId), guardSide);
    }

    bool C_CombatActor::RunAction(I_CombatActorActionPrivate **action) {
        return hook::this_call<bool>(gPatterns.C_CombatActor__RunAction, this, action);
    }

    // Reference counting mirrors PerformAttack in the engine: the factory
    // hands back one reference, the queue takes its own inside RunAction, and
    // the factory's is dropped afterwards whether or not the queue took it.
    void C_CombatActor::SetAttackInputClass(std::int32_t inputClassId, bool active) {
        hook::this_call<void>(gPatterns.C_CombatActor__SetAttackInputClass, this, inputClassId, active);
    }

    // The source only publishes Press after its controller has accepted the
    // input. Reproduction therefore applies the two lasting actor-level state
    // changes in native order. The source's 0x2000 action was only an
    // eligibility probe; it was released without queueing and is not part of
    // the held animation state.
    void C_CombatActor::BeginAttack(std::int32_t inputClassId) {
        if (auto *model = GetModel()) {
            model->SetCombatRequest(C_CombatActorModel::COMBAT_REQUEST_ATTACK, true);
        }

        SetAttackInputClass(inputClassId, true);
    }

    C_CombatActor::AttackOutcome C_CombatActor::TryUnpairedAttack(std::int32_t inputClassId) {
        const shared::C_ScopedRecursiveSRWLock lock(GetCombatGroupLock());
        const auto state = GetModel()->GetState();
        if (state != E_CombatActorStateId::Idle && state != E_CombatActorStateId::Guard) {
            return AttackOutcome::ActionRefused;
        }
        auto *manager = GetActionManager();
        if (manager == nullptr) {
            return AttackOutcome::NoManager;
        }
        auto *factory = static_cast<C_CombatActionAttackFactory *>(manager->GetAttackFactory());
        if (factory == nullptr) {
            return AttackOutcome::NoFactory;
        }
        // The native automation uses this flag to exclude paired attacks.
        // Combo actions can still be returned, so check the concrete type.
        constexpr std::uint32_t kUnpairedQuery = 0x2000;
        I_CombatActorActionPrivate *action     = nullptr;
        hook::this_call<void>(gPatterns.C_CombatActionAttackFactory__CreateAttackAction, factory, &action, inputClassId, kUnpairedQuery);
        if (action == nullptr) {
            return AttackOutcome::ActionRefused;
        }
        if (!C_CombatActorActionAttack::IsInstance(action)) {
            action->Release();
            return AttackOutcome::ActionRefused;
        }
        // The selector has already constructed the action and its helper.
        // Clear the query-only commit flags, keeping its selected context;
        // constructing a second copy would allocate both objects again.
        auto *attack = static_cast<C_CombatActorActionAttack *>(action);
        attack->ApplyAcceptedContext(attack->GetContextFlag0(), attack->GetContextFlag1(), attack->GetStateMode());
        const bool queued = RunAction(&action);
        action->Release();
        return queued ? AttackOutcome::Swung : AttackOutcome::QueueRefused;
    }

    C_CombatActor::AttackOutcome C_CombatActor::ReplayAcceptedAttack(std::uint32_t attackRowIndex, std::uint8_t candidateFlag0, std::uint8_t candidateFlag1, std::uint8_t contextFlag0, std::uint8_t contextFlag1, std::uint32_t stateMode) {
        C_CombatActionAttackDatabase *database           = C_CombatActionAttackDatabase::GetInstance();
        const S_CombatActionAttackDataDBData *attackData = database->GetRow(attackRowIndex);
        if (attackData == nullptr) {
            return AttackOutcome::InvalidRow;
        }

        C_CombatActorActionManager *manager = GetActionManager();
        if (manager == nullptr) {
            return AttackOutcome::NoManager;
        }
        auto *factory = static_cast<C_CombatActionAttackFactory *>(manager->GetAttackFactory());
        if (factory == nullptr) {
            return AttackOutcome::NoFactory;
        }

        S_CombatActionAttackCandidate candidate;
        candidate.m_pAttackData           = attackData;
        candidate.m_candidateFlag0        = candidateFlag0;
        candidate.m_candidateFlag1        = candidateFlag1;
        C_CombatActorActionAttack *attack = factory->CreateSelectedAttack(candidate);
        if (attack == nullptr) {
            return AttackOutcome::ActionRefused;
        }

        attack->ApplyAcceptedContext(contextFlag0, contextFlag1, stateMode);
        I_CombatActorActionPrivate *action = attack;
        bool queued                        = RunAction(&action);

        // Refused, the swing came in behind an action this body is still
        // finishing: a hit, a parry, a block, a recoil or the previous swing,
        // relayed a round trip late. The sender's engine already let this
        // swing start, so its own body is out of that action, and this one is
        // let in the way the queue lets anything in -- by standing higher than
        // what runs in its slot. The queue then ends the late action through
        // its own transition, and the attack's Enter puts its state back.
        // Nothing paired with another body is cut short.
        if (!queued) {
            I_CombatActorAction *running = GetRunningAction(attack->GetSlot());
            if (running != nullptr && running->IsActive() && !IsSyncAction(running)) {
                attack->ApplyAcceptedContext(contextFlag0, contextFlag1, running->GetState() + 1);
                queued = RunAction(&action);
            }
        }

        attack->Release();
        return queued ? AttackOutcome::Swung : AttackOutcome::QueueRefused;
    }
} // namespace SDK::wh::combatmodule
