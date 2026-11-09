set(KCDC_SERVER_FILES
    ../shared/game/custom_buff_registry.cpp
    ../shared/game/custom_buff_modifiers.cpp
    src/core/server.cpp
    src/features/player/player_station_service.cpp
    src/core/presentation.cpp
    src/core/scripting_registry.cpp
    src/core/scripting/player_arg.cpp
    src/core/scripting/wrap.cpp

    ../shared/generated/animation_catalog.cpp
    ../shared/generated/appearance_catalog.cpp
    ../shared/generated/body_part_catalog.cpp
    ../shared/generated/combat_catalog.cpp
    ../shared/features/player/player_combat_pricing.cpp
    ../shared/generated/buff_catalog.cpp
    ../shared/generated/progression_catalog.cpp
    ../shared/generated/dlc_catalog.cpp
    ../shared/generated/soul_profile_catalog.cpp
    ../shared/generated/buff_rule_catalog.cpp
    ../shared/game/soul_scalar.cpp
    ../shared/generated/soul_rule_catalog.cpp
    ../shared/generated/crafting_catalog.cpp
    ../shared/generated/alchemy_catalog.cpp
    ../shared/generated/alchemy_ingredients.cpp
    ../shared/generated/alchemy_animations.cpp
    ../shared/inventory_property_policy.cpp
    ../shared/generated/inventory_property_classes.cpp
    ../shared/game/custom_item_registry.cpp
    ../shared/image/png_image.cpp
    ../shared/game/item_class_lookup.cpp
    ../shared/game/mod_item_tables.cpp
    ../shared/game/custom_recipes.cpp
    ../shared/game/custom_recipe_registry.cpp
    ../shared/generated/clothing_armor_types.cpp
    ../shared/xml/mini_xml.cpp
    ../shared/generated/item_classes.cpp
    ../shared/generated/poi_catalog.cpp
    ../shared/game/missile_catalog.cpp
    ../shared/generated/emote_catalog.cpp
    ../shared/generated/door_catalog.cpp
    ../shared/generated/smart_object_blueprints.cpp
    ../shared/generated/area_catalog.cpp
    ../shared/generated/dice_catalog.cpp
    ../shared/generated/gate_catalog.cpp
    ../shared/generated/cook_pot_catalog.cpp
    ../shared/generated/consumable_catalog.cpp
    ../shared/generated/siege_catalog.cpp
    ../shared/generated/siege_defence_catalog.cpp
    ../shared/generated/stash_catalog.cpp
    ../shared/generated/gathering_catalog.cpp
    ../shared/game/gathering_sources.cpp
    ../shared/generated/marker_catalog.cpp
    ../shared/game/npc_roles.cpp
    ../shared/generated/npc_placement_catalog.cpp
    ../shared/generated/animal_catalog.cpp
    ../shared/generated/animal_loot.cpp
    ../shared/generated/equipment_catalog.cpp
    ../shared/generated/horse_catalog.cpp
    ../shared/game/horse_gear.cpp
    ../shared/generated/horse_gear_catalog.cpp
    ../shared/generated/vfx_catalog.cpp
    ../shared/generated/clothing_presets.cpp
    ../shared/generated/prop_models.cpp
    ../shared/generated/cart_blueprints.cpp
    ../shared/register_entities.cpp
    ../shared/streamed_assets/lua_script_scan.cpp
    ../shared/streamed_assets/streamed_asset_policy.cpp

    ${CMAKE_BINARY_DIR}/kcdc_version.cpp
)

file(GLOB_RECURSE KCDC_SERVER_FEATURES CONFIGURE_DEPENDS "src/features/*/*.cpp")

add_library(KCDCServerRuntime STATIC ${KCDC_SERVER_FILES} ${KCDC_SERVER_FEATURES})
target_include_directories(KCDCServerRuntime PUBLIC src ${CMAKE_CURRENT_SOURCE_DIR}/../)
target_link_libraries(KCDCServerRuntime PUBLIC Framework::Server KCDCDetour)

add_executable(KCDCServer src/main.cpp)
fw_set_binary_identity(KCDCServer PRODUCT "Kingdoms Connected" VERSION ${KCDC_VERSION} COMPANY "MafiaHub" DESCRIPTION "Kingdoms Connected Server")
target_link_libraries(KCDCServer PRIVATE KCDCServerRuntime)
# libnode and the crash handler next to the binary, $ORIGIN on Linux
fw_stage_runtime(KCDCServer SERVER)

# The authored herb catalog is linked into each executable as read-only data
# rather than compiled as source. A Windows resource only links from objects
# an executable is given directly, so the runtime hands its consumers the
# compiled .rc.
set(KCDC_GATHERING_SOURCES_BIN "${CMAKE_CURRENT_SOURCE_DIR}/../shared/generated/gathering_sources.bin")
if(WIN32)
    set(KCDC_GATHERING_SOURCES_RC "${CMAKE_CURRENT_BINARY_DIR}/gathering_sources.rc")
    file(CONFIGURE OUTPUT "${KCDC_GATHERING_SOURCES_RC}" CONTENT "KCDC_GATHERING_SOURCES RCDATA \"${KCDC_GATHERING_SOURCES_BIN}\"\n")
    set_source_files_properties("${KCDC_GATHERING_SOURCES_RC}" PROPERTIES OBJECT_DEPENDS "${KCDC_GATHERING_SOURCES_BIN}")
    add_library(KCDCGatheringSources OBJECT "${KCDC_GATHERING_SOURCES_RC}")
    target_link_libraries(KCDCServerRuntime INTERFACE "$<TARGET_OBJECTS:KCDCGatheringSources>")
else()
    set_source_files_properties(../shared/game/gathering_sources.cpp PROPERTIES
        COMPILE_DEFINITIONS "KCDC_GATHERING_SOURCES_BIN=\"${KCDC_GATHERING_SOURCES_BIN}\""
        OBJECT_DEPENDS "${KCDC_GATHERING_SOURCES_BIN}")
endif()

if(WIN32)
    target_compile_definitions(KCDCServerRuntime PUBLIC NOMINMAX _USE_MATH_DEFINES)
endif()

# The committed metadata path the docs are generated from; the source writes it on boot in debug builds (gated on NDEBUG). See scripting-api/.
get_filename_component(KCDC_SCRIPTING_API_GENERATED_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../scripting-api/generated" ABSOLUTE)
target_compile_definitions(KCDCServerRuntime PRIVATE KCDC_SCRIPTING_API_METADATA_FILE="${KCDC_SCRIPTING_API_GENERATED_DIR}/server-api.json")

# Compile the gamemode. In the default emit mode this is what the local server
# actually loads; when the release pipeline stages sources instead it is still
# worth running, as the type-check that catches a resource which no longer
# builds against the generated contracts at the time the server is built rather
# than the first time someone deploys it. The gamemode reads those contracts as
# @kingdomsconnected/types, linked to scripting-api/, so a regenerated contract
# recompiles it too.
get_filename_component(KCDC_RESOURCES_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../resources" ABSOLUTE)
get_filename_component(KCDC_GAMEMODE_DIR "${KCDC_RESOURCES_DIR}/kcdc-gamemode" ABSOLUTE)

file(GLOB_RECURSE KCDC_GAMEMODE_SOURCES CONFIGURE_DEPENDS "${KCDC_GAMEMODE_DIR}/src/*.ts")
get_filename_component(KCDC_SCRIPTING_API_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../scripting-api" ABSOLUTE)
set(KCDC_GAMEMODE_DECLARATIONS "${KCDC_SCRIPTING_API_DIR}/shared.d.ts" "${KCDC_SCRIPTING_API_DIR}/generated/server-api.d.ts" "${KCDC_SCRIPTING_API_DIR}/generated/client-api.d.ts")
find_program(KCDC_NPM_EXECUTABLE NAMES npm.cmd npm)
if(KCDC_NPM_EXECUTABLE AND EXISTS "${KCDC_GAMEMODE_DIR}/node_modules")
    add_custom_command(
        OUTPUT "${KCDC_GAMEMODE_DIR}/dist/server/index.js" "${KCDC_GAMEMODE_DIR}/dist/client/index.js"
        COMMAND "${KCDC_NPM_EXECUTABLE}" run build
        DEPENDS ${KCDC_GAMEMODE_SOURCES} ${KCDC_GAMEMODE_DECLARATIONS} "${KCDC_GAMEMODE_DIR}/tsconfig.json" "${KCDC_GAMEMODE_DIR}/src/client/tsconfig.json" "${KCDC_GAMEMODE_DIR}/package.json"
        WORKING_DIRECTORY "${KCDC_GAMEMODE_DIR}"
        COMMENT "Compiling the KCDC gamemode resource"
        VERBATIM
    )
    add_custom_target(KCDCGamemode DEPENDS "${KCDC_GAMEMODE_DIR}/dist/server/index.js" "${KCDC_GAMEMODE_DIR}/dist/client/index.js")
    add_dependencies(KCDCServer KCDCGamemode)
else()
    message(WARNING "npm or ${KCDC_GAMEMODE_DIR}/node_modules is missing: the default gamemode will not be type-checked. Run `npm ci` in that directory.")
endif()

# Build optional resources without placing them in automatic discovery.
get_filename_component(KCDC_PLAYGROUND_DIR "${KCDC_RESOURCES_DIR}/optional/kcdc-playground" ABSOLUTE)
file(GLOB_RECURSE KCDC_PLAYGROUND_SOURCES CONFIGURE_DEPENDS "${KCDC_PLAYGROUND_DIR}/src/*.ts")
if(KCDC_NPM_EXECUTABLE AND EXISTS "${KCDC_PLAYGROUND_DIR}/node_modules")
    add_custom_command(
        OUTPUT "${KCDC_PLAYGROUND_DIR}/dist/server/index.js" "${KCDC_PLAYGROUND_DIR}/dist/client/index.js"
        COMMAND "${KCDC_NPM_EXECUTABLE}" run build
        DEPENDS ${KCDC_PLAYGROUND_SOURCES} ${KCDC_GAMEMODE_DECLARATIONS} "${KCDC_PLAYGROUND_DIR}/tsconfig.json" "${KCDC_PLAYGROUND_DIR}/src/client/tsconfig.json" "${KCDC_PLAYGROUND_DIR}/package.json"
        WORKING_DIRECTORY "${KCDC_PLAYGROUND_DIR}"
        COMMENT "Compiling the optional KCDC playground resource"
        VERBATIM
    )
    add_custom_target(KCDCPlayground DEPENDS "${KCDC_PLAYGROUND_DIR}/dist/server/index.js" "${KCDC_PLAYGROUND_DIR}/dist/client/index.js")
endif()

# Which half of a resource is staged beside the binary. OFF is the local dev
# loop: stage the emit, so the server that was just built runs the gamemode
# without anyone compiling anything by hand. The release pipeline configures
# with ON, which ships the TypeScript instead and no dist/ at all -- a gamemode
# an operator reads and changes, built by whoever deploys it. See
# cmake/sync_resources.cmake.
option(KCDC_STAGE_RESOURCE_SOURCES "Stage resource TypeScript instead of its compiled output" OFF)
if(KCDC_STAGE_RESOURCE_SOURCES)
    set(KCDC_RESOURCES_STAGE "source")
else()
    set(KCDC_RESOURCES_STAGE "emit")
endif()

add_custom_target(KCDCResources
    COMMAND ${CMAKE_COMMAND}
        "-DKCDC_RESOURCES_SOURCE=${KCDC_RESOURCES_DIR}"
        "-DKCDC_RESOURCES_DESTINATION=${CMAKE_BINARY_DIR}/bin/resources"
        "-DKCDC_RESOURCES_STAGE=${KCDC_RESOURCES_STAGE}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/sync_resources.cmake"
    COMMENT "Syncing KCDC resources into the runtime directory (${KCDC_RESOURCES_STAGE})"
    VERBATIM
)
if(TARGET KCDCGamemode)
    add_dependencies(KCDCResources KCDCGamemode)
endif()
if(TARGET KCDCPlayground)
    add_dependencies(KCDCResources KCDCPlayground)
endif()
add_dependencies(KCDCServer KCDCResources)
