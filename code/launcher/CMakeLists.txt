set(KCDC_LAUNCHER_FILES
    src/main.cpp
    src/patcher.cpp
)

# The build patcher, off by default: only the release workflow has credentials
# for it. KCDC_PATCHER_CHANNEL is never compiled in -- it seeds
# `.mafiahub/channel` beside a locally built launcher, and the release bundle
# carries its own stamp.
option(KCDC_PATCHER_ENABLED "Enable MafiaHub build patching before launching KCDC" OFF)
set(KCDC_PATCHER_API_URL "" CACHE STRING "MafiaHub API base URL used by the KCDC build patcher")
set(KCDC_PATCHER_MOD_ID "" CACHE STRING "MafiaHub mod UUID used by the KCDC build patcher")
set(KCDC_PATCHER_CHANNEL "stable" CACHE STRING "Initial MafiaHub build channel stored next to the KCDC launcher")
set(KCDC_PATCHER_DOWNLOAD_TOKEN "" CACHE STRING "MafiaHub build download token embedded into the KCDC launcher")
set(KCDC_PATCHER_DOWNLOAD_CONCURRENCY "4" CACHE STRING "MafiaHub build patcher download concurrency")

if(KCDC_PATCHER_ENABLED)
    if("${KCDC_PATCHER_API_URL}" STREQUAL "")
        message(FATAL_ERROR "KCDC_PATCHER_ENABLED requires KCDC_PATCHER_API_URL")
    endif()
    if("${KCDC_PATCHER_MOD_ID}" STREQUAL "" OR "${KCDC_PATCHER_MOD_ID}" STREQUAL "00000000-0000-0000-0000-000000000000")
        message(FATAL_ERROR "KCDC_PATCHER_ENABLED requires KCDC_PATCHER_MOD_ID")
    endif()
    string(LENGTH "${KCDC_PATCHER_CHANNEL}" KCDC_PATCHER_CHANNEL_LENGTH)
    if(NOT KCDC_PATCHER_CHANNEL MATCHES "^[a-z0-9._-]+$" OR KCDC_PATCHER_CHANNEL_LENGTH GREATER 32)
        message(FATAL_ERROR "KCDC_PATCHER_CHANNEL must contain 1-32 lowercase letters, numbers, dots, underscores, or dashes")
    endif()
    if("${KCDC_PATCHER_DOWNLOAD_TOKEN}" STREQUAL "")
        message(FATAL_ERROR "KCDC_PATCHER_ENABLED requires KCDC_PATCHER_DOWNLOAD_TOKEN")
    endif()
    if(NOT KCDC_PATCHER_DOWNLOAD_CONCURRENCY MATCHES "^[1-9][0-9]*$")
        message(FATAL_ERROR "KCDC_PATCHER_DOWNLOAD_CONCURRENCY must be a positive integer")
    endif()
    if(NOT TARGET MafiaHubUpdater)
        message(FATAL_ERROR "KCDC_PATCHER_ENABLED requires MafiaHubUpdater target")
    endif()
    set(KCDC_PATCHER_ENABLED_CPP true)
else()
    set(KCDC_PATCHER_ENABLED_CPP false)
endif()

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/src/patcher_config.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/kcdc_patcher_config.h"
    @ONLY
)

file(GENERATE
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated/kcdc-channel"
    CONTENT "${KCDC_PATCHER_CHANNEL}"
)

add_executable(KCDCLauncher ${KCDC_LAUNCHER_FILES})
# The icon is the press kit's emblem (kingdomsconnected.com/press, brand/emblem.png)
fw_set_binary_identity(KCDCLauncher PRODUCT "Kingdoms Connected" VERSION ${KCDC_VERSION} COMPANY "MafiaHub" DESCRIPTION "Kingdoms Connected Launcher" ICON "${CMAKE_CURRENT_SOURCE_DIR}/resources/kcdc.ico")
target_include_directories(KCDCLauncher PRIVATE . "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_libraries(KCDCLauncher PRIVATE Framework::Loader KCDCClient)
target_link_libraries(KCDCLauncher PRIVATE Shell32)

# KingdomCome.exe maps to 1.5 MiB; the game itself is WHGame.dll, which Windows loads on its own
target_compile_definitions(KCDCLauncher PRIVATE FW_LAUNCHER_GAME_RESERVE=0x1000000)

if(KCDC_PATCHER_ENABLED)
    target_link_libraries(KCDCLauncher PRIVATE MafiaHubServices)
    add_custom_command(TARGET KCDCLauncher POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:KCDCLauncher>/.mafiahub"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_BINARY_DIR}/generated/kcdc-channel"
            "$<TARGET_FILE_DIR:KCDCLauncher>/.mafiahub/channel"
        COMMENT "Writing the installation-local KCDC build channel"
    )
endif()

# A separate process because it replaces files this launcher holds open.
if(WIN32 AND TARGET MafiaHubUpdater)
    add_custom_command(TARGET KCDCLauncher POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:MafiaHubUpdater>"
            "$<TARGET_FILE_DIR:KCDCLauncher>"
        COMMENT "Bundling MafiaHubUpdater next to KCDCLauncher"
    )
endif()
