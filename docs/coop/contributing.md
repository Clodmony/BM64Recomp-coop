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
   administrator PowerShell), then open the Ubuntu shell.
2. Install the build dependencies:

   ```bash
   sudo apt install git gh cmake ninja-build clang lld llvm python3 \
       libsdl2-dev libgtk-3-dev libfreetype-dev pkg-config
   ```

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

Not run yet (the collaborator's first setup check finds out, and what it
finds goes here): the game window through WSLg, `tools/run.sh`'s headless
runs in gamescope inside WSL2, and the scenarios. If headless runs fail in
WSL2, `tools/verify.sh` without `--scenarios` still checks a branch; the
scenarios then run on the owner's machine before the merge.

To **play** on Windows, use the Windows build rather than the WSL2 one: the
playtest kit's `windows/` folder ([playtest.md](playtest.md)), made by
`tools/playtest-kit.sh` on either machine. Both players need builds with the
same build identity, or the session refuses the join; builds of the same
commit from `tools/verify.sh` and `tools/build-windows.sh` have it. A native
Windows build made with Visual Studio (upstream's [BUILDING.md](../../BUILDING.md))
compiles the patches with another clang and likely does not match (#29).

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
