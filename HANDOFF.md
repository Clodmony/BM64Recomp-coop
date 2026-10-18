# Handoff

The session note for this repository. Read it first in a fresh session and
rewrite it at the end of every session (issue loop, last step). It lives in the
repository so it survives any worktree path and any tool; per-checkout agent
memory is not read by a session that starts in another worktree. State goes
here; stable recipes go into `docs/coop/`; personal data and hardware never.

Last updated 2026-10-18 (spike #4 done in draft PR #15, stacked on #12).

## Repository state

- Fork of `RevoSucks/BM64Recomp`. `master` = upstream `master` (`726adc9`).
  Default branch `bm64recomp-coop`. Ruleset "Protected" blocks force-pushes
  and deletion on both.
- Merged into `bm64recomp-coop`: #1 (workflow setup), #5 (closes #3, the
  story-mode audit, `docs/coop/design.md`).
- Open issues: #2 and #6–#10 (closed by PR #12 when merged), #4 (closed by
  PR #15), #11 (Europe follow-ups), #13 (slot 4's command record), #14
  (player 2's placement off the entry walk).
- Local git config of the clone: Claude identity, `upstream` with push
  `DISABLED`, `core.hooksPath .githooks`, `pushguard.romDir` set.

## Open pull requests

- **#12** `coop/2-europe-build` → `bm64recomp-coop`, draft: the Europe
  build. The owner reviews it first; merge only when they say so.
- **#15** `coop/4-player2-spike` → `bm64recomp-coop`, draft, **stacked on
  #12** (branched from it; its diff shows #12's commits until #12 is
  merged). The test harness (scripted pads, warp into a room, probes,
  scenarios with checks, `verify.sh --scenarios`) and the player 2 spike.
  Verify 2026-10-18: Europe `--full --scenarios` 8/8 stages, 43/43 scenario
  checks; US compile only 5/5. Merge #12 first; merged afterwards, #15
  brings only its own commits.

## Owner decisions (2026-10-18)

- **Scope: online co-op only for now.** Local couch co-op is dropped for now.
- Player 2 in **slot 4** via the command record and shared lives are
  **proposals**. The spike confirmed slot 4 spawns, renders, bombs, takes
  hits and dies like a player; its input through the record is not right
  yet (#13).
- **Issue groups are allowed** (AGENTS.md, *Workflow*).
- #4 stacked on #12 instead of waiting for the merge (2026-10-18).

## What the spike found (details: design.md, *The follow-up spike*)

- Works: spawn/respawn of slot 1 or 4 from the room's own init, model 0x4B,
  pad 1 drives player 2 independently of pad 0, bombs and hits, death runs
  the story death path (room retry, one life gone), cutscenes lock player 1
  only. Hooks in main functions are reached from overlay calls.
- Slot 1 (pad 1) moves camera-relative like player 1; slot 4 through the
  command record moves along one world axis (#13).
- Player 2 on player 1's entry walk locks player 1 (#14); scenarios place
  player 2 with `BM64_COOP_P2_TILE=4,3` in room 0x29.

## Next candidates

1. **#13**, slot 4's command record (needed before online input goes
   through it). First step: compare how `func_8024E088` uses the vector for
   controlType 1 and 2; scenario `p2-slot4` has the checks to turn around.
2. **#14**, placement off the entry walk.
3. **#11**, Europe follow-ups.
4. Still to file: the **session core** for online co-op (lockstep, host and
   client, input transport; design.md proposes the command record, shared
   seed, desync detection). Both players need the same version.

## Scratch (not in the repository)

Under `~/dev-scratch/BM64Recomp-coop/`:

- `rom/`: decompressed and .z64 Europe ROM (never commit or upload). Kept.
- `build/<checkout>/`, `build/<checkout>-eu/`, `n64recomp/`: kept.
- `runs/`: run folders of `tools/run.sh` and the scenarios (each holds a ROM
  copy); delete freely.
- `verify/`: verify logs; delete freely.
- `eu-disasm/`, `xref/`, `scripts/`, `venv/`, `audit/`, `ref/`: the
  disassembly, cross-references and notes from #3, used again in #4 and
  useful for #13 and #14 (`scripts/fn.sh <section> <function>` prints one
  function).

## Working notes (stable, keep short)

- `gh`: pass `-R Clodmony/BM64Recomp-coop`, or run `.superset/setup.sh` once.
- A new worktree has no submodules; `.superset/setup.sh` fetches them.
- Patches: clang 16+ needs `-Wno-error=incompatible-pointer-types`, the host
  build needs `-include cstdint` with libstdc++ 15; `tools/verify.sh` passes
  both (docs/coop/building.md).
- A shell without a session lacks `XDG_RUNTIME_DIR`; `tools/run.sh` fills it
  in, or the game cannot open its window.
- The game window opens on the desktop during a run (no headless mode);
  `tools/run.sh` mutes sound by default. Screenshots: capture the game's
  window only, never the active window (it may be another one).
- In recompiled code `$ra` is 0 at a function's entry: a hook cannot tell
  its caller from it.
- Hook text runs inside the recompiled C function: no `stderr`/`printf`
  there, call a host function (`src/coop/harness.cpp`); `return;` at an
  entry hook skips the function.

## Cleanup the owner may approve

- After #12 and #15 are merged: delete `coop/2-europe-build` and
  `coop/4-player2-spike`.
- `eu-disasm/`, `xref/`, `scripts/`, `venv/`, `audit/`, `ref/` in scratch:
  proposed to keep until #13 and #14 are done.
