#include <utils/safe_win32.h>

#include <MinHook.h>

#include <logging/logger.h>
#include <utils/hooking/hook_function.h>

#include "core/application.h"
#include "features/door/door_service.h"
#include "features/stash/stash_service.h"

#include "sdk/cryengine/action/i_game_object.h"
#include "sdk/cryengine/scriptsystem/i_function_handler.h"
#include "sdk/patterns.h"
#include "sdk/wh/entitymodule/c_actor.h"
#include "sdk/wh/entitymodule/c_anim_door.h"
#include "sdk/wh/entitymodule/c_lock_base.h"
#include "sdk/wh/entitymodule/c_script_bind_anim_door.h"
#include "sdk/wh/entitymodule/c_script_bind_lock_base.h"

#include <limits>
#include <stdexcept>

namespace KCDC::Features::Door {
    namespace {
        constexpr const char *kLogger = "Door";

        using GetObjectAnimName_t            = int(__fastcall *)(SDK::wh::entitymodule::C_ScriptBindAnimDoor *, SDK::IFunctionHandler *, SDK::EntityId, bool, bool, bool);
        using AnimDoorInit_t                 = bool(__fastcall *)(SDK::wh::entitymodule::C_AnimDoor *, SDK::IGameObject *);
        using ReportManipulationStart_t      = int(__fastcall *)(SDK::wh::entitymodule::C_ScriptBindAnimDoor *, SDK::IFunctionHandler *);
        using IsInteractive_t                = int(__fastcall *)(SDK::wh::entitymodule::C_ScriptBindAnimDoor *, SDK::IFunctionHandler *);
        using ReportLockState_t              = int(__fastcall *)(SDK::wh::entitymodule::C_ScriptBindLockBase *, SDK::IFunctionHandler *);
        using OnLockpicked_t                 = void(__fastcall *)(SDK::wh::entitymodule::C_LockBase *);
        using StartInteractiveActionByName_t = void(__fastcall *)(SDK::wh::entitymodule::C_Actor *, const char *, SDK::EntityId, bool, float);

        GetObjectAnimName_t g_getObjectAnimNameOriginal                       = nullptr;
        AnimDoorInit_t g_animDoorInitOriginal                                 = nullptr;
        ReportManipulationStart_t g_reportManipulationStartOriginal           = nullptr;
        IsInteractive_t g_isInteractiveOriginal                               = nullptr;
        ReportLockState_t g_reportLockedOriginal                              = nullptr;
        ReportLockState_t g_reportUnlockedOriginal                            = nullptr;
        OnLockpicked_t g_onLockpickedOriginal                                 = nullptr;
        StartInteractiveActionByName_t g_startInteractiveActionByNameOriginal = nullptr;

        /** Roots the Lua-authored door methods before the class is discarded. */
        bool __fastcall AnimDoorInit_Hook(SDK::wh::entitymodule::C_AnimDoor *door, SDK::IGameObject *gameObject) {
            const bool initialized = g_animDoorInitOriginal(door, gameObject);

            // The level builds its doors on the loading thread, which can
            // reach this before the application exists. The service used to
            // be a singleton that was valid whenever it was asked for.
            if (initialized && door != nullptr && KCDC::Core::gApplication != nullptr) {
                KCDC::Core::gApplication->GetDoorService().NotifyDoorInitialized(door);
            }
            return initialized;
        }

        /**
         * Every door transition chooses its clip here, immediately before
         * AnimDoor.lua starts it and, on a player's use, the actor action.
         *
         * During authoritative replay the module supplies the originating
         * actor and access side, so the game's own animation table picks the
         * clip rather than a copy of it.
         */
        int __fastcall GetObjectAnimName_Hook(SDK::wh::entitymodule::C_ScriptBindAnimDoor *scriptBind, SDK::IFunctionHandler *handler, SDK::EntityId userEntityId, bool isOpened, bool shouldBeOpened, bool isFrontSide) {
            const SDK::ScriptHandle doorHandle   = handler != nullptr ? handler->GetThis() : 0;
            const bool doorHandleCarriesEntityId = doorHandle != 0 && doorHandle <= std::numeric_limits<SDK::EntityId>::max();
            SDK::EntityId effectiveUserEntityId  = userEntityId;
            bool effectiveIsFrontSide            = isFrontSide;
            if (KCDC::Core::gApplication != nullptr && doorHandleCarriesEntityId) {
                KCDC::Core::gApplication->GetDoorService().PrepareObjectAnimation(static_cast<SDK::EntityId>(doorHandle), effectiveUserEntityId, shouldBeOpened, effectiveIsFrontSide);
            }

            const int result = g_getObjectAnimNameOriginal(scriptBind, handler, effectiveUserEntityId, isOpened, shouldBeOpened, effectiveIsFrontSide);
            if (KCDC::Core::gApplication != nullptr && doorHandleCarriesEntityId) {
                KCDC::Core::gApplication->GetDoorService().NotifyAccessSide(static_cast<SDK::EntityId>(doorHandle), isFrontSide);
            }
            return result;
        }

        /** The entity id Lua's `__this` carries into a script-bind call, or 0. */
        SDK::EntityId CallerEntityId(SDK::IFunctionHandler *handler) {
            const SDK::ScriptHandle handle = handler != nullptr ? handler->GetThis() : 0;
            return handle <= std::numeric_limits<SDK::EntityId>::max() ? static_cast<SDK::EntityId>(handle) : 0;
        }

        /**
         * AnimDoor.lua's DoPlayAnimation, the only code that changes a door's
         * direction, raises this for every transition -- a player's use, a
         * quest's Event_Open, the engine's Open and Close, Lock() closing the
         * door first.
         */
        int __fastcall ReportManipulationStart_Hook(SDK::wh::entitymodule::C_ScriptBindAnimDoor *scriptBind, SDK::IFunctionHandler *handler) {
            // Read `__this` while the Lua call frame is unquestionably live.
            const SDK::EntityId doorEntityId = CallerEntityId(handler);
            const int result                 = g_reportManipulationStartOriginal(scriptBind, handler);
            if (KCDC::Core::gApplication != nullptr && doorEntityId != 0) {
                KCDC::Core::gApplication->GetDoorService().NotifyNativeEdge(doorEntityId);
            }
            return result;
        }

        /**
         * AnimDoor.lua asks every interaction scan, before it offers a
         * door's prompts and before a use moves anything. The game's own
         * answer stands unless the server made the door unusable.
         */
        int __fastcall IsInteractive_Hook(SDK::wh::entitymodule::C_ScriptBindAnimDoor *scriptBind, SDK::IFunctionHandler *handler) {
            const SDK::EntityId doorEntityId = CallerEntityId(handler);
            if (KCDC::Core::gApplication != nullptr && doorEntityId != 0 && !KCDC::Core::gApplication->GetDoorService().IsUsable(doorEntityId)) {
                return handler->EndFunctionAny(SDK::ScriptAnyValue(false));
            }
            return g_isInteractiveOriginal(scriptBind, handler);
        }

        /** Every lock's Lock(), a door's among them, whoever calls it. */
        int __fastcall ReportLocked_Hook(SDK::wh::entitymodule::C_ScriptBindLockBase *scriptBind, SDK::IFunctionHandler *handler) {
            const SDK::EntityId lockEntityId = CallerEntityId(handler);
            const int result                 = g_reportLockedOriginal(scriptBind, handler);
            if (KCDC::Core::gApplication != nullptr && lockEntityId != 0) {
                KCDC::Core::gApplication->GetDoorService().NotifyNativeEdge(lockEntityId);
            }
            return result;
        }

        /** Every lock's Unlock(); each service ignores what is not one of its own. */
        int __fastcall ReportUnlocked_Hook(SDK::wh::entitymodule::C_ScriptBindLockBase *scriptBind, SDK::IFunctionHandler *handler) {
            const SDK::EntityId lockEntityId = CallerEntityId(handler);
            const int result                 = g_reportUnlockedOriginal(scriptBind, handler);
            if (KCDC::Core::gApplication != nullptr && lockEntityId != 0) {
                KCDC::Core::gApplication->GetDoorService().NotifyNativeEdge(lockEntityId);
                KCDC::Core::gApplication->GetStashService().NotifyUnlocked(lockEntityId);
            }
            return result;
        }

        /** Native lockpick success, after the lock has been driven unlocked. */
        void __fastcall OnLockpicked_Hook(SDK::wh::entitymodule::C_LockBase *lock) {
            // Native success already broadcast the +0x60 signal before this
            // call. Publish now, before entity:OnLockpicked() can synchronously
            // relock the door and raise the next ordered state edge.
            if (KCDC::Core::gApplication != nullptr && lock != nullptr) {
                KCDC::Core::gApplication->GetDoorService().NotifyLockpicked(lock->GetEntityId());
                KCDC::Core::gApplication->GetStashService().NotifyLockpicked(lock->GetEntityId());
            }
            g_onLockpickedOriginal(lock);
        }

        /** The human half of a player's own door use. */
        void __fastcall StartInteractiveActionByName_Hook(SDK::wh::entitymodule::C_Actor *actor, const char *actionName, SDK::EntityId objectEntityId, bool updateVisibility, float animationSpeed) {
            g_startInteractiveActionByNameOriginal(actor, actionName, objectEntityId, updateVisibility, animationSpeed);
            if (KCDC::Core::gApplication != nullptr && actor != nullptr) {
                KCDC::Core::gApplication->GetDoorService().NotifyActorAction(actor, actionName, objectEntityId);
            }
        }
    } // namespace
} // namespace KCDC::Features::Door

static InitFunction init(
    []() {
        const MH_STATUS doorInitStatus = MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_AnimDoor__Init), reinterpret_cast<LPVOID>(&KCDC::Features::Door::AnimDoorInit_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Door::g_animDoorInitOriginal));
        const MH_STATUS objectAnimStatus =
            MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_ScriptBindAnimDoor__GetObjectAnimName), reinterpret_cast<LPVOID>(&KCDC::Features::Door::GetObjectAnimName_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Door::g_getObjectAnimNameOriginal));
        const MH_STATUS lockpickedStatus  = MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_LockBase__OnLockpicked), reinterpret_cast<LPVOID>(&KCDC::Features::Door::OnLockpicked_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Door::g_onLockpickedOriginal));
        const MH_STATUS actorActionStatus = MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_Actor__StartInteractiveActionByName), reinterpret_cast<LPVOID>(&KCDC::Features::Door::StartInteractiveActionByName_Hook),
            reinterpret_cast<LPVOID *>(&KCDC::Features::Door::g_startInteractiveActionByNameOriginal));
        const MH_STATUS manipulationStatus =
            MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_ScriptBindAnimDoor__ReportManipulationStart), reinterpret_cast<LPVOID>(&KCDC::Features::Door::ReportManipulationStart_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Door::g_reportManipulationStartOriginal));
        const MH_STATUS interactiveStatus =
            MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_ScriptBindAnimDoor__IsInteractive), reinterpret_cast<LPVOID>(&KCDC::Features::Door::IsInteractive_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Door::g_isInteractiveOriginal));
        const MH_STATUS lockedStatus = MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_ScriptBindLockBase__ReportLocked), reinterpret_cast<LPVOID>(&KCDC::Features::Door::ReportLocked_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Door::g_reportLockedOriginal));
        const MH_STATUS unlockedStatus =
            MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_ScriptBindLockBase__ReportUnlocked), reinterpret_cast<LPVOID>(&KCDC::Features::Door::ReportUnlocked_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Door::g_reportUnlockedOriginal));
        if (doorInitStatus != MH_OK || objectAnimStatus != MH_OK || lockpickedStatus != MH_OK || actorActionStatus != MH_OK || manipulationStatus != MH_OK || interactiveStatus != MH_OK || lockedStatus != MH_OK || unlockedStatus != MH_OK) {
            throw std::runtime_error("Could not install native door synchronization hooks");
        }
        Framework::Logging::GetLogger(KCDC::Features::Door::kLogger)->info("Native door synchronization hooks installed");
    },
    "Doors");
