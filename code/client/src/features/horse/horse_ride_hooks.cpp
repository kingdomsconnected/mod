#include <utils/safe_win32.h>

#include <MinHook.h>

#include <utils/hooking/hook_function.h>

#include "core/application.h"

#include "sdk/cryengine/action/i_actor_system.h"
#include "sdk/cryengine/entitysystem/i_entity.h"
#include "sdk/patterns.h"
#include "sdk/wh/entitymodule/c_action_actor.h"
#include "sdk/wh/entitymodule/c_actor.h"
#include "sdk/wh/entitymodule/c_actor_action_rider.h"
#include "sdk/wh/entitymodule/c_horse.h"
#include "sdk/wh/entitymodule/c_horse_ride_context.h"
#include "sdk/wh/entitymodule/i_horse_rider_sync.h"

#include <cstdint>

namespace KCDC::Features::Horse {
    namespace {
        using SetState_t = void(__fastcall *)(SDK::wh::entitymodule::C_HorseRideContext *, std::uint8_t, bool);
        SetState_t g_setStateOriginal = nullptr;

        /**
         * Reports a saddle transition the moment the engine decides on it.
         *
         * The horse's rider-sync property is installed by the rider action's
         * entry and cleared by its exit, so watching it reports a get-off only
         * once the get-off has finished playing -- and everyone else starts
         * theirs an animation late. The state machine moves first, and mount,
         * get-off and tear-down all pass through here.
         *
         * Read after the original, because the machine validates what it was
         * handed and forces itself back when the transition cannot be queued.
         * Only what it settled on is worth sending.
         */
        void __fastcall SetState_Hook(SDK::wh::entitymodule::C_HorseRideContext *context, std::uint8_t state, bool force) {
            g_setStateOriginal(context, state, force);
            if (context == nullptr) {
                return;
            }

            auto *action = context->GetAction();
            auto *queue  = action != nullptr ? action->GetActionActor() : nullptr;
            auto *rider  = queue != nullptr ? queue->GetActor() : nullptr;
            if (rider == nullptr || KCDC::Core::gApplication == nullptr) {
                return;
            }

            KCDC::Core::gApplication->GetHorseService().NotifyRideState(rider->GetEntityId(), action->GetHorseEntityId(), context->GetState(), context->GetDismountSide());
        }

        using SetRiderSync_t                  = void(__fastcall *)(SDK::wh::entitymodule::C_Horse *, SDK::wh::entitymodule::I_HorseRiderSync *);
        SetRiderSync_t g_setRiderSyncOriginal = nullptr;

        /**
         * Somebody took this animal's saddle, or gave it up.
         *
         * The whole truth about who is on a horse: the engine's own mount
         * check reads this very property to refuse an occupied animal. The
         * state machine beside it answers a different question -- what the
         * ride is doing, across eleven states -- and is no use for this one.
         *
         * Read after the original, so every reader agrees within the call.
         */
        void __fastcall SetRiderSync_Hook(SDK::wh::entitymodule::C_Horse *horse, SDK::wh::entitymodule::I_HorseRiderSync *riderSync) {
            g_setRiderSyncOriginal(horse, riderSync);
            if (horse == nullptr || KCDC::Core::gApplication == nullptr) {
                return;
            }

            auto *rider = riderSync != nullptr ? riderSync->GetRiderRaw() : nullptr;
            KCDC::Core::gApplication->GetHorseService().NotifySeat(horse->GetEntityId(), rider != nullptr ? rider->GetEntityId() : 0);
        }

        using Enter_t           = bool(__fastcall *)(SDK::wh::entitymodule::C_ActorActionRider *, void *);
        Enter_t g_enterOriginal = nullptr;

        using RemoveLastChildAction_t                           = void(__fastcall *)(SDK::wh::entitymodule::C_ActorActionRider *);
        RemoveLastChildAction_t g_removeLastChildActionOriginal = nullptr;

        /**
         * The rollback of a refused entry.
         *
         * `C_ActionActor::TryEnterAction` calls this on every action whose
         * `Enter` answered false, and the rider's version assumes a child was
         * installed: it reads the last element of the child range without
         * asking whether there is one. The rider's own `Enter` refuses a horse
         * that is gone or soulless before installing any, and `Enter_Hook`
         * below refuses bodies that are not ready the same way -- either one
         * would read `end[-1]` of an empty range. A range with a child is
         * rolled back as the game wrote it.
         */
        void __fastcall RemoveLastChildAction_Hook(SDK::wh::entitymodule::C_ActorActionRider *action) {
            if (action->HasChildActions()) {
                g_removeLastChildActionOriginal(action);
            }
        }

        /**
         * A queued mount becoming a seat, or being refused.
         *
         * The entry already refuses a horse that is gone or has no soul, and
         * the queue then drops the action. It does not refuse a horse with no
         * character in its entity's slot 0, and `C_RiderSync::AttachToHorse`
         * hands that character a callback without asking. A horse that is put
         * away has none: putting an actor away drops its render proxy, and the
         * slots with it, until the wake re-runs the model setup. Every horse
         * this client spawns starts put away and wakes a frame or two later,
         * so a rider the wire already names for it is queued into exactly that
         * gap. Mounting also reads both action controllers without checking
         * them (the rider's at WHGame+0xAA0C4E). An actor model or character
         * alone does not establish that readiness. Check at entry, since a
         * queued action may start later than the frame that requested it.
         */
        bool __fastcall Enter_Hook(SDK::wh::entitymodule::C_ActorActionRider *action, void *previousActionHandle) {
            auto *horse        = static_cast<SDK::wh::entitymodule::C_Actor *>(SDK::IActorSystem::GetInstance()->GetActor(action->GetHorseEntityId()));
            auto *queue        = action->GetActionActor();
            auto *rider        = queue != nullptr ? queue->GetActor() : nullptr;
            const bool ready   = HorseService::IsBodyReadyToRide(rider) && HorseService::IsBodyReadyToRide(horse);
            const bool entered = ready && g_enterOriginal(action, previousActionHandle);
            if (entered || KCDC::Core::gApplication == nullptr) {
                return entered;
            }

            if (rider != nullptr) {
                KCDC::Core::gApplication->GetHorseService().NotifyMountRefused(rider->GetEntityId(), action->GetHorseEntityId(), !ready);
            }
            return false;
        }

        using SetPutAway_t                = std::uint64_t(__fastcall *)(SDK::wh::entitymodule::C_Actor *, bool);
        SetPutAway_t g_setPutAwayOriginal = nullptr;

        /**
         * The raw put-away transition, which every caller reaches: the
         * engine's manager, an immediate request, and `C_Actor::Init`.
         *
         * Put away, a body loses its character; see
         * `HorseService::HoldPutAway` for why a ridden horse must not. Woken,
         * it has one again, which is what a refused mount waits for.
         */
        std::uint64_t __fastcall SetPutAway_Hook(SDK::wh::entitymodule::C_Actor *actor, bool putAway) {
            if (KCDC::Core::gApplication == nullptr) {
                return g_setPutAwayOriginal(actor, putAway);
            }

            auto &horses = KCDC::Core::gApplication->GetHorseService();
            if (putAway && horses.HoldPutAway(actor)) {
                return 0;
            }

            const std::uint64_t result = g_setPutAwayOriginal(actor, putAway);
            if (!putAway) {
                horses.NotifyActorWoken(actor->GetEntityId());
            }
            return result;
        }
    } // namespace
} // namespace KCDC::Features::Horse

static InitFunction init(
    []() {
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_HorseRideContext__SetState), reinterpret_cast<LPVOID>(&KCDC::Features::Horse::SetState_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Horse::g_setStateOriginal));

        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_Horse__SetRiderSync), reinterpret_cast<LPVOID>(&KCDC::Features::Horse::SetRiderSync_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Horse::g_setRiderSyncOriginal));

        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_ActorActionRider__Enter), reinterpret_cast<LPVOID>(&KCDC::Features::Horse::Enter_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Horse::g_enterOriginal));

        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_ActorActionRider__RemoveLastChildAction), reinterpret_cast<LPVOID>(&KCDC::Features::Horse::RemoveLastChildAction_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Horse::g_removeLastChildActionOriginal));

        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_Actor__SetPutAway), reinterpret_cast<LPVOID>(&KCDC::Features::Horse::SetPutAway_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Horse::g_setPutAwayOriginal));
    },
    "HorseRide");
