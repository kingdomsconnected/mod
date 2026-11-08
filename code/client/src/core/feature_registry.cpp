#include "feature_registry.h"
#include "features/items/custom_item_module.h"
#include "features/items/inventory_use_service.h"

#include "core/application.h"
#include "core/local_events.h"
#include "core/presentation_module.h"
#include "features/animation/animation_service.h"
#include "features/book/book_service.h"
#include "features/buff/custom_buff_module.h"
#include "features/camera/camera_control_service.h"
#include "features/cart/cart_service.h"
#include "features/character_creator/character_creator_service.h"
#include "features/cook_pot/cook_pot_service.h"
#include "features/crafting/custom_recipe_module.h"
#include "features/dialogue/dialogue_module.h"
#include "features/dice/dice_module.h"
#include "features/dog/dog_service.h"
#include "features/door/door_service.h"
#include "features/door/gate_service.h"
#include "features/follow/follow_service.h"
#include "features/gizmos/gizmos_service.h"
#include "features/ground_item/ground_item_service.h"
#include "features/horse/horse_service.h"
#include "features/map/map_service.h"
#include "features/marker/marker_service.h"
#include "features/npc/npc_inventory_module.h"
#include "features/npc/npc_locomotion_service.h"
#include "features/npc/npc_service.h"
#include "features/pickpocket/pickpocket_module.h"
#include "features/player/player_action_service.h"
#include "features/player/player_buff_service.h"
#include "features/player/player_duel_service.h"
#include "features/player/player_inventory_service.h"
#include "features/player/player_missile_service.h"
#include "features/player/player_progression_service.h"
#include "features/player/player_service.h"
#include "features/prop/prop_ghost.h"
#include "features/prop/prop_placer_service.h"
#include "features/prop/prop_service.h"
#include "features/siege/siege_ladder_module.h"
#include "features/siege/siege_module.h"
#include "features/siege/stone_pile_module.h"
#include "features/smart_object/smart_object_module.h"
#include "features/social/social_emote_service.h"
#include "features/social/social_emote_wheel.h"
#include "features/social/social_scoreboard.h"
#include "features/stash/stash_service.h"
#include "features/ui/native_ui_service.h"
#include "features/ui/ui_action_hints.h"
#include "features/ui/ui_game_menus.h"
#include "features/ui/ui_hud_elements.h"
#include "features/ui/ui_hud_screen_effects.h"
#include "features/vendor/vendor_module.h"
#include "features/vfx/vfx_service.h"
#include "features/world/weapon_hit_module.h"
#include "features/world/world_query_service.h"
#include "features/world/world_service.h"

namespace KCDC::Core {
    /**
     * Registration order is sweep order, and the sequence below is the one
     * SessionConnectedState::OnUpdate used to spell out call by call. The
     * comments that justified a position there are carried here unchanged,
     * because this is now the only file that decides it.
     *
     * NativeContextChanged needs no second list: filtering this order down to
     * the services that override it yields World, Door, Gate, Prop, PropGhost and PropPlacer, which is
     * what Application::SetCurrentLevelName used to call explicitly. Reset
     * sweeps this same order. Alchemy retires its native table before player
     * inventory clears the projection it may be holding.
     *
     * VoiceService is deliberately absent. It ticks outside a session too,
     * from Application::PostUpdate, and stays an explicit call there.
     */
    void BuildSessionFeatureRegistry(Application &application, std::vector<IFeatureService *> &services) {
        services.clear();
        services.reserve(24);

        // Install definitions before any inventory or ground projection.
        services.push_back(&KCDC::Features::Items::CustomItemModule::Get());
        services.push_back(&KCDC::Features::Buff::CustomBuffModule::Get());
        services.push_back(&KCDC::Features::Crafting::CustomRecipeModule::Get());

        // First: a flight decides where the local player's body is this
        // frame, and everything below reads that transform.
        services.push_back(&application.GetCameraControl());

        services.push_back(&application.GetWorldService());
        services.push_back(&KCDC::Features::Gizmos::GizmosService::Get());

        // A replicated drop must consume the remote body's held item before
        // Human applies the matching holster state. On the dropper, this also
        // settles the stack's physics before LocalEvents publishes the
        // held-to-ground edge later in this same frame.
        services.push_back(&application.GetGroundItemService());

        // Not a feature -- presentation is shell infrastructure, and section
        // 2.1 keeps it that way. It is registered only because its tick sits
        // inside this run, between the drops and the bodies: the sweep has to
        // reproduce that position rather than rearrange the order around it.
        services.push_back(&KCDC::Core::PresentationModule::Get());

        // Ahead of the players: it resolves the local controller the Queue
        // hook compares against before the owner replica publishes what that
        // hook caught.
        services.push_back(&KCDC::Features::Player::PlayerActionService::Get());
        services.push_back(&application.GetPlayerService());
        services.push_back(&application.GetDoorService());

        // With the doors: both bind level entities by GUID and drive them
        // from replicated state, and neither depends on the tick before it.
        services.push_back(&application.GetGateService());
        services.push_back(&KCDC::Features::CookPot::CookPotService::Get());
        services.push_back(&KCDC::Features::SmartObject::SmartObjectModule::Get());

        // After the bodies, like the gates it borrows its playback from: a
        // blast hurts the local player against the pose the human tick has
        // just settled.
        services.push_back(&KCDC::Features::Siege::SiegeModule::Get());
        services.push_back(&KCDC::Features::Siege::SiegeLadderModule::Get());
        services.push_back(&KCDC::Features::Siege::StonePileModule::Get());

        // Before the emote service: a release this tick chooses a gesture,
        // and the service's own tick is what queues it, so this way round
        // the gesture starts on the frame the key came up rather than the
        // one after it.
        services.push_back(&application.GetEmoteWheelService());
        services.push_back(&application.GetEmoteService());

        // Reads nothing any other service writes; anywhere in the run would do.
        services.push_back(&application.GetScoreboardService());

        // Reads nothing any other service writes either: it drives the pause
        // screen and its own figure, neither of which a body tick touches.
        services.push_back(&KCDC::Features::CharacterCreator::CharacterCreatorService::Get());
        services.push_back(&application.GetPlayerMissileService());

        // After the bodies and before anything reads a fight: the pairing is
        // decided against the poses the human tick has just settled, and every
        // combat hook below it tests a body against the table this refreshes.
        services.push_back(&application.GetPlayerDuelService());
        services.push_back(&application.GetPropService());
        services.push_back(&application.GetLevelEditService());
        services.push_back(&application.GetAreaCollisionService());

        // After the bodies: a container's lock is decided against the local
        // player's own replica, which the human tick is what publishes.
        services.push_back(&application.GetStashService());

        // Alchemy sets how far the inventory projection is held before the
        // inventory projects, and releases native callbacks before the
        // inventory reset sweep.
        services.push_back(&application.GetAlchemyService());
        services.push_back(&application.GetSmithingService());
        services.push_back(&application.GetPlayerInventoryService());
        services.push_back(&application.GetGatheringService());

        // After the bodies and the grants: the digest is read off the local
        // soul the human tick has just settled, and a server order applied
        // here lands on the same frame it arrived.
        services.push_back(&application.GetPlayerBuffService());
        services.push_back(&application.GetPlayerProgressionService());

        // Hand hooks only latch native results. Class resolution and replica
        // publication happen here, outside the engine detour, and only on the
        // frames where an edge actually changed something.
        services.push_back(&application.GetLocalEvents());
        services.push_back(&KCDC::Features::Items::InventoryUseService::Get());

        // After the bodies: a mount is reported from what the engine says is
        // in the saddle, which the horses' own frame is what refreshes.
        services.push_back(&application.GetHorseService());

        // After the horses and the bodies: a cart seats the bodies the human
        // tick has just placed, and its own horses are not the replicated ones.
        services.push_back(&KCDC::Features::Cart::CartService::Get());

        // After the bodies: a dog states its possession against its owner's
        // soul, which the human tick is what settles -- and a dog whose owner
        // spawned this same frame would otherwise be a stray for a frame.
        services.push_back(&application.GetDogService());

        // After the players and before the queries: an NPC's simulator decides
        // where its body is from the poses the human tick has just settled --
        // a follow reads its target's -- and a sphere query afterwards reports
        // the NPC where this frame put it.
        services.push_back(&KCDC::Features::Npc::NpcService::Get());
        services.push_back(&KCDC::Features::Npc::NpcInventoryModule::Get());
        services.push_back(&KCDC::Features::Follow::FollowService::Get());

        // After every body that can be made to play something: a request is
        // applied to the actor this frame's player and NPC ticks have just
        // settled, so a body spawned this frame plays it on this frame.
        services.push_back(&KCDC::Features::Animation::AnimationService::Get());

        // After every body, so a sphere reports the replicated ones where
        // this frame has just placed them.
        services.push_back(&application.GetWorldQueryService());
        services.push_back(&KCDC::Features::World::WeaponHitModule::Get());

        // After every body too: an effect the server attached to a player hangs
        // on the entity this frame's human tick has just settled.
        services.push_back(&application.GetVfxService());
        services.push_back(&application.GetLightService());

        // Reads nothing the others write: a screen effect is this machine's
        // picture, not anything in the world. Its sweep matters more than its
        // tick -- a session that ends takes every resource's grade with it.
        services.push_back(&KCDC::Features::UI::HudScreenEffects::Get());
        services.push_back(&KCDC::Features::UI::HudElements::Get());
        services.push_back(&KCDC::Features::UI::GameMenus::Get());

        // Reads nothing the others write. Its tick runs the handlers of the
        // hints pressed since the last one; its sweep takes every resource's
        // hints down with the session.
        services.push_back(&KCDC::Features::UI::ActionHints::Get());

        // Markers are decal nodes standing on the world rather than on any
        // body, so nothing above has to have settled before they are placed.
        services.push_back(&application.GetMarkerService());

        // Ghosts before the placer: the placer moves a ghost every tick, and
        // the ghost service is what notices a level change and drops the
        // nodes it would otherwise be moving.
        services.push_back(&application.GetPropGhostService());
        services.push_back(&application.GetPropPlacerService());

        // Quests are journal state rather than anything in the world, so this
        // sits beside the markers only because both are content the server
        // authored and neither depends on the other.
        services.push_back(&application.GetQuestService());

        // Reads nothing the others write. A finished book's carrier is
        // deleted here, outside the native Finish that reported it, and a
        // book still on its way to the hands is confirmed or given up on.
        services.push_back(&KCDC::Features::Book::BookService::Get());

        // Last, and only for the sweep it is actually in: the locomotion table
        // has no tick of its own -- the bodies above publish into it from
        // theirs -- but every entry in it belongs to a replicated body, so it
        // has to be emptied when they go.
        services.push_back(&application.GetNpcLocomotionService());

        // Also here for the sweep alone: the map service has no tick, but the
        // compass suppression it may be holding is not reference-counted, so
        // a leftover would take the compass away for the rest of the process.
        // Last, which is where the explicit call it replaces already ran.
        services.push_back(&application.GetMapService());

        // Here for the sweep alone as well: native screens are ticked from
        // the shell, because the debug harness opens them outside a session
        // too. What the sweep is for is the other half -- a screen belongs to
        // a resource, and every resource goes when the session does.
        services.push_back(&application.GetNativeUIService());

        // Last of the UI participants: a page is drawn against the HUD the
        // frame it arrives, and nothing above it reads the conversation.
        services.push_back(&KCDC::Features::Dialogue::DialogueModule::Get());

        // After the conversation list: a gamemode opens a vendor from a
        // dialogue option, and the screen it puts up replaces that list.
        services.push_back(&KCDC::Features::Vendor::VendorModule::Get());

        // The local player's pickpocketing attempt. Its order carries nothing:
        // a victim body that goes ends the attempt from Human::Despawn.
        services.push_back(&KCDC::Features::Pickpocket::PickpocketModule::Get());

        // A dice match against another player. Its order carries nothing
        // either: the opponent's body going closes the table from
        // Human::Despawn.
        services.push_back(&KCDC::Features::Dice::DiceModule::Get());
    }
} // namespace KCDC::Core
