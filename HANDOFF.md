# Handoff

The session note for this repository. Read it first in a fresh session and
rewrite it at the end of every session (issue loop, last step). It lives in the
repository so it survives any worktree path and any tool; per-checkout agent
memory is not read by a session that starts in another worktree. State goes
here; stable recipes go into `docs/coop/`; personal data and hardware never.

Last updated 2026-10-18 (group #19 done in draft PR #20, stacked on #15
and #12).

## Repository state

- Fork of `RevoSucks/BM64Recomp`. `master` = upstream `master` (`726adc9`).
  Default branch `bm64recomp-coop`. Ruleset "Protected" blocks force-pushes
  and deletion on both.
- Merged into `bm64recomp-coop`: #1 (workflow setup), #5 (closes #3, the
  story-mode audit, `docs/coop/design.md`).
- Open issues: #2 and #6–#10 (closed by PR #12 when merged), #4 (closed by
  PR #15), #13, #17, #18 (closed by PR #20), #14 and #16 (partly done in
  PR #20, see below), #19 (the group), #11 (Europe follow-ups).
- Local git config of the clone: Claude identity, `upstream` with push
  `DISABLED`, `core.hooksPath .githooks`, `pushguard.romDir` set.

## Open pull requests (merge in this order)

- **#12** `coop/2-europe-build` → `bm64recomp-coop`, draft: the Europe build.
- **#15** `coop/4-player2-spike`, draft, stacked on #12: the test harness and
  the player 2 spike.
- **#20** `coop/19-player2-playable`, draft, stacked on #15: the group #19.
  Verify 2026-10-18: Europe `--full --scenarios`, all stages and all 7
  scenarios passed; US not run (compile-only check, see the PR).
- Owner (2026-10-18): review #12 and #15, merge later, when the owner says
  so. Each one merged shrinks the next one's diff to its own commits.

## What #19 found and did (details: design.md, *Player 2 in a story room*)

- #13 was a harness bug: hook register copies kept `f_odd` pointing at the
  original context, so game double arithmetic on the copy went wrong. Every
  copy sets `c.f_odd` now (building.md, *The test harness*). The command
  record = the pad path's angle (before the camera's yaw), elevation, length,
  buttons.
- #14: player 2 is not spawned by the room; it joins on the tile player 1
  leaves with its first own step (60 frames after a walk or freeze). Spawned
  mid-room, an actor is reset to the origin on its first frames, so it is
  placed again 3 frames later. Origin waiting and the tile behind room 0x29's
  entry walk both killed player 2 (cause 3, flag 0x40000000).
- #16: freeze wrappers apply to player 2; #17: rectangle tests (RECOMP_PATCH)
  accept player 2; #18: player 2 is downed and respawns for a shared life.
- Harness: `tile SLOT X Z` step, `near` and `not-after` checks; patches in
  `patches/coop_player2.c`, `coop_harness.c`, `coop.h`.

## Next candidates

1. **Rest of #14 and #16** (if the owner keeps them open): a story teleport
   (`func_802512C0` from a room script; find one with
   `xref/callers.tsv` beyond Green Garden stage 1) and a scene with player 2
   already in the room (room 0x28's later tutorial, or a boss intro), then a
   check that pad 1 does not move a frozen player 2.
2. **The session core** for online co-op (still to file): lockstep, host and
   client, input transport through the command record (#13 settled its
   format), shared seed, desync detection. Both players need the same
   version.
3. Follow-ups from #19 to file (owner to decide): player 1 downed while
   player 2 plays; player 2 frozen during player 1's death animation; the
   slot-0 resets (remote bombs, continue) for player 2; player 2 visible only
   after player 1's first step; a US run of the scenarios.
4. **#11**, Europe follow-ups.

## Scratch (not in the repository)

Under `~/dev-scratch/BM64Recomp-coop/`:

- `rom/`: decompressed and .z64 Europe ROM (never commit or upload). Kept.
- `build/<checkout>/`, `build/<checkout>-eu/`, `n64recomp/`: kept.
- `runs/`, `verify/`: run and verify logs; delete freely.
- `eu-disasm/`, `xref/`, `scripts/`, `venv/`, `audit/`, `ref/`: the
  disassembly, cross-references and notes; `scripts/fn.sh <section>
  <function>`, run from the scratch folder, prints one function. Kept for the
  follow-ups unless the owner says otherwise.

## Working notes (stable, keep short)

- `gh`: pass `-R Clodmony/BM64Recomp-coop`, or run `.superset/setup.sh` once.
- A new worktree has no submodules; `.superset/setup.sh` fetches them.
- Patches: clang 16+ needs `-Wno-error=incompatible-pointer-types`, the host
  build needs `-include cstdint` with libstdc++ 15; `tools/verify.sh` passes
  both (docs/coop/building.md).
- A shell without a session lacks `XDG_RUNTIME_DIR`; `tools/run.sh` fills it
  in, or the game cannot open its window.
- The game window opens on the desktop during a run (no headless mode); it is
  a native Wayland window, so `xprop`/`import` cannot capture it alone.
- In recompiled code `$ra` is 0 at a function's entry: a hook cannot tell
  its caller from it. Hook text runs inside the recompiled C function: no
  `stderr`/`printf` there, call a host function; `return;` at an entry hook
  skips the function. Hook text must not hold US data addresses (the Europe
  config copies it): read them from registers or in the patches.
- Room 0x28's opening dialogue needs repeated A taps (40 × 4 frames held,
  16 apart, `p2-room28`); START there goes back to the title.
- One full `tools/verify.sh --full --version eu` takes about 3 minutes when
  only patches and hooks changed; a scenario 10–30 s.

## Cleanup the owner may approve

- After #12, #15 and #20 are merged: delete their branches.
- `eu-disasm/`, `xref/`, `scripts/`, `venv/`, `audit/`, `ref/` in scratch:
  when the player 2 follow-ups are done.
