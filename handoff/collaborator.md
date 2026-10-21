# Handoff: collaborator

The collaborator's own session note (see `HANDOFF.md`, *How the notes
work*). Only the collaborator's sessions rewrite it: session history, what
each issue found, decisions taken in those sessions, the next step. No
names, handles or hardware in it.

Last updated 2026-10-21 (#77: the owner's review points closed).

## State

- **#77's review points are done and pushed**, branch `coop/77-skip-intro`,
  draft pull request **#84**, the owner's review asked for again. Section
  *#77* below. Three commits beyond what the owner reviewed: the narrower
  button, the hint on screen, and the probe that proves the save file.
- **#72 (#71) and #73 (#67) are approved and stuck on conflicts.** Both are
  drafts, both were approved before #74 and #59 were merged, and both now
  conflict with `bm64recomp-coop`. Nobody has resolved them. Each review says
  exactly what the resolution must keep:
  - **#72** (`tools/scenario.py`): #59 wrapped `run_scenario` in `except
    Exception` so a crashing scenario becomes a failed check. With the lanes
    that guard has to wrap `future.result()` (or sit inside the submitted
    function), or one crash raises out of the pool and ends the whole run —
    #46's bug back. #59's "missing `game.log` is a failed check" has to
    survive the `said` rewrite too.
  - **#73** (`src/coop/session.cpp`, `tools/verify.sh`): #74 moved the
    session protocol to 5 and added a `harness` stage. Keep protocol 5 and
    both stages.
  - Both also conflict in `HANDOFF.md` and `handoff/collaborator.md`: keep
    both people's lines.
- **#65 (#63) and #68 (#29) are merged** (2026-10-20), so their rows are out
  of `HANDOFF.md` and `run.sh` picks its headless wrapper by itself. A branch
  cut from `bm64recomp-coop` no longer needs the local gamescope stand-in.
- **#88 (#75, the owner's) waits for the collaborator's review.** Not started.
- **#89 filed this session** (below).

## #77: what the review asked, and what was done (2026-10-21)

The owner approved the approach and requested changes for one thing: **no file
was current after the skip, and the game saves anyway.** The first version
started a new file with `func_8023DA04(0)` through the warp's step, which
never sets `D_802A1DB4`, so a stage clear would have saved into file -1 —
0x88 bytes below the EEPROM mirror (`func_802947AC`, 0x802947B0). The owner
suggested making the first empty file current before the step.

**It was solved the other way round, and that is stronger:** Z no longer acts
before a file has been picked. It acts in **one** state, a picked file's
opening cutscene (overlay `0x1A`), and its step is the input script's `scene`
step (`func_8023AE6C`) in the **running** session, not the warp's. So no new
file is ever started, the file the person picked in the file select is the
file that is played, and the -1 case cannot arise. The demo-loop flag went
with the demo cases: "am I in the intro?" is now one overlay id. The file
select and the Co-op menu are reached the normal way again, which the first
version took away.

**The check the review asked for could not be built as suggested:** the
harness's own `save` step calls `func_80293EA0(file)` before saving
(`coop_harness_save`), so it sets the very thing it would test. So the probe
says it instead — every `probe` step now logs `coop: file current=N` beside
its `coop: state` line, and `skip-intro` checks for `current=0` in the world
map. Measured:

- `skip-intro` (menus driven normally, Z in the cutscene): `coop: file
  current=0`. The picked file is still the save file after the skip.
- `skip-intro-room`, which warps in with `BM64_COOP_WARP` and no file:
  `coop: file current=-1`, before and after. **The owner's reading is right,
  and the gap is the file-less warp's** — filed as **#89**, with the two ways
  out it names (the warp picks a file, or the save refuses -1).

**PRESS Z TO SKIP on screen** (`patches/coop_intro_hint.c`, new), asked for by
the person running the session after trying the skip by hand: nobody can guess
the button. Drawn the way the co-op HUD draws its icons (#53) — the text
painted once into an RGBA5551 texture from a 5x7 font, white with a black
outline, on a draw object of the game's own pool (`func_8022A328`) at twice
that size. The host side decides when it shows, in the same place as the skip
(`in_story_intro()` and no online session), and only a *change* is passed to
the patch, the per-frame hook's new request bit 8. So it never shows in a
session, where the skip is refused anyway, and it writes no game state. The
free checks the slot is still ours (#86).

Red first for it: with the host side forced never to ask for the hint,
`skip-intro` scores **7/9**, both new `after` checks failing ("first pattern
not logged"); with it, 9/9.

The owner's two small points: `skip-intro-room` allowed **200** units where
the body claimed 0.0 — now **5**, as the other scenarios use (measured 0.0);
the missing blank line in `building.md` was already fixed by the commit before
the review arrived.

Also corrected: two comments named a `coop_skip_hint_on()` that does not
exist (the host side passes the change through `args[13]`), and one claimed
both machines of a session see the hint change together — they never see it
at all.

The three scenarios together: **21/21**. Docs: `building.md` gets *The hint on
screen* under the Z paragraph and the `coop: file current` line under the
`probe` step.

## Next step

- **Ask the owner for the review of #84** (done, asked again this session) and
  **resolve the conflicts on #72 and #73** so they can be merged — that is
  the next session's obvious work, and each review says what the resolution
  must keep (above). Merge only when the person running the session says so,
  locally with `git merge --no-ff`, never `gh pr merge`.
- **Review #88** (#75, the owner's): it waits for this side, and the owner's
  next playtest step waits on its merge.
- **#89** is open and unstarted, found this session.
- **#78 is still the next new issue of this machine**: a downed player plays
  no death animation. Why is already read: `coop_partner_death` returns 1 at
  `func_8023A870`'s entry while the partner lives, so flags 0x92, the death
  process `func_8023F26C` and the room's retry are all skipped, and the
  animation hangs on that path (#22, `docs/coop/design.md`, *The death
  chain*). `func_8023F26C` cannot simply be let run: in the story case it
  takes **slot 0's actor and slot 0** (0x8023F3A4-0x8023F3BC), so a player 2
  death would play and end as player 1's, with the retry behind it. The
  animation has to be driven on the dying actor itself; the open points are
  listed in #78. `p2-ghost` and `p1-ghost` are the scenarios to extend.
- **#81 and #83** are open and unstarted (#81 also needs a scripted way to
  clear a stage, which nothing has today).
- **#66** waits for a machine with both headless wrappers (the owner's).

## Machine notes

Setup facts that only hold on this machine, kept general (for example "WSL2:
the game window opens through WSLg" or "WSL2: gamescope headless fails"),
never the hardware itself. Recipes that hold for everyone go into
`docs/coop/contributing.md` instead.

- WSL2 with WSLg supplies `DISPLAY`, `WAYLAND_DISPLAY` and
  `XDG_RUNTIME_DIR` to every shell, so the game's window needs no setup of
  its own and SDL picks its `x11` driver (through Xwayland).
- gamescope is not packaged for Ubuntu 24.04 (noble), with universe enabled.
  Since #63 `run.sh` picks Xvfb by itself. The local stand-in named
  `gamescope` in `~/.local/bin` is still there and still on `PATH` in these
  sessions, which is why run lines say `display: gamescope` while the runs
  are really Xvfb; removing it is the honest setup now.
- **`xdotool` is installed** (apt, the person approved it for #77). It is how
  a real key press can be tested, which no scenario can do: start
  `Xvfb :N -screen 0 640x480x24`, run the game with `DISPLAY=:N` and
  `run.sh --window`, then `xdotool search --onlyvisible --name Bomberman`,
  `xdotool windowfocus <id>` and `xdotool key F9`. It worked without a window
  manager (the window is named "Bomberman 64: Recompiled"); `windowactivate`
  would not, it needs EWMH. A **pad** button needs none of this: an input
  script presses it (`pad 0 Z`), which is why #77 ended up reading the pads.
- **The agent session runs on the Windows side** and drives WSL2 through
  `wsl.exe`. What a later session should know:
  - The WSL checkout is reachable from Windows as
    `//wsl.localhost/<distro>/home/<user>/BM64Recomp-coop/…`, so the Read,
    Edit and Write tools work on it directly. That is far more reliable than
    patching files through a here-document (a backslash gets mangled on the
    way through `wsl.exe`). **But a file written that way loses its
    executable bit**: after editing a script, `chmod +x` it in WSL and check
    `git diff --summary` for a mode change. A file the tools cannot overwrite
    because this session has not read it can be written to the Windows
    scratchpad and copied in with `cp`.
  - **Write the script to a file and pass its path** —
    `wsl.exe -- bash /mnt/c/…/script.sh` — rather than feeding it on stdin:
    the quoting survives and nothing is cut short. From a Git Bash tool call
    this needs **`MSYS_NO_PATHCONV=1`**, or Git Bash rewrites `/mnt/c/…` into
    a Windows path before `wsl.exe` ever sees it and the script "does not
    exist". Set `WSL_UTF8=1`, or `wsl.exe` writes UTF-16.
  - A non-login `bash` has no `~/.local/bin` on `PATH`, which hides
    `clang-cl`, `xwin` and the gamescope stand-in: export it in the script.
  - A long run (a verify) stays in the **foreground** of the `wsl.exe` call,
    held open by the agent's own background mode; `nohup … &` inside the call
    dies with it. Stopping that background task from the Windows side **does**
    take the Linux side down with it: after one such stop, `pgrep` for
    `verify.sh`, `BM64Recompiled`, `scenario.py` and `Xvfb` found nothing
    left. Worth checking anyway before starting the next run.
- The Windows cross build on this machine: `xwin` from a release binary in
  `~/.local/bin`, its splat in `~/dev-scratch/BM64Recomp-coop/win/sdk`
  (630 MB, the download cache beside it), `clang-cl` a symlink to clang 18,
  `wine64` from apt (it warns about a missing wine32; the 64-bit build does
  not care). A full cross build is about 2350 objects and takes a while; it
  is incremental afterwards.
- A native Windows run needs no WSL at all: a portable folder on the Windows
  side with the `.exe`, the three DLLs, `assets/`, `recompcontrollerdb.txt`,
  `portable.txt` and the ROM as `bm64_eu.z64`, started with the same
  environment variables `tools/run.sh` would set. Two such folders make a
  session over 127.0.0.1. A window opens — Windows has no headless wrapper
  here — so keep such runs short.
- Ubuntu 24.04 packages `gh` 2.45, old enough that some commands fail on a
  deprecated GraphQL field: `gh pr edit --add-reviewer` dies with
  "Projects (classic) is being deprecated ... (repository.pullRequest.projectCards)"
  and changes nothing. The REST call works:
  `gh api -X POST repos/<owner>/<repo>/pulls/<N>/requested_reviewers -f 'reviewers[]=<login>'`.
  Installing `gh` from GitHub's own apt repository would avoid it.
- WSL2's network to the Windows side (#29): the Windows side reaches a game
  listening inside WSL2 at `127.0.0.1` (WSL2's localhost relay), and the WSL2
  side reaches a game listening on Windows at WSL2's default route
  (`ip route | grep default`). The second direction needed no firewall rule
  of its own here; a host on a real LAN is a different question. Both
  directions played a session.
- PowerShell's `Start-Process -ArgumentList` refuses an **empty string** in
  the list, so an optional argument has to be appended only when it has a
  value, not passed as `""`.
- Audio: the WSL2 runs log skips (91 over a 104 s run, queue 65-82 ms), the
  native Windows runs of the same commit log none. So the skips are not the
  game itself; what they are is #66's question.

## Scratch on this machine

- `~/dev-scratch/BM64Recomp-coop/i77/`: **deleted** with #84 open, as the
  project asks; its pull request body and the #89 issue text are on GitHub.
- `wt/77-skip-intro/`: #77's worktree, with its build folder
  `build/77-skip-intro-eu`. **Kept on purpose** until #84 is merged, so a
  further review point needs no rebuild; remove both afterwards.
- `win/` (the xwin splat, its download cache and the Wine prefix, 2.4 GB
  together): **keep**, or the next Windows build starts with the download
  again.
- On the **Windows side**, `dev-scratch/BM64Recomp-coop/win-play/`: the two
  portable folders (host and client, with the ROM), `win-session.ps1`,
  `cross-session.ps1` (either direction) and the pad scripts it hands each
  side. **Keep** — they are what a later session needs to play either pair
  again without building anything.
- `rom/`, `build/BM64Recomp-coop-eu/`, `build/win-eu/`, `n64recomp/`: keep.
  `build/win-77-eu/` is #77's Windows build; delete it with the worktree.
- `runs/`: emptied 2026-10-21 (1.9 GB of #77's test runs). `verify/`: only
  `20261021-192532` left, the one quoted in #84; the folders quoted in #72
  and #73 are gone, their numbers are in those pull requests.
