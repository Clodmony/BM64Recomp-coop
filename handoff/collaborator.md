# Handoff: collaborator

The collaborator's own session note (see `HANDOFF.md`, *How the notes
work*). Only the collaborator's sessions rewrite it: session history, what
each issue found, decisions taken in those sessions, the next step. No
names, handles or hardware in it.

Last updated 2026-10-20 (#67: the build identity comes from the sources).

## State

- **#67 done**, branch `coop/67-build-identity` off `bm64recomp-coop`,
  **draft pull request PR_NUMBER** (`Closes #67`): the identity a session's
  hello compares is hashed from the patches' **sources** and the recompiler
  configs instead of the compiled `patches/patches.bin`, so the two people's
  builds of one commit accept each other although their clang versions
  differ. Section *#67* below.
- **#71 done**, branch `coop/71-fast-scenarios`, **draft pull request #72**
  (`Closes #71`): `tools/scenario.py` runs several scenarios at once, so the
  scenario stage takes 2:44 instead of 11:47 and a full verify 5:14 instead
  of 14:18, with 383/383 checks unchanged. Still waiting for the owner's
  review.
- **#63 (#65) and #29's Windows side (#68) were merged by the owner**
  2026-10-20. What they found lives in `docs/coop/building.md` (*Running the
  game*, *The Windows build*, *A session between the Windows and the Linux
  build*) and `docs/coop/contributing.md`.
- #67's branch was cut after those merges, so it holds #63's `run.sh` but
  **not** #71's parallel scenarios: a full verify on it takes the old
  ~15 minutes.
- Open issues this person filed and left alone: **#66** (are gamescope and
  Xvfb runs comparable? only a machine with both can answer, so the
  owner's), **#69** (the session over the internet, a forwarded port),
  **#70** (the macOS build). #29 itself is now only the LAN playtest.

## #67: what it found and what the fix is

The identity was FNV-1a over `patches/patches.bin`, the MIPS binary
`make -C patches` builds with whatever clang the machine has. Both people
measured it at the same commit `18e4c8b` (Europe config):

| | this machine | the other one |
|---|---|---|
| clang | 18.1.3 | 23.1.3 |
| `patches/patches.bin` | 46 312 bytes | 46 328 bytes |
| config half | `0xFC2D198C` | `0xFC2D198C` (same) |
| build identity | `0xFC44BFB4` | `0x7F5EB1B6` |

So the hello would have refused the join and the playtest (#29) could not
have started, although the code was the same. Replayed here with one clang,
by compiling the identical sources twice with different code generation
(`-O1` added to the second): 46 312 against 32 788 bytes, `0xFC44BFB4`
against `0xD447083F`. The first variant reproduces this machine's measured
number, which says the old formula was replayed correctly.

The fix, the way the person chose out of the three the issue listed (the
sources; the other two were pinning the patches' clang and dropping the
identity from the hello for the playtest):

- **`tools/build_id.py`** hashes the patches' sources — every `patches/*.c`,
  `*.h`, `*.ld` and the `Makefile` with its flags, each with its path — then
  `patches.toml` and, as before, the SHA-256 head of `bm64.<version>.toml`.
  `--number` prints the value alone, `--files` the covered paths, `check`
  the properties it must have.
- **`src/coop/version.cmake`** runs the script when configuring, compiles
  the number in as `BM64_COOP_BUILD_IDENTITY` and configures again whenever
  a covered file changes; a script that fails leaves the identity 0 and
  CMake warns. `build_id()` in `src/coop/session.cpp` only reports it, and
  the run-time hash of `mm_patches_bin` is gone. The macro is deliberately
  **not** called `BM64_COOP_BUILD_ID`: that name stays the test switch which
  fakes the identity (`session-build`).
- **`tools/verify.sh`** gained a `buildid` stage (in `--quick` too) running
  `tools/build_id.py check`: the compiled binary stays out of the sum, every
  `patches/*.c` and both configs are in it, and a change to any covered file
  must change the number.

Given up, and said so in the docs: two builds whose compilers really do
generate different code are no longer refused at the hello. Such a
difference is caught where it shows, by the session's per-frame state hash
(`coop: session desync`).

What the running game says: CMake logs `-- Co-op build identity: 0x6738F333
(eu)` when configuring, and all 19 sessions of the verify run logged
`coop: session build 0x6738F333`, the number `tools/build_id.py --version eu`
prints offline; 18 of them reached `coop: session lockstep from frame 0`, and
`session-build`'s faked `0x12345678` still shows the refusal path works.

Verify on the branch: `tools/verify.sh --full --version eu --scenarios`,
**9/9 stages, 383/383 checks, RESULT: ALL PASSED** (2026-10-20 21:49, log
folder `~/dev-scratch/BM64Recomp-coop/verify/20261020-214910/`), run from a
non-login shell so the scenarios used Xvfb. Europe only, scripted; the US
build was not run and no session between two machines happened here.

The identity of this checkout at the branch's tip: `0x6738F333` (Europe),
`0x0CA40841` (US). It changes with every edit under `patches/` or in a
config, so a number holds for one commit only.

## Next step

- **Ask the owner to review and merge #72 and PR_NUMBER** (either person may
  merge after the other's approving review, `HANDOFF.md`, *Project
  decisions*; locally with `git merge --no-ff`, never `gh pr merge`). Then
  delete the branches and take their rows out of *In progress*.
- **The playtest's blocker is gone**: with both people on one commit the
  identities agree by construction. Worth confirming once in a real session
  between the two machines, which is what is left of #29.
- Candidates left for this person: **#69** (the internet, a forwarded port),
  **#70** (macOS, needs such a machine), **#66** (needs a machine with both
  headless wrappers, so the owner's), and #46, #14 or #16 out of the shared
  pool once the owner's group #56 releases them.

## Machine notes

Setup facts that only hold on this machine, kept general (for example "WSL2:
the game window opens through WSLg" or "WSL2: gamescope headless fails"),
never the hardware itself. Recipes that hold for everyone go into
`docs/coop/contributing.md` instead.

- WSL2 with WSLg supplies `DISPLAY`, `WAYLAND_DISPLAY` and
  `XDG_RUNTIME_DIR` to every shell, so the game's window needs no setup of
  its own and SDL picks its `x11` driver (through Xwayland).
- gamescope is not packaged for Ubuntu 24.04 (noble), with universe enabled;
  Xvfb is, and since #63 `run.sh` picks it by itself. The old stand-in named
  `gamescope` still sits in `~/.local/bin`, so a **login** shell
  (`bash -lc`) puts it on `PATH` and the runs pick the stand-in instead of
  Xvfb: start verify and run scripts from a **non-login** shell
  (`wsl.exe -- bash -c …`), as #63's and #67's verify runs were.
- This machine's clang is 18.1.3, the other machine's 23. Since #67 that no
  longer separates the two builds (above).
- **The agent session runs on the Windows side** and drives WSL2 through
  `wsl.exe`. What a later session should know:
  - The WSL checkout is reachable from Windows as
    `//wsl.localhost/<distro>/home/<user>/BM64Recomp-coop/…`, so the Read,
    Edit and Write tools work on it directly. That is far more reliable than
    patching files through a here-document (a backslash gets mangled on the
    way through `wsl.exe`). **But a file written that way loses its
    executable bit**: after editing a script, `chmod 755` it in WSL and
    check `git diff --summary` for a mode change (it cost #67 one run,
    *Permission denied* on `tools/verify.sh`).
  - Pass a script on **stdin** (`wsl.exe -- bash -s < file`), not a long
    `bash -lc '…'`, whose quoting breaks and whose output is sometimes cut
    short; set `WSL_UTF8=1`, or `wsl.exe` writes UTF-16. PowerShell has no
    here-document, so such calls go through the agent's Bash tool.
  - A non-login `bash -s` has no `~/.local/bin` on `PATH`, which hides
    `clang-cl`, `xwin` and the gamescope stand-in: export it in the script
    where they are wanted.
  - `ps` shows a `stage`'s subshell with the same command line as
    `tools/verify.sh` itself, so one verify run looks like two processes.
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
- A background process started inside `wsl.exe -- bash -s` **dies with that
  call**: `nohup … &` left nothing behind, not even its output file (seen
  again in #67). A long run (a verify) has to stay in the foreground of the
  call, with the agent's own background mode holding `wsl.exe` open.
- Audio: the WSL2 runs log skips (91 over a 104 s run, queue 65-82 ms), the
  native Windows runs of the same commit log none. So the skips are not the
  game itself; what they are is #66's question.

## Scratch on this machine

- `~/dev-scratch/BM64Recomp-coop/i67/`: deleted with the pull request open,
  as the project asks; its reproduction (two `patches.bin` from the same
  sources) is quoted in the pull request and in `docs/coop/building.md`.
- `win/` (the xwin splat 630 MB, its download cache and the Wine prefix,
  2.4 GB together): **keep**, or the next Windows build starts with the
  download again.
- On the **Windows side**, `dev-scratch/BM64Recomp-coop/win-play/`: the two
  portable folders (host and client, with the ROM), `win-session.ps1` (the
  Windows pair), `cross-session.ps1` (the Windows-against-Linux pair, either
  way, `-Direction linux-host|windows-host [-Suffix -long]`), the pad scripts
  it hands each side and `linux-side.sh`, which it starts inside WSL2 (the
  path to it is at the top of the .ps1). **Keep** — they are what a later
  session needs to play either pair again without building anything.
- `rom/`, `build/BM64Recomp-coop-eu/`, `build/win-eu/`, `n64recomp/`: keep.
- `runs/`, `verify/`: delete freely, except the folder the open pull requests
  quote (#72's and #67's, named in those bodies and above).
