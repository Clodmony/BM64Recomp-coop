# Handoff

The shared session note of this repository. Two people work on it, the
**owner** and a **collaborator**, each with their own agent sessions. Read
this file first in a fresh session, then your own note in `handoff/`.

## How the notes work

- **This file** is shared: the repository's state, who works on which issue,
  the decisions that bind both people, the pool of next issues and the stable
  working notes. A session never rewrites it whole: it edits only the lines
  its work changed (its row under *In progress*, a new decision, a candidate
  taken or added), so the other person's edits survive a merge.
- **`handoff/owner.md`** and **`handoff/collaborator.md`** are each one
  person's own note (session history, findings, decisions taken in their
  sessions, their next step). Only that person's sessions rewrite it.
- **Who you work for:** `gh api user --jq .login` is the repository owner's
  account (`Clodmony`) for the owner; any other account is the collaborator.
  Never write a person's name, handle or machine into these files.
- Stable recipes go into `docs/coop/`, not here. No personal data or
  hardware, ever.

Last updated 2026-10-19 (#47: notes split for two people).

## Repository state

- Fork of `RevoSucks/BM64Recomp`. `master` = upstream `master` (`726adc9`).
  Default branch `bm64recomp-coop`. Ruleset "Protected" blocks force-pushes
  and deletion on both.
- Merged into `bm64recomp-coop` up to #50 (2026-10-19); the history is in
  `handoff/owner.md`.
- Open issues: #11 (Europe follow-ups, the owner's eye check), #14 and #16
  (leftover checks), #19 (their group), #24 (follow-ups of #19), #29
  (two machines, Windows/macOS), #46 (scenario.py and a garbled log line), #49 (enemy targeting),
  #51–#55 (playtest findings; #55 urgent), #56 (their group), #57 (live client log), #58 (the collaborator's
  setup, first),
  and the owner's last step of #47: inviting the collaborator on GitHub.
- Open pull requests: none. #50 (#22, downed players are ghosts) merged
  2026-10-19 without a review (owner's one-off exception).

## In progress

One row per issue someone works on. Claim an issue by assigning yourself on
GitHub *and* adding the row; remove the row when its pull request is merged.

| Issue | Person | Branch | State |
|-------|--------|--------|-------|
| #56 (group: #55, #52, #49, #51, #53, #46, #57) | owner | `coop/56-playtest` (lanes `coop/56-a-player2`, `-b-enemies`, `-c-hud`, `-d-harness`) | four lane agents started 2026-10-19, worktrees under scratch `wt/` |

## Project decisions (bind both people)

- **Merging** (owner, 2026-10-19): either person may merge a pull request
  once the other has reviewed it on GitHub (an approving review). Merge
  locally under the Claude identity, never with `gh pr merge` (AGENTS.md,
  *Workflow*).
- Local split-screen co-op is on hold (owner, 2026-10-18); the co-op is
  played online.
- Heart items (#36): shared, but a full player does not take one.
- **Downed players (#22, owner, 2026-10-19):** approach B, S stays player 1.
  The same rule for both players: down while the partner lives, as a
  battle-style ghost (moves along, no bombs, no triggers, enemies ignore it,
  no damage); no timed respawn; revived only at stage start, stage clear, a cutscene, or when the
  living player finds a heart (the heart is used up for it); not at a room
  change, warp or scripted walk. Each revive costs one shared life, the one
  at a stage start too. A cutscene revives at its end (the release), placed
  next to the living player (owner, 2026-10-19). Built in #50: design.md,
  *Downed players are ghosts (#22)*.
- The client plays on a copy of the host's save (#26).
- **Co-op HUD (#53, owner, 2026-10-19):** a corner per player (face,
  hearts, bomb count, fire), not one panel; player 1 always first (left),
  player 2 second (right), the same on the host and the client.
- Test runs are headless and keep background input off (owner).
- The older decisions with their reasons are in `handoff/owner.md`.

## Next candidates

A shared pool: take one by claiming it under *In progress*.

0. **Group #56, next (owner, 2026-10-19):** the playtest findings and the
   #22 follow-ups in four parallel lanes, one agent each, merged in the
   order D, A, B, C: lane A #55 (urgent: player 2 cannot walk before
   player 1's first step), then #52, then #54 (needs both `coop.log` files);
   lane B #49, then #51; lane C #53 (layout decided, *Project decisions*);
   lane D #46, then #57 (the client streams its log to the host). Lanes,
   shared files and the run order are in #56. The host's playtest folder
   is the kit's `linux/` here; start it with `start.sh`, or it writes no
   `coop.log` (until #57). The owner
   starts the multi-agent run ("use a workflow" / "fan out agents").
1. **The playtest** (both people, two PCs: one Linux, one Windows): the kit
   from `tools/playtest-kit.sh`, guide in `docs/coop/playtest.md`;
   afterwards both `coop.log` files. What breaks becomes an issue (a desync
   line first). This is also the real-PC part of #29.
2. **#46** (scenario.py and a garbled log line): a small harness fix, a good
   first issue for a session with a smaller plan.
3. **#58, the collaborator's first issue** (owner, 2026-10-19: before any
   other): the WSL2 setup of `docs/coop/contributing.md`, the quick and full
   verify, `run.sh`, the scenarios and the window in WSL2; what does not
   work becomes an issue. Needs the owner's GitHub invitation first.
4. **A hand try of the Co-op menu**: two games on one machine, Host 7777 in
   one, Join 127.0.0.1 7777 in the other, then turn each camera with the
   C-buttons (#33).
5. **#29** (the rest): the internet, macOS; whether two compilers give the
   same build identity; whether two games on one machine read the same
   gamepad.
6. **Rest of #14 and #16**: a story teleport (`func_802512C0` from a room
   script, beyond Green Garden stage 1) and a scene with player 2 already in
   the room, then a check that pad 1 does not move a frozen player 2.
7. **#49** (enemies target the nearest living player, ghosts left out;
   #22's PR has `coop_is_down` and the ghost flag to test); **#24** overlaps
   #44's file and needs a US ROM for its US run.
8. With #45: scenarios through the file select, Custom screen and pause
   menu (the `title-menu` recipe in building.md).

## Scratch (not in the repository)

Under `~/dev-scratch/BM64Recomp-coop/` on each person's machine. Scratch is
not shared: what one machine has, the other must make itself (its own ROM
image, build folders, N64Recomp cache). The analysis folders below exist on
the owner's machine only.

- `rom/`: decompressed and .z64 Europe ROM (never commit or upload). Kept.
- `build/<checkout>/`, `build/<checkout>-eu/`, `n64recomp/`: kept.
- `runs/`, `verify/`: run and verify logs; delete freely.
- `eu-disasm/`, `xref/`, `scripts/`, `venv/`, `audit/`, `ref/`: the
  disassembly, cross-references and notes; `scripts/fn.sh <section>
  <function>`, run from the scratch folder, prints one function. Kept for the
  follow-ups unless the owner says otherwise.

## Working notes (stable, keep short)

- `gh`: pass `-R Clodmony/BM64Recomp-coop`, or run `.superset/setup.sh` once.
- A new worktree has no submodules; `.superset/setup.sh` fetches them.
- Patches: clang 16+ needs `-Wno-error=incompatible-pointer-types`, the host
  build needs `-include cstdint` with libstdc++ 15; `tools/verify.sh` passes
  both (docs/coop/building.md).
- A shell without a session lacks `XDG_RUNTIME_DIR`; `tools/run.sh` fills it
  in, or the game cannot open its window.
- Runs are headless in gamescope (#41's `run.sh`); `--window` opens a window
  on the desktop. Owner: test runs stay headless.
- Parallel agents (one worktree each) work, given #41's `run.sh` (unique run
  folders) and separate build folders; session scenarios pick free ports.
- In recompiled code `$ra` is 0 at a function's entry: a hook cannot tell
  its caller from it. Hook text runs inside the recompiled C function: no
  `stderr`/`printf` there, call a host function; `return;` at an entry hook
  skips the function. Hook text must not hold US data addresses (the Europe
  config copies it): read them from registers or in the patches.
- Room 0x28's opening dialogue needs repeated A taps (40 × 4 frames held,
  16 apart, `p2-room28`); START there goes back to the title.
- One full `tools/verify.sh --full --version eu` takes about 3 minutes when
  only patches and hooks changed; a scenario 10–30 s. A hook text change
  needs the full pipeline (configs, ROM recompile): `cmake --build` alone
  links against the old hooks and fails.
- A session scenario opens two game windows at once.
- Never screenshot with `spectacle -a` (the active window): the game rarely
  has the focus, and it captures whatever does (happened once in #26; both
  captures deleted at once).
- `python3 -m py_compile` leaves `tools/__pycache__/` (now ignored); the
  push guard refuses a commit that holds one.
