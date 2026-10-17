# Handoff

The session note for this repository. Read it first in a fresh session and
rewrite it at the end of every session (issue loop, last step). It lives in the
repository so it survives any worktree path and any tool; per-checkout agent
memory is not read by a session that starts in another worktree. State goes
here; stable recipes go into `docs/coop/`; personal data and hardware never.

Last updated 2026-10-18 (#3 story-mode audit merged as #5; next #2).

## Repository state

- Fork of `RevoSucks/BM64Recomp`. `master` = upstream `master` (`726adc9`).
  Default branch `bm64recomp-coop`. Ruleset "Protected" blocks force-pushes
  and deletion on both.
- Merged into `bm64recomp-coop`: #1 (workflow setup) and #5 (closes #3:
  `docs/coop/design.md`, the story-mode single-player audit and the co-op
  design). Branches of both deleted.
- Open issues: #2 (Europe ROM support), #4 (spike: player 2 in a story stage).
- Verify on `coop/3-story-audit`, 2026-10-18, no ROM: 5/5 stages passed,
  `recomp` skipped, `build` compile only (not linked). Docs-only change.
- Local git config of the clone: Claude identity, `upstream` with push
  `DISABLED`, `core.hooksPath .githooks`, `pushguard.romDir` set.

## Owner decisions (2026-10-18)

- **Scope: online co-op only for now.** Local couch co-op (split-screen or
  shared screen) is dropped for now; AGENTS.md, README.md and design.md say so.
- The design's other proposals (player 2 in **slot 4** driven through the AI
  command record `g_ComputerPlayerMovement`, shared lives with
  downed-and-respawn) are **proposals**, not decisions, until #4 confirms them
  on a build.
- Next issue: **#2**. Scratch for #3 kept until #4 is done.

## Open pull requests

None.

## Waiting for the owner

- **The ROM.** Linking and running need the decompressed 1.0 US ROM, or #2.
  The owner has only the Europe release, hence #2 next. Until a build links,
  every session checks compile-only and says so.

## Next candidates

1. **Next: #2, Europe ROM support.** Branch `coop/2-europe-rom`. Start from
   `~/dev-scratch/BM64Recomp-coop/scripts/unpack_eu.py` (port it into
   `tools/`). Then a Europe recompiler config next to `bm64.us.toml`, a
   `GameEntry` for it, link, boot, check PAL speed. **Address trap:** main
   bss is 0x10 lower in Europe than US (design.md, *Addresses*); upstream's
   patches name US bss addresses (`D_802AC610`, `D_802AC884`, …) and will
   hit the wrong variables on Europe. #2 must handle that (separate symbol
   file or per-version addresses in the patches). Consider moving the
   disassembly, stub-check and xref scripts into `tools/` with it (no game
   data in them).
2. **#4, the spike**, once a build runs: slot 1 in Green Garden room 0x28
   first, then slot 4 (model case, controlType 2, input via the command
   record). Steps and what to log are in the issue and design.md.
3. Issues still to file: the **test harness** (unattended runs in portable
   mode, scripted input, state probes, local captures; needs a linked build)
   and the **session core** for online co-op (lockstep, host and client,
   input transport: design.md proposes the command record, shared seed,
   desync detection; stage-local flags `D_8008E640` must stay equal).

## Scratch tooling (not in the repository)

Under `~/dev-scratch/BM64Recomp-coop/`:

- `rom/bm64.decomp.eu.z64`: the Europe ROM unpacked (a ROM copy: never commit
  or upload it).
- `eu-disasm/<section>/`: spimdisasm output of all 98 sections with US
  symbols; overlay call stubs named `sc_<target>`. Regenerate:
  `python3 -I scripts/disasm_eu.py rom/bm64.decomp.eu.z64 <checkout>/BM64Syms eu-disasm venv/bin/spimdisasm`.
- `xref/funcs.tsv`, `xref/callers.tsv`: per-function calls (every stub
  resolved from its encoding) and globals. `python3 -I scripts/xref.py eu-disasm <checkout>/BM64Syms xref`.
- `scripts/`: `unpack_eu.py`, `disasm_eu.py`, `check_stubs.py` (stub table
  against the ROM), `xref.py`, `reach.py` (who touches a global),
  `fn.sh <section> <func>` (print one function), `check_doc_syms.py` (every
  symbol a markdown page cites exists in BM64Syms; run it on design.md after
  edits).
- `audit/`: the four topic notes behind design.md (`overlays.md`,
  `players.md`, `state.md`, `world.md`, ~1500 lines with more addresses and
  evidence) and their helper scripts in `tmp-*/`. Kept until #4 is done.
- `ref/bm64/`: shallow clone of the decompilation. `venv/`: spimdisasm 1.42.4.

## Working notes (stable, keep short)

- `gh`: pass `-R Clodmony/BM64Recomp-coop`, or run `.superset/setup.sh` once.
- A new worktree has no submodules; `.superset/setup.sh` fetches them.
- Patches: clang 16+ needs `-Wno-error=incompatible-pointer-types`, the host
  build needs `-include cstdint` with libstdc++ 15; `tools/verify.sh` passes
  both (docs/coop/building.md).

## Cleanup the owner may approve

- After #4: delete `audit/` and `ref/` in scratch. After #2: decide whether
  `eu-disasm/`, `xref/` and the scripts stay (or move into `tools/`).
  Always kept: `n64recomp/`, `build/`, `rom/`.
