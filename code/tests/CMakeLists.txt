# The one KCDC test executable. Unit modules and whole-function suites run in a
# single process; the suites that boot a full server run as `--suite <name>`,
# one process and one working directory each. The server runtime brings every
# server feature and shared catalog, so only client sources are listed here.
add_executable(KCDCTests
    ../shared/features/world_builder/mcp_server.cpp
    locomotion_ut.cpp
    inventory_model_ut.cpp
    custom_items_ut.cpp
    custom_item_bindings_ut.cpp
    inventory_sync_ut.cpp
    inventory_reconciliation_ut.cpp
    inventory_property_policy_ut.cpp
    inventory_gameplay_ut.cpp
    npc_api_ut.cpp
    door_api_ut.cpp
    dice_npc_ut.cpp
    npc_inventory_ut.cpp
    animal_gamemode_ut.cpp
    alchemy_tests_main.cpp
    alchemy_module_ut.cpp
    soul_evaluator_ut.cpp
    alchemy_catalog_ut.cpp
    alchemy_entry_continuation_ut.cpp
    alchemy_character_evaluator_ut.cpp
    alchemy_protocol_ut.cpp
    alchemy_thermal_ut.cpp
    smithing_tests_main.cpp
    smithing_module_ut.cpp
    navigation_ut.cpp
    npc_order_ut.cpp
    streamed_assets_ut.cpp
    mod_item_tables_ut.cpp
    custom_recipes_ut.cpp
    world_inventory_main.cpp
    ground_stock_ut.cpp
    custom_item_world_ut.cpp
    drop_selection_ut.cpp
    ground_item_replication_ut.cpp
    stash_inventory_ut.cpp
    stash_lid_ut.cpp
    gathering_ut.cpp
    item_carry_ut.cpp
    world_resource_ut.cpp
    patrol_api_ut.cpp
    stash_rpc_ut.cpp
    cook_pot_rpc_ut.cpp
    ../client/src/features/npc/npc_locomotion_service.cpp
    ../client/src/features/ui/ui_hud_screen_effect_stack.cpp
    ../client/src/common/replication/pose_snapshot.cpp
    ../shared/generated/body_action_catalog.cpp
    ../shared/game/horse_gait.cpp
    ../shared/generated/horse_gait_catalog.cpp
    ../shared/generated/locomotion_bands.cpp
)
if(WIN32)
    target_sources(KCDCTests PRIVATE
        hud_elements_ut.cpp
        world_builder_ut.cpp
        world_builder_gizmo_ut.cpp
        world_builder_areas_ut.cpp
        world_builder_patrols_ut.cpp
        ../client/src/features/ui/ui_hud_elements.cpp
        ../client/src/sdk/cryengine/scaleform/c_flash_ui_element.cpp
        ../client/src/sdk/wh/guimodule/c_ui_hud_mask.cpp
        ../client/src/features/prop/prop_editor_patrols.cpp
        ../client/src/features/prop/prop_editor_document.cpp
        ../client/src/features/prop/prop_editor_world_objects.cpp
        ../client/src/features/prop/prop_editor_areas.cpp
        ../client/src/features/camera/anchor_camera.cpp
        ../client/src/features/camera/camera_view.cpp
        ../client/src/sdk/cryengine/system/cry_math.cpp
    )
    target_link_libraries(KCDCTests PRIVATE comdlg32 ole32 shell32 ImGuizmo)
endif()
# unit.h is the Framework's test harness: its source tree in nested and source builds, the SDK's
# include/framework-tests otherwise.
if(DEFINED Framework_SOURCE_DIR)
    set(KCDC_FRAMEWORK_TESTS_DIR "${Framework_SOURCE_DIR}/code/tests")
else()
    set(KCDC_FRAMEWORK_TESTS_DIR "${Framework_SDK_ROOT}/include/framework-tests")
endif()
target_include_directories(KCDCTests PRIVATE ${KCDC_FRAMEWORK_TESTS_DIR} ../server/src ../client/src ../)
target_link_libraries(KCDCTests PRIVATE KCDCServerRuntime Framework::Client imgui::imgui httplib::httplib)
# It links the client too, so it needs the client runtime (CEF, libnode) beside it.
fw_stage_runtime(KCDCTests CLIENT)

find_program(KCDC_TEST_NODE NAMES node REQUIRED)
target_compile_definitions(KCDCTests PRIVATE
    KCDC_TEST_NODE="${KCDC_TEST_NODE}"
    KCDC_ANIMAL_POLICY_TEST="${CMAKE_CURRENT_SOURCE_DIR}/animal_gamemode_ut.cjs"
    KCDC_ANIMAL_POLICY_MODULE="${CMAKE_CURRENT_SOURCE_DIR}/../../resources/kcdc-gamemode/dist/server/npc-spawn.js"
)
if(TARGET KCDCGamemode)
    add_dependencies(KCDCTests KCDCGamemode)
endif()

set(KCDC_TEST_RUNTIME "${CMAKE_CURRENT_BINARY_DIR}/test-runtime")
foreach(suite gathering-api area-transfer player-hit-transfer world-resource world-resource-empty patrol-api stash-rpc cook-pot-rpc npc-api door-api dice-npc)
    file(MAKE_DIRECTORY "${KCDC_TEST_RUNTIME}/${suite}")
endforeach()
add_custom_target(RunKCDCTests
    COMMAND $<TARGET_FILE:KCDCTests>
    COMMAND $<TARGET_FILE:KCDCTests> --suite world-builder-mcp
    COMMAND ${CMAKE_COMMAND} -E chdir "${KCDC_TEST_RUNTIME}/gathering-api" $<TARGET_FILE:KCDCTests> --suite gathering-api
    COMMAND ${CMAKE_COMMAND} -E chdir "${KCDC_TEST_RUNTIME}/area-transfer" $<TARGET_FILE:KCDCTests> --suite area-transfer
    COMMAND ${CMAKE_COMMAND} -E chdir "${KCDC_TEST_RUNTIME}/player-hit-transfer" $<TARGET_FILE:KCDCTests> --suite player-hit-transfer
    COMMAND ${CMAKE_COMMAND} -E chdir "${KCDC_TEST_RUNTIME}/world-resource" $<TARGET_FILE:KCDCTests> --suite world-resource
    COMMAND ${CMAKE_COMMAND} -E chdir "${KCDC_TEST_RUNTIME}/world-resource-empty" $<TARGET_FILE:KCDCTests> --suite world-resource-empty
    COMMAND ${CMAKE_COMMAND} -E chdir "${KCDC_TEST_RUNTIME}/patrol-api" $<TARGET_FILE:KCDCTests> --suite patrol-api
    COMMAND ${CMAKE_COMMAND} -E chdir "${KCDC_TEST_RUNTIME}/stash-rpc" $<TARGET_FILE:KCDCTests> --suite stash-rpc
    COMMAND ${CMAKE_COMMAND} -E chdir "${KCDC_TEST_RUNTIME}/cook-pot-rpc" $<TARGET_FILE:KCDCTests> --suite cook-pot-rpc
    COMMAND ${CMAKE_COMMAND} -E chdir "${KCDC_TEST_RUNTIME}/npc-api" $<TARGET_FILE:KCDCTests> --suite npc-api
    COMMAND ${CMAKE_COMMAND} -E chdir "${KCDC_TEST_RUNTIME}/door-api" $<TARGET_FILE:KCDCTests> --suite door-api
    COMMAND ${CMAKE_COMMAND} -E chdir "${KCDC_TEST_RUNTIME}/dice-npc" $<TARGET_FILE:KCDCTests> --suite dice-npc
    COMMAND "${KCDC_TEST_NODE}" --test "${CMAKE_CURRENT_SOURCE_DIR}/../../resources/kcdc-gamemode/tests/npcdemo.test.cjs"
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    DEPENDS KCDCTests
)
