#pragma once

#include <cstddef>
#include <cstdint>

#include "../3dengine/c_3d_engine.h"
#include "../../wh/entitymodule/c_actor.h"
#include "../../wh/entitymodule/c_actor_movement_controller.h"
#include "c_actor_system.h"
#include "../../wh/entitymodule/c_actor_equipment_handler.h"
#include "../../wh/entitymodule/c_actor_material_effects.h"
#include "c_animated_character.h"
#include "../../wh/entitymodule/c_animal.h"
#include "../../wh/xgenaimodule/c_animal_spawner.h"
#include "../animation/c_animation_set.h"
#include "../entitysystem/c_area_manager.h"
#include "../physics/c_articulated_entity.h"
#include "../animation/c_attachment_bone.h"
#include "../animation/c_attachment_face.h"
#include "../animation/c_attachment_manager.h"
#include "../animation/c_attachment_prow.h"
#include "../animation/c_attachment_skin.h"
#include "../animation/c_attachment_skin_base.h"
#include "../animation/c_attachment_vcloth.h"
#include "../entitysystem/c_breakable_manager.h"
#include "../entitysystem/c_bsp_tree_3d.h"
#include "../3dengine/c_brush.h"
#include "../../wh/game/c_camera.h"
#include "../system/c_camera_native.h"
#include "../../wh/game/c_camera_bone_animated.h"
#include "../../wh/game/c_camera_combat_delegate.h"
#include "../../wh/game/c_camera_dialog.h"
#include "../../wh/game/c_camera_first_person.h"
#include "../../wh/game/c_camera_manager.h"
#include "../../wh/game/c_camera_minigame.h"
#include "../../wh/game/c_camera_rider.h"
#include "../../wh/game/c_camera_third_person.h"
#include "../animation/c_cgf_attachment.h"
#include "../animation/c_char_instance.h"
#include "../../wh/entitymodule/c_chat_follow_manager.h"
#include "../renderer/c_clear_surface_pass.h"
#include "../../wh/entitymodule/c_clothing_attachment_manager.h"
#include "../../wh/entitymodule/c_clothing_attachment_owner_actor.h"
#include "../../wh/entitymodule/c_clothing_attachment_owner_anim_char.h"
#include "../../wh/entitymodule/c_clothing_system.h"
#include "../../wh/combatmodule/c_combat_actor_collisions.h"
#include "../../wh/combatmodule/c_combat_actor_object.h"
#include "../../wh/combatmodule/c_combat_actor_updated_object.h"
#include "../../wh/combatmodule/c_combat_actor_virtual_weapon.h"
#include "../../wh/combatmodule/c_combat_actor_weapons.h"
#include "../../wh/combatmodule/c_combat_automation_action.h"
#include "../../wh/combatmodule/c_combat_automation_weapons.h"
#include "../../wh/combatmodule/c_combat_camera_manager.h"
#include "../../wh/combatmodule/c_combat_environment_manager.h"
#include "../../wh/combatmodule/c_combat_player_input.h"
#include "../../wh/combatmodule/c_combat_player_controller.h"
#include "../entitysystem/c_component_event_distributer.h"
#include "../renderer/c_compute_render_pass.h"
#include "../renderer/c_primitive_render_pass.h"
#include "../renderer/c_cry_dx12_gi_adapter.h"
#include "../renderer/c_cry_dx12_gi_factory.h"
#include "../renderer/c_cry_dx12_gi_output.h"
#include "../renderer/c_cry_dx12_swap_chain.h"
#include "../3dengine/c_decal_render_node.h"
#include "../3dengine/c_distance_cloud_render_node.h"
#include "../animation/c_default_skeleton.h"
#include "../renderer/c_device_buffer.h"
#include "../renderer/c_device_compute_pso.h"
#include "../renderer/c_device_graphics_pso.h"
#include "../../ncrydx12/c_device_object_dx12.h"
#include "../renderer/c_device_pso_desc.h"
#include "../renderer/c_device_resource.h"
#include "../renderer/c_device_resource_layout.h"
#include "../renderer/c_device_render_pass.h"
#include "../renderer/c_device_render_pass_desc.h"
#include "../renderer/c_device_resource_set.h"
#include "../renderer/c_device_texture.h"
#include "../../wh/entitymodule/c_dog.h"
#include "../animation/c_effect_attachment.h"
#include "../entitysystem/c_entity.h"
#include "../animation/c_entity_attachment.h"
#include "../entitysystem/c_entity_archetype.h"
#include "../entitysystem/c_entity_class.h"
#include "../entitysystem/c_entity_class_registry.h"
#include "../../wh/entitymodule/c_entity_module.h"
#include "../entitysystem/c_entity_iterator.h"
#include "../entitysystem/c_entity_layer.h"
#include "../entitysystem/c_entity_load_manager.h"
#include "../entitysystem/c_entity_pool_manager.h"
#include "../../wh/environmentmodule/c_environment_module.h"
#include "../entitysystem/c_entity_system.h"
#include "../../wh/c_entity_streaming_manager.h"
#include "../../wh/entitymodule/c_equipment_manager.h"
#include "../../wh/entitymodule/c_equipment_slot.h"
#include "../3dengine/c_fog_volume_render_node.h"
#include "../../wh/entitymodule/c_front_collision.h"
#include "../../wh/entitymodule/e_combat_state.h"
#include "../../wh/entitymodule/e_outfitting_mode.h"
#include "../../wh/xgenaimodule/behaviortree/e_trespass_level.h"
#include "../../wh/environmentmodule/e_weather_profile_delayed_action.h"
#include "c_game_object.h"
#include "c_game_object_system.h"
#include "../physics/c_geom_manager.h"
#include "../../wh/entitymodule/c_horse.h"
#include "../../wh/entitymodule/c_horse_usage_manager.h"
#include "../../wh/entitymodule/c_human.h"
#include "../../wh/entitymodule/c_human_hand_helper.h"
#include "../../wh/entitymodule/c_homogenous_item_descriptor.h"
#include "../../wh/entitymodule/c_inventory.h"
#include "../../wh/entitymodule/c_inventory_base.h"
#include "../../wh/entitymodule/c_inventory_manager.h"
#include "../../wh/entitymodule/c_item_collection.h"
#include "../../wh/rpgmodule/c_inventory_soul.h"
#include "../../wh/entitymodule/c_world_inventory.h"
#include "../../wh/entitymodule/c_item.h"
#include "../../wh/entitymodule/c_item_class_descriptor.h"
#include "../../wh/entitymodule/c_item_delivery.h"
#include "../../wh/entitymodule/c_item_holder.h"
#include "../../wh/entitymodule/c_item_indexer.h"
#include "../../wh/entitymodule/c_item_property_descriptor.h"
#include "../../wh/entitymodule/c_item_pick_up_helper.h"
#include "../../wh/entitymodule/c_item_slot.h"
#include "../../wh/entitymodule/c_item_slot_book.h"
#include "../../wh/entitymodule/c_item_slot_dynamic_spawner.h"
#include "../../wh/entitymodule/c_item_slot_manager.h"
#include "../../wh/entitymodule/c_item_slot_multi_class.h"
#include "../../wh/entitymodule/c_item_slot_pile.h"
#include "../animation/c_light_attachment.h"
#include "../../wh/entitymodule/c_light_common_editor_properties.h"
#include "../../wh/xgenaimodule/c_linkable_object_extension.h"
#include "../../wh/xgenaimodule/c_linkable_object_holder.h"
#include "../physics/c_living_entity.h"
#include "../../wh/shared/c_listeners.h"
#include "../../wh/entitymodule/c_missile_weapon_item.h"
#include "../../wh/entitymodule/c_missile_weapon_player_controller.h"
#include "../3dengine/c_movable_brush.h"
#include "../../wh/entitymodule/c_npc_actor.h"
#include "../3dengine/c_ocean.h"
#include "../../wh/playermodule/c_outfit_manager.h"
#include "../../wh/framework/c_override_conflict_manager.h"
#include "../../wh/databasemodule/c_object_database.h"
#include "../../wh/databasemodule/c_object_database_base.h"
#include "../../wh/databasemodule/c_object_tree_database.h"
#include "../3dengine/c_owned_brush.h"
#include "../physics/c_particle_entity.h"
#include "../../wh/entitymodule/c_pickable_item.h"
#include "../../wh/entitymodule/c_player.h"
#include "../../wh/entitymodule/c_player_carry_corpse.h"
#include "../../wh/entitymodule/c_player_input.h"
#include "../../wh/playermodule/c_player_inventory_soul.h"
#include "../../wh/entitymodule/c_player_material_effects.h"
#include "../../wh/entitymodule/c_player_tense_circumstance.h"
#include "../../wh/entitymodule/c_player_unconscious.h"
#include "../../wh/entitymodule/c_player_weapon.h"
#include "../physics/c_physical_entity.h"
#include "../physics/c_physical_placeholder.h"
#include "../physics/c_physical_world.h"
#include "../../wh/playermodule/c_qam_manager.h"
#include "../../wh/rpgmodule/c_poi.h"
#include "../../wh/rpgmodule/c_rpg_location.h"
#include "../../wh/environmentmodule/c_rain.h"
#include "../../wh/environmentmodule/c_respawn_manager.h"
#include "../renderer/c_render_display_context.h"
#include "../renderer/c_render_pass_base.h"
#include "../renderer/c_resource_transfer_pass.h"
#include "../physics/c_rigid_entity.h"
#include "../../wh/entitymodule/c_rigid_only_collision.h"
#include "../3dengine/c_road_render_node.h"
#include "../../wh/entitymodule/c_rider_player_input.h"
#include "../../wh/entitymodule/c_rider_sync.h"
#include "../physics/c_rope_entity.h"
#include "../../wh/entitymodule/c_save_extension_manager.h"
#include "../renderer/c_scene_render_pass.h"
#include "../animation/c_skeleton_anim.h"
#include "../physics/c_soft_entity.h"
#include "../renderer/s_resource_binding.h"
#include "../../wh/xgenaimodule/c_spawn_item_event_implementation.h"
#include "../../wh/xgenaimodule/c_spawned_items_holder.h"
#include "../renderer/c_swap_chain_backed_render_display_context.h"
#include "../../wh/c_streamable_entity_class.h"
#include "../3dengine/c_stat_obj_async_loader.h"
#include "../3dengine/c_terrain.h"
#include "../3dengine/c_time_of_day.h"
#include "../renderer/c_texture_backed_render_display_context.h"
#include "../../wh/playermodule/c_tutorial_manager.h"
#include "../../wh/playermodule/s_tutorial.h"
#include "../../wh/environmentmodule/s_weather_profile_data.h"
#include "../../wh/playermodule/s_overlay_tutorial.h"
#include "../../wh/playermodule/s_popup_tutorial.h"
#include "../../wh/guimodule/c_ui_base.h"
#include "../../wh/guimodule/c_ui_flash_base.h"
#include "../../wh/guimodule/c_ui_action_hint_manager.h"
#include "../../wh/guimodule/c_ui_hud_messages.h"
#include "../../wh/guimodule/c_ui_hud_hints.h"
#include "../../wh/guimodule/c_ui_modal_dialog_info.h"
#include "../../wh/guimodule/c_ui_modal_dialog_question.h"
#include "c_view.h"
#include "c_view_system.h"
#include "../3dengine/c_vegetation.h"
#include "../3dengine/c_vis_area_manager.h"
#include "../3dengine/c_water_wave_render_node.h"
#include "../3dengine/c_water_volume_render_node.h"
#include "../../wh/environmentmodule/c_weather_profile_database.h"
#include "../../wh/environmentmodule/c_weather_system.h"
#include "../../wh/combatmodule/c_weapon_proc_solver.h"
#include "../../wh/combatmodule/c_weapon_solver.h"
#include "../physics/c_wheeled_vehicle_entity.h"
#include "../animation/character_manager.h"
#include "../3dengine/cry_3d_engine_base.h"
#include "../3dengine/i_3d_engine.h"
#include "i_action_map_event_listener.h"
#include "../../wh/xgenaimodule/i_anim_event_implementation.h"
#include "../animation/i_animation_set.h"
#include "i_animation_graph_state.h"
#include "i_animation_graph_state_listener.h"
#include "i_animated_character.h"
#include "../animation/i_attachment.h"
#include "../animation/i_attachment_manager.h"
#include "../animation/i_attachment_object.h"
#include "../animation/i_attachment_skin.h"
#include "i_actor_system.h"
#include "../entitysystem/i_area_manager.h"
#include "../entitysystem/i_bsp_tree_3d.h"
#include "../3dengine/i_brush.h"
#include "../entitysystem/i_breakable_manager.h"
#include "../../wh/game/i_camera_activation.h"
#include "../../wh/entitymodule/i_chat_follow_manager.h"
#include "../animation/i_character_instance.h"
#include "../animation/i_character_manager.h"
#include "../../wh/entitymodule/i_clothing_attachment_manager.h"
#include "../../wh/entitymodule/i_clothing_attachment_owner.h"
#include "../../wh/entitymodule/i_clothing_system.h"
#include "../../wh/combatmodule/i_combat_actor_weapons.h"
#include "../../wh/combatmodule/i_combat_camera_manager.h"
#include "../../wh/combatmodule/i_combat_environment_manager.h"
#include "../../wh/combatmodule/i_combat_player_controller.h"
#include "../entitysystem/i_component_event_distributer.h"
#include "../3dengine/i_decal_render_node.h"
#include "../3dengine/i_distance_cloud_render_node.h"
#include "../animation/i_default_skeleton.h"
#include "../entitysystem/i_entity_archetype.h"
#include "../entitysystem/i_entity_pool_manager.h"
#include "../entitysystem/i_entity_pool_listener.h"
#include "../../wh/game/i_entity_side_effect_callback.h"
#include "../entitysystem/i_entity_system_sink.h"
#include "../../wh/framework/i_event_log_listener.h"
#include "../../wh/playermodule/i_fog_of_war.h"
#include "../3dengine/i_fog_volume_render_node.h"
#include "../../wh/entitymodule/i_equipment_manager_listener.h"
#include "i_game_object_system.h"
#include "i_game_object_extension_creator.h"
#include "i_game_framework_listener.h"
#include "../../wh/entitymodule/i_game_object_light.h"
#include "i_game_rules_system.h"
#include "../../wh/entitymodule/i_human_hand_helper.h"
#include "../physics/i_geom_manager.h"
#include "../../wh/entitymodule/i_horse_move_adapter.h"
#include "../../wh/entitymodule/i_horse_rider_sync.h"
#include "i_inventory.h"
#include "../../wh/framework/i_bindable_object.h"
#include "../../wh/entitymodule/i_item_descriptor.h"
#include "../../wh/entitymodule/i_item_slot_listener.h"
#include "../../wh/entitymodule/i_item_collection_listener.h"
#include "../../wh/entitymodule/i_item_attachment_listener.h"
#include "../../wh/entitymodule/i_inventory_listener.h"
#include "i_item.h"
#include "i_item_system.h"
#include "i_level_system.h"
#include "../../wh/rpgmodule/i_location_listener.h"
#include "../3dengine/i_merge_mesh_stream_listener.h"
#include "i_movement_controller.h"
#include "../../wh/entitymodule/i_movement_input_listener.h"
#include "../../wh/framework/i_module_message_listener.h"
#include "../movie/i_movie_user.h"
#include "../../wh/databasemodule/i_object_database.h"
#include "../../wh/playermodule/i_outfit_manager.h"
#include "../physics/i_phys_utils.h"
#include "../physics/i_physical_entity.h"
#include "../physics/i_physical_entity_it.h"
#include "../system/i_process.h"
#include "../physics/i_physical_world.h"
#include "../../wh/playermodule/i_qam_manager.h"
#include "../3dengine/i_render_node.h"
#include "../3dengine/i_road_render_node.h"
#include "../../wh/environmentmodule/i_respawn_manager.h"
#include "../../wh/animationmodule/i_scene_listener.h"
#include "../3dengine/i_shadow_caster.h"
#include "../animation/i_skeleton_anim.h"
#include "../../wh/framework/i_source_monitor_listener.h"
#include "../3dengine/i_stat_inst_group.h"
#include "../3dengine/i_stat_obj_loaded_callback.h"
#include "../system/i_stream_callback.h"
#include "../system/i_system_event_listener.h"
#include "../3dengine/i_terrain.h"
#include "../3dengine/i_time_of_day.h"
#include "../3dengine/i_time_of_day_update_callback.h"
#include "../scaleform/i_ui_element_event_listener.h"
#include "../../wh/playermodule/i_ui_action_hint_manager.h"
#include "../../wh/framework/i_ui_hud_messages.h"
#include "../../wh/framework/i_ui_hud_events.h"
#include "../../wh/questmodule/i_ui_hud_events_quest.h"
#include "../../wh/playermodule/i_ui_hud_hints.h"
#include "../../wh/guimodule/i_ui_drag_and_drop_viewer.h"
#include "../3dengine/i_vegetation.h"
#include "i_view.h"
#include "i_view_system.h"
#include "i_view_system_listener.h"
#include "../3dengine/i_vis_area_manager.h"
#include "../3dengine/i_water_volume_render_node.h"
#include "../3dengine/i_water_wave_render_node.h"
#include "../../wh/environmentmodule/i_weather_system.h"
#include "../../wh/framework/i_wuid_mapping_provider.h"
#include "../../wh/guimodule/c_ui_hud_events.h"
#include "../../wh/guimodule/c_ui_menu_confirmation.h"
#include "../../wh/guimodule/c_ui_modal_dialog_amount.h"
#include "../../wh/guimodule/c_ui_modal_dialog_item.h"
#include "../../wh/guimodule/c_ui_modal_dialog_random_event.h"
#include "../../wh/framework/c_localized_string.h"
#include "../animation/s_attachment_base.h"
#include "i_mannequin.h"
#include "i_player_profile_manager.h"
#include "s_camera_params.h"
#include "s_view_params.h"

namespace SDK {
    class IActionMapManager;
    class ILanQueryListener;
    class IBreakReplicator;
    class INetChannel;
    class ISystem;
    class IUIDraw;
    class IGameToEditorInterface;
    struct SGameContextParams;
    struct SGameStartParams;
    struct SSystemInitParams;

    /**
     * IGameFramework, vtable 0x184046E70; CCryAction's implementation is at
     * 0x1840472D0. Slots through the client-actor accessors are retained.
     */
    class IGameFramework {
      public:
        template <typename TActor>
        class CActorCreator;

        // Factory overloads emitted before the destructor by CryAction macros.
        virtual void unk_factory0000() = 0;
        virtual void unk_factory0001() = 0;
        virtual void unk_factory0002() = 0;
        virtual void unk_factory0003() = 0;
        virtual void unk_factory0004() = 0;
        virtual ~IGameFramework() = default; // +0x028

        virtual bool Init(SSystemInitParams &startupParams) = 0;
        virtual void InitGameType(bool multiplayer, bool fromInit) = 0;
        virtual bool CompleteInit() = 0;
        virtual void Shutdown() = 0;
        virtual bool PreUpdate(bool haveFocus, unsigned int updateFlags) = 0;
        virtual bool PostUpdate(bool haveFocus, unsigned int updateFlags) = 0;
        virtual void Reset(bool clients) = 0;
        /**
         * Warhorse's, not stock CryEngine's: fifteen reason counters at
         * CCryAction+0x08 rather than one `m_paused` flag, so screens nest and
         * the last one out resumes the game. Reason 7 is every UI screen that
         * takes the world away -- the escape menu, the in-game tab pages, the
         * credits -- 8 the first-time overlays, 9 the modal item dialog, 6 and
         * 2 Lua's Action.PauseGame, 11 the shooting action.
         */
        virtual void PauseGame(bool pause, std::uint16_t reason, bool releaseAll) = 0;

        /** True while `reason`'s own counter is above zero. */
        virtual bool IsGamePausedForReason(std::uint16_t reason) = 0;

        /** True if any reason is held, or the timer is disabled anyway. */
        virtual bool IsGamePaused() = 0;

        virtual bool IsGameStarted() = 0;
        virtual bool IsLevelPrecachingDone() const = 0;
        virtual void SetLevelPrecachingDone(bool value) = 0;
        virtual ISystem *GetISystem() = 0;                       // +0x098
        virtual ILanQueryListener *GetILanQueryListener() = 0;
        virtual IUIDraw *GetIUIDraw() = 0;
        virtual IMannequin &GetMannequinInterface() = 0;
        virtual IGameObjectSystem *GetIGameObjectSystem() = 0;
        virtual ILevelSystem *GetILevelSystem() = 0;
        virtual IActorSystem *GetIActorSystem() = 0;             // +0x0C8
        virtual IItemSystem *GetIItemSystem() = 0;                // +0x0D0
        virtual IBreakReplicator *GetIBreakReplicator() = 0;      // +0x0D8
        virtual IActionMapManager *GetIActionMapManager() = 0;    // +0x0E0
        virtual IViewSystem *GetIViewSystem() = 0;                // +0x0E8
        // +0x0F0 through +0x120: stock CryEngine puts the gameplay recorder,
        // game rules, effect, material-effect and dialog systems here, but
        // KCD2 has two more entries in front of the profile manager than
        // stock does, so only the one proven below is named.
        virtual void unk_fn0030() = 0; // +0x0F0

        /**
         * Proven: CCryAction's entry (0x1805CA5A0) returns +0x548, where
         * `CCryAction::Init` stores the CGameRulesSystem it constructs, and
         * `C_ScriptBindGameRules` reaches the current rules through this slot
         * and IGameRulesSystem's +0x50.
         */
        virtual IGameRulesSystem *GetIGameRulesSystem() = 0; // +0x0F8
        virtual void unk_fn0032() = 0; // +0x100
        virtual void unk_fn0033() = 0; // +0x108
        virtual void unk_fn0034() = 0; // +0x110
        virtual void unk_fn0035() = 0; // +0x118
        virtual void unk_fn0036() = 0; // +0x120

        /**
         * Proven: CCryAction's entry at +0x128 (0x180602290) returns +0x590,
         * which CCryAction's init fills with the CPlayerProfileManager it
         * constructs, and C_UISettings's constructor registers its profile
         * listener on what this returns.
         */
        virtual IPlayerProfileManager *GetIPlayerProfileManager() = 0; // +0x128

        // +0x130 through +0x168: subtitle manager, remote update, statistics
        // and the rest in stock order, shifted by the same insertions and
        // equally unplaced.
        virtual void unk_fn0038() = 0; // +0x130
        virtual void unk_fn0039() = 0; // +0x138
        virtual void unk_fn0040() = 0; // +0x140
        virtual void unk_fn0041() = 0; // +0x148
        virtual void unk_fn0042() = 0; // +0x150
        virtual void unk_fn0043() = 0; // +0x158
        virtual void unk_fn0044() = 0; // +0x160
        virtual void unk_fn0045() = 0; // +0x168

        // Four KCD2 additions occupy +0x170 through +0x188. Keeping them here
        // puts the game-context methods on their proven concrete slots.
        virtual void unk_kcd2_0046() = 0;
        virtual void unk_kcd2_0047() = 0;
        virtual void unk_kcd2_0048() = 0;
        virtual void unk_kcd2_0049() = 0;

        virtual bool StartGameContext(const SGameStartParams *params) = 0;       // +0x190
        virtual bool ChangeGameContext(const SGameContextParams *params) = 0;    // +0x198
        // Nullary: CCryAction::EndGameContext (0x180B6E3FC) reads no second
        // argument, and MapCmd, UnloadCmd and DisconnectCmd all pass none.
        virtual void EndGameContext() = 0;                                       // +0x1A0
        virtual bool StartedGameContext() const = 0;                             // +0x1A8
        virtual bool StartingGameContext() const = 0;                            // +0x1B0
        virtual void unk_kcd2_0055() = 0;
        virtual bool BlockingSpawnPlayer() = 0;                                  // +0x1C0
        virtual void unk_kcd2_0057() = 0;
        virtual void unk_kcd2_0058() = 0;
        virtual void unk_kcd2_0059() = 0;
        virtual void unk_kcd2_0060() = 0;
        virtual void unk_kcd2_0061() = 0;
        virtual void unk_kcd2_0062() = 0;
        virtual void unk_kcd2_0063() = 0;

        /*
         * KCD2 inserts slots between the proven GetIActionMapManager (+0x0E0)
         * and the proven GetClientActor (+0x200). The game-context subset above
         * is now pinned to its concrete CCryAction entries; the remaining names
         * in that range are inferred from stock CryEngine ordering and must not
         * be called blind.
         *
         * Proof below: CViewSystem::IsClientActorViewActive (WHGame.dll+0x7EF0A4)
         * calls +0x208 and compares the result with a view's linked EntityId,
         * and +0x200 resolves GetIActorSystem() then CActorSystem::GetActor.
         */
        virtual IActor *GetClientActor() const = 0;              // +0x200
        virtual EntityId GetClientActorId() const = 0;           // +0x208
        virtual IEntity *GetClientEntity() const = 0;            // +0x210
        virtual EntityId GetClientEntityId() const = 0;          // +0x218
        virtual INetChannel *GetClientChannel() const = 0;                         // +0x220
        virtual void DelegateAuthority(EntityId entityId, ChannelId channelId) = 0; // +0x228

        static IGameFramework *GetInstance();
    };

    /**
     * Actor-specific game-object extension creator.
     *
     * RTTI places this template inside IGameFramework. The C_Player,
     * C_NPCActor, C_Horse, C_Animal, and C_Dog specializations each have the
     * exact three-slot IGameObjectExtensionCreatorBase table and no instance
     * fields. Their Create implementations allocate the corresponding
     * concrete actor into the native two-word extension handle; all five share
     * the same static RMI metadata publisher. Their concrete vtables are at
     * 0x1840B7000 (player), 0x1840B7020 (NPC), 0x1840B6FE0 (horse),
     * 0x1840B6F80 (animal), and 0x1840B6FC0 (dog).
     */
    template <typename TActor>
    class IGameFramework::CActorCreator
        : public IGameObjectExtensionCreatorBase {
      public:
        ~CActorCreator() override = default;
        IGameObjectExtensionPtr Create() override = 0;
        void GetGameObjectExtensionRMIData(void **rmiData,
                                           std::size_t *count) override = 0;
    };

    using CPlayerActorCreator =
        IGameFramework::CActorCreator<wh::entitymodule::C_Player>;
    using CNPCActorCreator =
        IGameFramework::CActorCreator<wh::entitymodule::C_NPCActor>;
    using CHorseActorCreator =
        IGameFramework::CActorCreator<wh::entitymodule::C_Horse>;
    using CAnimalActorCreator =
        IGameFramework::CActorCreator<wh::entitymodule::C_Animal>;
    using CDogActorCreator =
        IGameFramework::CActorCreator<wh::entitymodule::C_Dog>;

    static_assert(sizeof(CPlayerActorCreator) == 0x8,
                  "IGameFramework::CActorCreator size mismatch");
    static_assert(sizeof(CNPCActorCreator) == 0x8,
                  "IGameFramework::CActorCreator size mismatch");
    static_assert(sizeof(CHorseActorCreator) == 0x8,
                  "IGameFramework::CActorCreator size mismatch");
    static_assert(sizeof(CAnimalActorCreator) == 0x8,
                  "IGameFramework::CActorCreator size mismatch");
    static_assert(sizeof(CDogActorCreator) == 0x8,
                  "IGameFramework::CActorCreator size mismatch");
} // namespace SDK
