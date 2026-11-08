set(KCDC_CLIENT_FILES
    src/features/buff/custom_buff_module.cpp
    ../shared/game/custom_buff_registry.cpp
    ../shared/game/custom_buff_modifiers.cpp
    src/main.cpp
    src/features/area/area_collision.cpp
    src/features/area/area_collision_service.cpp
    src/features/prop/prop_editor_map_travel.cpp

    # core/ -- application lifecycle and framework wiring
    src/core/application.cpp
    src/core/crash_reporting.cpp
    src/core/discord_presence.cpp
    src/core/dlc.cpp
    src/core/feature_registry.cpp
    src/core/local_events.cpp
    src/core/register_entities.cpp
    src/core/scripting_registry.cpp

    # core/boot/ -- process bring-up and CryEngine boot hooks
    src/core/boot/boot.cpp
    src/core/boot/boot_state.cpp
    src/core/boot/engine.cpp
    src/core/boot/init_patches.cpp
    src/core/boot/loading_tick.cpp
    src/core/boot/local_mods.cpp
    src/core/boot/quit.cpp
    src/core/boot/render_device.cpp
    src/core/boot/splash.cpp
    src/core/boot/window.cpp

    # core/input/ -- the mod's own keys, declared to the game's controls
    src/core/input/controls_scripting.cpp
    src/core/input/interaction_gate.cpp
    src/core/input/native_keybinds.cpp

    # core/ui/ -- the shell's own UI and the Scaleform movie host
    src/core/ui/brand_logo.cpp
    src/core/ui/loading_screen_hooks.cpp
    src/core/ui/multiplayer_frontend.cpp
    src/core/ui/native_menu.cpp
    src/core/ui/native_menu_labels.cpp
    src/core/ui/native_ui/native_ui.cpp
    src/core/ui/native_ui/native_ui_assets.cpp
    src/core/ui/native_ui/native_ui_host.cpp
    src/core/ui/native_ui/native_ui_module.cpp
    src/core/ui/native_ui/native_ui_trace.cpp

    # core/streamed_assets/ -- the asset paks a server's resources ship, mounted and registered
    src/core/streamed_assets/streamed_asset_scripting.cpp
    src/core/streamed_assets/streamed_asset_service.cpp
    src/core/streamed_assets/streamed_asset_settings.cpp

    # core/states/ -- client lifecycle state machine
    src/core/states/game_setup.cpp
    src/core/states/initialize.cpp
    src/core/states/main_menu.cpp
    src/core/states/world_builder.cpp
    src/core/states/session_connected.cpp
    src/core/states/session_connection.cpp
    src/core/states/session_startup.cpp
    src/core/states/session_disconnection.cpp
    src/core/states/shutdown.cpp

    # game/ -- cross-feature gameplay infrastructure
    src/common/actor_dressing.cpp
    src/common/input.cpp
    src/common/module_support.cpp
    src/features/player/native_player_module.cpp
    src/core/presentation_module.cpp
    src/common/replication/body_life.cpp
    src/common/replication/pose_snapshot.cpp
    src/common/spawn/actor_spawner.cpp
    src/common/spawn/prop_spawner.cpp
    src/common/spawn/soul_catalog.cpp
    src/common/spawn/static_brush.cpp

    # shared/ -- tables generated from the game's own data
    ../shared/generated/appearance_catalog.cpp
    ../shared/generated/animation_catalog.cpp
    ../shared/generated/locomotion_bands.cpp
    ../shared/generated/body_action_catalog.cpp
    ../shared/generated/body_part_catalog.cpp
    ../shared/generated/buff_catalog.cpp
    ../shared/generated/buff_rule_catalog.cpp
    ../shared/generated/progression_catalog.cpp
    ../shared/generated/clothing_presets.cpp
    ../shared/generated/dlc_catalog.cpp
    ../shared/generated/prop_models.cpp
    ../shared/generated/cart_blueprints.cpp
    ../shared/generated/crafting_catalog.cpp
    ../shared/generated/hand_frames.cpp
    ../shared/inventory_property_policy.cpp
    ../shared/generated/inventory_property_classes.cpp
    src/features/items/native_inventory_projection.cpp
    src/features/items/native_item_effects.cpp
    ../shared/game/custom_item_registry.cpp
    ../shared/image/png_image.cpp
    ../shared/game/item_class_lookup.cpp
    ../shared/game/mod_item_tables.cpp
    ../shared/game/custom_recipes.cpp
    ../shared/generated/clothing_armor_types.cpp
    ../shared/xml/mini_xml.cpp
    ../shared/generated/item_classes.cpp
    ../shared/game/horse_gear.cpp
    ../shared/generated/horse_gear_catalog.cpp
    ../shared/game/horse_gait.cpp
    ../shared/generated/horse_gait_catalog.cpp
    ../shared/game/missile_catalog.cpp
    ../shared/generated/emote_catalog.cpp
    ../shared/generated/gate_catalog.cpp
    ../shared/generated/cook_pot_catalog.cpp
    ../shared/generated/siege_catalog.cpp
    ../shared/generated/siege_defence_catalog.cpp
    ../shared/generated/area_catalog.cpp
    ../shared/generated/door_catalog.cpp
    ../shared/generated/smart_object_blueprints.cpp
    ../shared/generated/marker_catalog.cpp
    ../shared/game/npc_roles.cpp
    ../shared/generated/equipment_catalog.cpp
    ../shared/generated/vfx_catalog.cpp
    ../shared/streamed_assets/streamed_asset_policy.cpp

    ${CMAKE_BINARY_DIR}/kcdc_version.cpp
)

SET(KCDC_SDK_FILES
    src/sdk/patterns.cpp
    src/sdk/fmod/fmod_api.cpp
    src/sdk/cryengine/system/cry_math.cpp
    src/sdk/cryengine/system/cry_string.cpp
    src/sdk/wh/rpgmodule/c_buff_manager.cpp
    src/sdk/wh/rpgmodule/c_soul.cpp
    src/sdk/wh/rpgmodule/c_soul_group.cpp
    src/sdk/wh/shared/c_signal.cpp
    src/sdk/wh/xgenaimodule/activitysystem/dog_companion_mode.cpp
    src/sdk/wh/xgenaimodule/activitysystem/c_dog_companion.cpp
    src/sdk/wh/xgenaimodule/activitysystem/c_dog_objective.cpp
    src/sdk/wh/xgenaimodule/c_npc_manager.cpp
    src/sdk/wh/rpgmodule/c_calendar.cpp
    src/sdk/wh/game/c_camera_manager.cpp
    src/sdk/wh/guimodule/c_compass_mark.cpp
    src/sdk/wh/guimodule/native_shared_ref.cpp
    src/sdk/wh/guimodule/c_ui_flash_base.cpp
    src/sdk/wh/guimodule/c_ui_hud_mask.cpp
    src/sdk/wh/guimodule/c_ui_flash_map_poi_marker.cpp
    src/sdk/cryengine/scaleform/s_ui_arguments.cpp
    src/sdk/wh/playermodule/c_fast_travel.cpp
    src/sdk/wh/entitymodule/c_clothing_system.cpp
    src/sdk/wh/combatmodule/c_combat_actor.cpp
    src/sdk/cryengine/renderer/c_cry_dx12_gi_adapter.cpp
    src/sdk/cryengine/renderer/s_render_light.cpp
    src/sdk/cryengine/renderer/c_cry_dx12_gi_factory.cpp
    src/sdk/cryengine/renderer/c_cry_dx12_gi_output.cpp
    src/sdk/cryengine/renderer/c_device_resource_layout.cpp
    src/sdk/wh/entitymodule/c_inventory_manager.cpp
    src/sdk/wh/framework/c_runtime_prefab_manager.cpp
    src/sdk/cryengine/3dengine/c_terrain.cpp
    src/sdk/cryengine/3dengine/i_particle_manager.cpp
    src/sdk/cryengine/3dengine/spawn_params.cpp
    src/sdk/wh/guimodule/c_ui_hud_messages.cpp
    src/sdk/wh/guimodule/c_ui_compass.cpp
    src/sdk/wh/guimodule/c_ui_apse.cpp
    src/sdk/wh/guimodule/c_ui_overlay.cpp
    src/sdk/wh/guimodule/c_ui_map.cpp
    src/sdk/wh/guimodule/c_ui_quest_log.cpp
    src/sdk/wh/guimodule/s_quest_marker_entry.cpp
    src/sdk/wh/guimodule/c_ui_state_blockers.cpp
    src/sdk/wh/guimodule/c_ui_loading_screen.cpp
    src/sdk/wh/guimodule/c_ui_menu.cpp
    src/sdk/wh/guimodule/c_ui_save_load.cpp
    src/sdk/wh/guimodule/c_ui_hud_messages.cpp
    src/sdk/wh/framework/i_ui_hud_events.cpp
    src/sdk/cryengine/entitysystem/c_entity_archetype_manager.cpp
    src/sdk/cryengine/entitysystem/s_entity_proximity_query.cpp
    src/sdk/wh/environmentmodule/c_environment_module.cpp
    src/sdk/wh/guimodule/c_gui_module.cpp
    src/sdk/wh/guimodule/c_ui_action_hint_manager.cpp
    src/sdk/cryengine/action/c_game_object_system.cpp
    src/sdk/cryengine/physics/c_physical_world.cpp
    src/sdk/wh/entitymodule/c_player.cpp
    src/sdk/cryengine/3dengine/c_vis_area_manager.cpp
    src/sdk/wh/environmentmodule/c_weather_profile_database.cpp
    src/sdk/wh/entitymodule/c_clothing_preset_database.cpp
    src/sdk/wh/playermodule/c_alchemy_recipe_database.cpp
    src/sdk/wh/playermodule/c_blacksmith_recipe_database.cpp
    src/sdk/wh/entitymodule/c_weapon_preset_database.cpp
    src/sdk/wh/entitymodule/c_item_database.cpp
    src/sdk/wh/environmentmodule/c_weather_system.cpp
    src/sdk/cryengine/action/i_action_map_manager.cpp
    src/sdk/wh/entitymodule/c_action_actor.cpp
    src/sdk/wh/entitymodule/c_actor.cpp
    src/sdk/wh/entitymodule/i_actor_carry_corpse.cpp
    src/sdk/wh/entitymodule/c_action_collection.cpp
    src/sdk/wh/entitymodule/c_dog.cpp
    src/sdk/wh/entitymodule/c_dog_external_action_sync_anim_master.cpp
    src/sdk/cryengine/action/c_tag_definition.cpp
    src/sdk/cryengine/action/i_actor_system.cpp
    src/sdk/cryengine/entitysystem/i_ai_object.cpp
    src/sdk/cryengine/input/i_hardware_mouse.cpp
    src/sdk/cryengine/input/i_input.cpp
    src/sdk/wh/rpgmodule/i_soul_system.cpp
    src/sdk/wh/playermodule/i_ui_hud_hints.cpp
    src/sdk/cryengine/system/i_console.cpp
    src/sdk/cryengine/system/s_system_global_environment.cpp
    src/sdk/cryengine/audio/i_audio_system.cpp
    src/sdk/cryengine/entitysystem/i_entity_system.cpp
    src/sdk/wh/framework/i_ui_hud_events.cpp
    src/sdk/cryengine/scaleform/i_flash_player.cpp
    src/sdk/cryengine/scaleform/i_flash_ui.cpp
    src/sdk/cryengine/input/c_base_input.cpp
    src/sdk/cryengine/input/c_keyboard.cpp
    src/sdk/cryengine/system/i_cry_pak.cpp
    src/sdk/cryengine/system/i_localization_manager.cpp
    src/sdk/wh/databasemodule/c_object_database_manager.cpp
    src/sdk/wh/s_mod_info.cpp
    src/sdk/cryengine/system/i_system.cpp
    src/sdk/cryengine/scaleform/c_flash_ui_element.cpp
    src/sdk/cryengine/action/i_game_framework.cpp
    src/sdk/cryengine/renderer/i_renderer.cpp
    src/sdk/cryengine/3dengine/i_3d_engine.cpp
    src/sdk/cryengine/scriptsystem/i_script_system.cpp
    src/sdk/wh/conceptmodule/c_shared_resource.cpp
    src/sdk/wh/conceptmodule/c_node.cpp
    src/sdk/wh/conceptmodule/c_constant_port.cpp
    src/sdk/wh/questmodule/c_log_base.cpp
    src/sdk/wh/questmodule/c_objective.cpp
    src/sdk/wh/questmodule/c_quest.cpp
    src/sdk/wh/questmodule/c_quest_manager.cpp
)

file(GLOB_RECURSE KCDC_CLIENT_FEATURES CONFIGURE_DEPENDS "src/features/*/*.cpp")

add_library(KCDCClient SHARED ${KCDC_CLIENT_FILES} ${KCDC_CLIENT_FEATURES} ${KCDC_SDK_FILES})
fw_set_binary_identity(KCDCClient PRODUCT "Kingdoms Connected" VERSION ${KCDC_VERSION} COMPANY "MafiaHub" DESCRIPTION "Kingdoms Connected Client")
target_include_directories(KCDCClient PRIVATE . src ../server/src)
target_sources(KCDCClient PRIVATE
    ../shared/features/world_builder/mcp_server.cpp
    ../server/src/features/npc/npc_order.cpp
    ../server/src/features/npc/npc_navigation.cpp
    ../server/src/features/navigation/navmesh.cpp
    ../server/src/features/navigation/navmesh_codec.cpp
    ../server/src/features/navigation/navmesh_storage.cpp
)

option(KCDC_AGENT_TESTING "Use desktop-fitted windowed SDR/Low settings (up to 1600x900) for agent-led testing" OFF)
if(KCDC_AGENT_TESTING)
    target_sources(KCDCClient PRIVATE src/core/boot/agent_testing.cpp)
    target_compile_definitions(KCDCClient PRIVATE KCDC_AGENT_TESTING=1)
endif()
# miniz builds the CryPak archive a resource's own native movie is mounted from.
target_link_libraries(KCDCClient Framework::Client KCDCDetour miniz::miniz comdlg32 ole32 shell32 shlwapi d3d12 dxgi windowscodecs bcrypt httplib::httplib)
target_link_options(KCDCClient PRIVATE /SAFESEH:NO /MANIFEST:NO)
# CEF, libnode and the helper executables next to the DLL
fw_stage_runtime(KCDCClient CLIENT)

# The committed metadata path the docs are generated from; the source writes it on scripting init in debug builds (gated on NDEBUG), which for the client is on connect. See scripting-api/.
get_filename_component(KCDC_SCRIPTING_API_GENERATED_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../scripting-api/generated" ABSOLUTE)
target_compile_definitions(KCDCClient PRIVATE KCDC_SCRIPTING_API_METADATA_FILE="${KCDC_SCRIPTING_API_GENERATED_DIR}/client-api.json")

# --- Static-analysis hardening (release only) -------------------------------
# Left unhardened, the shipped DLL hands a reverser its whole class/method map
# (RTTI decorated names), its pattern target-list (get_pattern signature
# literals) and its source tree (__FILE__ paths). This turns those off for the
# release binary only: /GR- drops the RTTI name tables, /d1trimfile strips the
# absolute source prefix from __FILE__, and KCDC_OBFUSCATE switches the
# core/obfuscation.h string cipher on. Debug builds keep RTTI and readable
# strings so their crash minidumps stay triage-able.
if(NOT DEFINED KCDC_OBFUSCATE)
    if(CMAKE_BUILD_TYPE MATCHES "^(Release|RelWithDebInfo|MinSizeRel)$")
        set(KCDC_OBFUSCATE ON)
    else()
        set(KCDC_OBFUSCATE OFF)
    endif()
endif()
if(KCDC_OBFUSCATE)
    file(TO_NATIVE_PATH "${CMAKE_SOURCE_DIR}/" KCDC_TRIM_PREFIX)
    target_compile_definitions(KCDCClient PRIVATE KCDC_OBFUSCATE=1)
    target_compile_options(KCDCClient PRIVATE /GR- "/d1trimfile:${KCDC_TRIM_PREFIX}")
    message(STATUS "KCDCClient: static-analysis hardening ON (RTTI off, __FILE__ trimmed, strings hidden)")
endif()

set(KCDC_UI_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../ui")
set(KCDC_UI_OUTPUT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../files/kcdc/ui")
find_program(KCDC_NPM_EXECUTABLE NAMES npm.cmd npm)
if(KCDC_NPM_EXECUTABLE AND EXISTS "${KCDC_UI_SOURCE_DIR}/node_modules/.bin/vite.cmd")
    add_custom_command(
        OUTPUT "${KCDC_UI_OUTPUT_DIR}/index.html" "${KCDC_UI_OUTPUT_DIR}/menu.css" "${KCDC_UI_OUTPUT_DIR}/menu.js"
        COMMAND "${KCDC_NPM_EXECUTABLE}" run build
        DEPENDS "${KCDC_UI_SOURCE_DIR}/index.html" "${KCDC_UI_SOURCE_DIR}/package.json" "${KCDC_UI_SOURCE_DIR}/package-lock.json" "${KCDC_UI_SOURCE_DIR}/tsconfig.json" "${KCDC_UI_SOURCE_DIR}/vite.config.ts"
                "${KCDC_UI_SOURCE_DIR}/src/bridge.ts" "${KCDC_UI_SOURCE_DIR}/src/main.tsx" "${KCDC_UI_SOURCE_DIR}/src/masterlist.ts" "${KCDC_UI_SOURCE_DIR}/src/menu.css" "${KCDC_UI_SOURCE_DIR}/src/tokens.css"
                "${KCDC_UI_SOURCE_DIR}/src/types.ts"
        WORKING_DIRECTORY "${KCDC_UI_SOURCE_DIR}"
        COMMENT "Building the KCDC Preact frontend"
        VERBATIM
    )
    add_custom_target(KCDCClientUI COMMAND ${CMAKE_COMMAND} -E copy_directory "${KCDC_UI_OUTPUT_DIR}" "${CMAKE_BINARY_DIR}/bin/files/kcdc/ui" DEPENDS "${KCDC_UI_OUTPUT_DIR}/index.html" "${KCDC_UI_OUTPUT_DIR}/menu.css" "${KCDC_UI_OUTPUT_DIR}/menu.js")
    add_dependencies(KCDCClient KCDCClientUI)
endif()

add_custom_command(TARGET KCDCClient POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E rm -rf "${CMAKE_BINARY_DIR}/bin/files/kcdc/fonts"
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/../../files" "${CMAKE_BINARY_DIR}/bin/files/"
            )

# Pre-resolved addresses for the WHGame.dll builds patterns.cpp targets, so a boot looks each
# pattern up instead of scanning an 89 MB image close to six hundred times. The table holds a
# block per storefront (Steam, and the Microsoft Store / Xbox app copy), and every pattern
# must be unique in each: one set of patterns serves every storefront.
#
# The table is committed, and this step keeps it in step with patterns.cpp: when the two
# disagree, or a storefront's module has no block, it is rebuilt from every game module this
# machine has -- KCDC_GAME_MODULE, the environment variable of that name, the Steam install
# and the Microsoft Store install. A storefront it does not have keeps its committed block. A rebuild reuses every
# pattern the table already holds, so adding one costs about a second. A machine without
# the game still fails with the command to run on one that has it.
#
# Only an unhardened build stages it. The table is a plain list of every address the mod
# hooks, which is exactly what KCDC_HIDE keeps out of a hardened client, and a release
# client scans in a fraction of the time a debug one does.
set(KCDC_GAME_MODULE "" CACHE FILEPATH "WHGame.dll the pattern table is regenerated against")
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    set(KCDC_PATTERN_TABLE_ARGS "${CMAKE_CURRENT_SOURCE_DIR}/data/kcdc.patterns" "${CMAKE_CURRENT_SOURCE_DIR}/src/sdk/patterns.cpp" call --regenerate --exe-env KCDC_GAME_MODULE --steam-app 1771300 --steam-relative "Bin/Win64MasterMasterSteamPGO/WHGame.dll" --msstore-family DeepSilver.77536C3FE941_hmv7qcest37me --msstore-relative WHGame.dll --require-unique)
    if(KCDC_GAME_MODULE)
        list(APPEND KCDC_PATTERN_TABLE_ARGS --exe "${KCDC_GAME_MODULE}")
    endif()
    add_custom_command(TARGET KCDCClient PRE_BUILD
        COMMAND ${Python3_EXECUTABLE} "${Framework_SCRIPTS_DIR}/check_pattern_table.py" ${KCDC_PATTERN_TABLE_ARGS}
        COMMENT "Checking the pattern table against patterns.cpp"
    )
endif()

if(NOT KCDC_OBFUSCATE)
    add_custom_command(TARGET KCDCClient POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/data/kcdc.patterns" "$<TARGET_FILE_DIR:KCDCClient>/kcdc.patterns"
        COMMENT "Copying the pattern table to output directory"
    )
endif()
