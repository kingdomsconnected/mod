# Detour 1.6.0 (recastnavigation, zlib licence -- License.txt), runtime only:
# no Recast, no DetourNavMeshBuilder, no crowd. The server never builds a mesh,
# it loads the one the game ships and rebuilds each tile in Detour's own layout.
#
# Vendored rather than taken from the vcpkg port because the port's build
# options are fixed by vcpkg.json and the triplets, which belong to the
# Framework repository rather than to this project: turning on upstream's
# RECASTNAVIGATION_DT_POLYREF64 there, even scoped to the port in a triplet,
# would change Detour for every project built from it. The game's larger levels need it -- kutnohorsko has 54620 tiles and up
# to 317 polygons in one, which leaves a 32-bit reference 7 bits of salt where
# dtNavMesh::init demands 10. The define changes dtPolyRef, so every
# translation unit that includes a Detour header has to see it -- hence PUBLIC.
# DT_VIRTUAL_QUERYFILTER lets the server's filter consult door state on an
# off-mesh link, which a flag mask cannot express.
add_library(KCDCDetour STATIC
    Detour/Source/DetourAlloc.cpp
    Detour/Source/DetourAssert.cpp
    Detour/Source/DetourCommon.cpp
    Detour/Source/DetourNavMesh.cpp
    Detour/Source/DetourNavMeshQuery.cpp
    Detour/Source/DetourNode.cpp
)
target_include_directories(KCDCDetour PUBLIC Detour/Include)
target_compile_definitions(KCDCDetour PUBLIC DT_POLYREF64 DT_VIRTUAL_QUERYFILTER)
