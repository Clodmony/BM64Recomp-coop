#!/usr/bin/env bash
# Folders to play a co-op session from, one per platform (#29;
# docs/coop/playtest.md):
#
#   tools/playtest-kit.sh [--version us|eu]
#
# Makes ~/dev-scratch/BM64Recomp-coop/playtest/<version>/linux/ from the
# tools/verify.sh build and .../windows/ (and windows.zip) from the
# tools/build-windows.sh build, each a portable folder: the game, its DLLs,
# assets/, the controller database and portable.txt, so saves and settings
# stay in the folder. No ROM: each player picks their own in the launcher.
# A folder that exists is updated in place: the program files are replaced,
# and what a game run from it left (settings, saves, logs) is kept; the zip
# is made from a clean copy.
# These are builds of the game: copy them to the playtest machines by hand,
# never upload or publish them (AGENTS.md).
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
scratch="$HOME/dev-scratch/BM64Recomp-coop"
version="eu"
while [ $# -gt 0 ]; do
	case "$1" in
	--version) version="$2"; shift 2 ;;
	*) echo "usage: $0 [--version us|eu]" >&2; exit 2 ;;
	esac
done
suffix=""
[ "$version" = eu ] && suffix="-eu"
linux_exe="$scratch/build/$(basename "$root")$suffix/BM64Recompiled"
win_build="$scratch/build/win-$version"
out="$scratch/playtest/$version"

common() { # folder
	mkdir -p "$1"
	rsync -a --delete --exclude scss "$root/assets/" "$1/assets/"
	cp "$root/recompcontrollerdb.txt" "$1/"
	touch "$1/portable.txt"
	cp "$root/docs/coop/playtest.md" "$1/PLAYTEST.md"
}

if [ -x "$linux_exe" ]; then
	common "$out/linux"
	cp "$linux_exe" "$out/linux/"
	cat > "$out/linux/start.sh" <<'EOF'
#!/bin/sh
# Starts the game from this folder and keeps its log in coop.log, the run
# before's in coop.prev.log (a session sends it to the other side).
cd "$(dirname "$0")" || exit 1
[ -f coop.log ] && mv -f coop.log coop.prev.log
./BM64Recompiled 2>&1 | tee coop.log
EOF
	chmod +x "$out/linux/start.sh"
	echo "linux:   $out/linux"
else
	echo "no Linux build at $linux_exe (tools/verify.sh --full --version $version)" >&2
fi
if [ -f "$win_build/BM64Recompiled.exe" ]; then
	common "$out/windows"
	cp "$win_build"/{BM64Recompiled.exe,BM64Recompiled.pdb,SDL2.dll,dxcompiler.dll,dxil.dll} "$out/windows/"
	stage="$out/.zip-stage"
	rm -rf "$stage"
	common "$stage/windows"
	cp "$win_build"/{BM64Recompiled.exe,BM64Recompiled.pdb,SDL2.dll,dxcompiler.dll,dxil.dll} "$stage/windows/"
	(cd "$stage" && rm -f ../windows.zip && python3 -I -m zipfile -c ../windows.zip windows)
	rm -rf "$stage"
	echo "windows: $out/windows (and windows.zip)"
else
	echo "no Windows build at $win_build (tools/build-windows.sh --version $version)" >&2
fi
