#!/usr/bin/env bash
# Builds N64Recomp and RSPRecomp at the commit upstream's CI pins
# (.github/workflows/validate.yml, input N64RECOMP_COMMIT) into a cache folder
# outside the repository, once per commit, and links both binaries into the
# repository root, where bm64.us.toml, aspMain.toml and CMakeLists.txt (the
# patches.toml step) expect them. Both names are in .gitignore.
#
#   tools/n64recomp.sh            build if missing, link, print the cache folder
#
# Cache: $N64RECOMP_CACHE (default ~/dev-scratch/BM64Recomp-coop/n64recomp).
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
commit="$(sed -n "/N64RECOMP_COMMIT:/,/default:/s/.*default: *'\([0-9a-f]*\)'.*/\1/p" \
	"$root/.github/workflows/validate.yml")"
[ -n "$commit" ] || { echo "n64recomp: no N64RECOMP_COMMIT in validate.yml" >&2; exit 1; }

cache="${N64RECOMP_CACHE:-$HOME/dev-scratch/BM64Recomp-coop/n64recomp}"
dir="$cache/${commit:0:12}"
src="$dir/src"

if [ ! -x "$dir/N64Recomp" ] || [ ! -x "$dir/RSPRecomp" ]; then
	mkdir -p "$dir"
	if [ ! -d "$src/.git" ]; then
		git clone -q https://github.com/N64Recomp/N64Recomp.git "$src"
	fi
	git -C "$src" checkout -q "$commit"
	git -C "$src" submodule update -q --init --recursive
	cmake -S "$src" -B "$src/build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
		-DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ > "$dir/configure.log"
	cmake --build "$src/build" --target N64RecompCLI RSPRecomp -j "$(nproc)" > "$dir/build.log"
	cp "$src/build/N64Recomp" "$src/build/RSPRecomp" "$dir/"
fi

ln -sf "$dir/N64Recomp" "$root/N64Recomp"
ln -sf "$dir/RSPRecomp" "$root/RSPRecomp"
echo "$dir"
