# CMake toolchain: the Windows x64 build cross-compiled on Linux with
# clang-cl and lld-link against the MSVC CRT and Windows SDK that xwin splats
# (tools/build-windows.sh, docs/coop/building.md, *The Windows build*).
# XWIN_SDK names the splat folder (crt/, sdk/). Build-time tools built for
# Windows (file_to_c, RT64's dxc.exe) run through Wine.
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR AMD64)

set(XWIN_SDK "$ENV{XWIN_SDK}" CACHE PATH "xwin splat folder")
if (NOT EXISTS "${XWIN_SDK}/crt/include")
    message(FATAL_ERROR "XWIN_SDK (${XWIN_SDK}) holds no xwin splat")
endif()

set(CMAKE_C_COMPILER clang-cl)
set(CMAKE_CXX_COMPILER clang-cl)
set(CMAKE_LINKER lld-link)
set(CMAKE_AR llvm-lib)
set(CMAKE_RC_COMPILER llvm-rc)
set(CMAKE_MT llvm-mt)
set(CMAKE_CROSSCOMPILING_EMULATOR wine)
# xwin splats the release CRT only (no msvcrtd.lib)
set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
set(CMAKE_TRY_COMPILE_CONFIGURATION Release) # RelWithDebInfo builds link the same CRT

set(_xwin_include
    "/imsvc \"${XWIN_SDK}/crt/include\" /imsvc \"${XWIN_SDK}/sdk/include/ucrt\" /imsvc \"${XWIN_SDK}/sdk/include/um\" /imsvc \"${XWIN_SDK}/sdk/include/shared\" /imsvc \"${XWIN_SDK}/sdk/include/winrt\"")
# RT64's SDL 2.26.3 headers define _m_prefetch for older clang-cl; newer clang
# has it as a builtin and refuses the definition, so its guard is set.
set(_xwin_defs "-D__PRFCHWINTRIN_H")
set(CMAKE_C_FLAGS_INIT "--target=x86_64-pc-windows-msvc ${_xwin_defs} ${_xwin_include}")
set(CMAKE_CXX_FLAGS_INIT "--target=x86_64-pc-windows-msvc /EHsc ${_xwin_defs} ${_xwin_include}")
set(CMAKE_RC_FLAGS_INIT "/I \"${XWIN_SDK}/sdk/include/um\" /I \"${XWIN_SDK}/sdk/include/shared\"")

set(_xwin_libs
    "/libpath:\"${XWIN_SDK}/crt/lib/x86_64\" /libpath:\"${XWIN_SDK}/sdk/lib/um/x86_64\" /libpath:\"${XWIN_SDK}/sdk/lib/ucrt/x86_64\"")
set(CMAKE_EXE_LINKER_FLAGS_INIT "${_xwin_libs}")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "${_xwin_libs}")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "${_xwin_libs}")

# libraries and headers only from the SDK (not the host's libm)
set(CMAKE_FIND_ROOT_PATH "${XWIN_SDK}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
