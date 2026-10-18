# The game version a build is for, chosen when configuring (docs/coop/building.md):
#   -DBM64_VERSION=us   the 1.0 US ROM, upstream's build (default)
#   -DBM64_VERSION=eu   the Europe ROM; run `tools/europe.py configs` first
# The checkout's recompiled output (RecompiledFuncs/, rsp/aspMain.cpp,
# RecompiledPatches/) holds one version at a time.
set(BM64_VERSION "us" CACHE STRING "Game version to build: us or eu")
set_property(CACHE BM64_VERSION PROPERTY STRINGS us eu)

if(BM64_VERSION STREQUAL "us")
    set(BM64_PATCHES_TOML patches.toml)
elseif(BM64_VERSION STREQUAL "eu")
    set(BM64_PATCHES_TOML patches.eu.toml)
    if(NOT EXISTS ${CMAKE_SOURCE_DIR}/patches.eu.toml)
        message(FATAL_ERROR "BM64_VERSION=eu needs the Europe configs: python3 -I tools/europe.py configs")
    endif()
    # main.cpp registers the Europe ROM instead of the US one
    add_compile_definitions(BM64_VERSION_EU)
else()
    message(FATAL_ERROR "BM64_VERSION must be us or eu, not '${BM64_VERSION}'")
endif()

# tools/verify.sh writes the version next to the ROM's recompiled output; a
# mismatch would link one version's code with the other's data addresses.
# Configuring again whenever that file changes keeps this check current.
# (verify.sh turns it off for a compile-only run, which never builds that output.)
option(BM64_FUNCS_VERSION_CHECK "Check that RecompiledFuncs/ is for BM64_VERSION" ON)
set(BM64_FUNCS_VERSION_FILE ${CMAKE_SOURCE_DIR}/RecompiledFuncs/bm64_version.txt)
if(BM64_FUNCS_VERSION_CHECK AND EXISTS ${BM64_FUNCS_VERSION_FILE})
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${BM64_FUNCS_VERSION_FILE})
    file(STRINGS ${BM64_FUNCS_VERSION_FILE} BM64_FUNCS_VERSION LIMIT_COUNT 1)
    if(NOT BM64_FUNCS_VERSION STREQUAL BM64_VERSION)
        message(FATAL_ERROR "RecompiledFuncs/ was recompiled for '${BM64_FUNCS_VERSION}', "
            "this build is BM64_VERSION=${BM64_VERSION}: recompile the ROM for this version")
    endif()
endif()

# The patches are recompiled by the build itself (CMakeLists.txt); a stamp
# per version makes a build re-run that step when the other version ran last.
set(BM64_PATCHES_STAMP ${CMAKE_SOURCE_DIR}/RecompiledPatches/version_${BM64_VERSION}.stamp)
set(BM64_PATCHES_STAMPS
    ${CMAKE_SOURCE_DIR}/RecompiledPatches/version_us.stamp
    ${CMAKE_SOURCE_DIR}/RecompiledPatches/version_eu.stamp)

# The build identity an online session's hello compares (src/coop/session.cpp,
# #26) covers the hooks through the recompiler config of the game, hashed here
# (configuring again when it changes); the patches' binary is hashed at run time.
set(BM64_ROM_TOML ${CMAKE_SOURCE_DIR}/bm64.${BM64_VERSION}.toml)
set(BM64_COOP_HOOKS_ID 0)
if(EXISTS ${BM64_ROM_TOML})
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${BM64_ROM_TOML})
    file(SHA256 ${BM64_ROM_TOML} BM64_ROM_TOML_SHA256)
    string(SUBSTRING ${BM64_ROM_TOML_SHA256} 0 8 BM64_COOP_HOOKS_ID)
    set(BM64_COOP_HOOKS_ID 0x${BM64_COOP_HOOKS_ID})
endif()
set_property(SOURCE ${CMAKE_SOURCE_DIR}/src/coop/session.cpp APPEND PROPERTY
    COMPILE_DEFINITIONS BM64_COOP_HOOKS_ID=${BM64_COOP_HOOKS_ID})
