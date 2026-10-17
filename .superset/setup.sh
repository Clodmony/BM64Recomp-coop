#!/usr/bin/env bash
# Makes a fresh worktree of this fork ready for an agent session. Superset runs
# it on workspace creation (.superset/config.json); run it by hand in any other
# worktree. Idempotent; touches only this repository's git config, its
# submodules and the scratch folder. Never installs packages and never changes
# the owner's global git identity.
set -uo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"

git config user.name Claude
git config user.email noreply@anthropic.com
if ! git remote get-url upstream >/dev/null 2>&1; then
	git remote add upstream https://github.com/RevoSucks/BM64Recomp.git
fi
git remote set-url --push upstream DISABLED
git config core.hooksPath .githooks
# gh would otherwise pick the upstream remote's repository
gh repo set-default Clodmony/BM64Recomp-coop >/dev/null 2>&1 \
	|| echo "setup: gh repo set-default failed; pass -R Clodmony/BM64Recomp-coop" >&2

mkdir -p "$HOME/dev-scratch/BM64Recomp-coop"

# rt64 and RmlUi are large; a worktree starts without submodules
git submodule update --init --recursive --jobs 8 >/dev/null \
	|| echo "setup: submodule update failed; run git submodule update --init --recursive" >&2

for t in cmake ninja clang clang++ ld.lld python3 gh; do
	command -v "$t" >/dev/null || echo "setup: $t missing (tools/verify.sh needs it)" >&2
done
pkg-config --exists sdl2 gtk+-3.0 freetype2 2>/dev/null \
	|| echo "setup: SDL2, GTK 3 or FreeType development files missing (host build)" >&2

rom="${BM64_DECOMP_ROM:-$HOME/dev-scratch/BM64Recomp-coop/rom/bm64.decomp.us.z64}"
[ -f "$rom" ] || echo "setup: no decompressed US ROM at $rom; tools/verify.sh compiles without linking (docs/coop/building.md)" >&2

echo "setup: $(git config user.name) <$(git config user.email)>, branch $(git branch --show-current)"
echo "setup: read HANDOFF.md first, then AGENTS.md"
