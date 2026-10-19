---
name: issue-loop
description: One tracked issue end to end in one session - orient from the handoff, branch, reproduce, fix, scripted verify, PR, handoff, then /clear. Use when starting or continuing work on a project's issues ("continue", "next issue", "work on #N").
---

# Issue loop

One issue per session. This copy is the fork's own (#47): two people, the owner and a collaborator, run it on their own machines, so it reads and writes two handoff notes (AGENTS.md, *Workflow*). *The person* below is whoever runs the session: `gh api user --jq .login` is `Clodmony` for the owner, any other account is the collaborator.

A long session re-sends the whole conversation on every step, so the loop ends at a **handoff** and a fresh context, not at the next issue.

## Steps

1. **Orient.** Read `HANDOFF.md` at the repository root (shared: state, *In progress*, project decisions, next candidates), then the person's own note, `handoff/owner.md` or `handoff/collaborator.md`. They are in the repository on purpose: agent memory is keyed by checkout path and machine, so another worktree or the other person's machine does not see it. Pull first (`git fetch origin`). Check the open PRs, their reviews and the issues' assignees. Done when you know: the integration branch, open PRs and whether the person asked to merge any, what the other person is working on, and which issue is next. If the next issue isn't decided, ask the person with the candidates nobody has claimed as options. Then claim it: assign the person on GitHub and add a row under *In progress* in `HANDOFF.md`.

2. **Merge what is approved.** Only PRs the person said to merge and the other person approved in a GitHub review, in the project's merge style (AGENTS.md: local `git merge --no-ff` under the Claude identity, never `gh pr merge`). Remove the PR's row under *In progress*. Done when the PR shows merged.

3. **Branch** from the up-to-date integration branch: one branch per issue, named after the issue. Done when `git status` shows the new branch, clean.

4. **Reproduce.** Read the issue and its comments, then find the gap in the running program, not only in the code: drive it with the project's test harness and probe scripts kept in the scratch folder. Done when every part of the issue is either *red* (a failure you can trigger on demand) or confirmed working by a run.

5. **Test first.** Turn each red into an automated scenario or test that fails on the current build. Record the failing output: the PR quotes it.

6. **Fix.** Smallest change that turns the scenario green, in the style of the surrounding code, with the project's change markers. A claim in a comment, commit or doc states only what a run showed. Done when the new scenarios pass on a rebuilt program.

7. **Verify by script.** Run the project's verify script in the background (see *Verify script*), wait for its notification, read only the summary. A failure in code you didn't touch: rerun that scenario alone before calling it flaky; a failure from your change: fix, commit, rerun the affected parts. Done when the summary says all passed, or every remaining failure is explained and rerun.

8. **Docs and PR.** Update the project's docs in the same branch. Commit, push, open the PR with a body file kept in the scratch folder: what was replayed, what broke, the fix, before/after test results, verify summary. "Closes #N" only when the whole issue is done. Done when the PR URL exists.

9. **Handoff and stop.** Rewrite the person's own note in `handoff/`: what the issue found, open PRs and what they need (a review by the other person), branch and worktree state, background processes, the next step, cleanup the person may approve. In `HANDOFF.md` edit only your lines: the issue's row under *In progress*, decisions that bind both people, candidates taken or added; never rewrite it whole, so the other person's lines survive. No personal data or hardware in either, no names (say *owner* and *collaborator*); stable recipes go to the project's docs. Commit the notes on the issue branch; when the session ends without a PR, or a note must be visible before the merge, commit the notes alone (`handoff: <date>`), pull, and push them to the integration branch directly, the only files allowed there without a PR (on a conflict keep both people's lines). Delete this issue's scratch data. Then ask the person about every task still theirs (PRs to merge, reviews to ask the other person for, proposed decisions, wording to review, cleanup to approve) as choices with options, the recommended option first and marked; a decision that binds both people is the owner's. Record the answers (docs, notes) and act on the ones that are yours. Then tell the person: "Issue done, please /clear before the next issue." Start nothing new.

## Scratch storage

All working data lives on disk under `~/dev-scratch/<project>/` (or the folder the project names): probe scripts, PR bodies, build dirs, package dry runs, reference clones, temporary worktrees, captures. /tmp is a small RAM disk here. Long background commands write their output to a file there or to the project's run folder.

## Verify script

The project's single entry point for "is this branch good". If the project has none, propose one to the owner before the first fix. Its contract:

- one command, runs unattended, safe to start in the background
- builds the branch, runs the chosen scenarios (or `--suite` for all), the project's compatibility/lint gates and unit tests (`--tests`)
- writes full logs to a stamped folder on disk and a summary of a few lines: each stage ok/failed, `N/M checks passed`, every FAILED line, one RESULT line
- exit code 0 only when everything passed
- never touches the owner's own saves, settings or running program (private data folders, own ports or instances)

## Probing

Interactive probing is for debugging only. Put a probe in a script file and run it in one call; print only the lines you need. Check before reusing an assumption about the program from an earlier session: the code may have moved.
