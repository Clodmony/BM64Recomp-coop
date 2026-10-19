#!/usr/bin/env bash
# One unattended check of the checked-out branch (the co-op fork's verify
# script), with a summary of a few lines at the end.
#
#   tools/verify.sh [--quick] [--full] [--version us|eu] [--scenarios]
#
#   --quick     only the fast gates: submodules, configs, harness, patches,
#               europe
#   --full      a decompressed ROM is required; without one the recomp and
#               link stages fail instead of being skipped
#   --version   the game version to build (BM64_VERSION, src/coop/version.cmake);
#               default: us if its decompressed image exists, else eu if that
#               one does, else us
#   --scenarios also run the game's test scenarios (tools/scenarios/*.txt,
#               tools/scenario.py) on the linked build; opens the game window
#
# Stages:
#   submodules  every submodule checked out at the commit the branch records
#   configs     `tools/europe.py configs`, then the US and Europe configs
#               (bm64/patches/aspMain/data_dump) parse as TOML
#   harness     tools/test_scenario.py: scenario.py's checks on fake runs'
#               logs, garbled ones too (#46); no game, no ROM
#   n64recomp   N64Recomp/RSPRecomp at upstream CI's pinned commit
#               (tools/n64recomp.sh, cached outside the repository)
#   patches     patches/*.c compiled for MIPS, then `N64Recomp patches.toml`
#               (eu: patches.eu.toml) in strict patch mode (every patched
#               symbol must exist in the symbol files): no ROM needed
#   europe      eu only: `tools/europe.py check` on the decompressed image
#               (the Europe data addresses, the patches' references, the
#               instruction patch sites)
#   recomp      `N64Recomp bm64.<us|eu>.toml` and `RSPRecomp aspMain(.eu).toml`;
#               needs the decompressed ROM ($BM64_DECOMP_ROM, default
#               ~/dev-scratch/BM64Recomp-coop/rom/bm64.decomp.<us|eu>.z64,
#               made by tools/unpack_rom.py)
#   build       with the ROM: BM64Recompiled built and linked;
#               without it: every object file the game links compiled (host
#               code, runtime, RT64, UI) except the ROM's output and
#               register_overlays.cpp; stubs let CMake configure; no link
#   scenarios   with --scenarios and a linked build: tools/scenario.py on every
#               scenario, each in its own portable run folder
#
# Full logs go to $VERIFY_OUT/<stamp>/ (default
# ~/dev-scratch/BM64Recomp-coop/verify), the summary to <stamp>/summary.txt and
# stdout. The CMake build folder is kept between runs for speed:
# $VERIFY_BUILD (default ~/dev-scratch/BM64Recomp-coop/build/<checkout name>,
# with -eu appended for the Europe build).
# Exits 0 only if no stage failed; a skipped stage is listed in the summary.
# Only --scenarios starts the game, through tools/run.sh (portable run folders,
# never the owner's saves and settings).
set -uo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
scratch="$HOME/dev-scratch/BM64Recomp-coop"
quick=0
full=0
scenarios=0
version=
while (( $# )); do
	case "$1" in
		--quick) quick=1 ;;
		--full) full=1 ;;
		--scenarios) scenarios=1 ;;
		--version) version="${2:-}"; shift ;;
		--version=*) version="${1#--version=}" ;;
		-h|--help) sed -n '2,49p' "$0"; exit 0 ;;
		*) echo "unknown option: $1" >&2; exit 2 ;;
	esac
	shift
done
if [ -z "$version" ]; then
	version=us
	[ ! -f "$scratch/rom/bm64.decomp.us.z64" ] && [ -f "$scratch/rom/bm64.decomp.eu.z64" ] && version=eu
fi
case "$version" in
	us) suffix= ; bm64_toml=bm64.us.toml; patches_toml=patches.toml; asp_toml=aspMain.toml ;;
	eu) suffix=-eu; bm64_toml=bm64.eu.toml; patches_toml=patches.eu.toml; asp_toml=aspMain.eu.toml ;;
	*) echo "--version must be us or eu" >&2; exit 2 ;;
esac

out="${VERIFY_OUT:-$scratch/verify}/$(date +%Y%m%d-%H%M%S)"
build="${VERIFY_BUILD:-$scratch/build/$(basename "$root")$suffix}"
rom="${BM64_DECOMP_ROM:-$scratch/rom/bm64.decomp.$version.z64}"
mkdir -p "$out"
summary="$out/summary.txt"
failed=0
skipped=0

# clang 16+ turns -Wincompatible-pointer-types into an error; upstream builds the
# patches with clang-15, where it is a warning, so keep it one.
patches_cc="${PATCHES_C_COMPILER:-clang -Wno-error=incompatible-pointer-types}"
# libstdc++ 15 no longer includes <cstdint> through other headers, which RmlUi's
# debugger relies on; harmless with older toolchains.
cxx_flags="${VERIFY_CXX_FLAGS:--include cstdint}"

note() { echo "$*" >> "$summary"; }
note "verify $(git -C "$root" rev-parse --abbrev-ref HEAD) @ $(git -C "$root" rev-parse --short HEAD), version $version ($(date '+%F %T'))"

# stage <name> <command...>: run from the repository root, output in <name>.log.
stage() {
	local name="$1"; shift
	if (cd "$root" && "$@") > "$out/$name.log" 2>&1; then
		note "$name: ok"
	else
		note "$name: FAILED (tail of $name.log:)"
		tail -n 5 "$out/$name.log" | sed 's/^/    /' >> "$summary"
		failed=$((failed + 1))
	fi
}
skip() {
	note "$1: skipped ($2)"
	skipped=$((skipped + 1))
}

submodules() {
	local bad
	git submodule status --recursive
	bad="$(git submodule status --recursive | grep -E '^[-+U]')"
	[ -z "$bad" ] || { echo "not at the recorded commit (git submodule update --init --recursive):"; echo "$bad"; return 1; }
}
stage submodules submodules

configs() {
	python3 -I tools/europe.py configs || return 1
	python3 - bm64.us.toml patches.toml aspMain.toml \
		bm64.eu.toml patches.eu.toml aspMain.eu.toml data_dump.eu.toml <<'PY'
import sys, tomllib
for path in sys.argv[1:]:
    with open(path, "rb") as f:
        tomllib.load(f)
    print(f"{path}: ok")
PY
}
stage configs configs

stage harness python3 -I tools/test_scenario.py

stage n64recomp tools/n64recomp.sh

patches() {
	make -C patches clean
	make -C patches CC="$patches_cc" LD=ld.lld -j "$(nproc)"
	./N64Recomp "$patches_toml"
}
stage patches patches

if [ "$version" = eu ]; then
	if [ -f "$rom" ]; then
		stage europe python3 -I tools/europe.py check "$rom"
	elif (( full )); then
		note "europe: FAILED (no decompressed Europe ROM at $rom; see docs/coop/building.md)"
		failed=$((failed + 1))
	else
		skip europe "no decompressed Europe ROM"
	fi
fi

if (( ! quick )); then
	# Stubs for the ROM's output, only where it is missing; removed at the end.
	stubs=()
	cleanup_stubs() {
		local f
		for f in "${stubs[@]}"; do
			rm -f "$root/$f"
			rmdir "$root/$(dirname "$f")" 2>/dev/null || true
		done
	}
	trap cleanup_stubs EXIT
	add_stub() {
		mkdir -p "$root/$(dirname "$1")"
		echo "/* tools/verify.sh stub: no decompressed ROM, compile-only build */" > "$root/$1"
		stubs+=("$1")
	}

	if [ -f "$rom" ]; then
		rm -f "$root/RecompiledFuncs/verify_stub.c"
		recomp() {
			ln -sf "$rom" "bm64.decomp.$version.z64"
			./N64Recomp "$bm64_toml" && ./RSPRecomp "$asp_toml" \
				&& echo "$version" > RecompiledFuncs/bm64_version.txt # src/coop/version.cmake
		}
		stage recomp recomp
		mode=link
	else
		if (( full )); then
			note "recomp: FAILED (no decompressed $version ROM at $rom; see docs/coop/building.md)"
			failed=$((failed + 1))
		else
			skip recomp "no decompressed $version ROM; see docs/coop/building.md"
		fi
		mode=compile
		compgen -G "$root/RecompiledFuncs/*.c" > /dev/null || add_stub RecompiledFuncs/verify_stub.c
		[ -f "$root/rsp/aspMain.cpp" ] || add_stub rsp/aspMain.cpp
	fi

	host_build() {
		cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
			-DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
			-DCMAKE_CXX_FLAGS="$cxx_flags" -DBM64_VERSION="$version" \
			-DBM64_FUNCS_VERSION_CHECK="$([ "$mode" = link ] && echo ON || echo OFF)" \
			-DPATCHES_C_COMPILER="$patches_cc" -DPATCHES_LD=ld.lld || return 1
		if [ "$mode" = link ]; then
			cmake --build "$build" --target BM64Recompiled -j "$(nproc)"
		else
			# every object file the game links, except the ROM's output and
			# register_overlays.cpp, which includes its overlay table
			command ninja -C "$build" -t inputs BM64Recompiled \
				| grep '\.o$' \
				| grep -vE 'RecompiledFuncs\.dir/|/register_overlays\.cpp\.o$|/rsp/aspMain\.cpp\.o$' \
				> "$out/objects.txt"
			command ninja -C "$build" -j "$(nproc)" $(cat "$out/objects.txt")
		fi
	}
	if (( full )) && [ "$mode" = compile ]; then
		skip build "no ROM, so no link"
	else
		stage build host_build
		[ "$mode" = compile ] && sed -i 's/^build: ok$/build: ok (compile only, no ROM: not linked)/' "$summary"
	fi
	if (( scenarios )); then
		if [ "$mode" = link ] && grep -q '^build: ok$' "$summary"; then
			stage scenarios python3 -I tools/scenario.py --version "$version" tools/scenarios/*.txt
			grep -E 'checks passed$|FAILED' "$out/scenarios.log" | sed 's/^/    /' >> "$summary"
		else
			skip scenarios "no linked build"
		fi
	fi
fi

stages="$(grep -cE '^[a-z0-9]+: (ok|FAILED)' "$summary")"
note "$((stages - failed))/$stages stages passed, $skipped skipped; logs in $out"
if (( failed )); then note "RESULT: FAILURES"
elif (( skipped )); then note "RESULT: PASSED (with skipped stages)"
else note "RESULT: ALL PASSED"; fi
cat "$summary"
exit $(( failed > 0 ))
