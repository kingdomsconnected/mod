#include <utils/safe_win32.h>

#include <MinHook.h>

#include <utils/hooking/hook_function.h>

#include "features/cart/cart_service.h"
#include "features/player/native_player_module.h"

#include "sdk/cryengine/entitysystem/i_entity.h"
#include "sdk/patterns.h"
#include "sdk/types.h"
#include "sdk/wh/entitymodule/c_action_actor.h"
#include "sdk/wh/entitymodule/c_actor.h"
#include "sdk/wh/entitymodule/c_actor_action_cart.h"
#include "sdk/wh/entitymodule/c_actor_state_expansion.h"
#include "sdk/wh/entitymodule/c_cart.h"
#include "sdk/wh/entitymodule/c_cart_player_control.h"
#include "sdk/wh/entitymodule/c_player.h"
#include "sdk/wh/entitymodule/e_cart_actor_state.h"
#include "sdk/wh/entitymodule/e_cart_position.h"
#include "sdk/wh/entitymodule/s_actor_action_cart_params.h"
#include "sdk/wh/entitymodule/s_cart_harness_horse.h"

namespace KCDC::Features::Cart {
    namespace {
        using CanOccupyPosition_t                       = bool(__fastcall *)(const SDK::wh::entitymodule::C_Cart *, SDK::wh::entitymodule::E_CartPosition::Type, SDK::wh::entitymodule::C_Actor *);
        CanOccupyPosition_t g_canOccupyPositionOriginal = nullptr;

        using SetCartActorState_t                       = void(__fastcall *)(SDK::wh::entitymodule::C_ActorActionCart *, SDK::wh::entitymodule::E_CartActorState::Type, bool);
        SetCartActorState_t g_setCartActorStateOriginal = nullptr;

        using CanPlayerMount_t                    = bool(__fastcall *)(SDK::wh::entitymodule::C_CartPlayerControl *, SDK::EntityId, bool);
        CanPlayerMount_t g_canPlayerMountOriginal = nullptr;

        using MountPlayer_t                 = void(__fastcall *)(SDK::wh::entitymodule::C_CartPlayerControl *, SDK::EntityId, bool);
        MountPlayer_t g_mountPlayerOriginal = nullptr;

        using OnSeated_t              = void(__fastcall *)(SDK::wh::entitymodule::C_CartPlayerControl *);
        OnSeated_t g_onSeatedOriginal = nullptr;

        using RequestCartAction_t                       = bool(__fastcall *)(SDK::wh::entitymodule::C_ActorStateExpansion *, const SDK::wh::entitymodule::S_ActorActionCartParams &);
        RequestCartAction_t g_requestCartActionOriginal = nullptr;

        /**
         * The local player's mount asks for the bench or the right rail only
         * (`MountPlayer`, 0x1828F8A38); one this module runs is given the
         * seat's own position instead, so the approach, the climbs and the
         * idle are that place's own.
         */
        bool __fastcall RequestCartAction_Hook(SDK::wh::entitymodule::C_ActorStateExpansion *expansion, const SDK::wh::entitymodule::S_ActorActionCartParams &params) {
            SDK::wh::entitymodule::S_ActorActionCartParams own = params;
            own.position                                       = CartService::Get().MountPosition(params.position);
            return g_requestCartActionOriginal(expansion, own);
        }

        using ApplyReinsModifier_t                        = void(__fastcall *)(SDK::wh::entitymodule::S_CartHarnessHorse *, SDK::EntityId);
        ApplyReinsModifier_t g_applyReinsModifierOriginal = nullptr;

        /**
         * Who may take a seat in one of this mod's carts.
         *
         * The game's rule exclusive-ors the seat against the actor being
         * player-controlled: the co-driver's bench and the right rail want a
         * player, every other seat anything but -- and a remote player is
         * player-controlled here too, by its channel. On a cart of ours the
         * server has already decided who sits where, players and NPCs alike;
         * what is left to ask is what is physical -- the seat enabled and
         * empty.
         */
        bool __fastcall CanOccupyPosition_Hook(const SDK::wh::entitymodule::C_Cart *cart, SDK::wh::entitymodule::E_CartPosition::Type position, SDK::wh::entitymodule::C_Actor *actor) {
            if (cart != nullptr && position < SDK::wh::entitymodule::E_CartPosition::Count && CartService::Get().IsModCart(cart)) {
                return cart->IsPositionEnabled(position) && cart->GetActorAtPosition(position) == 0;
            }
            return g_canOccupyPositionOriginal(cart, position, actor);
        }

        /**
         * The one place a body's stay in a cart moves: leaving `Mounting` is
         * the seat taken, `None` from the action's `Exit` the seat given back.
         * Recorded after the engine has acted on it, so the register never
         * runs ahead of the game.
         *
         * The native also sets the profile's save lock 19 while a
         * player-controlled body sits in a cart and clears it when it gets
         * out -- a remote player is player-controlled by its channel, and its
         * seat would lock and unlock this machine's saving. It runs from the
         * action's own update, outside any remote player's call, so the scope
         * is opened here.
         */
        void __fastcall SetCartActorState_Hook(SDK::wh::entitymodule::C_ActorActionCart *action, SDK::wh::entitymodule::E_CartActorState::Type state, bool notify) {
            const SDK::wh::entitymodule::E_CartActorState::Type before = action != nullptr ? action->GetState() : state;
            {
                SDK::wh::entitymodule::C_ActionActor *actionActor = action != nullptr ? action->GetActionActor() : nullptr;
                const KCDC::Features::Player::NativePlayerModule::RemoteScope scope(KCDC::Features::Player::NativePlayerModule::IsRemoteExecution() || (actionActor != nullptr && KCDC::Features::Player::NativePlayerModule::IsRemote(actionActor->GetActor())));
                g_setCartActorStateOriginal(action, state, notify);
            }
            if (action != nullptr && action->GetState() != before) {
                CartService::Get().NotifyCartState(action, action->GetState());
            }
        }

        /**
         * The cart control the prompt and the mount are asked of. The global
         * `Cart` script binding answers for one control, and every C_Player
         * builds its own binding (`C_ScriptBind_Cart`, 0x180A4C6EC, from the
         * control's constructor 0x180A4C688) and names it the global, so the
         * binding ends up holding whichever body was built last -- another
         * player's copy, here. Its prompt and its mount are the local
         * player's, and are asked of the local player's control.
         */
        SDK::wh::entitymodule::C_CartPlayerControl *LocalControl(SDK::wh::entitymodule::C_CartPlayerControl *control) {
            if (control == nullptr || !KCDC::Features::Player::NativePlayerModule::IsRemote(static_cast<const SDK::wh::entitymodule::C_Actor *>(control->GetPlayer()))) {
                return control;
            }
            SDK::wh::entitymodule::C_Player *player = SDK::wh::entitymodule::C_Player::GetInstance();
            return player != nullptr ? player->GetCartPlayerControl() : control;
        }

        /** The game's mount prompt: shown only while this answers, for the local player. */
        bool __fastcall CanPlayerMount_Hook(SDK::wh::entitymodule::C_CartPlayerControl *control, SDK::EntityId cartEntityId, bool frontSeat) {
            return g_canPlayerMountOriginal(LocalControl(control), cartEntityId, frontSeat);
        }

        /**
         * The game's own mount prompt, and this mod's own replay of it. A
         * player using a mount point on a cart of ours asks the server for a
         * seat instead of taking one; the seat the server writes back is what
         * the player is then climbed into, through this same function.
         */
        void __fastcall MountPlayer_Hook(SDK::wh::entitymodule::C_CartPlayerControl *control, SDK::EntityId cartEntityId, bool frontSeat) {
            if (CartService::Get().InterceptPromptMount(cartEntityId, frontSeat)) {
                return;
            }
            g_mountPlayerOriginal(LocalControl(control), cartEntityId, frontSeat);
        }

        /**
         * Every C_Player owns a cart control, and its actor model calls
         * `OnSeated` when the body sits down in a cart -- a remote copy's too.
         * What it does is all this machine's: it registers the global
         * `cart_dismount` handler, swaps the global `cart_action` map for
         * `cart`, and listens for the end of the mount animation to undo it
         * all again. A remote player seated in a cart anywhere on the map
         * would take over the local player's cart and dismount controls, so a
         * copy's seat is left to the cart action alone.
         */
        void __fastcall OnSeated_Hook(SDK::wh::entitymodule::C_CartPlayerControl *control) {
            if (control != nullptr && KCDC::Features::Player::NativePlayerModule::IsRemote(static_cast<const SDK::wh::entitymodule::C_Actor *>(control->GetPlayer()))) {
                return;
            }
            g_onSeatedOriginal(control);
        }

        /**
         * The reins, drawn from the driver's hands to the horse's head. The
         * native pushes a pose modifier onto the skeleton of the character in
         * the horse entity's slot 0, and dereferences that character without
         * asking. A horse spawned at runtime renders through the slot's
         * animation override and never has one there -- see
         * `ActorSpawner::LoadClassCharacter` -- so for such a horse the reins
         * are left undrawn rather than the process taken down. Everything else
         * the cart's movement does to the animal still runs.
         */
        void __fastcall ApplyReinsModifier_Hook(SDK::wh::entitymodule::S_CartHarnessHorse *harness, SDK::EntityId driverEntityId) {
            SDK::wh::entitymodule::C_Actor *horse = harness != nullptr ? harness->GetHorse() : nullptr;
            SDK::IEntity *entity                  = horse != nullptr ? horse->GetEntity() : nullptr;
            if (entity != nullptr && entity->GetCharacter(0) == nullptr) {
                return;
            }
            g_applyReinsModifierOriginal(harness, driverEntityId);
        }

    } // namespace
} // namespace KCDC::Features::Cart

static InitFunction init(
    []() {
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_Cart__CanOccupyPosition), reinterpret_cast<LPVOID>(&KCDC::Features::Cart::CanOccupyPosition_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Cart::g_canOccupyPositionOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.SetCartActorState), reinterpret_cast<LPVOID>(&KCDC::Features::Cart::SetCartActorState_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Cart::g_setCartActorStateOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_CartPlayerControl__CanPlayerMount), reinterpret_cast<LPVOID>(&KCDC::Features::Cart::CanPlayerMount_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Cart::g_canPlayerMountOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_CartPlayerControl__MountPlayer), reinterpret_cast<LPVOID>(&KCDC::Features::Cart::MountPlayer_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Cart::g_mountPlayerOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_CartPlayerControl__OnSeated), reinterpret_cast<LPVOID>(&KCDC::Features::Cart::OnSeated_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Cart::g_onSeatedOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_ActorStateExpansion__RequestCartAction), reinterpret_cast<LPVOID>(&KCDC::Features::Cart::RequestCartAction_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Cart::g_requestCartActionOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.ApplyCartReinsModifier), reinterpret_cast<LPVOID>(&KCDC::Features::Cart::ApplyReinsModifier_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Cart::g_applyReinsModifierOriginal));
    },
    "Cart");
