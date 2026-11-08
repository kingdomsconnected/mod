cmake_minimum_required(VERSION 3.24)
cmake_policy(SET CMP0091 NEW)

# The Framework: the pinned prebuilt SDK (cmake/FrameworkPin.cmake), a local install tree
# (-DFW_SDK_ROOT) or a source checkout built alongside (-DFW_SOURCE_DIR). Must precede project():
# it selects the vcpkg toolchain.
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/FrameworkBootstrap.cmake)

project(KCDC CXX)
fw_bootstrap_finish()
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake/")

# Keep the licenses beside the binaries, including the license of the Framework actually used.
if(FW_BOOTSTRAP_MODE STREQUAL "sdk")
    set(_kcdc_framework_license "${Framework_SHARE_DIR}/LICENSE.txt")
elseif(FW_BOOTSTRAP_MODE STREQUAL "source")
    set(_kcdc_framework_license "${FW_SOURCE_DIR}/LICENSE.txt")
else()
    set(_kcdc_framework_license "${Framework_SOURCE_DIR}/LICENSE.txt")
endif()
configure_file("${_kcdc_framework_license}" "${CMAKE_BINARY_DIR}/bin/licenses/Framework-LICENSE.txt" COPYONLY)
foreach(_kcdc_notice LICENSE.txt NOTICE.txt)
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/${_kcdc_notice}" "${CMAKE_BINARY_DIR}/bin/${_kcdc_notice}" COPYONLY)
endforeach()

# httplib::httplib is the name httplib's own CMake gives its target inside the Framework tree; the SDK
# exports it as Framework::httplib.
if(NOT TARGET httplib::httplib AND TARGET Framework::httplib)
    add_library(httplib::httplib ALIAS Framework::httplib)
endif()

find_program(KCDC_NODE_EXECUTABLE node)
find_program(KCDC_PNPM_EXECUTABLE pnpm)
if(KCDC_NODE_EXECUTABLE AND KCDC_PNPM_EXECUTABLE)
    # --skip-missing, so the target is usable on a checkout that has never run a debug client.
    # The publish workflow deliberately does not pass it.
    add_custom_target(KCDCScriptingContract
        COMMAND ${KCDC_NODE_EXECUTABLE}
                "${CMAKE_CURRENT_SOURCE_DIR}/scripts/build_scripting_contract.mjs"
                --out "${CMAKE_BINARY_DIR}/scripting-contract/kcdc"
                --skip-missing
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        VERBATIM
    )
else()
    message(STATUS "KCDCScriptingContract unavailable: node and pnpm are required")
endif()

add_subdirectory(vendors/recastnavigation)
add_subdirectory(code)

# Deployment key for every RPC identifier, handed to the Framework at runtime through
# InstanceOptions::rpcIdentifierSalt. Release CI passes it per shipped build; client and server
# must carry the same value or no RPC dispatches (the build token gate catches the mismatch).
set(FW_RPC_IDENTIFIER_SALT "" CACHE STRING "64-bit salt keying every RPC identifier; must match on client and server")
# Release CI turns this on, so a salt that failed to arrive stops the configure instead of shipping a
# build keyed on the Framework's public default.
option(KCDC_REQUIRE_RPC_IDENTIFIER_SALT "Fail the configure when FW_RPC_IDENTIFIER_SALT is unset" OFF)
if(KCDC_REQUIRE_RPC_IDENTIFIER_SALT AND FW_RPC_IDENTIFIER_SALT STREQUAL "")
    message(FATAL_ERROR "KCDC_REQUIRE_RPC_IDENTIFIER_SALT is ON but FW_RPC_IDENTIFIER_SALT is empty; refusing to build on the public default salt")
endif()
if(NOT FW_RPC_IDENTIFIER_SALT STREQUAL "")
    string(REGEX REPLACE "[uUlL]+$" "" _kcdc_rpc_salt "${FW_RPC_IDENTIFIER_SALT}")
    if(NOT _kcdc_rpc_salt MATCHES "^(0[xX][0-9a-fA-F]+|[0-9]+)$")
        message(FATAL_ERROR "FW_RPC_IDENTIFIER_SALT must be a hexadecimal (0x...) or decimal integer")
    endif()
    foreach(_kcdc_target KCDCClient KCDCServer)
        if(TARGET ${_kcdc_target})
            target_compile_definitions(${_kcdc_target} PRIVATE KCDC_RPC_IDENTIFIER_SALT=${_kcdc_rpc_salt}ULL)
        endif()
    endforeach()
    message(STATUS "KCDC: RPC identifier salt set")
endif()
