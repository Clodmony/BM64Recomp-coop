# Handoff

The session note for this repository. Read it first in a fresh session and
rewrite it at the end of every session (issue loop, last step). It lives in the
repository so it survives any worktree path and any tool; per-checkout agent
memory is not read by a session that starts in another worktree. State goes
here; stable recipes go into `docs/coop/`; personal data and hardware never.

Last updated 2026-10-18 (#26, the host's save and the build check, in draft
PR #30, stacked on #25, #20, #15 and #12).

## Repository state

- Fork of `RevoSucks/BM64Recomp`. `master` = upstream `master` (`726adc9`).
  Default branch `bm64recomp-coop`. Ruleset "Protected" blocks force-pushes
  and deletion on both.
- Merged into `bm64recomp-coop`: #1 (workflow setup), #5 (closes #3, the
  story-mode audit, `docs/coop/design.md`).
- Open issues: #2 and #6–#10 (closed by PR #12 when merged), #4 (closed by
  PR #15), #13, #17, #18 (closed by PR #20), #14 and #16 (partly done in
  PR #20, kept open for leftover checks), #19 (the group), #21 (closed by
  PR #25), #11 (Europe follow-ups), #22–#24 (follow-ups of #19), #26 (closed
  by PR #30), #27–#29 (follow-ups of #21), #31 (scripted pads on the title).
- Local git config of the clone: Claude identity, `upstream` with push
  `DISABLED`, `core.hooksPath .githooks`, `pushguard.romDir` set.

## Open pull requests (merge in this order)

- **#12** `coop/2-europe-build` → `bm64recomp-coop`, draft: the Europe build.
- **#15** `coop/4-player2-spike`, draft, stacked on #12: the test harness and
  the player 2 spike.
- **#20** `coop/19-player2-playable`, draft, stacked on #15: the group #19.
- **#25** `coop/21-session-core`, draft, stacked on #20: the session core
  (#21). Verify 2026-10-18: Europe `--full --scenarios`, all 8 stages,
  145/145 checks (the 6 `p2-*` and 4 new `session-*` scenarios); US not run
  (CI compiles it without a ROM).
- **#30** `coop/26-host-save`, draft, stacked on #25: the client plays on
  the host's save, the hello checks the build (#26). Verify 2026-10-18:
  Europe `--full --scenarios`, all 8 stages, 167/167 checks (new:
  `save-file`, `session-save`, `session-build`); US not run.
- Owner (2026-10-18): review first, merge later in that order, when the
  owner says so. Each one merged shrinks the next one's diff.

## Owner decisions (2026-10-18)

- End of #19: #14 and #16 stay open for their leftover checks; player 2
  joining only at player 1's first step is fine for now.
- End of #21: the lockstep sends the pads, not the command record
  (accepted; PR #25 closes #21). Filed: #26 the host's save and a build
  check, #27 each camera on its own player, #28 host or join from the
  launcher, #29 two machines and the Windows/macOS builds.
- PRs: review first, merge later in order (12, 15, 20, 25, then 30).
- #26 next (chosen at the start of its session). End of #26: PR #30 review
  first, merge later after #25; the client keeps playing on a copy of the
  host's save (not no-op saves); the build identity covering patches and
  hooks only (not host code, librecomp, mods) is enough for now, revisit
  with #29.

## What #26 found and did (details: design.md, *The save and the build*)

- The game reads the EEPROM once, right after the first pad read
  (`func_80225840`: `func_8023A22C`, then `func_80294E54`), where the
  session connects. After the hellos the host sends its whole save file
  (0x20000 bytes); the client writes it to `saves/coop-session/<id>.bin`
  and switches librecomp to it (`ultramodern::change_save_file`). The
  client's own save is never written; both games run the same code.
- Hello protocol 2 with a build identity: FNV-1a of `mm_patches_bin` and a
  SHA-256 prefix of `bm64.<version>.toml` (configure time,
  `src/coop/version.cmake`). Not covered: host code, librecomp, mods.
- The state hash covers the save data (`D_80063000`, `D_8008E570`,
  `D_802A1DB4`).
- Harness: `save FILE` step, `BM64_COOP_WARP=ID:ENTRY:FILE`, `run.sh
  --saves`, `#saves-host`/`#saves-client`, `file`/`no-file` checks,
  `BM64_COOP_BUILD_ID`. Scripted START taps do not get past the title (#31).
- The #21 notes (lockstep on the pads, per-frame step, desync hash) are in
  design.md, *The session core*.

## Next candidates

1. **Session follow-ups**: #27 (the client's camera on player 2; open
   question 4; first: a per-machine camera target on the client, then
   session-lockstep with same-hashes), #28 (host or join from the launcher;
   first: ui_launcher.cpp's Start path and coop::autostart), #29 (two
   machines, Windows/macOS; also whether two compilers give the same build
   identity). #27 first: the client sees player 1's view today.
2. **Rest of #14 and #16** (kept open, owner): a story teleport
   (`func_802512C0` from a room script; find one with `xref/callers.tsv`
   beyond Green Garden stage 1) and a scene with player 2 already in the
   room, then a check that pad 1 does not move a frozen player 2.
3. Follow-ups of #19: #22 (player 1 downed), #23 (freeze during player 1's
   death animation), #24 (slot-0 resets, US run).
4. **#11**, Europe follow-ups.
5. **#31**, scripted pads on the title (needed to test the file select,
   Custom screen and pause menu through their own menus).

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
  only patches and hooks changed; a scenario 10–30 s. A hook text change
  needs the full pipeline (configs, ROM recompile): `cmake --build` alone
  links against the old hooks and fails.
- A session scenario opens two game windows at once.
- Never screenshot with `spectacle -a` (the active window): the game rarely
  has the focus, and it captures whatever does (happened once in #26; both
  captures deleted at once).
- `python3 -m py_compile` leaves `tools/__pycache__/` (now ignored); the
  push guard refuses a commit that holds one.

## Cleanup the owner may approve

- After #12, #15, #20, #25 and #30 are merged: delete their branches.
- `eu-disasm/`, `xref/`, `scripts/`, `venv/`, `audit/`, `ref/` in scratch:
  when the player 2 follow-ups are done.
