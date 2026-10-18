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
