#include <utils/safe_win32.h>

#include <MinHook.h>
#include <utils/hooking/hook_function.h>

#include "core/application.h"
#include "core/local_events.h"
#include "features/player/player_inventory_service.h"
#include "native_alchemy_back_capture.h"
#include "sdk/cryengine/animation/anim_event_instance.h"
#include "sdk/cryengine/animation/i_character_instance.h"
#include "sdk/cryengine/scriptsystem/i_function_handler.h"
#include "sdk/wh/entitymodule/c_actor.h"
#include "sdk/wh/entitymodule/c_actor_action_minigame.h"
#include "sdk/wh/entitymodule/c_item_attachment_manager.h"
#include "sdk/wh/framework/wuid.h"
#include "sdk/wh/playermodule/c_alchemy.h"
#include "sdk/wh/playermodule/c_blacksmithing.h"
#include "sdk/wh/playermodule/c_blacksmithing_slot_tool_attachment.h"
#include "sdk/wh/playermodule/c_return_ingredient_action.h"
#include "sdk/wh/playermodule/c_script_bind_alchemy.h"
#include "sdk/wh/playermodule/c_take_ingredient_action.h"
#include "sdk/wh/playermodule/c_use_bellows_action.h"
#include "sdk/wh/playermodule/c_use_mortar_action.h"
#include "sdk/wh/playermodule/c_use_resource_action.h"
#include "sdk/wh/playermodule/c_use_retort_action.h"
#include "sdk/wh/rpgmodule/i_rpg_minigames.h"

namespace KCDC::Features::Crafting {
    namespace {
        // Both concrete Starts return acceptance in AL. The native dispatcher
        // at 0x18086BFC8 tests it before installing its completion listener.
        using AlchemyStart_t                                              = bool(__fastcall *)(SDK::wh::playermodule::C_Alchemy *, SDK::EntityId);
        using AlchemyScriptStart_t                                        = int(__fastcall *)(SDK::wh::playermodule::C_ScriptBind_Alchemy *, SDK::IFunctionHandler *, SDK::ScriptHandle, SDK::ScriptHandle);
        using AlchemySelect_t                                             = void(__fastcall *)(SDK::wh::playermodule::C_Alchemy *, SDK::wh::playermodule::AlchemyItemType);
        using AlchemyBack_t                                               = void(__fastcall *)(KCDC::Features::Crafting::NativeAlchemyBackCapture181FFEC90 *);
        AlchemyBack_t g_alchemyBackOriginal                               = nullptr;
        using AlchemyVoid_t                                               = void(__fastcall *)(SDK::wh::playermodule::C_Alchemy *);
        AlchemyVoid_t g_alchemyEntryReadyOriginal                         = nullptr;
        using AlchemyTakeEvent_t                                          = void(__fastcall *)(SDK::wh::playermodule::C_TakeIngredientAction *, SDK::ICharacterInstance *, const SDK::AnimEventInstance &);
        using AlchemyBaseEvent_t                                          = void(__fastcall *)(SDK::wh::playermodule::C_UseResourceAction *, SDK::ICharacterInstance *, const SDK::AnimEventInstance &);
        AlchemyTakeEvent_t g_alchemyTakeEventOriginal                     = nullptr;
        AlchemyBaseEvent_t g_alchemyBaseEventOriginal                     = nullptr;
        using AlchemyReturnIngredient_t                                   = void(__fastcall *)(SDK::wh::playermodule::C_ReturnIngredientAction *);
        AlchemyReturnIngredient_t g_alchemyReturnIngredientOriginal       = nullptr;
        using AlchemyBellowsVoid_t                                        = void(__fastcall *)(SDK::wh::playermodule::C_UseBellowsAction *);
        using AlchemyRetortVoid_t                                         = void(__fastcall *)(SDK::wh::playermodule::C_UseRetortAction *);
        using AlchemyMortarStart_t                                        = bool(__fastcall *)(SDK::wh::playermodule::C_UseMortarAction *);
        AlchemyMortarStart_t g_alchemyMortarStartOriginal                 = nullptr;
        using AlchemyRetortCount_t                                        = std::uint32_t(__fastcall *)(SDK::wh::playermodule::C_UseRetortAction *);
        AlchemyBellowsVoid_t g_alchemyBellowsPumpOriginal                 = nullptr;
        AlchemyBellowsVoid_t g_alchemyBellowsReturnOriginal               = nullptr;
        AlchemyRetortVoid_t g_alchemyRetortPumpOriginal                   = nullptr;
        AlchemyRetortVoid_t g_alchemyRetortFinishOriginal                 = nullptr;
        AlchemyRetortCount_t g_alchemyRetortCountOriginal                 = nullptr;
        using AlchemyUpdate_t                                             = void(__fastcall *)(SDK::wh::playermodule::C_Alchemy *, float);
        using AlchemyResourceConstructor_t                                = SDK::wh::playermodule::C_AlchemyResource *(__fastcall *)(SDK::wh::playermodule::C_AlchemyResource *, SDK::wh::entitymodule::C_Item *);
        using AlchemyBaseConstructor_t                                    = SDK::wh::playermodule::C_AlchemyResource *(__fastcall *)(SDK::wh::playermodule::C_AlchemyResource *, SDK::wh::playermodule::AlchemyItemType);
        AlchemyBaseConstructor_t g_alchemyBaseConstructorOriginal         = nullptr;
        AlchemyScriptStart_t g_alchemyScriptStartOriginal                 = nullptr;
        AlchemySelect_t g_alchemySelectOriginal                           = nullptr;
        AlchemyVoid_t g_alchemyRequestCleanupOriginal                     = nullptr;
        AlchemyVoid_t g_alchemyShutdownOriginal                           = nullptr;
        AlchemyVoid_t g_alchemyDestructorOriginal                         = nullptr;
        AlchemyUpdate_t g_alchemyUpdateOriginal                           = nullptr;
        AlchemyResourceConstructor_t g_alchemyResourceConstructorOriginal = nullptr;
        AlchemySelect_t g_alchemyEvaluateOriginal                         = nullptr;
        AlchemyVoid_t g_alchemyResetOriginal                              = nullptr;
        using AlchemyLifecycleStart_t                                     = float(__fastcall *)(SDK::wh::rpgmodule::I_RPGMinigames *, SDK::wh::rpgmodule::I_Soul *);
        using AlchemyLifecycleFinish_t                                    = void(__fastcall *)(SDK::wh::rpgmodule::I_RPGMinigames *, SDK::wh::rpgmodule::I_Soul *);
        AlchemyLifecycleStart_t g_alchemyLifecycleStartOriginal           = nullptr;
        AlchemyLifecycleFinish_t g_alchemyLifecycleFinishOriginal         = nullptr;
        using AlchemyRecipeRange_t                                        = bool(__fastcall *)(SDK::wh::rpgmodule::I_RPGMinigames *, SDK::wh::rpgmodule::I_Soul *, std::uint32_t, std::uint32_t, std::uint32_t);
        AlchemyRecipeRange_t g_alchemyRecipeRangeOriginal                 = nullptr;
        using AlchemyRecipeStep_t                                         = bool(__fastcall *)(SDK::wh::rpgmodule::I_RPGMinigames *, SDK::wh::rpgmodule::I_Soul *, std::uint32_t, std::uint32_t);
        AlchemyRecipeStep_t g_alchemyRecipeStepOriginal                   = nullptr;
        AlchemyStart_t g_alchemyStartOriginal                             = nullptr;
        using BlacksmithingVoid_t                                         = void(__fastcall *)(SDK::wh::playermodule::C_Blacksmithing *);
        using BlacksmithingOutcome_t                                      = void(__fastcall *)(SDK::wh::playermodule::C_Blacksmithing *, bool);
        // RPG slot 73: the created product's WUID through the hidden result, and whether lockpicks were granted.
        using BlacksmithingFinished_t                           = SDK::wh::framework::WUID *(__fastcall *)(SDK::wh::rpgmodule::I_RPGMinigames *, SDK::wh::framework::WUID *, const void *, bool *);
        BlacksmithingVoid_t g_blacksmithingCancelOriginal           = nullptr;
        using ActorActionMinigameExit_t                             = void(__fastcall *)(SDK::wh::entitymodule::C_ActorActionMinigame *);
        ActorActionMinigameExit_t g_actorActionMinigameExitOriginal = nullptr;
        BlacksmithingVoid_t g_blacksmithingAcceptOriginal       = nullptr;
        BlacksmithingOutcome_t g_blacksmithingFinishOriginal    = nullptr;
        BlacksmithingOutcome_t g_blacksmithingConsumeOriginal   = nullptr;
        BlacksmithingFinished_t g_blacksmithingFinishedOriginal = nullptr;
        using BlacksmithingToolAttach_t                             = void(__fastcall *)(SDK::wh::playermodule::C_BlacksmithingSlotToolAttachment *, SDK::wh::entitymodule::C_Actor *);
        using BlacksmithingToolDetach_t                             = void(__fastcall *)(SDK::wh::playermodule::C_BlacksmithingSlotToolAttachment *);
        BlacksmithingToolAttach_t g_blacksmithingToolAttachOriginal = nullptr;
        BlacksmithingToolDetach_t g_blacksmithingToolDetachOriginal = nullptr;

        int __fastcall AlchemyScriptStart(SDK::wh::playermodule::C_ScriptBind_Alchemy *binding, SDK::IFunctionHandler *handler, SDK::ScriptHandle actor, SDK::ScriptHandle station) {
            auto *application = KCDC::Core::gApplication.get();
            if (application == nullptr || !application->GetPlayerInventoryService().HasAuthority()) {
                return g_alchemyScriptStartOriginal(binding, handler, actor, station);
            }
            auto &events = application->GetLocalEvents();
            if (!events.BeginAlchemyScriptCall(actor, station)) {
                return handler->EndFunctionAny(SDK::ScriptAnyValue(false));
            }
            const int count = g_alchemyScriptStartOriginal(binding, handler, actor, station);
            return handler->EndFunctionAny(SDK::ScriptAnyValue(events.EndAlchemyScriptCall(count)));
        }

        bool __fastcall AlchemyStart(SDK::wh::playermodule::C_Alchemy *minigame, SDK::EntityId station) {
            auto *application = KCDC::Core::gApplication.get();
            if (application == nullptr || !application->GetPlayerInventoryService().HasAuthority()) {
                return g_alchemyStartOriginal(minigame, station);
            }
            auto &events = application->GetLocalEvents();
            if (!events.AlchemyStarting(minigame, station)) {
                return false;
            }
            const bool accepted = g_alchemyStartOriginal(minigame, station);
            events.AlchemyStarted(minigame, station, accepted);
            return accepted;
        }

        void __fastcall AlchemySelect(SDK::wh::playermodule::C_Alchemy *minigame, SDK::wh::playermodule::AlchemyItemType target) {
            auto *application = KCDC::Core::gApplication.get();
            if (application == nullptr || !application->GetPlayerInventoryService().HasAuthority()) {
                g_alchemySelectOriginal(minigame, target);
                return;
            }
            auto &events = application->GetLocalEvents();
            if (!events.AlchemySelecting(minigame, target)) {
                return;
            }
            const bool scoped = events.AlchemyNativeScopeStarting(minigame);
            g_alchemySelectOriginal(minigame, target);
            events.AlchemySelected(minigame, target);
            if (scoped) {
                events.AlchemyNativeScopeEnded();
            }
        }

        void __fastcall AlchemyBack(KCDC::Features::Crafting::NativeAlchemyBackCapture181FFEC90 *capture) {
            auto *application = KCDC::Core::gApplication.get();
            if (application == nullptr || !application->GetPlayerInventoryService().HasAuthority() || application->GetLocalEvents().AlchemyReturning(capture->owner)) {
                g_alchemyBackOriginal(capture);
            }
        }

        void __fastcall AlchemyEntryReady(SDK::wh::playermodule::C_Alchemy *minigame) {
            auto *application = KCDC::Core::gApplication.get();
            if (application == nullptr || !application->GetPlayerInventoryService().HasAuthority()) {
                g_alchemyEntryReadyOriginal(minigame);
                return;
            }
            auto &events = application->GetLocalEvents();
            if (!events.AlchemyEntryReady(minigame)) {
                return;
            }
            const bool scoped = events.AlchemyNativeScopeStarting(minigame);
            g_alchemyEntryReadyOriginal(minigame);
            if (scoped) {
                events.AlchemyNativeScopeEnded();
            }
        }

        void __fastcall AlchemyTakeEvent(SDK::wh::playermodule::C_TakeIngredientAction *action, SDK::ICharacterInstance *character, const SDK::AnimEventInstance &event) {
            auto *application = KCDC::Core::gApplication.get();
            if (application == nullptr || !application->GetPlayerInventoryService().HasAuthority()) {
                g_alchemyTakeEventOriginal(action, character, event);
                return;
            }
            auto &events = application->GetLocalEvents();
            if (!events.AlchemyActionScopeStarting(action->GetAlchemy(), action)) {
                return;
            }
            g_alchemyTakeEventOriginal(action, character, event);
            events.AlchemyNativeScopeEnded();
        }

        void __fastcall AlchemyBaseEvent(SDK::wh::playermodule::C_UseResourceAction *action, SDK::ICharacterInstance *character, const SDK::AnimEventInstance &event) {
            auto *application = KCDC::Core::gApplication.get();
            if (application == nullptr || !application->GetPlayerInventoryService().HasAuthority()) {
                g_alchemyBaseEventOriginal(action, character, event);
                return;
            }
            auto &events = application->GetLocalEvents();
            if (!events.AlchemyActionScopeStarting(action->GetAlchemy(), action)) {
                return;
            }
            g_alchemyBaseEventOriginal(action, character, event);
            events.AlchemyNativeScopeEnded();
        }

        void __fastcall AlchemyReturnIngredient(SDK::wh::playermodule::C_ReturnIngredientAction *action) {
            auto *application = KCDC::Core::gApplication.get();
            if (application == nullptr || !application->GetPlayerInventoryService().HasAuthority()) {
                g_alchemyReturnIngredientOriginal(action);
                return;
            }
            auto &events = application->GetLocalEvents();
            if (!events.AlchemyActionScopeStarting(action->GetAlchemy(), action)) {
                return;
            }
            g_alchemyReturnIngredientOriginal(action);
            events.AlchemyNativeScopeEnded();
        }

        void __fastcall AlchemyUpdate(SDK::wh::playermodule::C_Alchemy *minigame, float deltaSeconds) {
            auto *application = KCDC::Core::gApplication.get();
            const bool scoped = application != nullptr && application->GetLocalEvents().AlchemyNativeScopeStarting(minigame);
            g_alchemyUpdateOriginal(minigame, deltaSeconds);
            if (scoped) {
                application->GetLocalEvents().AlchemyNativeScopeEnded();
            }
        }

        void __fastcall AlchemyRequestCleanup(SDK::wh::playermodule::C_Alchemy *minigame) {
            if (KCDC::Core::gApplication != nullptr) {
                KCDC::Core::gApplication->GetLocalEvents().AlchemyExitRequested(minigame);
            }
            g_alchemyRequestCleanupOriginal(minigame);
        }

        void __fastcall AlchemyShutdown(SDK::wh::playermodule::C_Alchemy *minigame) {
            auto *application = KCDC::Core::gApplication.get();
            const bool scoped = application != nullptr && application->GetLocalEvents().AlchemyNativeCleanupScopeStarting(minigame);
            if (application != nullptr) {
                application->GetLocalEvents().AlchemyShuttingDown(minigame);
            }
            // The idle action's shutdown drops the table's camera request
            // (0x180A96740); Finish closes the table and unhooks
            // SetCameraLimitsEnabled from the request's signal (0x18193BE5C).
            // A visit that ended with something still taken, out of the idle,
            // left the request raised and its actor's camera limits counted
            // for good, so every later visit kept the view clamped. Dropped
            // here while the signal still reaches the actor it raised; never
            // below zero, which the actor's count does not guard itself
            // (0x180B66DFC).
            const SDK::wh::entitymodule::C_Actor *actor = minigame->GetActor();
            if (minigame->AreCameraLimitsRequested() && actor != nullptr && actor->GetCameraLimitCount() > 0) {
                minigame->SetCameraLimitsRequested(false);
            }
            g_alchemyShutdownOriginal(minigame);
            if (application != nullptr) {
                application->GetLocalEvents().AlchemyShutDown(minigame);
            }
            if (scoped) {
                application->GetLocalEvents().AlchemyNativeCleanupScopeEnded();
            }
        }

        void __fastcall AlchemyDestructor(SDK::wh::playermodule::C_Alchemy *minigame) {
            auto *application = KCDC::Core::gApplication.get();
            const bool scoped = application != nullptr && application->GetLocalEvents().AlchemyNativeCleanupScopeStarting(minigame);
            if (application != nullptr) {
                application->GetLocalEvents().AlchemyDestroying(minigame);
            }
            g_alchemyDestructorOriginal(minigame);
            if (application != nullptr) {
                application->GetLocalEvents().AlchemyDestroyed(minigame);
            }
            if (scoped) {
                application->GetLocalEvents().AlchemyNativeCleanupScopeEnded();
            }
        }

        SDK::wh::playermodule::C_AlchemyResource *__fastcall AlchemyResourceConstructor(SDK::wh::playermodule::C_AlchemyResource *resource, SDK::wh::entitymodule::C_Item *item) {
            auto *application = KCDC::Core::gApplication.get();
            if (application != nullptr) {
                application->GetLocalEvents().AlchemyResourceConstructing(resource, item);
            }
            auto *result = g_alchemyResourceConstructorOriginal(resource, item);
            if (application != nullptr) {
                application->GetLocalEvents().AlchemyResourceConstructed(resource, item);
            }
            return result;
        }

        SDK::wh::playermodule::C_AlchemyResource *__fastcall AlchemyBaseConstructor(SDK::wh::playermodule::C_AlchemyResource *resource, SDK::wh::playermodule::AlchemyItemType selection) {
            auto *result      = g_alchemyBaseConstructorOriginal(resource, selection);
            auto *application = KCDC::Core::gApplication.get();
            if (application != nullptr) {
                application->GetLocalEvents().AlchemyBaseResourceConstructed(result, selection);
            }
            return result;
        }

        void __fastcall AlchemyEvaluate(SDK::wh::playermodule::C_Alchemy *minigame, SDK::wh::playermodule::AlchemyItemType container) {
            auto *application = KCDC::Core::gApplication.get();
            if (application != nullptr && application->GetPlayerInventoryService().HasAuthority()) {
                auto &events        = application->GetLocalEvents();
                const bool scoped   = events.AlchemyNativeScopeStarting(minigame);
                const bool suppress = events.AlchemySuppressNativeOutcome(minigame, container);
                if (scoped) {
                    events.AlchemyNativeScopeEnded();
                }
                if (suppress) {
                    return;
                }
            }
            g_alchemyEvaluateOriginal(minigame, container);
        }

        void __fastcall AlchemyReset(SDK::wh::playermodule::C_Alchemy *minigame) {
            auto *application = KCDC::Core::gApplication.get();
            const bool scoped = application != nullptr && application->GetLocalEvents().AlchemyNativeCleanupScopeStarting(minigame);
            g_alchemyResetOriginal(minigame);
            if (application != nullptr) {
                application->GetLocalEvents().AlchemyResetNative(minigame);
            }
            if (scoped) {
                application->GetLocalEvents().AlchemyNativeCleanupScopeEnded();
            }
        }

        float __fastcall AlchemyLifecycleStart(SDK::wh::rpgmodule::I_RPGMinigames *minigames, SDK::wh::rpgmodule::I_Soul *soul) {
            auto *application = KCDC::Core::gApplication.get();
            float value;
            bool executeNative;
            if (application != nullptr && application->GetLocalEvents().AlchemyStartedQuery(soul, value, executeNative)) {
                if (executeNative) {
                    g_alchemyLifecycleStartOriginal(minigames, soul);
                }
                return value;
            }
            return g_alchemyLifecycleStartOriginal(minigames, soul);
        }

        void __fastcall AlchemyLifecycleFinish(SDK::wh::rpgmodule::I_RPGMinigames *minigames, SDK::wh::rpgmodule::I_Soul *soul) {
            auto *application = KCDC::Core::gApplication.get();
            if (application != nullptr && application->GetLocalEvents().AlchemyFinishedEvent(soul)) {
                return;
            }
            g_alchemyLifecycleFinishOriginal(minigames, soul);
        }

        bool __fastcall AlchemyRecipeRange(SDK::wh::rpgmodule::I_RPGMinigames *minigames, SDK::wh::rpgmodule::I_Soul *soul, std::uint32_t recipe, std::uint32_t first, std::uint32_t last) {
            auto *application = KCDC::Core::gApplication.get();
            bool value;
            if (application != nullptr && application->GetPlayerInventoryService().HasAuthority() && application->GetLocalEvents().AlchemyRecipeKnowledgeQuery(soul, recipe, first, last, value)) {
                return value;
            }
            return g_alchemyRecipeRangeOriginal(minigames, soul, recipe, first, last);
        }

        // The table book lists a recipe only when one of its steps is known
        // here (C_AlchemyRecipe::HasAnyKnownConditionalStep 0x1808C3A9C).
        bool __fastcall AlchemyRecipeStep(SDK::wh::rpgmodule::I_RPGMinigames *minigames, SDK::wh::rpgmodule::I_Soul *soul, std::uint32_t recipe, std::uint32_t step) {
            auto *application = KCDC::Core::gApplication.get();
            bool value;
            if (application != nullptr && application->GetPlayerInventoryService().HasAuthority() && application->GetLocalEvents().AlchemyRecipeKnowledgeQuery(soul, recipe, step, step, value)) {
                return value;
            }
            return g_alchemyRecipeStepOriginal(minigames, soul, recipe, step);
        }

        void __fastcall AlchemyBellowsPump(SDK::wh::playermodule::C_UseBellowsAction *action) {
            auto *application = KCDC::Core::gApplication.get();
            if (application == nullptr || !application->GetPlayerInventoryService().HasAuthority()) {
                g_alchemyBellowsPumpOriginal(action);
                return;
            }
            auto &events = application->GetLocalEvents();
            if (!events.AlchemyBellowsPumping(action)) {
                return;
            }
            const bool scoped = events.AlchemyActionScopeStarting(action->GetAlchemy(), action);
            if (!scoped) {
                return;
            }
            g_alchemyBellowsPumpOriginal(action);
            events.AlchemyNativeScopeEnded();
        }

        void __fastcall AlchemyBellowsReturn(SDK::wh::playermodule::C_UseBellowsAction *action) {
            auto *application = KCDC::Core::gApplication.get();
            if (application == nullptr || !application->GetPlayerInventoryService().HasAuthority()) {
                g_alchemyBellowsReturnOriginal(action);
                return;
            }
            auto &events = application->GetLocalEvents();
            if (!events.AlchemyBellowsReturning(action)) {
                return;
            }
            const bool scoped = events.AlchemyActionScopeStarting(action->GetAlchemy(), action);
            if (!scoped) {
                return;
            }
            g_alchemyBellowsReturnOriginal(action);
            events.AlchemyNativeScopeEnded();
        }

        void __fastcall AlchemyRetortPump(SDK::wh::playermodule::C_UseRetortAction *action) {
            auto *application = KCDC::Core::gApplication.get();
            if (application == nullptr || !application->GetPlayerInventoryService().HasAuthority()) {
                g_alchemyRetortPumpOriginal(action);
                return;
            }
            auto &events = application->GetLocalEvents();
            if (!events.AlchemyRetortPumping(action)) {
                return;
            }
            const bool scoped = events.AlchemyActionScopeStarting(action->GetAlchemy(), action);
            if (!scoped) {
                return;
            }
            g_alchemyRetortPumpOriginal(action);
            events.AlchemyNativeScopeEnded();
        }

        void __fastcall AlchemyRetortFinish(SDK::wh::playermodule::C_UseRetortAction *action) {
            auto *application = KCDC::Core::gApplication.get();
            if (application == nullptr || !application->GetPlayerInventoryService().HasAuthority()) {
                g_alchemyRetortFinishOriginal(action);
                return;
            }
            auto &events = application->GetLocalEvents();
            if (!events.AlchemyRetortFinishing(action)) {
                return;
            }
            const bool scoped = events.AlchemyActionScopeStarting(action->GetAlchemy(), action);
            if (!scoped) {
                return;
            }
            g_alchemyRetortFinishOriginal(action);
            events.AlchemyNativeScopeEnded();
        }

        bool __fastcall AlchemyMortarStart(SDK::wh::playermodule::C_UseMortarAction *action) {
            const bool accepted = g_alchemyMortarStartOriginal(action);
            auto *application   = KCDC::Core::gApplication.get();
            if (application != nullptr && application->GetPlayerInventoryService().HasAuthority()) {
                application->GetLocalEvents().AlchemyMortarStarted(action, accepted);
            }
            return accepted;
        }

        std::uint32_t __fastcall AlchemyRetortCount(SDK::wh::playermodule::C_UseRetortAction *action) {
            auto *application = KCDC::Core::gApplication.get();
            std::uint32_t value;
            if (application != nullptr && application->GetPlayerInventoryService().HasAuthority() && application->GetLocalEvents().AlchemyRetortRequiredPulses(action, value)) {
                return value;
            }
            return g_alchemyRetortCountOriginal(action);
        }

        void __fastcall BlacksmithingAccept(SDK::wh::playermodule::C_Blacksmithing *minigame) {
            if (KCDC::Core::gApplication == nullptr || KCDC::Core::gApplication->GetLocalEvents().SmithingRecipeAccepting(minigame)) {
                g_blacksmithingAcceptOriginal(minigame);
            }
        }

        void __fastcall ActorActionMinigameExit(SDK::wh::entitymodule::C_ActorActionMinigame *action) {
            // Ahead of the original: a replaced action ends its minigame
            // through the same Cancel a normal exit request calls.
            if (KCDC::Core::gApplication != nullptr && action->WasCancelled()) {
                KCDC::Core::gApplication->GetLocalEvents().MinigameActionReplaced(action->GetMinigame());
            }
            g_actorActionMinigameExitOriginal(action);
        }

        void __fastcall BlacksmithingCancel(SDK::wh::playermodule::C_Blacksmithing *minigame) {
            if (KCDC::Core::gApplication != nullptr) {
                KCDC::Core::gApplication->GetLocalEvents().SmithingCancelling(minigame);
            }
            g_blacksmithingCancelOriginal(minigame);
        }

        void __fastcall BlacksmithingFinish(SDK::wh::playermodule::C_Blacksmithing *minigame, bool success) {
            if (KCDC::Core::gApplication == nullptr) {
                g_blacksmithingFinishOriginal(minigame, success);
                return;
            }
            auto &events = KCDC::Core::gApplication->GetLocalEvents();
            events.SmithingWorkpieceFinishing(minigame, success);
            g_blacksmithingFinishOriginal(minigame, success);
            events.SmithingWorkpieceFinished(minigame);
        }

        /**
         * The smithery's slot-held tool going to the smith's hand. Its item
         * travels through the smith's inventory (`CompleteItemTransfer`, no
         * destination holder), which the inventory authority otherwise
         * refuses as a pickup -- and without it the hand stays empty and the
         * animation has nothing to hold.
         */
        void __fastcall BlacksmithingToolAttach(SDK::wh::playermodule::C_BlacksmithingSlotToolAttachment *tool, SDK::wh::entitymodule::C_Actor *actor) {
            if (KCDC::Core::gApplication == nullptr || !KCDC::Core::gApplication->GetLocalEvents().SmithingToolAttaching(tool, actor)) {
                g_blacksmithingToolAttachOriginal(tool, actor);
                return;
            }
            const KCDC::Features::Player::PlayerInventoryService::AuthorityProjection lending(KCDC::Core::gApplication->GetPlayerInventoryService());
            g_blacksmithingToolAttachOriginal(tool, actor);
        }

        /** The same tool going back to the smithery, through `BeginItemTransfer`. */
        void __fastcall BlacksmithingToolDetach(SDK::wh::playermodule::C_BlacksmithingSlotToolAttachment *tool) {
            if (KCDC::Core::gApplication == nullptr || !KCDC::Core::gApplication->GetLocalEvents().SmithingToolDetaching(tool)) {
                g_blacksmithingToolDetachOriginal(tool);
                return;
            }
            {
                const KCDC::Features::Player::PlayerInventoryService::AuthorityProjection returning(KCDC::Core::gApplication->GetPlayerInventoryService());
                g_blacksmithingToolDetachOriginal(tool);
            }
            KCDC::Core::gApplication->GetLocalEvents().SmithingToolDetached(tool);
        }

        void __fastcall BlacksmithingConsume(SDK::wh::playermodule::C_Blacksmithing *minigame, bool success) {
            if (KCDC::Core::gApplication == nullptr || !KCDC::Core::gApplication->GetLocalEvents().SmithingSuppressNativeSettlement(minigame)) {
                g_blacksmithingConsumeOriginal(minigame, success);
            }
        }

        SDK::wh::framework::WUID *__fastcall BlacksmithingFinished(SDK::wh::rpgmodule::I_RPGMinigames *minigames, SDK::wh::framework::WUID *result, const void *outcome, bool *lockpicks) {
            if (KCDC::Core::gApplication == nullptr || !KCDC::Core::gApplication->GetLocalEvents().SmithingSuppressNativeOutcome()) {
                return g_blacksmithingFinishedOriginal(minigames, result, outcome, lockpicks);
            }
            // The server grants the product, the finishing XP and the
            // lockpicks. No item was created, which FinishWorkpiece's result
            // screen reads as nothing to show.
            *result    = SDK::wh::entitymodule::C_ItemAttachmentManager::GetInvalidItemId();
            *lockpicks = false;
            return result;
        }
    } // namespace
} // namespace KCDC::Features::Crafting

static InitFunction init(
    []() {
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_ScriptBind_Alchemy__StartAlchemy), reinterpret_cast<LPVOID>(&KCDC::Features::Crafting::AlchemyScriptStart), reinterpret_cast<LPVOID *>(&KCDC::Features::Crafting::g_alchemyScriptStartOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_Alchemy__SelectObject), reinterpret_cast<LPVOID>(&KCDC::Features::Crafting::AlchemySelect), reinterpret_cast<LPVOID *>(&KCDC::Features::Crafting::g_alchemySelectOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.AlchemyBackCallback), reinterpret_cast<LPVOID>(&KCDC::Features::Crafting::AlchemyBack), reinterpret_cast<LPVOID *>(&KCDC::Features::Crafting::g_alchemyBackOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_RPGMinigames__OnAlchemyStarted), reinterpret_cast<LPVOID>(&KCDC::Features::Crafting::AlchemyLifecycleStart), reinterpret_cast<LPVOID *>(&KCDC::Features::Crafting::g_alchemyLifecycleStartOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_RPGMinigames__OnAlchemyFinished), reinterpret_cast<LPVOID>(&KCDC::Features::Crafting::AlchemyLifecycleFinish), reinterpret_cast<LPVOID *>(&KCDC::Features::Crafting::g_alchemyLifecycleFinishOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_RPGMinigames__IsAlchemyRecipeRangeKnown), reinterpret_cast<LPVOID>(&KCDC::Features::Crafting::AlchemyRecipeRange), reinterpret_cast<LPVOID *>(&KCDC::Features::Crafting::g_alchemyRecipeRangeOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_RPGMinigames__IsAlchemyRecipeStepKnown), reinterpret_cast<LPVOID>(&KCDC::Features::Crafting::AlchemyRecipeStep), reinterpret_cast<LPVOID *>(&KCDC::Features::Crafting::g_alchemyRecipeStepOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_Alchemy__OnEntryActionFinished), reinterpret_cast<LPVOID>(&KCDC::Features::Crafting::AlchemyEntryReady), reinterpret_cast<LPVOID *>(&KCDC::Features::Crafting::g_alchemyEntryReadyOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_TakeIngredientAction__OnAnimationEvent), reinterpret_cast<LPVOID>(&KCDC::Features::Crafting::AlchemyTakeEvent), reinterpret_cast<LPVOID *>(&KCDC::Features::Crafting::g_alchemyTakeEventOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_UseResourceAction__OnAnimationEvent), reinterpret_cast<LPVOID>(&KCDC::Features::Crafting::AlchemyBaseEvent), reinterpret_cast<LPVOID *>(&KCDC::Features::Crafting::g_alchemyBaseEventOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_ReturnIngredientAction__ReturnHeldIngredient), reinterpret_cast<LPVOID>(&KCDC::Features::Crafting::AlchemyReturnIngredient), reinterpret_cast<LPVOID *>(&KCDC::Features::Crafting::g_alchemyReturnIngredientOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_Alchemy__Update), reinterpret_cast<LPVOID>(&KCDC::Features::Crafting::AlchemyUpdate), reinterpret_cast<LPVOID *>(&KCDC::Features::Crafting::g_alchemyUpdateOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_Alchemy__RequestCleanup), reinterpret_cast<LPVOID>(&KCDC::Features::Crafting::AlchemyRequestCleanup), reinterpret_cast<LPVOID *>(&KCDC::Features::Crafting::g_alchemyRequestCleanupOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_Alchemy__Shutdown), reinterpret_cast<LPVOID>(&KCDC::Features::Crafting::AlchemyShutdown), reinterpret_cast<LPVOID *>(&KCDC::Features::Crafting::g_alchemyShutdownOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_Alchemy__Destructor), reinterpret_cast<LPVOID>(&KCDC::Features::Crafting::AlchemyDestructor), reinterpret_cast<LPVOID *>(&KCDC::Features::Crafting::g_alchemyDestructorOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_AlchemyResource__Constructor), reinterpret_cast<LPVOID>(&KCDC::Features::Crafting::AlchemyResourceConstructor), reinterpret_cast<LPVOID *>(&KCDC::Features::Crafting::g_alchemyResourceConstructorOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_AlchemyResource__BaseConstructor), reinterpret_cast<LPVOID>(&KCDC::Features::Crafting::AlchemyBaseConstructor), reinterpret_cast<LPVOID *>(&KCDC::Features::Crafting::g_alchemyBaseConstructorOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_Alchemy__EvaluateContainer), reinterpret_cast<LPVOID>(&KCDC::Features::Crafting::AlchemyEvaluate), reinterpret_cast<LPVOID *>(&KCDC::Features::Crafting::g_alchemyEvaluateOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_Alchemy__Reset), reinterpret_cast<LPVOID>(&KCDC::Features::Crafting::AlchemyReset), reinterpret_cast<LPVOID *>(&KCDC::Features::Crafting::g_alchemyResetOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_Alchemy__Start), reinterpret_cast<LPVOID>(&KCDC::Features::Crafting::AlchemyStart), reinterpret_cast<LPVOID *>(&KCDC::Features::Crafting::g_alchemyStartOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_UseMortarAction__StartAction), reinterpret_cast<LPVOID>(&KCDC::Features::Crafting::AlchemyMortarStart), reinterpret_cast<LPVOID *>(&KCDC::Features::Crafting::g_alchemyMortarStartOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_UseBellowsAction__Pump), reinterpret_cast<LPVOID>(&KCDC::Features::Crafting::AlchemyBellowsPump), reinterpret_cast<LPVOID *>(&KCDC::Features::Crafting::g_alchemyBellowsPumpOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_UseBellowsAction__ReturnBellows), reinterpret_cast<LPVOID>(&KCDC::Features::Crafting::AlchemyBellowsReturn), reinterpret_cast<LPVOID *>(&KCDC::Features::Crafting::g_alchemyBellowsReturnOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_UseRetortAction__Pump), reinterpret_cast<LPVOID>(&KCDC::Features::Crafting::AlchemyRetortPump), reinterpret_cast<LPVOID *>(&KCDC::Features::Crafting::g_alchemyRetortPumpOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_UseRetortAction__FinishDistillation), reinterpret_cast<LPVOID>(&KCDC::Features::Crafting::AlchemyRetortFinish), reinterpret_cast<LPVOID *>(&KCDC::Features::Crafting::g_alchemyRetortFinishOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_UseRetortAction__GetRequiredBellowsCycles), reinterpret_cast<LPVOID>(&KCDC::Features::Crafting::AlchemyRetortCount), reinterpret_cast<LPVOID *>(&KCDC::Features::Crafting::g_alchemyRetortCountOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_Blacksmithing__AcceptSelectedRecipe), reinterpret_cast<LPVOID>(&KCDC::Features::Crafting::BlacksmithingAccept), reinterpret_cast<LPVOID *>(&KCDC::Features::Crafting::g_blacksmithingAcceptOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_Blacksmithing__Cancel), reinterpret_cast<LPVOID>(&KCDC::Features::Crafting::BlacksmithingCancel), reinterpret_cast<LPVOID *>(&KCDC::Features::Crafting::g_blacksmithingCancelOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_ActorActionMinigame__Exit), reinterpret_cast<LPVOID>(&KCDC::Features::Crafting::ActorActionMinigameExit), reinterpret_cast<LPVOID *>(&KCDC::Features::Crafting::g_actorActionMinigameExitOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_Blacksmithing__FinishWorkpiece), reinterpret_cast<LPVOID>(&KCDC::Features::Crafting::BlacksmithingFinish), reinterpret_cast<LPVOID *>(&KCDC::Features::Crafting::g_blacksmithingFinishOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_Blacksmithing__ConsumeRecipeMaterials), reinterpret_cast<LPVOID>(&KCDC::Features::Crafting::BlacksmithingConsume), reinterpret_cast<LPVOID *>(&KCDC::Features::Crafting::g_blacksmithingConsumeOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_BlacksmithingSlotToolAttachment__Attach), reinterpret_cast<LPVOID>(&KCDC::Features::Crafting::BlacksmithingToolAttach), reinterpret_cast<LPVOID *>(&KCDC::Features::Crafting::g_blacksmithingToolAttachOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_BlacksmithingSlotToolAttachment__Detach), reinterpret_cast<LPVOID>(&KCDC::Features::Crafting::BlacksmithingToolDetach), reinterpret_cast<LPVOID *>(&KCDC::Features::Crafting::g_blacksmithingToolDetachOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_RPGMinigames__FinishBlacksmithing), reinterpret_cast<LPVOID>(&KCDC::Features::Crafting::BlacksmithingFinished), reinterpret_cast<LPVOID *>(&KCDC::Features::Crafting::g_blacksmithingFinishedOriginal));
    },
    "Item production");
