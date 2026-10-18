# Handoff

The session note for this repository. Read it first in a fresh session and
rewrite it at the end of every session (issue loop, last step). It lives in the
repository so it survives any worktree path and any tool; per-checkout agent
memory is not read by a session that starts in another worktree. State goes
here; stable recipes go into `docs/coop/`; personal data and hardware never.

Last updated 2026-10-19 (parallel session: #41–#45 merged; #11 open for the
owner's eye check).

## Repository state

- Fork of `RevoSucks/BM64Recomp`. `master` = upstream `master` (`726adc9`).
  Default branch `bm64recomp-coop`. Ruleset "Protected" blocks force-pushes
  and deletion on both.
- Merged into `bm64recomp-coop`: #1 (workflow setup), #5 (story-mode
  audit), and on 2026-10-18 the chain #12 (Europe build), #15 (harness,
  player 2 spike), #20 (group #19), #25 (session core), #30 (host's save,
  build check), #34 (camera), #35 (#32, the knocked-out heart), #37 (#36,
  heart items), #38 (#28, the launcher's Co-op menu), #40 (#33, cameras
  that turn), and on 2026-10-19 #41 (#29 in part), #42 (#39), #43 (#11 in
  part), #44 (#23), #45 (#31). Their branches are deleted. No open PRs.
- Open issues: #14 and #16 (leftover checks, owner), #19 (their group),
  #11 (Europe follow-ups), #22–#24 (follow-ups of #19), #29 (session
  follow-ups), #31 (scripted pads on the title), #39 (`session-launcher`
  misses the Connecting prompt now and then).
- Local git config of the clone: Claude identity, `upstream` with push
  `DISABLED`, `core.hooksPath .githooks`, `pushguard.romDir` set.

## Merged 2026-10-19 (owner's word, scripted Europe runs only)

Order #41 → #42 → #44 → #45 → #43 (one building.md conflict, resolved).
Merged branch verified before the push: `tools/verify.sh --full --version eu
--scenarios` 8/8 stages, 300/300 checks. Not played by hand; US build not run.

- **#41** (part of #29): Windows build cross-compiled on Linux, Wine in the
  harness, playtest kit, headless runs (`gamescope --backend headless`), run
  folders with a mktemp suffix (parallel runs started in the same second
  shared one folder and game.log).
- **#42** (closed #39): `session-launcher` checks the session's own
  `waiting on port` / `connecting to` lines.
- **#43** (part of #11): refresh-rate hint from the host
  (`coop_game_frame_rate`: 50 Europe, 60 with `BM64_COOP_PAL60=1`, 60 US),
  scenarios `europe-rate*`; per-frame patched functions read against the
  Europe originals (no differences beyond addresses); launcher text names the
  wanted version (compiled only); 50 Hz presentation judged by reading RT64
  (hint 50: 6 frames per 5 at 60).
- **#44** (closed #23): hook in `func_8023A870` at 0x8023A9AC →
  `coop_story_death`; player 2 frozen from player 1's story death until the
  retry's room init (`p2-p1-death-freeze`).
- **#45** (closed #31): no code change; the first 0x20 after boot is the
  logos, which always go on to the demo (0x1B); the real title is the second
  0x20. `title-menu` drives title → Adventure → file select (0x1F) → new
  file (0x1A).

Worktrees: only this Superset workspace (now on `bm64recomp-coop`) and the
main checkout. The issue worktrees and their build folders are removed.

## Owner decisions, parallel session (2026-10-19)

- Work several issues at once with one agent per worktree (owner's
  request); test runs headless (owner).
- Merge all five PRs (done). #11 stays open: the owner watches a Europe
  build at Manual 60 (smooth scrolling? artefacts from generated frames?);
  if worse, fall back to 60 or recommend "Original". Launcher text: reading
  is enough.
- #31's leftovers dropped: room 0x28's repeated A taps (likely the
  dialogue's timing) and a skip-intro switch (the scripted taps suffice).
- #46 filed: `scenario.py` crashes on one garbled game.log line.
- #44: player 2's own death while player 1 is out stays as it is (the game
  freezes the dying slot itself).

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

## Owner decisions, end of #32 (2026-10-18)

- Merge #35 now (done). Who may catch the other player's knocked-out heart:
  filed as #36 (run it first, then the owner decides: shared, or only the
  player who lost it).

## Owner decisions, #36 (2026-10-18)

- #36 next (chosen at the start of its session). Heart rule: shared, but a
  full player does not take it (not "only who lost it", not the game's
  rule as is).
- End of #36: PR #37 review first, merge later when the owner says so; the
  rule covers every heart item (the stage's too), kept; the one-actor
  collision case stays a note, no issue; old run folders deleted.

## Owner decisions, state review (2026-10-18)

- Asked whether a first session with a player 2 can be played. Answer given:
  only as a test setup (environment switches, one machine, scripted pads,
  Green Garden stage 1, Europe; never played by hand, never on two
  machines). Known gaps: no menu (#28), two machines untested (#29), the
  client's C-buttons (#33), player 2 appears at player 1's first step,
  player 1 downed (#22), no player 2 heart display, no local second
  controller (pad 1 is not mapped to a physical controller).
- **#28 next** (host or join from the launcher), chosen over a separate
  playtest issue, #29 and #33.
- `coop/36-heart-catch` deleted, and this workspace's build folders.

## Owner decisions, end of #28 (2026-10-18)

- Merge #38 now (done), on the scenario results; problems found by hand
  become new issues.
- Test runs keep background input off (`run.sh`'s `general.json`).
- A Cancel or refusal in the Co-op menu goes back to the menu (not: start
  alone).

## Owner decisions, after #33 (2026-10-18)

- "Make it ready for a playtest": Linux and Windows PCs on the same home
  network; the Windows build cross-compiled here (xwin's CRT and SDK, the
  owner accepted Microsoft's licence) and tested in Wine. Done in #41.

## What #29 (in part) found and did (details: building.md, *The Windows build*)

- `tools/build-windows.sh` (RelWithDebInfo, PDB) into `build/win-<version>/`;
  xwin's SDK in scratch `win/sdk`, the Wine prefix `win/prefix`. Patches from
  the Linux clang: one build identity for both builds.
- Linux and Windows (Wine) builds against each other, both directions:
  lockstep, camera-turn, camera, save, lost all pass with the same hashes on
  every frame (81/81 each way).
- Crash found: the harness's `setvbuf(_IOLBF, 0)` is an invalid parameter in
  the MS CRT; now unbuffered on Windows. No console: `coop.log` in the app
  folder (not run: Wine from a terminal has a console).
- Kit: `tools/playtest-kit.sh` into scratch `playtest/<version>/` (linux/,
  windows/, windows.zip; no ROM). Guide `docs/coop/playtest.md`.
- Still open in #29: a real Windows PC, two machines, the internet, macOS;
  whether a Windows build made by MSVC's clang (upstream CI, LLVM 19 for
  the patches) would match the Linux build's identity (likely not).

## Owner decisions, end of #33 (2026-10-18)

- #33 next (chosen at the start of its session). Merge #40 now (done);
  problems found by hand become new issues.
- #39: fix the check (the scenario looks for the session's own
  `coop: session client: connecting to` line); the brief prompt stays.
- Old verify logs in scratch deleted.

## What #33 found and did (details: design.md, *Cameras that turn*)

- Room 0x29's camera turns by the C-buttons too (C-left yaw 90 → 45,
  C-right 90 → 135, C-down/C-up distance 2400/2000). `func_80235934` read
  pad 0, the host's: the host turned both cameras, the client none.
- Hook in `func_80235934` at 0x80235978: pad 1 when the camera follows slot
  4 (`coop_camera_pad`, harness.cpp). Alone, it desynced at frame 458: the
  player update turns the stick by the camera's yaw and pitch
  (`func_80234858` at 0x8024E880, sp+0x9C/0xA0).
- Frame message protocol 3 (32 bytes) carries the sender's yaw and pitch;
  hook at 0x8024E888 (s0 = slot) puts in the driving machine's angles
  (`coop_session_view`, session.cpp). `session-camera-turn`: 672 frames the
  same, each player walks by its own camera on both machines.
- Harness: `#check camera PROBE yaw|pitch|distance OP VALUE`.
- Not traced: the other ~150 readers of the angles and 202 of the eye;
  rooms whose camera turns by itself.
- `D_8008FFF8` (set from the actor's facing after `D_80090004` frames) is an
  offset of the look-at point, not the yaw (read).

## What #28 found and did (details: design.md, *Hosting and joining*)

- The launcher's **Co-op** button opens a menu (`src/coop/session_ui.cpp`):
  Host on a port, or Join an address and port. It connects and exchanges the
  hellos on a thread while a prompt shows the wait (Cancel, no time limit),
  then starts the game; refused or cancelled, back to the menu. Connected,
  refused and lost show as prompts and log `coop: session ui: …`.
- Why the wait is in the launcher: once the game has started, RT64 presents
  only the game's frames, and a game held at its first pad read makes none
  (probe: the UI drew fewer than 60 frames in a 15 s wait).
- `session.cpp`: `configure_session`, `session_status`, `cancel_session`,
  `connect_session`; `start()` split into `connect_hello` and
  `exchange_save` (still at the first pad read).
- `BM64_COOP_SESSION` with run.sh's autostart now also connects in the
  launcher (notification, no Cancel) and then starts, alone if it failed.
- Harness: `BM64_COOP_LAUNCHER`, `BM64_COOP_CANCEL_AFTER`, scenario.py's
  `#launcher`; `run.sh` writes a `general.json` with background input off
  (a full verify once saw both games of `session-save` cancel their wait;
  a controller press fits, inferred).

## What #36 found and did (details: design.md, *The knocked-out heart*)

- Run, seed 1, room 0x29: either player catches the other's heart
  (`p2-heart-swap`). The pickup sets the catcher's hearts to 2, so a full
  player used the other's heart up.
- Fix: a hook in `func_80279CA8` at 0x80279CFC asks `coop_item_passes`
  (`patches/coop_player2.c`); a player a heart would give nothing, while
  player 2 plays, passes through it (`coop: pass`), the heart stays
  (`p2-heart-full`, red before the hook: 8/11). Every heart item, also the
  stage's. Not run: a full player standing on a heart with the partner on
  the same spot (the collision returns one actor).
- Harness: `coop: item from=… type=… pos=…` probe line (last knocked-out
  item while in the room), `hearts SLOT N` step, `coop: pass` event.
- A `tile` step on slot 0 is a room entry: player 2 is held frozen and
  damage-guarded until player 1's next step (now in building.md); walk
  player 1 instead.
- Probe and `tile` steps each take a frame: time a scenario's probes from a
  trace with the same steps.

## What #32 found and did (details: design.md, *What the runs showed*)

- Not a co-op bug. A hit that leaves hearts knocks one out as an item
  (`func_80248148` → `func_8027B39C(4, slot)`, 200 units up, direction
  `g_getRandomNumber(7)`); whoever touches it gets the heart back
  (`func_802797BC`, type 4). Seed 2's heart lands back on player 2 in
  `p2-slot4`, about 30–40 frames after the hit.
- Hooks log `coop: drop slot=S item=T` and `coop: pickup slot=S item=T`.
  `p2-slot4` pins seed 1 and checks no heart is picked up; `p2-heart`
  (seed 2) checks the recovery. A change that moves the random numbers
  fails those checks by name.
- Not run: player 1 catching player 2's heart (the code allows it; #36).
- Adding `probe` steps shifts a script's timing (a trace with a probe every
  5 frames reached the hit earlier in wait time than one probe did): time a
  new scenario's probes with a trace copy of it.

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

1. **The playtest** (owner): the kit in scratch `playtest/eu/`, guide in
   `PLAYTEST.md`; afterwards both `coop.log` files. What breaks becomes an
   issue (a desync line first).
2. **A hand try of the Co-op menu** (owner): two games on one machine, Host
   7777 in one, Join 127.0.0.1 7777 in the other, then turn each camera with
   the C-buttons (#33); what breaks becomes an issue.
3. **Other session follow-ups**: #29 (the rest) (two machines, Windows/macOS; also
   whether two compilers give the same build identity; whether two games on
   one machine read the same gamepad).
4. **Rest of #14 and #16** (kept open, owner): a story teleport
   (`func_802512C0` from a room script; find one with `xref/callers.tsv`
   beyond Green Garden stage 1) and a scene with player 2 already in the
   room, then a check that pad 1 does not move a frozen player 2.
5. Follow-ups of #19: #22 (player 1 downed), #23 (freeze during player 1's
   death animation), #24 (slot-0 resets, US run).
6. **#46** (scenario.py and a garbled log line), small harness fix.
7. **#22** needs the owner's choice of approach first (S moves to player 2,
   or the S code copes with a downed S); #24 overlaps #44's file and needs a
   US ROM for its US run.
8. With #45: scenarios through the file select, Custom screen and pause
   menu (the `title-menu` recipe in building.md).

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
- Runs are headless in gamescope (#41's `run.sh`); `--window` opens a window
  on the desktop. Owner: test runs stay headless.
- Parallel agents (one worktree each) work, given #41's `run.sh` (unique run
  folders) and separate build folders; session scenarios pick free ports.
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

- The Superset workspace `prepare-project-according-a3ceb80a` (now on
  `bm64recomp-coop`, nothing unmerged; its build folder
  `build/prepare-project-according-a3ceb80a-eu` in scratch): remove it in
  Superset.
- `eu-disasm/`, `xref/`, `scripts/`, `venv/`, `audit/`, `ref/` in scratch:
  when the player 2 follow-ups are done.
