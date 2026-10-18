# Handoff

The session note for this repository. Read it first in a fresh session and
rewrite it at the end of every session (issue loop, last step). It lives in the
repository so it survives any worktree path and any tool; per-checkout agent
memory is not read by a session that starts in another worktree. State goes
here; stable recipes go into `docs/coop/`; personal data and hardware never.

Last updated 2026-10-18 (Europe group #2, #6–#10 done in draft PR #12).

## Repository state

- Fork of `RevoSucks/BM64Recomp`. `master` = upstream `master` (`726adc9`).
  Default branch `bm64recomp-coop`. Ruleset "Protected" blocks force-pushes
  and deletion on both.
- Merged into `bm64recomp-coop`: #1 (workflow setup), #5 (closes #3, the
  story-mode audit, `docs/coop/design.md`).
- Open issues: #2 and #6–#10 (closed by PR #12 when merged), #4 (spike:
  player 2 in a story stage), #11 (Europe follow-ups).
- Local git config of the clone: Claude identity, `upstream` with push
  `DISABLED`, `core.hooksPath .githooks`, `pushguard.romDir` set.

## Open pull requests

- **#12** `coop/2-europe-build` → `bm64recomp-coop`, draft. The Europe ROM
  builds, links and runs at PAL speed: `tools/unpack_rom.py`,
  `tools/europe.py` (Europe configs and their check on the image),
  `BM64_VERSION=us|eu`, `tools/verify.sh --version`, `tools/run.sh`, PAL
  pacing. Verify 2026-10-18: Europe `--full` 7/7 (linked), US 5/5 compile
  only (no US ROM). Needs the owner's review and merge.

## Owner decisions (2026-10-18)

- **Scope: online co-op only for now.** Local couch co-op is dropped for now.
- Player 2 in **slot 4** via the command record and shared lives are
  **proposals** until #4 confirms them on a build.
- **Issue groups are allowed**: a milestone of linked issues may be one
  session, one branch, one pull request (now in AGENTS.md, *Workflow*, via #12).

## Decisions made in #12 (the owner may overrule)

- Europe configs and symbol files are **generated** (`tools/europe.py
  configs`, gitignored `*.eu.toml`), not committed: the issue suggested a
  committed `data_dump.eu.toml` plus a sync check.
- Europe `GameEntry`: `game_id` `bm64_eu`, `mod_game_id` `bm64` (the mod menu
  hard-codes `bm64`).
- PAL speed fixed host-side by dropping 1 of 6 VI messages in the game's
  scheduler loop (hook added only to `bm64.eu.toml`); the screen still
  presents at 60 Hz.

## Next candidates

1. **#4, the spike**, now that a build runs: slot 1 in Green Garden room 0x28
   first, then slot 4 (model case, controlType 2, input via the command
   record). Steps in the issue and design.md. Use the Europe build
   (`tools/verify.sh --version eu`, `tools/run.sh --version eu`); design.md's
   addresses are US, Europe ones via `data_dump.eu.toml`. Getting into a
   story stage unattended needs scripted input, which does not exist yet:
   first step is probably a `--env` switch that feeds pad input (or a
   co-op patch that jumps to a stage), part of the test harness.
2. **#11**, Europe follow-ups (RT64 refresh-rate hint 60 vs 50, patched
   functions against their Europe originals, launcher text, 50 Hz
   presentation).
3. Issues still to file: the rest of the **test harness** (scripted input,
   state probes; `tools/run.sh` and the overlay/frame logs are its first
   pieces) and the **session core** for online co-op (lockstep, host and
   client, input transport; design.md proposes the command record, shared
   seed, desync detection). Both players need the same version.

## Scratch (not in the repository)

Under `~/dev-scratch/BM64Recomp-coop/`:

- `rom/`: `bm64.decomp.eu.z64` (decompressed image) and `bm64.eu.z64` (the
  ROM in .z64 order), made by `tools/unpack_rom.py`. ROM copies: never commit
  or upload. Kept.
- `build/<checkout>/`, `build/<checkout>-eu/`: CMake folders. `n64recomp/`:
  the pinned recompiler. Kept.
- `runs/`: run folders of `tools/run.sh` (each holds a ROM copy); delete
  freely.
- `eu-disasm/`, `xref/`, `scripts/`, `venv/`: the spimdisasm disassembly and
  cross-references from #3. `tools/europe.py` replaced the parts #7 needed
  (its own %hi/%lo scanner, no spimdisasm). `audit/`, `ref/`: the #3 notes
  and the decompilation clone, kept until #4 is done.

## Working notes (stable, keep short)

- `gh`: pass `-R Clodmony/BM64Recomp-coop`, or run `.superset/setup.sh` once.
- A new worktree has no submodules; `.superset/setup.sh` fetches them.
- Patches: clang 16+ needs `-Wno-error=incompatible-pointer-types`, the host
  build needs `-include cstdint` with libstdc++ 15; `tools/verify.sh` passes
  both (docs/coop/building.md).
- A shell without a session lacks `XDG_RUNTIME_DIR`; `tools/run.sh` fills it
  in, or the game cannot open its window.
- The game window opens on the desktop during a run (no headless mode);
  `tools/run.sh` mutes sound by default.

## Cleanup the owner may approve

- After #12 is merged: delete the branch `coop/2-europe-build`.
- `eu-disasm/`, `xref/`, `scripts/`, `venv/` in scratch: delete now (europe.py
  covers #7's needs) or keep for #4's address work. After #4: `audit/`, `ref/`.
