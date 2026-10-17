# Handoff

The session note for this repository. Read it first in a fresh session and
rewrite it at the end of every session (issue loop, last step). It lives in the
repository so it survives any worktree path and any tool; per-checkout agent
memory is not read by a session that starts in another worktree. State goes
here; stable recipes go into `docs/coop/`; personal data and hardware never.

Last updated 2026-10-18 (project setup).

## Repository state

- Fork of `RevoSucks/BM64Recomp`. `master` = upstream `master` (`726adc9`).
  Default branch `bm64recomp-coop`, created from `726adc9`. Ruleset
  "Protected" blocks force-pushes and deletion on both. Issues are on; none
  filed yet.
- The setup (AGENTS.md, CLAUDE.md, this file, `tools/verify.sh`,
  `tools/n64recomp.sh`, `.superset/`, `.githooks/pre-commit`, `coop-ci.yml`,
  `docs/coop/building.md`, the README notice) is on branch
  `coop/setup-workflow`, draft PR #1.
- Verify on `coop/setup-workflow`, 2026-10-18, no ROM: 5/5 stages passed,
  `recomp` skipped, `build` compile only (549 objects, not linked).
- Local git config of the clone: Claude identity, `upstream` with push
  `DISABLED`, `core.hooksPath .githooks`, `pushguard.romDir` set to the
  owner's N64 ROM folder.

## Open pull requests

- #1 (draft), `coop/setup-workflow` → `bm64recomp-coop`: the workflow setup.
  Needs the owner's review and merge.

## Waiting for the owner

- **Actions on the fork.** GitHub lists no workflows on the fork yet: open
  the repository's Actions tab once and enable workflows. Then disable
  upstream's, which need a private ROM secret and would fail on every pull
  request:
  `for w in validate.yml validate-internal.yml validate-external.yml update-pr-artifacts.yml; do gh workflow disable $w -R Clodmony/BM64Recomp-coop; done`
  (a session can run this once the owner has enabled Actions).
- **The ROM.** Linking and running need the 1.0 US ROM; the owner's ROM
  folder has only the Europe release. To get a full build: a US dump,
  decompressed with [Bomberhackers/bm64](https://github.com/Bomberhackers/bm64),
  at `~/dev-scratch/BM64Recomp-coop/rom/bm64.decomp.us.z64`
  ([docs/coop/building.md](docs/coop/building.md)). Until then every
  session checks compile-only and says so.

## Next candidates (issues to file first)

1. **Co-op design** (`docs/coop/design.md`): what the Adventure mode
   assumes about one player (player object, camera, HUD, input, stage
   progress, saves), what Battle mode's four players already provide, and
   the decisions for online and split-screen co-op. First steps: the
   decompilation's player and controller code, `patches/` and the
   `[[patches.hook]]` entries in `bm64.us.toml`.
2. **Test harness**: run the game unattended in portable mode from scratch,
   drive input from a script, read game state, take captures that stay
   local. Needs the linked build (the ROM above).
3. **Session core** for online co-op: host and client, protocol, transport,
   logs, a development entry to start two instances on one machine.

## Working notes (stable, keep short)

- `gh`: pass `-R Clodmony/BM64Recomp-coop`, or run `.superset/setup.sh` once.
- A new worktree has no submodules; `.superset/setup.sh` fetches them.
- Patches: clang 16+ needs `-Wno-error=incompatible-pointer-types`, the host
  build needs `-include cstdint` with libstdc++ 15; `tools/verify.sh` passes
  both (docs/coop/building.md).

## Cleanup the owner may approve

- None yet. Scratch kept on purpose: `~/dev-scratch/BM64Recomp-coop/n64recomp/`
  (N64Recomp cache) and `build/` (incremental CMake folders).
