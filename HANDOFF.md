# Handoff

The session note for this repository. Read it first in a fresh session and
rewrite it at the end of every session (issue loop, last step). It lives in the
repository so it survives any worktree path and any tool; per-checkout agent
memory is not read by a session that starts in another worktree. State goes
here; stable recipes go into `docs/coop/`; personal data and hardware never.

Last updated 2026-10-18 (project setup; #1 merged).

## Repository state

- Fork of `RevoSucks/BM64Recomp`. `master` = upstream `master` (`726adc9`).
  Default branch `bm64recomp-coop`, created from `726adc9`. Ruleset
  "Protected" blocks force-pushes and deletion on both. Issues are on: #2
  (Europe ROM support) and #3 (story-mode single-player audit) are open.
- The setup (AGENTS.md, CLAUDE.md, this file, `tools/verify.sh`,
  `tools/n64recomp.sh`, `.superset/`, `.githooks/pre-commit`, `coop-ci.yml`,
  `docs/coop/building.md`, the README notice) is merged into
  `bm64recomp-coop` (#1, at the owner's word, 2026-10-18); its branch is
  deleted.
- Verify on `coop/setup-workflow`, 2026-10-18, no ROM: 5/5 stages passed,
  `recomp` skipped, `build` compile only (549 objects, not linked).
  `coop-ci` on #1 passed.
- Actions: only `coop-ci` is active. Upstream's `validate-internal`,
  `validate-external` and `update-pr-artifacts` are disabled (they need a
  private ROM secret; `validate.yml` is only called by them). Their failed
  runs on #1 from before the disabling can be ignored.
- Local git config of the clone: Claude identity, `upstream` with push
  `DISABLED`, `core.hooksPath .githooks`, `pushguard.romDir` set to the
  owner's N64 ROM folder.

## Open pull requests

None.

## Waiting for the owner

- **The ROM.** The recomp accepts only the 1.0 US ROM; the owner's ROM
  folder has only the Europe release. Either a US dump (decompressed with
  [Bomberhackers/bm64](https://github.com/Bomberhackers/bm64), at
  `~/dev-scratch/BM64Recomp-coop/rom/bm64.decomp.us.z64`), or #2 (Europe
  support) makes linking and running possible. Until then every session
  checks compile-only and says so.

## Next candidates

1. **Next: #3, story-mode audit** (owner, 2026-10-18: it decides the co-op
   approach and needs no linked build). Branch `coop/3-story-audit`. Start
   from the Europe disassembly in scratch (below): map overlays to worlds,
   follow how story code reaches `g_Players` (only main code references it
   directly, 258 times) and the game-state block `D_802AC5C0` (lives +0x64,
   credits +0x68, stage ID +0x16). Deliverable `docs/coop/design.md`.
2. **#2, Europe ROM support**: an unpacker in `tools/` (port of
   `scripts/unpack_eu.py` below), recompile from the Europe image, a
   `GameEntry` for it, then link, boot and check PAL speed. Unblocks running
   the game here.
3. Issues still to file once #3 has answers: the player-2 spike in a story
   stage, the **test harness** (unattended runs in portable mode, scripted
   input, state probes, local captures; needs a linked build), the
   **session core** for online co-op (lockstep: host and client, input
   transport, shared seed, desync detection).

## Scratch tooling (not in the repository)

Under `~/dev-scratch/BM64Recomp-coop/`, made 2026-10-18:

- `rom/bm64.decomp.eu.z64`: the Europe ROM unpacked like the US
  decompressed ROM (a ROM copy: never commit or upload it).
- `eu-disasm/<section>/`: spimdisasm output for all 98 code sections, named
  with the US symbols (`<section>.syms.txt` beside each).
- `scripts/unpack_eu.py`, `scripts/disasm_eu.py`,
  `scripts/us_overlay_table.txt` (the decompilation's overlay offsets):
  rerun with `python3 -I scripts/unpack_eu.py <rom> scripts/us_overlay_table.txt rom/bm64.decomp.eu.z64`, then
  `python3 -I scripts/disasm_eu.py rom/bm64.decomp.eu.z64 <checkout>/BM64Syms eu-disasm venv/bin/spimdisasm`.
- `venv/`: spimdisasm 1.42.4, rabbitizer 1.16.2.

## Working notes (stable, keep short)

- `gh`: pass `-R Clodmony/BM64Recomp-coop`, or run `.superset/setup.sh` once.
- A new worktree has no submodules; `.superset/setup.sh` fetches them.
- Patches: clang 16+ needs `-Wno-error=incompatible-pointer-types`, the host
  build needs `-include cstdint` with libstdc++ 15; `tools/verify.sh` passes
  both (docs/coop/building.md).

## Cleanup the owner may approve

- None yet. Scratch kept on purpose: `~/dev-scratch/BM64Recomp-coop/n64recomp/`
  (N64Recomp cache), `build/` (incremental CMake folders) and the Europe
  tooling above (until #2 and #3 are done).
