# Handoff

The session note for this repository. Read it first in a fresh session and
rewrite it at the end of every session (issue loop, last step). It lives in the
repository so it survives any worktree path and any tool; per-checkout agent
memory is not read by a session that starts in another worktree. State goes
here; stable recipes go into `docs/coop/`; personal data and hardware never.

Last updated 2026-10-18 (#27 done; PRs #12, #15, #20, #25, #30 and #34
merged into `bm64recomp-coop`; no open pull requests).

## Repository state

- Fork of `RevoSucks/BM64Recomp`. `master` = upstream `master` (`726adc9`).
  Default branch `bm64recomp-coop`. Ruleset "Protected" blocks force-pushes
  and deletion on both.
- Merged into `bm64recomp-coop`: #1 (workflow setup), #5 (story-mode
  audit), and on 2026-10-18 the chain #12 (Europe build), #15 (harness,
  player 2 spike), #20 (group #19), #25 (session core), #30 (host's save,
  build check), #34 (camera). Their branches are deleted.
- Open issues: #14 and #16 (leftover checks, owner), #19 (their group),
  #11 (Europe follow-ups), #22–#24 (follow-ups of #19), #28, #29 (session
  follow-ups), #31 (scripted pads on the title), #32 (`p2-slot4` fails for
  some seeds), #33 (cameras that turn, the client's C-buttons).
- Local git config of the clone: Claude identity, `upstream` with push
  `DISABLED`, `core.hooksPath .githooks`, `pushguard.romDir` set.

## Open pull requests

None. Verify of the merged tree (= the #34 branch), 2026-10-18: Europe
`--full --scenarios`, 7/8 stages, 175/185 checks; the 10 failures are all
`p2-slot4` with an unlucky clock seed (#32). US not run (CI compiles it
without a ROM).

## Owner decisions (2026-10-18)

- End of #19: #14 and #16 stay open for their leftover checks; player 2
  joining only at player 1's first step is fine for now.
- End of #21: the lockstep sends the pads, not the command record
  (accepted; PR #25 closes #21). Filed: #26 the host's save and a build
  check, #27 each camera on its own player, #28 host or join from the
  launcher, #29 two machines and the Windows/macOS builds.
- #26 next (chosen at the start of its session). End of #26: PR #30 review
  first, merge later after #25; the client keeps playing on a copy of the
  host's save (not no-op saves); the build identity covering patches and
  hooks only (not host code, librecomp, mods) is enough for now, revisit
  with #29.
- #27 next (chosen at the start of its session). End of #27: merge the
  whole chain now (done, 2026-10-18); PR #34 closes #27, the turning
  cameras and the client's C-buttons are #33; #32 gets its own session to
  find the cause (not a pinned seed).

## What #27 found and did (details: design.md, *The camera*)

- A hook in `func_80235D7C` at 0x80235E28 swaps the followed slot (v0, S)
  for the machine's own while that player is in play
  (`patches/coop_camera.c`); `coop_camera_slot` in `src/coop/harness.cpp`
  picks it: a live session's client 4, else the game's; `BM64_COOP_CAMERA`
  overrides.
- Camera globals (low bss, same in US and Europe): eye `D_8008F3E8`, yaw,
  pitch and distance `D_8008F3F8[0..2]`; the look-at point is the actor +
  100 in y. `D_80090020` is not the look-at point.
- Open question 4: no feedback in room 0x29 (589 frames, same hashes; the
  angles stay 90/45/2000 there). Rooms whose camera turns and the client's
  C-buttons (pad 0 = host's) are #33.
- Harness: a `coop: camera` probe line, `nearer PROBE camera A B` check.

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

1. **Session follow-ups**: #33 (cameras that turn and the client's
   C-buttons; first: find a story room with a turning camera, run
   session-camera's moves there with same-hashes), #28 (host or join from the
   launcher; first: ui_launcher.cpp's Start path and coop::autostart), #29
   (two machines, Windows/macOS; also whether two compilers give the same
   build identity).
2. **#32**, `p2-slot4` fails for seed 2 (a hit of cause 1 takes no heart);
   a full verify fails with it for some clock seeds. Owner: find the cause.
   First: `BM64_COOP_SEED=1` and `=2` with `BM64_COOP_LOG_PLAYERS=1`,
   compare the frames around the hit.
3. **Rest of #14 and #16** (kept open, owner): a story teleport
   (`func_802512C0` from a room script; find one with `xref/callers.tsv`
   beyond Green Garden stage 1) and a scene with player 2 already in the
   room, then a check that pad 1 does not move a frozen player 2.
4. Follow-ups of #19: #22 (player 1 downed), #23 (freeze during player 1's
   death animation), #24 (slot-0 resets, US run).
5. **#11**, Europe follow-ups.
6. **#31**, scripted pads on the title (needed to test the file select,
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

- The Superset workspace this session ran in (its branch is merged and
  deleted; the checkout is on `bm64recomp-coop`) and its build folders
  `build/prepare-project-according-a3ceb80a{,-eu}/` in scratch.
- `eu-disasm/`, `xref/`, `scripts/`, `venv/`, `audit/`, `ref/` in scratch:
  when the player 2 follow-ups are done.
