# Handoff: collaborator

The collaborator's own session note (see `HANDOFF.md`, *How the notes
work*). Only the collaborator's sessions rewrite it: session history, what
each issue found, decisions taken in those sessions, the next step. No
names, handles or hardware in it.

Last updated 2026-10-20 (#29's Windows side, pull request #68 open).

## State

- **#29's Windows side done**, branch `coop/29-windows-build`, **draft pull
  request #68** into `bm64recomp-coop` (no `Closes`: #29 stays open for the
  rest). It changes `tools/build-windows.sh`, `tools/windows-cross.cmake`,
  `docs/coop/building.md` and `docs/coop/contributing.md`.
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

**Not run:** a session between the Windows and the Linux build (the
playtest's real pair), two machines, the internet, macOS, a Visual Studio
build, and whether another clang version changes `patches.bin` (#67).

Verify on the branch: the summary is quoted in pull request #68 (the scenario
stage needed the gamescope stand-in, since the branch predates #63).

## Next step

- **Ask the owner to review and merge #68 and #65** (either person may merge
  after the other's approving review, `HANDOFF.md`, *Project decisions*;
  locally with `git merge --no-ff`, never `gh pr merge`). #65 first: #68 does
  not conflict with it, and with #63 merged a later session needs no
  stand-in.
- **#67 next, and it needs both machines** (collaborator, 2026-10-20):
  whichever person's session takes it, the other must run the offline sum in
  their own checkout of the same commit and report the number and their
  `clang --version`. If the numbers differ, the playtest cannot happen as
  planned and the project has to decide how the hello should treat it.
- Candidates after it from this side: the rest of #29 (a Windows-against-Linux
  session first — it needs nothing but the two builds this machine already
  has), or #46 if the owner's group #56 has released it.

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
- Audio: the WSL2 runs log skips (91 over a 104 s run, queue 65-82 ms), the
  native Windows runs of the same commit log none. So the skips are not the
  game itself; what they are is #66's question.

## Scratch on this machine

- `~/dev-scratch/BM64Recomp-coop/i29/`: this session's logs; delete with the
  pull request open, as the project asks.
- `win/` (the xwin splat, its cache and the Wine prefix) and the Windows-side
  portable folders: **keep** — a session between the Windows and the Linux
  build is the next step of #29 and they are ready for it.
- `rom/`, `build/BM64Recomp-coop-eu/`, `build/win-eu/`, `n64recomp/`: keep.
- `runs/`, `verify/`: delete freely.
