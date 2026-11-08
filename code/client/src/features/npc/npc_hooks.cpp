#include <utils/safe_win32.h>

#include <MinHook.h>

#include <utils/hooking/hook_function.h>

#include "features/npc/npc_service.h"
#include "features/player/native_player_module.h"
#include "common/module_support.h"

#include "sdk/cryengine/entitysystem/i_entity.h"
#include "sdk/cryengine/scriptsystem/i_function_handler.h"
#include "sdk/cryengine/scriptsystem/i_script_system.h"
#include "sdk/patterns.h"
#include "sdk/wh/combatmodule/c_combat_actor.h"
#include "sdk/wh/combatmodule/c_combat_actor_opponent_manager.h"
#include "sdk/wh/dialogmodule/s_decision_alias.h"
#include "sdk/wh/entitymodule/c_actor.h"
#include "sdk/wh/entitymodule/c_human.h"
#include "sdk/wh/entitymodule/c_script_bind_human.h"
#include "sdk/wh/xgenaimodule/c_ai_brain.h"

#include <cstdint>
#include <limits>

// Server-owned NPCs keep their general brain suppressed, except an animal's
// while its simulator runs a native flee. Combat is admitted only for a
// server-designated pair on its simulator.
// Three more hand the game's own interaction menu over to the server: Talk on
// the NPCs it lets players talk to, and Rob on none of its bodies. One more
// keeps a stealth attack off them, since the pairing it needs is refused.
namespace KCDC::Features::Npc {
    namespace {
        using TryEnterCombatMode_t = bool(__fastcall *)(SDK::wh::combatmodule::C_CombatActor *, const SDK::wh::combatmodule::C_CombatActor::CombatModeRequest *);
        using SetOpponent_t        = void(__fastcall *)(SDK::wh::combatmodule::C_CombatActorOpponentManager *, SDK::IEntity *);
        using RequestStealth_t     = bool(__fastcall *)(void *, SDK::EntityId, std::int32_t);
        using BrainUpdate_t        = void(__fastcall *)(SDK::wh::xgenaimodule::C_AIBrain *, void *);

        TryEnterCombatMode_t g_tryEnterCombatModeOriginal = nullptr;
        SetOpponent_t g_setOpponentOriginal               = nullptr;
        RequestStealth_t g_requestStealthOriginal         = nullptr;
        BrainUpdate_t g_multiSubbUpdateOriginal           = nullptr;
        BrainUpdate_t g_singleSubbUpdateOriginal          = nullptr;

        using CanTalk_t       = bool(__fastcall *)(SDK::wh::entitymodule::C_Human *, SDK::EntityId, const SDK::wh::dialogmodule::S_DecisionAlias &);
        using RequestDialog_t = bool(__fastcall *)(SDK::wh::entitymodule::C_Actor *, SDK::EntityId, const SDK::wh::dialogmodule::S_DecisionAlias &, bool, bool);
        using CanBeRobbed_t   = int(__fastcall *)(SDK::wh::entitymodule::C_ScriptBindHuman *, SDK::IFunctionHandler *);

        CanTalk_t g_canTalkOriginal             = nullptr;
        RequestDialog_t g_requestDialogOriginal = nullptr;
        CanBeRobbed_t g_canBeRobbedOriginal     = nullptr;

        bool IsHeldMind(SDK::wh::xgenaimodule::C_AIBrain *brain) {
            return brain != nullptr && NpcService::IsHeldBrainOwner(brain->GetOwner());
        }

        /** The entity a fighter belongs to, or 0 before its Init has run. */
        SDK::EntityId FighterEntityId(SDK::wh::combatmodule::C_CombatActor *fighter) {
            if (fighter == nullptr || !fighter->IsInitialised()) {
                return 0;
            }
            auto *owner = fighter->GetOwner();
            return owner != nullptr ? owner->GetEntityId() : 0;
        }

        /**
         * No reactions: the brain keeps everything it owns and stops thinking.
         *
         * A brain's tick is what turns perceiving something into doing something
         * about it, so refusing the tick refuses every reaction at once. The flags
         * at brain+0x18 are deliberately left alone -- the engine's own gate
         * (0x1807608B4) reads them and the engine writes them, so a mod that set
         * them would be overwritten.
         *
         * An animal whose simulator runs a native flee thinks again: the
         * escape is its brain's to take and to run.
         */
        void __fastcall MultiSubbUpdate_Hook(SDK::wh::xgenaimodule::C_AIBrain *brain, void *context) {
            if (IsHeldMind(brain)) {
                return;
            }
            g_multiSubbUpdateOriginal(brain, context);
        }

        /** The other concrete brain kind; `BuildBrainFromTemplate` picks between them. */
        void __fastcall SingleSubbUpdate_Hook(SDK::wh::xgenaimodule::C_AIBrain *brain, void *context) {
            if (IsHeldMind(brain)) {
                return;
            }
            g_singleSubbUpdateOriginal(brain, context);
        }

        /**
         * Passive NPCs stay out of combat. An explicit attack order admits its
         * fighter and target. The entry virtual this calls
         * (slot 216, 0x181003160) is the only caller of `EnterCombatMode`, so
         * everything downstream -- the automation, the guard selector, the slot
         * planner -- is never reached either.
         *
         * False is what the engine answers for a body it refuses itself.
         */
        bool __fastcall TryEnterCombatMode_Hook(SDK::wh::combatmodule::C_CombatActor *fighter, const SDK::wh::combatmodule::C_CombatActor::CombatModeRequest *requestedMode) {
            const SDK::EntityId fighterId = FighterEntityId(fighter);
            if (NpcService::IsServerOwnedBody(fighterId) && !NpcService::IsCombatEnabled(fighterId)) {
                return false;
            }
            return g_tryEnterCombatModeOriginal(fighter, requestedMode);
        }

        /**
         * Passive NPCs cannot acquire opponents. A server attack admits only
         * its designated pair, in either direction so the victim can answer.
         * Null always clears a pairing during cancellation.
         *
         * Do not refuse GetOrCreateCombatActor instead: native callers can
         * dereference its result even after a null short-circuits their guard.
         */
        void __fastcall SetOpponent_Hook(SDK::wh::combatmodule::C_CombatActorOpponentManager *manager, SDK::IEntity *opponent) {
            // Null passes through untouched: it is how a pairing is cleared.
            if (manager != nullptr && opponent != nullptr) {
                const SDK::EntityId fighterId = FighterEntityId(manager->GetCombatActor());
                const bool opponentOwned      = NpcService::IsServerOwnedBody(opponent->GetId());
                const bool fighterOwned       = !opponentOwned && NpcService::IsServerOwnedBody(fighterId);
                if ((opponentOwned || fighterOwned) && !NpcService::IsCombatPair(fighterId, opponent->GetId())) {
                    // Asked to take one of these bodies, the call is dropped and
                    // the fighter keeps whatever it had; one of these bodies'
                    // own manager is stood down instead.
                    if (fighterOwned) {
                        g_setOpponentOriginal(manager, nullptr);
                    }
                    return;
                }
            }
            g_setOpponentOriginal(manager, opponent);
        }

        /**
         * No stealth attack on one of these bodies, the other half of the
         * refusal above.
         *
         * The pending attack pairs attacker and victim through
         * `C_CombatActor::SetPairedActor`, which is `SetOpponent` underneath,
         * so the pairing is refused and the attacker is left with no opponent.
         * The attacker's fighting stance then enters a guard whose stealth
         * signal reads that opponent without a null test, and the client died
         * in `ShouldUseStealthTransition` (0x1816BCB40). The engine's own
         * request answers false for a victim it will not take, so this is one
         * more victim it will not take.
         */
        bool __fastcall RequestStealthAttack_Hook(void *stealthInterface, SDK::EntityId victimEntityId, std::int32_t attackKind) {
            if (NpcService::IsServerOwnedBody(victimEntityId)) {
                return false;
            }
            return g_requestStealthOriginal(stealthInterface, victimEntityId, attackKind);
        }

        /**
         * Talk is on offer. `BasicAIActions:GetActions` shows the native Talk
         * entry exactly when this says yes, and the game's own answer turns on
         * its dialogue graph having something for the NPC's soul -- which a
         * server-authored NPC's never does, and which would start the game's
         * conversation rather than the server's if it did.
         */
        bool __fastcall CanTalk_Hook(SDK::wh::entitymodule::C_Human *human, SDK::EntityId talkerEntityId, const SDK::wh::dialogmodule::S_DecisionAlias &alias) {
            if (human != nullptr && NpcService::IsInteractableBody(human->GetEntityId())) {
                return true;
            }
            // Another player's body is registered with the NPC manager as
            // the game registers a player, and has no conversation of its own.
            if (KCDC::Features::Player::NativePlayerModule::IsRemote(human)) {
                return false;
            }
            return g_canTalkOriginal(human, talkerEntityId, alias);
        }

        /**
         * Talk was chosen. `BasicAIActions:OnTalk` runs this on the NPC with the
         * player as the target; on one of the server's NPCs it becomes the same
         * interaction the mod's key sends, and no game conversation starts.
         *
         * Only the local player's request is taken: a quest or script asking
         * one of these bodies to talk to someone else is left to the engine.
         */
        bool __fastcall RequestDialog_Hook(SDK::wh::entitymodule::C_Actor *actor, SDK::EntityId targetEntityId, const SDK::wh::dialogmodule::S_DecisionAlias &alias, bool unkFlag0, bool unkFlag1) {
            if (actor != nullptr && NpcService::IsInteractableBody(actor->GetEntityId())) {
                SDK::IEntity *player = KCDC::Common::LocalPlayer();
                if (player != nullptr && player->GetId() == targetEntityId) {
                    NpcService::Get().InteractWith(actor->GetEntityId());
                    return true;
                }
            }
            return g_requestDialogOriginal(actor, targetEntityId, alias, unkFlag0, unkFlag1);
        }

        /**
         * No Rob. A pickpocket minigame on one of the server's bodies would
         * move items out of an inventory the server does not know changed.
         *
         * The bind answers Lua directly, so the refusal is returned the way
         * the bind's own helper returns its answer.
         */
        int __fastcall CanBeRobbed_Hook(SDK::wh::entitymodule::C_ScriptBindHuman *scriptBind, SDK::IFunctionHandler *handler) {
            const SDK::ScriptHandle victimHandle = handler != nullptr ? handler->GetThis() : 0;
            if (victimHandle != 0 && victimHandle <= std::numeric_limits<SDK::EntityId>::max() && NpcService::IsServerOwnedBody(static_cast<SDK::EntityId>(victimHandle))) {
                return handler->EndFunctionAny(SDK::ScriptAnyValue(false));
            }
            return g_canBeRobbedOriginal(scriptBind, handler);
        }
    } // namespace
} // namespace KCDC::Features::Npc

static InitFunction init(
    []() {
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_AIBrainMultiSubb__Update), reinterpret_cast<LPVOID>(&KCDC::Features::Npc::MultiSubbUpdate_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Npc::g_multiSubbUpdateOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_AIBrainSingleSubb__Update), reinterpret_cast<LPVOID>(&KCDC::Features::Npc::SingleSubbUpdate_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Npc::g_singleSubbUpdateOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_CombatActor__TryEnterCombatMode), reinterpret_cast<LPVOID>(&KCDC::Features::Npc::TryEnterCombatMode_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Npc::g_tryEnterCombatModeOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_CombatActorOpponentManager__SetOpponent), reinterpret_cast<LPVOID>(&KCDC::Features::Npc::SetOpponent_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Npc::g_setOpponentOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_CombatActorStealth__RequestStealthAttack), reinterpret_cast<LPVOID>(&KCDC::Features::Npc::RequestStealthAttack_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Npc::g_requestStealthOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_Human__CanTalk), reinterpret_cast<LPVOID>(&KCDC::Features::Npc::CanTalk_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Npc::g_canTalkOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_Actor__RequestDialog), reinterpret_cast<LPVOID>(&KCDC::Features::Npc::RequestDialog_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Npc::g_requestDialogOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_ScriptBindHuman__CanBeRobbed), reinterpret_cast<LPVOID>(&KCDC::Features::Npc::CanBeRobbed_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Npc::g_canBeRobbedOriginal));
    },
    "Npc");
