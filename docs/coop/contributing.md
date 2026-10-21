# Working on the fork as a second developer

Two people work on this fork: the **owner** (the repository's account) and a
**collaborator**, each with their own machine and their own Claude Code
sessions (#47). Both follow [AGENTS.md](../../AGENTS.md); this page is what
the collaborator needs on top of it, and what both need to stay out of each
other's way. Everything here is written for the agent as much as for the
person: point the first session at this file.

## Access

- The owner invites the collaborator on GitHub (*Settings → Collaborators*,
  write access). The collaborator works in the fork itself: issue branches
  `coop/<N>-<topic>` off `bm64recomp-coop`, draft pull requests into it. The
  ruleset "Protected" keeps both main branches safe from force-pushes and
  deletion for everyone.
- Commits are authored as `Claude <noreply@anthropic.com>` on both machines
  (`.superset/setup.sh` sets it in the clone's local git config; the global
  identity stays the person's own). Pull requests and reviews come from each
  person's own GitHub account.
- No personal data in the repository, also not the other person's: no
  names, handles, emails, hardware or local paths in commits, docs, issues,
  pull requests or the handoff notes. The notes say *owner* and
  *collaborator*.

## Who works on what

- **GitHub issues are the task list** for both. Claim an issue by assigning
  yourself (`gh issue edit N --add-assignee @me -R Clodmony/BM64Recomp-coop`)
  and adding a row under *In progress* in `HANDOFF.md`. Never start on an
  issue someone else is assigned to.
- **Handoff notes:** `HANDOFF.md` is shared and edited line by line, never
  rewritten whole; each person's sessions rewrite only their own note,
  `handoff/owner.md` or `handoff/collaborator.md` (`HANDOFF.md`, *How the
  notes work*). A session finds out whom it works for with
  `gh api user --jq .login`: the owner's account is `Clodmony`.
- **Reviews and merges:** either person may merge a pull request once the
  other has approved it in a GitHub review (owner, 2026-10-19). The merge is
  local, under the Claude identity: `git merge --no-ff <branch> -m "Merge #N:
  …"` in a checkout of `bm64recomp-coop`, push, delete the branch. Never
  `gh pr merge` (it authors the merge as the person's account).
- **Decisions:** a decision that changes the project for both (a rule, a
  game-play choice such as #22's approach, the merge rule) is the owner's to
  take, or both people's together; it goes into `HANDOFF.md`, *Project
  decisions*. A decision inside one's own issue goes into one's own note.
- Branch from the newest state:
  `git fetch origin && git switch -c coop/<N>-<topic> origin/bm64recomp-coop`.
  Rebase or merge `bm64recomp-coop` into a long
  branch before its review, never force-push a branch the other person has
  checked out.

## The agent's skill

The issue loop (`AGENTS.md`, *Workflow*) is a Claude Code skill kept in the
repository, `.claude/skills/issue-loop/SKILL.md`, so a session in any clone
of the fork has it. Start a session with "next issue" or "work on #N".

## Setting up on Windows: WSL2

The tools (`tools/verify.sh`, `run.sh`, `scenario.py`, `.superset/setup.sh`)
are Bash and Python scripts made for Linux. On a Windows PC, work inside
**WSL2** (Ubuntu 24.04 or newer, for Python 3.11+ and a recent clang):

1. Install WSL2 with Ubuntu (`wsl --install -d Ubuntu-24.04` in an
   administrator PowerShell), then open the Ubuntu shell. On a machine
   without WSL this takes two steps, because the optional components must be
   active before a distribution can be registered:
   `wsl --install --no-distribution`, a reboot, then the distribution. The
   distribution's disk, the build folders and the N64Recomp cache together
   take well over ten gigabytes; on a small system drive pass
   `--location <path>` to keep the distribution on another one.
2. Install the build dependencies:

   ```bash
   sudo apt install git gh make cmake ninja-build clang lld llvm python3 \
       libsdl2-dev libgtk-3-dev libfreetype-dev pkg-config xvfb
   ```

   `make` is easy to miss and not installed by default: `make -C patches`
   builds the patches, so without it the `patches` stage of
   `tools/verify.sh` fails with `make: command not found`, and `europe` then
   fails for the missing `patches.elf` (#58).

   `xvfb` is what the headless test runs use where `gamescope` is not
   packaged, which is every Ubuntu 24.04 (#63); `tools/run.sh` picks
   whichever of the two is there.

3. Install Claude Code inside WSL2 (Anthropic's Claude Code documentation,
   *Windows / WSL*), log in with your own account, and log in to GitHub with
   `gh auth login`.
4. Clone into the Linux file system, not under `/mnt/c` (much slower, and
   the scripts expect Linux paths):

   ```bash
   git clone https://github.com/Clodmony/BM64Recomp-coop.git ~/BM64Recomp-coop
   cd ~/BM64Recomp-coop
   .superset/setup.sh      # identity, upstream (push disabled), hooks, gh, submodules
   ```

5. Your own ROM: copy your Europe (or US) ROM into WSL2 and unpack it into
   scratch: `python3 -I tools/unpack_rom.py <rom>` ([building.md](building.md),
   *The pipeline*). The ROM never goes into the repository, a commit, an
   issue or a chat; the pre-commit hook refuses ROM files.
6. Check: `tools/verify.sh --quick`, then `tools/verify.sh --full --version
   eu` (links the game; the first run recompiles the whole ROM and takes a
   while).

**Checked in WSL2** (the collaborator's first setup check, #58, 2026-10-20,
Ubuntu 24.04 with clang 18): `tools/verify.sh --quick` and
`tools/verify.sh --full --version eu` both pass, so the Europe build
recompiles and links there. The game runs and plays the attract demo at full
speed, 5200 game frames in 104 s of wall-clock time at PAL 50, the rate the
owner's machine shows. `tools/verify.sh --full --version eu --scenarios`
passes whole, 383/383 checks, the two-game `session-*` scenarios included.
WSLg supplies `DISPLAY`, `WAYLAND_DISPLAY` and `XDG_RUNTIME_DIR` by itself,
so `tools/run.sh --window` opens the game window with no further setup and
SDL takes its `x11` driver.

**Ubuntu 24.04 packages no `gamescope`**, which `tools/run.sh` used to wrap
every headless run in; since #63 it picks its wrapper instead, so install
Xvfb once and the headless runs and the scenarios need nothing further:

```bash
sudo apt install xvfb
tools/run.sh --version eu --until-overlay 0x20   # headless, under Xvfb
```

The run's summary line names the display it used (`display: xvfb` here,
`gamescope` on a machine that has it); `--wrapper` forces one
([building.md](building.md), *Running the game*).

To **play** on Windows, use the Windows build rather than the WSL2 one: the
playtest kit's `windows/` folder ([playtest.md](playtest.md)), made by
`tools/playtest-kit.sh` on either machine. Both players need builds with the
same build identity, or the session refuses the join. Since #67 every build
of one commit has it, whatever clang the machine has: the identity is hashed
from the patches' sources and the configs, not from the compiled patches, and
`python3 -I tools/build_id.py --version eu` prints it without building or
playing ([building.md](building.md), *The build identity across platforms*).
Before that the two people's clang versions (18 and 23) gave different
numbers and their hellos would have refused each other. Compare the two
numbers before a playtest; a build made with Visual Studio (upstream's
[BUILDING.md](../../BUILDING.md)) is still untried, but its clang no longer
enters the identity.

The cross build itself runs in WSL2 too, and needs more than the Linux build:
`clang-cl` (the clang package ships no such name, link it),
`wine64`, `xwin` and its splat of Microsoft's CRT and Windows SDK, and for
Ubuntu's clang 18 the STL's version switch, which the toolchain sets by
itself ([building.md](building.md), *The Windows build*).
`tools/build-windows.sh` names what is missing before it builds. The
resulting `.exe` runs natively on the Windows side, outside WSL2, from a
portable folder of its own.

## Usage limits

The two people may have different Claude plans. A full issue session (a
full verify, scenario runs, reading the disassembly) uses a lot of a plan's
usage; on a smaller plan:

- Keep to one issue per session and end it with the handoff and `/clear`, as
  the loop says: a long session re-sends its whole conversation on every
  step.
- Iterate with `tools/verify.sh --quick` and single scenarios
  (`tools/scenario.py --version eu tools/scenarios/<one>.txt`); run the full
  verify once, at the end, in the background, and read only its summary.
- Prefer issues that are well bounded (harness fixes, Windows and two-machine
  checks, docs); the reverse-engineering issues need long sessions.

## Playing together

The playtest guide, [playtest.md](playtest.md), covers two PCs on one
network. Over the internet (two homes), the host's port 7777 (TCP) must be
reachable: a forwarded port on the host's router, or both PCs in one virtual
network (Tailscale, ZeroTier) and the host's address in it. Not run yet
(#29). After a session the host has both logs (its `coop.log` and the
other's in `coop-logs/`, #57); they go to whoever files the issue. Read a
log for local paths and names before pasting it into an issue.
