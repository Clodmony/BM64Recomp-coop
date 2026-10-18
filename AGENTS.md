# Agent instructions: BM64Recomp-coop

This repository ([Clodmony/BM64Recomp-coop](https://github.com/Clodmony/BM64Recomp-coop))
is a **public fork** of
[RevoSucks/BM64Recomp](https://github.com/RevoSucks/BM64Recomp), the static
recompilation of Bomberman 64 for PC. The fork adds a **cooperative
two-player Adventure campaign**, played **online**: two players share one
campaign over the network. Local split-screen co-op on one machine was part of
the plan and is on hold (owner, 2026-10-18). The co-op project
is built entirely with AI: every change after upstream commit `726adc9` is
written by an AI coding agent directed by the repository owner. Keep that
disclosure in `README.md` and here.

Read before working:

- **Start here:** [HANDOFF.md](HANDOFF.md), the session handoff note (state,
  open pull requests, next issue), and the open issues on GitHub (the only
  task list); work follows the issue loop under *Workflow*.
- [docs/coop/building.md](docs/coop/building.md): the build pipeline, which
  stages need the ROM, `tools/verify.sh`, the toolchain workarounds and how a
  test run keeps away from the owner's saves.
- [docs/coop/design.md](docs/coop/design.md): where story mode assumes one
  player (addresses, read from the Europe build) and the proposed co-op
  approach; note its Europe/US address rule before using any address.
- Upstream's [BUILDING.md](BUILDING.md) and [README.md](README.md).
- Co-op design documents go under `docs/coop/` as the work creates them.

## Code map

- `patches/`: C compiled for MIPS (`make -C patches`) and recompiled with
  `N64Recomp patches.toml`. A function marked `RECOMP_PATCH` replaces the
  game function of the same name (strict patch mode checks the name against
  `BM64Syms/`). Game-side co-op logic goes here.
- `bm64.us.toml`: the recompiler's configuration for the game, including
  `[[patches.hook]]` entries that call into code at given addresses. The
  Europe configs (`*.eu.toml`) are generated from the US ones by
  `tools/europe.py`, which also holds the Europe address map.
- `src/game/`: host-side C++ (input, config, the recomp APIs patches call),
  `src/ui/`: the RmlUi menus and launcher, `src/main/`: startup,
  `src/coop/`: co-op host code (the build version, debug switches, the test
  harness: scripted pads, warp, probes).
- `tools/`: `verify.sh` (the check of a branch), `run.sh` (unattended runs),
  `scenario.py` and `scenarios/` (scripted runs of the game with checks),
  `unpack_rom.py` (decompressed image from either ROM), `europe.py` (Europe
  configs and their check), `n64recomp.sh`.
- `lib/N64ModernRuntime` (librecomp, ultramodern), `lib/rt64` (renderer),
  `lib/sf64` (libultra headers the patches build against), `BM64Syms/`
  (symbols): submodules, never edited here.
- Names and structures of the game come from the decompilation,
  [Bomberhackers/bm64](https://github.com/Bomberhackers/bm64).

## Hard rules

- **Two branches.** `master` is an exact copy of upstream's `master`: never
  commit to it; bring it up to date with
  `git fetch upstream && git push origin upstream/master:master`. The co-op
  line is `bm64recomp-coop`, the default branch: issue branches come off it
  and draft pull requests go into it. Upstream fixes come in by merging
  `master` into it. A GitHub ruleset ("Protected") blocks force-pushes and
  deletion of both branches.
- **Licence.** GPLv3 (`LICENSE.txt`, from N64ModernRuntime). Keep upstream's
  notices and credits.
- **The fork is public, its builds are not.** Never publish releases,
  packages, Pages sites or build artifacts. Only `coop-ci.yml` runs here;
  upstream's workflows stay disabled in the fork's Actions settings.
- Never push to `upstream` (its push URL is disabled on purpose) and never
  open pull requests against it. Never force-push over existing work.
- No personal data in commits, docs, issues or pull requests (names, emails,
  hardware, local paths). Commits are authored as
  `Claude <noreply@anthropic.com>` (the clone's local git config).
- **No session links, ever** (owner). Commits and pull requests carry no
  `Claude-Session:` line and no claude.ai session URL, whatever Claude
  Code's attribution defaults say. Keep `Co-Authored-By: Claude …` and the
  pull request's "Generated with Claude Code" line (the AI disclosure).
- **No game data in the repository.** Never commit or upload ROMs,
  decompressed ROMs, the recompiler's output (`RecompiledFuncs/`,
  `rsp/aspMain.cpp`), saves, or captures of the game (screenshots and clips
  show the game's graphics: keep them local, never in commits, issues or pull
  requests). The owner's ROM folder is read-only. The pre-commit hook
  (`.githooks/`) and the owner's push guard refuse such files; never bypass
  either.
- **The owner's data is not test data.** The game keeps saves, config and
  mods in `~/.config/BM64Recompiled`; test runs use portable mode in a scratch
  folder ([docs/coop/building.md](docs/coop/building.md)) and never stop a
  game the owner is running.
- **Keep upstream merges cheap.** New code goes into new files
  (`patches/coop_*.c`, `src/coop/`, `docs/coop/`); edits to upstream files
  stay small hooks, each marked with a `// coop:` comment that says why.
- Do not claim something works on a platform or in a mode that was not run. A
  compile-only verify (no ROM) proves compiling, not linking or playing.

## Workflow

- **One issue per session**, following the `issue-loop` skill: orient from
  `HANDOFF.md`, branch `coop/<N>-<topic>` from `bm64recomp-coop`, reproduce
  (a failing test or scripted check first), fix, run `tools/verify.sh`,
  update the docs, push and open a draft pull request into `bm64recomp-coop`
  (`Closes #N` only when the whole issue is done). Then rewrite
  `HANDOFF.md`, ask the owner about everything still theirs to do or decide
  (merges, proposed decisions, wording, cleanup) as choices with options, the
  recommended one first, record the answers, and stop: the owner starts a
  fresh session for the next issue.
- **Issue groups** (owner, 2026-10-18): a milestone of linked issues may be
  done as one: one session, one branch `coop/<parent N>-<topic>`, one draft
  pull request that closes them all. The group is decided when the issues
  are filed (the parent issue lists them in order).
- **GitHub issues are the task list.** New work, bugs found on the way and
  follow-ups become issues; there is no task file in the repository. Always
  pass `-R Clodmony/BM64Recomp-coop` to `gh` (or run `.superset/setup.sh`,
  which sets the default), since the `upstream` remote would otherwise be
  picked.
- **The handoff lives in the repository.** `HANDOFF.md` is rewritten at the
  end of every session and committed on the issue's branch. When a session
  ends without a pull request, or the note must be visible before the owner
  merges, commit it alone (`handoff: <date>`) and push it to
  `bm64recomp-coop` directly: it is the one file that may land there without
  a pull request. Per-checkout agent memory is not read from another
  worktree, so state goes into this file, not into memory.
- **Worktrees.** One worktree per issue: a Superset workspace, or
  `git worktree add` under scratch. Superset runs `.superset/setup.sh` when it
  creates a workspace; run it by hand in any other worktree (identity,
  upstream remote, hooks, `gh` default, submodules, tool check). Remove a
  worktree when its pull request is merged.
- **The owner merges.** A pull request is merged only when the owner says so,
  locally in a checkout of `bm64recomp-coop` under the Claude identity:
  `git merge --no-ff <branch> -m "Merge #N: …"`, push, delete the merged
  branch. Never `gh pr merge` (it authors the merge as the owner).
- **Scratch lives on disk, outside the repository**, under
  `~/dev-scratch/BM64Recomp-coop/`: verify runs, build folders, the N64Recomp
  cache, the decompressed ROM, test-run folders, captures, pull request
  bodies. /tmp is a small RAM disk on the owner's machine. Delete an issue's
  scratch when its pull request is open; keep `n64recomp/`, `build/` and
  `rom/`.

## Setup

```bash
.superset/setup.sh    # idempotent: identity, upstream remote (push DISABLED),
                      # core.hooksPath .githooks, gh default repo, submodules
```

Build dependencies (Linux): `cmake`, `ninja`, `clang`, `lld`, `llvm`, SDL2,
GTK 3 and FreeType development files, `python3` 3.11+. Linking and running
need the decompressed image of the US 1.0 or the Europe ROM, made by
`tools/unpack_rom.py` (see [docs/coop/building.md](docs/coop/building.md)).

## Verification commands

```bash
# The check of a branch: submodules, configs, patches (compiled and recompiled
# in strict patch mode), then the ROM recompile and a linked build, or without
# the ROM a compile of every host object file. Logs and summary.txt in
# ~/dev-scratch/BM64Recomp-coop/verify/<stamp>/, exit 0 only if no stage failed.
tools/verify.sh
tools/verify.sh --quick    # submodules, configs, patches (and europe) only
tools/verify.sh --full     # a missing ROM fails instead of skipping
tools/verify.sh --version eu   # the Europe build (default: the image that exists)

# An unattended run of a built executable in its own portable folder under
# ~/dev-scratch/BM64Recomp-coop/runs/; 0 once overlay 0x20 (title) loads.
tools/run.sh --version eu --until-overlay 0x20 --timeout 60

# Scenarios: scripted pads and checks on the game's log (docs/coop/building.md,
# *The test harness*); --scenarios runs them all after a linked build.
tools/scenario.py --version eu tools/scenarios/*.txt
tools/verify.sh --full --version eu --scenarios

python3 -I tools/unpack_rom.py <rom>     # decompressed image into scratch
python3 -I tools/europe.py check <image> # the Europe map on the image
tools/n64recomp.sh         # N64Recomp/RSPRecomp at upstream CI's pinned commit
```
