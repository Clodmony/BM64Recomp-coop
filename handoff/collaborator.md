# Handoff: collaborator

The collaborator's own session note (see `HANDOFF.md`, *How the notes
work*). Only the collaborator's sessions rewrite it: session history, what
each issue found, decisions taken in those sessions, the next step. No
names, handles or hardware in it.

Last updated 2026-10-20 (#63, pull request #65 open).

## State

- **#63 done**, branch `coop/63-run-wrapper`, **draft pull request #65**
  open into `bm64recomp-coop` (`Closes #63`), the owner's review requested.
  The fix is one commit (`a8ab437`): `tools/run.sh`,
  `docs/coop/building.md`, `docs/coop/contributing.md`.
- **#58 is done and merged** (pull request #64, merged by the owner
  2026-10-19 22:42 UTC). Its row is out of `HANDOFF.md`.
- The first session of #63 was stopped on usage before the pull request; the
  second one finished the verify run and opened it. `git status` is clean on
  the branch.

## #63: what it found and what the fix is

`tools/run.sh` hard-coded `gamescope --backend headless` for every headless
run. Reproduced on Ubuntu 24.04 in WSL2 (Europe build, `gamescope` off
`PATH`, no stand-in):

- `tools/run.sh --version eu --until-overlay 0x20` → exit 2,
  `run eu: game exited (status 127) after 0.2 s`, `game.log`:
  `env: 'gamescope': No such file or directory`.
- `tools/scenario.py --version eu tools/scenarios/europe-rate.txt` →
  `1/3 checks passed`, `RESULT FAILED`, so no scenario could run at all.

The fix: `run.sh` chooses the wrapper of its headless display — `gamescope
--backend headless` when `gamescope` is on `PATH` (the same command line as
before, so the owner's machine is unchanged), else `env -u WAYLAND_DISPLAY
xvfb-run -a -s "-screen 0 640x480x24"`, else exit 4 with a message naming
both packages. New `--wrapper gamescope|xvfb` forces one (how a machine with
both can compare them, the open question in the issue), and the run's
summary line names the display used: `display: gamescope`, `display: xvfb`
or `display: window`.

`-u WAYLAND_DISPLAY` is needed under Xvfb: with it SDL takes the desktop's
Wayland display (WSLg's) and opens a window there instead of drawing into
Xvfb.

After the fix, same machine and conditions: `run.sh` loads overlay 0x20
after 0.8 s with `display: xvfb`, and `europe-rate` scores `2/2 checks
passed, RESULT ok`. All six selection paths were run one by one: default
without gamescope → xvfb; default with a `gamescope` on `PATH` → gamescope;
`--wrapper xvfb` while gamescope is there → xvfb; `--wrapper gamescope`
without it → exit 4, `no gamescope on PATH`; neither on `PATH` → exit 4,
`no headless wrapper on PATH: install gamescope or xvfb (xvfb-run), or pass
--window`; `--wrapper foo` → exit 4, `--wrapper must be gamescope or xvfb`.

**Not run here:** real gamescope headless. This machine has no such package,
so the gamescope branch was exercised with a stand-in on `PATH` that proves
only the selection. Its command line is byte-for-byte the one before this
change.

`tools/verify.sh --full --version eu --scenarios` on the branch, with no
gamescope and no stand-in on `PATH`: **8/8 stages, 383/383 checks, RESULT:
ALL PASSED** (2026-10-20 18:01, log folder
`~/dev-scratch/BM64Recomp-coop/verify/20261020-180114/`). Every scenario ran
under Xvfb, the two-game `session-*` ones included — before the change the
scenario stage could not run at all here.

Docs on the branch: `building.md` (*Running the game*) gets the wrapper
choice, `--wrapper`, the `display:` field and the `WAYLAND_DISPLAY` reason;
`contributing.md` gets `xvfb` in the dependency list with its reason and
loses the stand-in workaround #63 made unnecessary.

## Next step

- **Ask the owner to merge #65** once they have approved it; either person
  may merge after the other's approving review (`HANDOFF.md`, *Project
  decisions*), locally with `git merge --no-ff`, never `gh pr merge`.
  Afterwards: delete the branch, remove the #63 row from *In progress* in
  `HANDOFF.md` and drop #63 from its issue list.
- **Next issue: #29, the Windows side** (collaborator, 2026-10-20): whether
  a native Windows build has the same build identity as the WSL2 one, since
  an online session refuses the join when they differ. The whole of #29 is
  wider (the internet, macOS, two games on one gamepad); take the Windows
  part first. The owner's group #56 (#46, #49, #51–#53, #55, #57) and its
  four lane pull requests #59–#62 are theirs, not ours.
- The question #63 left open — **whether gamescope and Xvfb runs are close
  enough to compare** (frame rate, audio skips) — is now **issue #66**
  (collaborator, 2026-10-20). Only a machine with both wrappers can answer
  it, so it waits for the owner's; the audio skips this machine logged stay
  an observation until then.
- **#65 stays a draft** and waits for the owner's review (collaborator,
  2026-10-20): no further nudge beyond the review request.

## Machine notes

Setup facts that only hold on this machine, kept general (for example "WSL2:
the game window opens through WSLg" or "WSL2: gamescope headless fails"),
never the hardware itself. Recipes that hold for everyone go into
`docs/coop/contributing.md` instead.

- WSL2 with WSLg supplies `DISPLAY`, `WAYLAND_DISPLAY` and
  `XDG_RUNTIME_DIR` to every shell, so the game's window needs no setup of
  its own and SDL picks its `x11` driver (through Xwayland).
- gamescope is not packaged for Ubuntu 24.04 (noble), with universe enabled.
  Xvfb is, and `run.sh` uses it by itself since #63 — the local stand-in
  named `gamescope` that #58 needed is no longer required (it still sits in
  `~/.local/bin/gamescope`; removing it is the honest setup now, and it also
  makes the default run pick Xvfb).
- The agent session runs on the Windows side and drives WSL2 through
  `wsl.exe`. Three things to know if a later session does the same: pass a
  script on **stdin** (`wsl.exe -- bash -s <<'EOF' … EOF`), not a long
  `bash -lc '…'`, whose quoting breaks and whose output is sometimes cut
  short; `wsl.exe` writes UTF-16 unless `WSL_UTF8=1` is set; and a
  **backslash in a here-document gets mangled** on the way through, so a
  patch script must not match or write text containing one (edit such lines
  by line number or with `sed` instead). A non-login `bash -s` also has no
  `~/.local/bin` on `PATH`, which hides anything installed there.
- Ubuntu 24.04 packages `gh` 2.45, old enough that some commands fail on a
  deprecated GraphQL field: `gh pr edit --add-reviewer` dies with
  "Projects (classic) is being deprecated ... (repository.pullRequest.projectCards)"
  and changes nothing. The REST call works:
  `gh api -X POST repos/<owner>/<repo>/pulls/<N>/requested_reviewers -f 'reviewers[]=<login>'`.
  Installing `gh` from GitHub's own apt repository would avoid it.
- Audio in a headless run logs skips: 91 over a 104 s run, with the queue
  between 65 and 82 ms (the last two fields of `coop: frame`). Whether a
  native Linux run shows the same was not measured, so this is an
  observation, not a fault (collaborator, 2026-10-20) — worth comparing when
  both machines run the same scenario.

## Scratch on this machine

- `~/dev-scratch/BM64Recomp-coop/i63/`: deleted with the pull request
  open, as the project asks.
- `rom/`, `build/BM64Recomp-coop-eu/`, `n64recomp/`: keep.
- `runs/`, `verify/`: delete freely; the summary of `verify/20261020-180114/`
  is quoted in pull request #65 and above.
