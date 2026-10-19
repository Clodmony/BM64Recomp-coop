# Handoff: collaborator

The collaborator's own session note (see `HANDOFF.md`, *How the notes
work*). Only the collaborator's sessions rewrite it: session history, what
each issue found, decisions taken in those sessions, the next step. No
names, handles or hardware in it.

Last updated 2026-10-20 (#58, the first session).

## State

- **#58 done**, branch `coop/58-wsl2-setup`, draft pull request open into
  `bm64recomp-coop`, waiting for the owner's review. It changes
  `docs/coop/contributing.md` only (plus the notes).
- The machine is set up and checked: WSL2 with Ubuntu 24.04, the build
  dependencies, the fork cloned into the Linux file system, the Europe ROM
  unpacked into scratch, `gh` logged in with write access to the fork.
- One issue came out of it, **#63**: `tools/run.sh` wraps its headless runs
  in gamescope, which Ubuntu 24.04 does not package, so the default run
  fails and `tools/scenario.py`, which cannot pass `--window`, does not run
  at all there. The proposal in #63 is to let `run.sh` choose its wrapper,
  gamescope or Xvfb.

## What #58 found

Run on 2026-10-20, Ubuntu 24.04 in WSL2, clang 18, the Europe version:

- `tools/verify.sh --quick --version eu`: 5/5 stages, all passed.
- `tools/verify.sh --full --version eu`: 7/7, so the ROM recompiles and the
  Europe build links in WSL2.
- `tools/verify.sh --full --version eu --scenarios`: 8/8 stages,
  **383/383 checks**, every scenario including the two-game `session-*` ones
  (two games at once, the lockstep over the loopback network).
- `tools/run.sh --version eu --window`: the title overlay loads after 0.6 s.
  Under Xvfb instead of gamescope, after 0.4 s and with nothing on the
  desktop.
- Speed: a 100 s run played the attract demo through 14 overlay loads at
  5200 game frames in 104 s of wall-clock time, so 100 % at PAL 50 — the
  rate the owner's machine shows. No stutter in the frame log.
- Two gaps in `docs/coop/contributing.md`, both fixed on the branch: `make`
  was missing from the dependency list (the `patches` stage then fails with
  `make: command not found`, and `europe` after it for the missing
  `patches.elf`), and the WSL2 section still said the runs and scenarios
  were untried.

## Machine notes

Setup facts that only hold on this machine, kept general (for example "WSL2:
the game window opens through WSLg" or "WSL2: gamescope headless fails"),
never the hardware itself. Recipes that hold for everyone go into
`docs/coop/contributing.md` instead.

- WSL2 with WSLg supplies `DISPLAY`, `WAYLAND_DISPLAY` and
  `XDG_RUNTIME_DIR` to every shell, so the game's window needs no setup of
  its own and SDL picks its `x11` driver (through Xwayland).
- gamescope is not packaged for Ubuntu 24.04 (noble), with universe enabled
  (#63). Xvfb is, and stands in for it well. For `scenario.py`, which cannot
  pass `--window`, a stand-in named `gamescope` early on `PATH` that drops
  its own options and runs what follows `--` is enough; it lives outside the
  repository.
- Installing WSL on a machine that has none takes two steps with a reboot
  between them, because the optional components must be active before a
  distribution registers. `wsl --install -d <distro> --location <path>` puts
  the distribution on another drive, which the system drive may need: the
  distribution, the build folders, the N64Recomp cache and the ROM image
  together took about 5 GB before the first build and more after it.
- The agent session runs on the Windows side and drives WSL2 through
  `wsl.exe`. Two things to know if a later session does the same: pass a
  script file rather than a long `bash -lc '…'`, whose quoting breaks and
  whose output is sometimes cut short; and `wsl.exe` writes UTF-16 unless
  `WSL_UTF8=1` is set.
- Audio in a headless run logs skips: 91 over the 104 s run above, with the
  queue between 65 and 82 ms (the last two fields of `coop: frame`). Whether
  a native Linux run shows the same was not measured, so this is an
  observation, not a fault. Worth comparing when both machines run the same
  scenario.

## Next step

- Ask the owner to review the #58 pull request; either person may merge once
  the other has approved it (`HANDOFF.md`, *Project decisions*).
- Then take the next unclaimed candidate from `HANDOFF.md`, *Next
  candidates*. #63 is this machine's own itch and well bounded; #46 is the
  other small one, but it belongs to the owner's group #56.
