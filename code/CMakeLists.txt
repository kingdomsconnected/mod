include(UpdateGitHash)

update_git_version(KCDC "${CMAKE_CURRENT_SOURCE_DIR}/shared/version.cpp.in" "${CMAKE_BINARY_DIR}/kcdc_version.cpp")

include_directories(${CMAKE_CURRENT_SOURCE_DIR})

if(CMAKE_CL_64)
    add_subdirectory(client)
    add_subdirectory(launcher)
    add_subdirectory(tests)
endif()

if(CMAKE_CL_64 OR NOT MSVC)
    add_subdirectory(server)
endif()
