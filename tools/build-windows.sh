#!/usr/bin/env bash
# The Windows x64 build, cross-compiled on Linux (#29; docs/coop/building.md,
# *The Windows build*):
#
#   tools/build-windows.sh [--version us|eu]
#
# Needs clang-cl, lld-link, llvm-rc, llvm-mt, llvm-lib, ninja, Wine (with the
# binfmt entry for .exe files) and the MSVC CRT and Windows SDK that xwin
# splats into ~/dev-scratch/BM64Recomp-coop/win/sdk (XWIN_SDK; fetching them
# accepts Microsoft's licence); what is missing is named before the build
# starts. The recompiled game (RecompiledFuncs/, from tools/verify.sh) must
# exist for the version. The patches are built by the Linux clang, as in the
# Linux build, so both builds have the same build identity — measured on one
# machine, both 0xFC44BFB4 (#29, docs/coop/building.md). The shaders are
# compiled by RT64's Windows dxc.exe in Wine
# (tools/dxc-wine.sh, which turns the Unix paths into Wine paths), as in a
# Windows build: the generated build.ninja is pointed at the wrapper, so the
# submodule stays untouched. RelWithDebInfo as upstream's Windows CI, so a
# crash's backtrace names functions (BM64Recompiled.pdb). Output:
# ~/dev-scratch/BM64Recomp-coop/build/win-<version>/.
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
scratch="$HOME/dev-scratch/BM64Recomp-coop"
version=""
while [ $# -gt 0 ]; do
	case "$1" in
	--version) version="$2"; shift 2 ;;
	*) echo "usage: $0 [--version us|eu]" >&2; exit 2 ;;
	esac
done
[ -n "$version" ] || version="$(cat "$root/RecompiledFuncs/bm64_version.txt" 2>/dev/null || echo us)"
if [ "$(cat "$root/RecompiledFuncs/bm64_version.txt" 2>/dev/null)" != "$version" ]; then
	echo "RecompiledFuncs/ is not the $version build: run tools/verify.sh --full --version $version first" >&2
	exit 1
fi

export XWIN_SDK="${XWIN_SDK:-$scratch/win/sdk}"
# The cross build needs more than the Linux one; say what is missing instead
# of failing inside cmake or on the symlinks below (#29, which found this on a
# machine with none of it). Exit 4 is bad setup, as in tools/run.sh.
missing=""
for tool in clang-cl lld-link llvm-rc llvm-mt llvm-lib ninja cmake wine; do
	command -v "$tool" >/dev/null 2>&1 || missing="$missing $tool"
done
if [ -n "$missing" ]; then
	echo "tools/build-windows.sh: not on PATH:$missing" >&2
	echo "  apt install clang lld llvm ninja-build cmake wine64 (Ubuntu's clang" >&2
	echo "  package ships no clang-cl name: ln -s \"\$(command -v clang)\" ~/.local/bin/clang-cl)" >&2
	exit 4
fi
if [ ! -d "$XWIN_SDK/crt/include" ]; then
	echo "tools/build-windows.sh: XWIN_SDK ($XWIN_SDK) holds no xwin splat" >&2
	echo "  get xwin (github.com/Jake-Shadle/xwin), then (accepts Microsoft's licence):" >&2
	echo "  xwin --accept-license splat --output \"$XWIN_SDK\"" >&2
	exit 4
fi
build="$scratch/build/win-$version"
# Windows finds headers and libraries whatever their case; Linux needs the
# spellings the sources use (xwin links only those the SDK itself uses)
for link in include/um/Shlobj.h:ShlObj.h include/um/ShellScalingAPI.h:ShellScalingApi.h \
	lib/um/x86_64/Winmm.lib:winmm.lib lib/um/x86_64/Shcore.lib:shcore.lib; do
	[ -e "$XWIN_SDK/sdk/${link%%:*}" ] || ln -s "${link#*:}" "$XWIN_SDK/sdk/${link%%:*}"
done
dxc_dir="$root/lib/rt64/src/contrib/dxc"

cd "$root"
cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
	-DCMAKE_TOOLCHAIN_FILE="$root/tools/windows-cross.cmake" \
	-DBM64_VERSION="$version" -DBM64_FUNCS_VERSION_CHECK=ON \
	-DPATCHES_C_COMPILER=clang -DPATCHES_LD=ld.lld
export WINEPREFIX="$scratch/win/prefix" WINEDEBUG=-all
sed -i "s#$dxc_dir/bin/x64/dxc.exe#$root/tools/dxc-wine.sh#g" "$build/build.ninja"
cmake --build "$build" --target BM64Recompiled -j "$(nproc)"
ls -l "$build/BM64Recompiled.exe"
