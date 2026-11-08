# Copies the resource tree into a server's runtime directory.
#
# A resource is staged one of two ways, and never both:
#
#   emit   (KCDC_RESOURCES_STAGE=emit, the default) -- dist/, the manifest,
#          ui/ and the packages its "dependencies" name, with theirs. This is
#          the local dev loop: you edit the TypeScript in the repo,
#          the build compiles it, and the server beside the binary runs what
#          came out. Sources are not staged because you already have them.
#
#   source (KCDC_RESOURCES_STAGE=source) -- src/, the tsconfigs, the manifest,
#          the lockfile and ui/, with no dist/. This is what the release
#          pipeline ships: a gamemode an operator can read and change, built by
#          whoever deploys it.
#
# Run with `cmake -P` because that is what allows the exclusions: `cmake -E
# copy_directory` takes the lot.
#
# Takes KCDC_RESOURCES_SOURCE, KCDC_RESOURCES_DESTINATION and optionally
# KCDC_RESOURCES_STAGE on the command line. file(COPY) writes only what changed
# and deletes nothing, so it is cheap to re-run and leaves another project's
# resources alone.

if(NOT EXISTS "${KCDC_RESOURCES_SOURCE}")
    return()
endif()

if(NOT KCDC_RESOURCES_STAGE)
    set(KCDC_RESOURCES_STAGE "emit")
endif()
if(NOT KCDC_RESOURCES_STAGE STREQUAL "emit" AND NOT KCDC_RESOURCES_STAGE STREQUAL "source")
    message(FATAL_ERROR "KCDC_RESOURCES_STAGE must be 'emit' or 'source', got '${KCDC_RESOURCES_STAGE}'")
endif()

# file(COPY) never deletes, so a destination synced in the other mode would keep
# serving what that mode staged: a stale dist/ the sources no longer match, or
# sources beside an emit. Drop the other mode's files by name, one resource at a
# time, which still leaves another project's resources alone. Wiping the
# destination is not an option: it is shared with whatever else was built into
# the same tree.
file(GLOB KCDC_SYNCED_RESOURCES RELATIVE "${KCDC_RESOURCES_SOURCE}" "${KCDC_RESOURCES_SOURCE}/*")
file(GLOB KCDC_OPTIONAL_RESOURCES RELATIVE "${KCDC_RESOURCES_SOURCE}" "${KCDC_RESOURCES_SOURCE}/optional/*")
list(APPEND KCDC_SYNCED_RESOURCES ${KCDC_OPTIONAL_RESOURCES})
foreach(resource IN LISTS KCDC_SYNCED_RESOURCES)
    if(NOT IS_DIRECTORY "${KCDC_RESOURCES_SOURCE}/${resource}")
        continue()
    endif()
    if(KCDC_RESOURCES_STAGE STREQUAL "source")
        file(REMOVE_RECURSE "${KCDC_RESOURCES_DESTINATION}/${resource}/dist")
    else()
        file(REMOVE_RECURSE "${KCDC_RESOURCES_DESTINATION}/${resource}/src")
        file(REMOVE "${KCDC_RESOURCES_DESTINATION}/${resource}/package-lock.json")
        file(GLOB KCDC_STALE_TSCONFIGS "${KCDC_RESOURCES_DESTINATION}/${resource}/tsconfig*.json")
        if(KCDC_STALE_TSCONFIGS)
            file(REMOVE ${KCDC_STALE_TSCONFIGS})
        endif()
    endif()
endforeach()

# Copies an installed package and, through its manifest, every package it depends on in turn. npm
# installs a resource's packages flat, so each one is found in the resource's own node_modules.
function(kcdc_stage_package modules destination name)
    if(name IN_LIST KCDC_STAGED_PACKAGES)
        return()
    endif()
    set(KCDC_STAGED_PACKAGES ${KCDC_STAGED_PACKAGES} "${name}" PARENT_SCOPE)
    if(NOT EXISTS "${modules}/${name}/package.json")
        message(FATAL_ERROR "Package '${name}' is not installed in ${modules}: run `npm install` in its resource")
    endif()
    get_filename_component(parent "${destination}/${name}" DIRECTORY)
    file(COPY "${modules}/${name}" DESTINATION "${parent}")
    kcdc_stage_dependencies("${modules}" "${destination}" "${modules}/${name}/package.json")
    set(KCDC_STAGED_PACKAGES ${KCDC_STAGED_PACKAGES} PARENT_SCOPE)
endfunction()

function(kcdc_stage_dependencies modules destination manifest)
    file(READ "${manifest}" json)
    string(JSON count ERROR_VARIABLE missing LENGTH "${json}" dependencies)
    if(missing OR count EQUAL 0)
        return()
    endif()
    math(EXPR last "${count} - 1")
    foreach(index RANGE ${last})
        string(JSON name MEMBER "${json}" dependencies ${index})
        kcdc_stage_package("${modules}" "${destination}" "${name}")
    endforeach()
    set(KCDC_STAGED_PACKAGES ${KCDC_STAGED_PACKAGES} PARENT_SCOPE)
endfunction()

if(KCDC_RESOURCES_STAGE STREQUAL "source")
    file(COPY "${KCDC_RESOURCES_SOURCE}/"
        DESTINATION "${KCDC_RESOURCES_DESTINATION}"
        PATTERN "node_modules" EXCLUDE
        PATTERN "dist" EXCLUDE
        PATTERN "*.tsbuildinfo" EXCLUDE
        PATTERN ".gitignore" EXCLUDE
    )
else()
    file(COPY "${KCDC_RESOURCES_SOURCE}/"
        DESTINATION "${KCDC_RESOURCES_DESTINATION}"
        PATTERN "node_modules" EXCLUDE
        PATTERN "src" EXCLUDE
        PATTERN "tsconfig*.json" EXCLUDE
        PATTERN "package-lock.json" EXCLUDE
        PATTERN "*.tsbuildinfo" EXCLUDE
        PATTERN ".gitignore" EXCLUDE
    )

    # The emit runs against the packages the resource declares as runtime dependencies, so they are
    # staged with it; its devDependencies (the compiler, the declarations) stay behind.
    foreach(resource IN LISTS KCDC_SYNCED_RESOURCES)
        set(manifest "${KCDC_RESOURCES_SOURCE}/${resource}/package.json")
        if(EXISTS "${manifest}")
            set(KCDC_STAGED_PACKAGES "")
            kcdc_stage_dependencies("${KCDC_RESOURCES_SOURCE}/${resource}/node_modules" "${KCDC_RESOURCES_DESTINATION}/${resource}/node_modules" "${manifest}")
        endif()
    endforeach()
endif()
