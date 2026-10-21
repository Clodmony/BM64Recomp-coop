# Handoff: collaborator

The collaborator's own session note (see `HANDOFF.md`, *How the notes
work*). Only the collaborator's sessions rewrite it: session history, what
each issue found, decisions taken in those sessions, the next step. No
names, handles or hardware in it.

Last updated 2026-10-22 (#78 in pull request #92; #88 reviewed; #91 filed).

## State

- **#78 is done**, branch `coop/78-death-anim`, draft pull request **#92**
  (`Closes #78`), the owner's review asked for. Section *#78* below.
- **#88 was reviewed** (approved, with four non-blocking points, section
  *#88*). The owner had already merged it by then, as a one-off exception
  without a review, so the points sit on the pull request and nothing acted
  on them; the first of them (nothing checks the enemy half of a snapshot)
  is worth an issue if nobody picks it up.
- **#91 filed** (2026-10-21): `session-hud` crashes one of its two games
  with SIGSEGV when the scenario suite runs in lanes. Not a fault of any
  branch - measured on `bm64recomp-coop` as well.
- #72, #73, #84, #65 and #68 are merged; nothing else of this side waits.

## #78: what it found and what the fix is (2026-10-22)

**The finding, measured.** A temporary probe (removed again) dumped the
dying actor and its model word by word, and a second one logged them every
frame after a death:

- a down (#22) changed only the look (model flags, alpha 0xFF → 0x80); the
  actor's state word stayed as it was, so no animation;
- a story death set that word 1 → 4 with a death leap, and **the player
  update did not run at all** between the death and the room's teardown
  (the hook at `func_8024E088` logged nothing there).

So the animation is not the update's: the death process `func_8023F26C`
plays it, and #22's hook never lets that process start. Its branch for
causes 0 and 1 (`func_8023EE40`, read in `RecompiledFuncs/`) is four writes
on the actor and its model, and those are what the fix does on the dying
player instead (`coop_death_anim`): `func_802313DC(actor, 0xC000000)`, clear
the model's bit 0x4000, `func_80229E38(obj, 0x14, 0)` and
`func_80229E38(obj, -1, 1)`.

**The rules for the open points of #78** (the person running the session,
2026-10-21): replay the visible part on the dying actor rather than run the
whole death process; **only the dying player is frozen**, the partner plays
on; **a fall (cause 3) gets no animation**.

**The one real trap:** the countdown must not hang on a player's update. A
first version ticked in `coop_partner_tick` (player 1's update), and the
update of a **frozen** player does not run: a frozen player 1 never came out
of its own animation and stayed frozen (`moved … 0.0 units` in `p1-ghost`
and `session-ghost`). It now ticks in the per-frame hook (`bm64.us.toml`,
`func_8023A208`) - which also means the hook text changed and the build
identity with it.

**Before and after.** The new checks against the build of `bm64recomp-coop`
(`1e8cb95`): **36/44**, with `ghost=1` and `alpha=128` already in the
death's frame. On the branch they are green, and the probe inside the
animation reads `ghost=0`, `alpha=255`, the slot not moving.
`tools/verify.sh --full --version eu --scenarios`: **10/10 stages, 597/597
checks, ALL PASSED** (log folder `verify/20261021-235113`).

**Not run: no window.** This machine has no screenshot tool and the person
declined the install, so the pull request says plainly that the animation is
*set* and *timed*, not that it was *seen*. The eye check is open; the
natural place for it is the next playtest.

Six scenarios moved with the 60 frames: `p2-ghost` and `p1-ghost` get a
`dying` probe inside the animation; `enemy-ghost`, `session-ghost`,
`session-hud` and `p2-slot4` wait it out - the two session ones on both
sides, and a `#client wait` is easy to forget (it cost one run here).

## #88 (the owner's): the review this side gave (2026-10-21)

Approved. The random-generator fix is right (`g_RandomIndex` is US
`0x802ACFE0` and Europe `0x802ACFD0`, and the patch names the symbol, so
Europe follows), the 122 words line up in the struct, the hook and
`session.cpp`, the hash map is pruned after 600 frames and the frame
message's size is checked on receipt. Four non-blocking points, all on the
pull request:

1. **Nothing checks the enemy half of a snapshot** - `coop_entry_record`
   could return -1 for every entry and every check would still pass. Probed
   by hand on a merge of the branch: 37 `coop: snapshot … enemy rec=` lines
   in a 45 s run, so it works today; one scenario check would keep it so.
2. The 122-word contract lives in three places and a mismatch truncates
   silently (`std::min`).
3. `compare_snapshots` has no test, and `verify.sh` never runs
   `compare_logs.py`.
4. The enemy offsets are now written twice (`coop_enemy.c` and
   `coop_session.c`).

## #91: the crash under the lane suite (2026-10-21)

`session-hud` crashes one of its two games with **SIGSEGV** when all 50
scenarios run in lanes; alone it is green every time. Measured here:
`bm64recomp-coop` 1 crash in 3 full-suite runs, a local merge of #88 2 in 4.
The two crashes sat at different points, and `game.log` ends inside the
game's own file loading with no line of ours after it. It costs a session a
rerun to tell a real failure from this one. Open: a backtrace (no core dump
or gdb wrapper yet), and whether a lower `--jobs` avoids it.

## Next step

- **Ask the owner to review #92** (requested); merge only when the person
  says so, locally with `git merge --no-ff`, never `gh pr merge`.
- **The eye check of #78's animation**, at the next playtest or with a
  screenshot tool on this machine (`imagemagick`, an apt install the person
  would have to approve).
- Open and unstarted on this side: **#89** (the file-less warp leaves the
  save file at -1; waits for the owner's choice between the two ways out),
  **#81** (player 2 does not dance after a stage clear; also needs a
  scripted way to clear a stage), **#83** (the intro skip inside a session),
  **#91**, and the #88 review points above.
- **#66** still waits for a machine with both headless wrappers (the
  owner's).

## Machine notes

Setup facts that only hold on this machine, kept general (for example "WSL2:
the game window opens through WSLg" or "WSL2: gamescope headless fails"),
never the hardware itself. Recipes that hold for everyone go into
`docs/coop/contributing.md` instead.

- WSL2 with WSLg supplies `DISPLAY`, `WAYLAND_DISPLAY` and
  `XDG_RUNTIME_DIR` to every shell, so the game's window needs no setup of
  its own and SDL picks its `x11` driver (through Xwayland).
- gamescope is not packaged for Ubuntu 24.04 (noble). Since #63 `run.sh`
  picks Xvfb by itself. The local stand-in named `gamescope` in
  `~/.local/bin` is still there and still on `PATH` in these sessions, which
  is why run lines say `display: gamescope` while the runs are really Xvfb;
  removing it is the honest setup now.
- **No screenshot tool is installed** (no `import`, `convert`, `xwd` or
  `ffmpeg`), so nothing here can look at the game's picture. An eye check
  needs `imagemagick` (or `x11-apps`) first.
- **`xdotool` is installed** (apt, the person approved it for #77). It is how
  a real key press can be tested, which no scenario can do: start
  `Xvfb :N -screen 0 640x480x24`, run the game with `DISPLAY=:N` and
  `run.sh --window`, then `xdotool search --onlyvisible --name Bomberman`,
  `xdotool windowfocus <id>` and `xdotool key F9`.
- **Reading the game's own code without a disassembly:** `RecompiledFuncs/`
  in a built checkout holds every function as C with its MIPS line as a
  comment, which is enough to follow a function and find the calls it makes
  (that is how #78 found `func_8023EE40`). The decompilation
  (Bomberhackers/bm64) does **not** help: its `src/` holds no main code,
  only libultra and the loader, and its names are `func_…` as well.
- **The agent session runs on the Windows side** and drives WSL2 through
  `wsl.exe`. What a later session should know:
  - The WSL checkout is reachable from Windows as
    `//wsl.localhost/<distro>/home/<user>/BM64Recomp-coop/…`, so the Read,
    Edit and Write tools work on it directly. A file written that way loses
    its executable bit: `chmod +x` a script afterwards and check
    `git diff --summary`.
  - **Write the script to a file and pass its path**
    (`wsl.exe -- bash /mnt/c/…/script.sh`) rather than a long `bash -lc '…'`:
    the quoting survives. From a Git Bash tool call this needs
    **`MSYS_NO_PATHCONV=1`**, or the `/mnt/c/…` path is rewritten before
    `wsl.exe` sees it; set `WSL_UTF8=1`, or `wsl.exe` writes UTF-16. A
    `bash -lc` whose text holds `$PATH` or a bracket can die on the Windows
    `PATH` Git Bash exports - another reason for a script file.
  - A non-login `bash` has no `~/.local/bin` on `PATH`.
  - A long run (a verify) stays in the **foreground** of the `wsl.exe` call,
    held open by the agent's own background mode; `nohup … &` inside the call
    dies with it.
  - A background call whose output goes through `| tail` prints nothing
    until it ends: read the run's own log or `verify/<stamp>/summary.txt`
    instead of waiting on the pipe.
- A native Windows run needs no WSL at all: a portable folder on the Windows
  side with the `.exe`, the three DLLs, `assets/`, `recompcontrollerdb.txt`,
  `portable.txt` and the ROM as `bm64_eu.z64`. Two such folders make a
  session over 127.0.0.1. A window opens - Windows has no headless wrapper
  here - so keep such runs short.
- Ubuntu 24.04 packages `gh` 2.45: `gh pr edit --add-reviewer` dies on a
  deprecated GraphQL field. The REST call works:
  `gh api -X POST repos/<owner>/<repo>/pulls/<N>/requested_reviewers -f 'reviewers[]=<login>'`.
- WSL2's network to the Windows side (#29): the Windows side reaches a game
  listening inside WSL2 at `127.0.0.1`, and the WSL2 side reaches a game
  listening on Windows at WSL2's default route (`ip route | grep default`).
- Audio: the WSL2 runs log skips (91 over a 104 s run), the native Windows
  runs of the same commit log none - #66's question.

## Scratch on this machine

- `~/dev-scratch/BM64Recomp-coop/i78/`: #78's traces and suite logs. Delete
  with the pull request open, as the project asks (its numbers are in #92).
- `wt/78-death-anim/` with `build/78-death-anim-eu`: #78's worktree. Keep
  until #92 is merged, so a review point needs no rebuild.
- `wt/77-skip-intro/`: **now holds the local review merge of #88** (branch
  `review/88`, never pushed), which is what the review's verify ran on. #84
  is merged, so this worktree and `build/77-skip-intro-eu` can go once the
  #88 points are settled.
- `ref/bm64`: a shallow clone of the decompilation, pulled for #78 and of no
  use (see *Machine notes*). Delete it.
- `win/` (the xwin splat, its cache and the Wine prefix, 2.4 GB): keep, or
  the next Windows build starts with the download again.
- On the **Windows side**, `dev-scratch/BM64Recomp-coop/win-play/`: the two
  portable folders with the ROM and the session scripts. Keep.
- `rom/`, `build/BM64Recomp-coop-eu/`, `build/win-eu/`, `n64recomp/`: keep.
  `build/win-77-eu/` goes with the `77-skip-intro` worktree.
- `runs/` and `verify/`: delete freely, except `verify/20261021-235113`
  (quoted in #92) until it is merged.
