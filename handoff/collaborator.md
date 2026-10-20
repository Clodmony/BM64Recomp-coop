# Handoff: collaborator

The collaborator's own session note (see `HANDOFF.md`, *How the notes
work*). Only the collaborator's sessions rewrite it: session history, what
each issue found, decisions taken in those sessions, the next step. No
names, handles or hardware in it.

Last updated 2026-10-20 (#29: the Windows build against the Linux build).

## State

- **#29's Windows side and the Windows-against-Linux pair are done**, branch
  `coop/29-windows-build`, **draft pull request #68** into `bm64recomp-coop`
  (no `Closes`: #29 stays open for two real machines, the internet and
  macOS). It changes `tools/build-windows.sh`, `tools/windows-cross.cmake`,
  `docs/coop/building.md`, `docs/coop/contributing.md`,
  `docs/coop/playtest.md` and adds `tools/compare_logs.py`.
- **#63 still waits**: draft pull request #65, the owner's review requested.
  Nothing further to do on it until the review.
- Two follow-ups came out of this session: **#67** (do the two people's
  builds agree on the identity at all? the patches' clang — the playtest
  depends on it) and a comment on **#66** with the Windows audio numbers.
  #29 itself carries a comment on what is answered and what is left.
- The branch was cut from `bm64recomp-coop`, so it does **not** have #63's
  `run.sh`: runs on it need the local gamescope stand-in again (below).

## #29: what the Windows side found

One Windows machine with WSL2 (Ubuntu 24.04, clang 18), Europe build.

**The question answered:** the build identity is the same on both platforms.
The Linux build logs `coop: session build 0xFC44BFB4`; the cross-compiled
Windows build, run **natively on Windows 11** (not Wine), logs the same
`0xFC44BFB4`. Reason it must: neither part of the sum (FNV-1a over
`patches/patches.bin`, then the SHA-256 head of `bm64.<version>.toml`) comes
from the compiler of the C++ code. The sum can be computed offline from the
checkout, and gives the same number — the recipe is now in `building.md`,
*The build identity across platforms*, and it is what #67 asks the two
machines to compare.

**The Winsock branch runs**, first time on real Windows: two portable folders
on 127.0.0.1 played **3600 frames in lockstep**, same state hash at frame 0
(`199C12E7`) and 3600 (`AE6DD052`), no `session desync`, no `session lost`,
the same twelve overlay loads on both sides, ~49 frames a second at PAL 50,
and **no audio skips** (the last field of `coop: frame` stayed 0, queue about
16 ms) where this machine's WSL2 runs log them.

**Two things stood in the way of the build, both fixed on the branch:**

- `tools/build-windows.sh` died with
  `ln: failed to create symbolic link …: No such file or directory` when the
  xwin splat was missing, and said nothing about `clang-cl` or Wine either. It
  now names what is missing and exits 4 (checked both ways: no splat, and a
  `PATH` without `clang-cl`). A fresh Ubuntu 24.04 has none of the three:
  `clang-cl` (the clang package ships the driver but not that name),
  `wine64`, `xwin`.
- The MSVC STL stops clang 18 with `error STL1000`: the CRT xwin splats from
  the release channel is 14.44, which asks for clang 19, and the channel
  offers no older CRT (`--crt-version 14.41.34120` → *does not exist in the
  manifest*). `tools/windows-cross.cmake` now sets the STL's own switch
  `_ALLOW_COMPILER_AND_STL_VERSION_MISMATCH` for a clang below 19 and says so
  when configuring. A newer clang was the other way out and was **not** taken
  on purpose: it would also build the patches, and with them the identity.

**Not run then:** a Visual Studio build, and whether another clang version
changes `patches.bin` (#67). The Windows-against-Linux pair is the section
below; two real machines, the internet and macOS are what is left of #29.

Verify on the branch: `tools/verify.sh --full --version eu --scenarios`,
**8/8 stages, 383/383 checks, RESULT: ALL PASSED** (2026-10-20 18:57, log
folder `~/dev-scratch/BM64Recomp-coop/verify/20261020-185712/`). The scenario
stage needed the gamescope stand-in, since the branch predates #63.

## #29: the Windows build against the Linux build (2026-10-20)

The playtest's real pair, played on one machine: the native Windows build in
a portable folder on the Windows side against the Linux build in WSL2, the
two of them through different network stacks with WSL2's NAT between. Both
ways, with the `session-lockstep` scripts split over the two sides and both
logging a hash every frame:

- **Windows joins the WSL2 host** at `127.0.0.1` (WSL2's localhost relay) and
  **WSL2 joins the Windows host** at WSL2's default route: 448 frames each
  way, the same build identity `0xFC44BFB4`, `coop: session lockstep from
  frame 0`, **every** frame both logged with the same state hash, the same two
  overlay loads, no desync and no loss before the scripts ended.
- A longer run (WSL2 hosting): **3551 frames hashed by both, all equal**,
  about **49.4 frames a second** on either side at PAL 50. Audio: 4 skips on
  the WSL2 side (SDL's dummy driver, `run.sh` without `--audio`), 0 on the
  Windows side (the real device), so the WSL2 skip numbers of #66 are not a
  session fault.
- The WSL2 side joining the Windows host needed **no firewall rule**, which
  says nothing about a real LAN host: there Windows asks (playtest.md).
- **New tool `tools/compare_logs.py`**: `scenario.py` can only compare two
  games it starts itself on one machine, so a cross-machine session (and a
  playtest, which leaves one `coop.log` per PC) had nothing to compare the
  two logs with. It reports each side's build identity, the hash of every
  frame both logged with the first difference, the overlay loads, frames a
  second and audio skips, and any desync, loss or refusal; exit 0 only when
  the two agree. Logs without `BM64_COOP_LOG_HASH` (a playtest build) still
  give the rest.
- **One gamepad, two games** (the last small question of #29, **read in the
  code, not played**): background input is on by default and SDL's
  `JOYSTICK_ALLOW_BACKGROUND_EVENTS` is set, so two games on one machine both
  take the same controller's events. `run.sh` switches the option off for
  runs; a hand test (#33) has to do the same in each game. Now in
  building.md, *Session scenarios*.
- How to repeat it is in building.md, *A session between the Windows and the
  Linux build*; the two side scripts stayed in scratch (`i29x/`), since they
  hold this machine's paths.

Verify after these changes: `tools/verify.sh --full --version eu
--scenarios`, **8/8 stages, 383/383 checks, RESULT: ALL PASSED**
(2026-10-20 19:53, log folder
`~/dev-scratch/BM64Recomp-coop/verify/20261020-195322/`). The scenario stage
needed the gamescope stand-in again, since the branch predates #63.

## Next step

- **Ask the owner to review and merge #68 and #65** (either person may merge
  after the other's approving review, `HANDOFF.md`, *Project decisions*;
  locally with `git merge --no-ff`, never `gh pr merge`). #65 first: #68 does
  not conflict with it, and with #63 merged a later session needs no
  stand-in.
- **What is left of #29** can only be answered elsewhere: two real machines
  on a LAN (the playtest, `HANDOFF.md` candidate 1), the internet with a
  forwarded port, and macOS (no such machine here; upstream has no macOS
  build either). The owner decides whether those become their own issues and
  #29 closes with the playtest.
- **#67 needs both machines** (collaborator, 2026-10-20): whichever person's
  session takes it, the other must run the offline sum in their own checkout
  of the same commit and report the number and their `clang --version`. If
  they differ, the playtest cannot happen as planned and the project has to
  decide how the hello should treat it.
- **#66** waits for a machine with both wrappers (the owner's). This
  session's numbers say the audio skips are the WSL2 side's, not the
  session's.
- Candidates after that from this side: #46, if the owner's group #56 has
  released it.
- **#65 and #68 stay drafts** and wait for the owner's review
  (collaborator, 2026-10-20): no further nudge beyond the review request.

## Machine notes

Setup facts that only hold on this machine, kept general (for example "WSL2:
the game window opens through WSLg" or "WSL2: gamescope headless fails"),
never the hardware itself. Recipes that hold for everyone go into
`docs/coop/contributing.md` instead.

- WSL2 with WSLg supplies `DISPLAY`, `WAYLAND_DISPLAY` and
  `XDG_RUNTIME_DIR` to every shell, so the game's window needs no setup of
  its own and SDL picks its `x11` driver (through Xwayland).
- gamescope is not packaged for Ubuntu 24.04 (noble), with universe enabled.
  Since #63 `run.sh` picks Xvfb by itself — but on a branch cut from
  `bm64recomp-coop` before #63 is merged, the scenarios need the local
  stand-in named `gamescope` again. The honest shape of it drops gamescope's
  own options and runs what follows `--` **under Xvfb**, so the runs stay
  headless as the owner asks; it lives in `~/.local/bin`, outside the
  repository.
- **The agent session runs on the Windows side** and drives WSL2 through
  `wsl.exe`. What a later session should know:
  - The WSL checkout is reachable from Windows as
    `//wsl.localhost/<distro>/home/<user>/BM64Recomp-coop/…`, so the Read,
    Edit and Write tools work on it directly. That is far more reliable than
    patching files through a here-document (a backslash gets mangled on the
    way through `wsl.exe`). **But a file written that way loses its
    executable bit**: after editing a script, `chmod +x` it in WSL and check
    `git diff --summary` for a mode change.
  - Pass a script on **stdin** (`wsl.exe -- bash -s < file`), not a long
    `bash -lc '…'`, whose quoting breaks and whose output is sometimes cut
    short; set `WSL_UTF8=1`, or `wsl.exe` writes UTF-16.
  - A non-login `bash -s` has no `~/.local/bin` on `PATH`, which hides
    `clang-cl`, `xwin` and the gamescope stand-in: export it in the script.
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
- A background process started inside `wsl.exe -- bash -s` **dies with that
  call**: `nohup ... &` left nothing behind, not even its output file. A long
  run (a verify) has to stay in the foreground of the call, with the agent's
  own background mode holding `wsl.exe` open.
- Audio: the WSL2 runs log skips (91 over a 104 s run, queue 65-82 ms), the
  native Windows runs of the same commit log none. So the skips are not the
  game itself; what they are is #66's question.

## Scratch on this machine

- `~/dev-scratch/BM64Recomp-coop/i29/`: the Windows side's logs, deleted with
  the pull request open, as the project asks. `i29x/`: the pad scripts, the
  WSL2 side's run script and the logs of the Windows-against-Linux pair;
  delete once #68 is merged, the recipe itself is in building.md.
- `win/` (the xwin splat 630 MB, its download cache and the Wine prefix,
  2.4 GB together): **keep**, or the next Windows build starts with the
  download again.
- On the **Windows side**, `dev-scratch/BM64Recomp-coop/win-play/`: the two
  portable folders (host and client, with the ROM), `win-session.ps1` (the
  Windows pair) and `cross-session.ps1` (the Windows-against-Linux pair,
  either way, with each run's output beside it). **Keep** — they are what a
  later session needs to play either pair again without building anything.
- `rom/`, `build/BM64Recomp-coop-eu/`, `build/win-eu/`, `n64recomp/`: keep.
- `runs/`, `verify/`: delete freely.
