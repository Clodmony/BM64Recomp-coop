#!/usr/bin/env python3
"""Run test scenarios of the game unattended and check their logs.

    tools/scenario.py [--version us|eu] [--timeout SECONDS] [--env NAME=VALUE]... SCENARIO...

A scenario is an input script (format in src/coop/harness.cpp) with header
lines the script itself ignores as comments:

    #env NAME=VALUE              environment for the game (repeatable)
    #check log REGEX             a line of the game's log matches (REGEX is
                                 the rest of the line)
    #check no-log REGEX          no line matches
    #check after REGEX1 REGEX2   a line matching REGEX2 follows the first
                                 line matching REGEX1 (quote regexes with
                                 spaces, shell style)
    #check not-after REGEX1 REGEX2  no line matching REGEX2 follows it
    #check moved P1 P2 SLOT [MIN]   the player in SLOT is at least MIN world
                                 units (default 10) from where it was between
                                 the probes labelled P1 and P2
    #check still P1 P2 SLOT [MAX]   ... at most MAX (default 1) units
    #check delta P1 P2 SLOT AXIS OP VALUE
                                 the player's move along AXIS (x, y or z)
                                 from probe P1 to P2, e.g. z < -100
    #check near PROBE A B MAX [MIN]  the players in slots A and B are MIN
                                 (default 0) to MAX world units apart at PROBE
                                 (a tile is 100 units)
    #check field PROBE SLOT NAME OP VALUE
                                 a field of the player's probe line, OP one of
                                 = != < > <= >=, e.g. hearts < 2
    #check file PATH             the run left a file PATH (relative to its
                                 run folder, a glob: saves/bm64_*.bin)
    #check no-file PATH          ... did not

A session scenario (#21) runs two instances of the game, a host and a client
of an online session (src/coop/session.cpp) on a free local port, with these
header lines besides:

    #session                     the scenario is a session's
    #env-host NAME=VALUE         environment for the host only (also
                                 #env-client)
    #client STEP                 a step of the client's input script (the
                                 host's script is the scenario itself); its
                                 pad 0 is pad 1, player 2, in both games
    #check-client CHECK          a check on the client's log (#check checks
                                 the host's)
    #check same-hashes           both logged the same state hash for every
                                 frame both logged (needs BM64_COOP_LOG_HASH)
    #saves-host SCENARIO         the host starts with the saves/ folder that
                                 SCENARIO's run left (also #saves-client);
                                 SCENARIO runs first, alone, with its checks

Both logs are cut at their "coop: input end": once one game is stopped the
other loses the session. Both scripts should end at the same frame.

SCENARIO is a file or a name in tools/scenarios/ (without .txt). Each runs
with tools/run.sh --input SCENARIO --until-log "coop: input end" in its own
run folder under ~/dev-scratch/BM64Recomp-coop/runs/. The result is one line
per check, "N/M checks passed" and a RESULT line; exit 0 only if every check
of every scenario passed. --env adds environment after the scenario's own, so
it overrides a scenario's #env (e.g. --env BM64_COOP_P2=0 for a run without
player 2).
"""
import argparse
import math
import operator
import re
import shlex
import socket
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OPS = {"=": operator.eq, "!=": operator.ne, "<": operator.lt, ">": operator.gt,
       "<=": operator.le, ">=": operator.ge}


def parse_probes(lines):
    """{label: {slot: {field: int or (x, y, z)}}} from the probe lines."""
    probes, current = {}, None
    for line in lines:
        m = re.match(r"coop: probe (\S+)$", line)
        if m:
            current = probes.setdefault(m.group(1), {})
            continue
        m = re.match(r"coop: player slot=(\d+) (.*)", line)
        if m and current is not None:
            fields = {}
            for key, value in re.findall(r"(\w+)=(\S+)", m.group(2)):
                if key == "pos":
                    fields[key] = tuple(int(v) / 10 for v in value.split(","))
                elif re.fullmatch(r"-?\d+|0x[0-9A-Fa-f]+", value):
                    fields[key] = int(value, 0)
                else:
                    fields[key] = value
            current[int(m.group(1))] = fields
    return probes


def player(probes, label, slot):
    if label not in probes:
        raise LookupError(f"no probe {label}")
    if slot not in probes[label]:
        raise LookupError(f"no player in slot {slot} at probe {label}")
    return probes[label][slot]


def check(text, lines, probes, folder=None):
    kind, _, rest = text.partition(" ")
    rest = rest.strip()
    if kind in ("log", "no-log"):
        # the regex is the rest of the line; quotes around it are optional
        args = shlex.split(rest) if rest[:1] in "\"'" and rest else [rest]
    else:
        args = shlex.split(rest)
    if kind == "log":
        return any(re.search(args[0], line) for line in lines), ""
    if kind == "no-log":
        hits = [line for line in lines if re.search(args[0], line)]
        return not hits, hits[0] if hits else ""
    if kind in ("after", "not-after"):
        first = next((i for i, line in enumerate(lines) if re.search(args[0], line)), None)
        if first is None:
            return False, "first pattern not logged"
        hits = [line for line in lines[first + 1:] if re.search(args[1], line)]
        if kind == "not-after":
            return not hits, hits[0] if hits else ""
        return bool(hits), ""
    if kind in ("moved", "still"):
        a, b, slot = args[0], args[1], int(args[2])
        limit = float(args[3]) if len(args) > 3 else (10.0 if kind == "moved" else 1.0)
        pa, pb = player(probes, a, slot)["pos"], player(probes, b, slot)["pos"]
        dist = math.dist(pa, pb)
        ok = dist >= limit if kind == "moved" else dist <= limit
        return ok, f"{dist:.1f} units"
    if kind == "near":
        label, a, b, limit = args[0], int(args[1]), int(args[2]), float(args[3])
        least = float(args[4]) if len(args) > 4 else 0.0
        dist = math.dist(player(probes, label, a)["pos"], player(probes, label, b)["pos"])
        return least <= dist <= limit, f"{dist:.1f} units"
    if kind == "delta":
        a, b, slot, axis, op, value = args[0], args[1], int(args[2]), args[3], args[4], float(args[5])
        i = "xyz".index(axis)
        d = player(probes, b, slot)["pos"][i] - player(probes, a, slot)["pos"][i]
        return OPS[op](d, value), f"d{axis}={d:+.1f}"
    if kind == "field":
        label, slot, name, op, value = args[0], int(args[1]), args[2], args[3], int(args[4], 0)
        got = player(probes, label, slot).get(name)
        if got is None:
            return False, f"no field {name}"
        return OPS[op](got, value), f"{name}={got}"
    if kind in ("file", "no-file"):
        if folder is None:
            return False, "no run folder"
        there = any(folder.glob(args[0]))
        return there == (kind == "file"), "there" if there else "not there"
    raise ValueError(f"unknown check {kind}")


def run_cmd(script, version, timeout, envs, name=None, saves=None):
    cmd = [str(ROOT / "tools/run.sh"), "--input", str(script), "--until-log", "coop: input end",
           "--timeout", str(timeout)]
    if version:
        cmd += ["--version", version]
    if name:
        cmd += ["--name", name]
    if saves:
        cmd += ["--saves", str(saves)]
    for env in envs:
        cmd += ["--env", env]
    return cmd


def read_run(proc, label, results, folder=None):
    """The run's game log, cut at the script's end in a session; a failed
    run is a failed check. Its run folder is appended to `folder`."""
    out = proc.stdout.read() if hasattr(proc.stdout, "read") else proc.stdout
    summary = out.strip().splitlines()[-1] if out.strip() else ""
    print(f"{label}: {summary}")
    m = re.search(r"folder (\S+)$", summary)
    if proc.returncode != 0 or not m:
        results.append((False, "run", f"run.sh exit {proc.returncode}"))
    lines = (Path(m.group(1)) / "game.log").read_text(errors="replace").splitlines() if m else []
    if m and folder is not None:
        folder.append(Path(m.group(1)))
    errors = [line for line in lines if line.startswith("coop: input error")]
    if errors:
        results.append((False, "input script", errors[0]))
    return lines


def free_port():
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def same_hashes(host, client):
    pattern = re.compile(r"coop: hash frame=(\d+) hash=(\S+)$")
    a = dict(m.groups() for m in map(pattern.match, host) if m)
    b = dict(m.groups() for m in map(pattern.match, client) if m)
    both = sorted(set(a) & set(b), key=int)
    if not both:
        return False, "no frame hashed by both"
    differ = [f for f in both if a[f] != b[f]]
    if differ:
        return False, f"{len(differ)}/{len(both)} frames differ, first {differ[0]}"
    return True, f"{len(both)} frames"


def run_checks(checks, lines, probes, results, other=None, folder=None):
    for text in checks:
        try:
            if text == "same-hashes":
                ok, detail = same_hashes(lines, other)
            else:
                ok, detail = check(text, lines, probes, folder)
        except (LookupError, ValueError, IndexError, re.error) as err:
            ok, detail = False, str(err)
        results.append((ok, text, detail))


def until_end(lines):
    end = next((i for i, line in enumerate(lines) if line.startswith("coop: input end")), len(lines))
    return lines[:end + 1]


def scenario_path(name):
    path = Path(name)
    return path if path.is_file() else ROOT / "tools/scenarios" / f"{name}.txt"


def run_scenario(path, version, timeout, extra_envs, folder=None):
    envs, checks = [], []
    session, host_envs, client_envs, client_steps, client_checks = False, [], [], [], []
    saves_from = {}
    for line in path.read_text().splitlines():
        if line.startswith("#env "):
            envs.append(line[5:].strip())
        elif line.startswith("#check "):
            checks.append(line[7:].strip())
        elif line.strip() == "#session":
            session = True
        elif line.startswith("#env-host "):
            host_envs.append(line[10:].strip())
        elif line.startswith("#env-client "):
            client_envs.append(line[12:].strip())
        elif line.startswith("#client "):
            client_steps.append(line[8:].strip())
        elif line.startswith("#check-client "):
            client_checks.append(line[14:].strip())
        elif line.startswith(("#saves-host ", "#saves-client ")):
            side, _, name = line[7:].partition(" ")
            saves_from[side] = name.strip()
    results, saves = [], {}
    for side, name in saves_from.items():
        # the scenario that leaves the saves runs first, with its checks
        before, ran = scenario_path(name), []
        if not before.is_file():
            results.append((False, f"#saves-{side} {name}", "no such scenario"))
            continue
        results += [(ok, f"{before.stem}: {text}", detail)
                    for ok, text, detail in run_scenario(before, version, timeout, extra_envs, ran)]
        if ran and (ran[0] / "saves").is_dir():
            saves[side] = ran[0] / "saves"
        else:
            results.append((False, f"#saves-{side} {name}", "its run left no saves/ folder"))
    own = len(results)  # the earlier scenarios printed their own
    if not session:
        proc = subprocess.run(run_cmd(path, version, timeout, envs + extra_envs, saves=saves.get("host")),
                              capture_output=True, text=True)
        ran = folder if folder is not None else []
        lines = read_run(proc, path.stem, results, ran)
        run_checks(checks, lines, parse_probes(lines), results, folder=ran[-1] if ran else None)
    else:
        port = free_port()
        runs = Path.home() / "dev-scratch/BM64Recomp-coop/runs"
        runs.mkdir(parents=True, exist_ok=True)
        client_script = runs / f".{path.stem}-client-{port}.txt"
        client_script.write_text("\n".join(client_steps) + "\n")
        procs = []
        for name, script, own_envs, session_env in (
                ("host", path, host_envs, f"BM64_COOP_SESSION=host:{port}"),
                ("client", client_script, client_envs, f"BM64_COOP_SESSION=join:127.0.0.1:{port}")):
            cmd = run_cmd(script, version, timeout, envs + own_envs + [session_env] + extra_envs, name,
                          saves.get(name))
            procs.append(subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True))
        for proc in procs:
            proc.wait()
        client_script.unlink()
        host_dir, client_dir = [], []
        host = until_end(read_run(procs[0], f"{path.stem} host", results, host_dir))
        client = until_end(read_run(procs[1], f"{path.stem} client", results, client_dir))
        run_checks(checks, host, parse_probes(host), results, client, host_dir[0] if host_dir else None)
        client_results = []
        run_checks(client_checks, client, parse_probes(client), client_results, host,
                   client_dir[0] if client_dir else None)
        results += [(ok, f"client: {text}", detail) for ok, text, detail in client_results]
    for ok, text, detail in results[own:]:
        print(f"  {'ok    ' if ok else 'FAILED'} {text}{'  (' + detail + ')' if detail else ''}")
    return results


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--version", choices=["us", "eu"])
    parser.add_argument("--timeout", type=int, default=120)
    parser.add_argument("--env", action="append", default=[])
    parser.add_argument("scenarios", nargs="+")
    args = parser.parse_args()
    results = []
    for name in args.scenarios:
        path = scenario_path(name)
        if not path.is_file():
            print(f"FAILED no scenario {name}")
            results.append((False, name, ""))
            continue
        results += run_scenario(path, args.version, args.timeout, args.env)
    passed = sum(ok for ok, _, _ in results)
    print(f"{passed}/{len(results)} checks passed")
    print(f"RESULT {'ok' if passed == len(results) else 'FAILED'}")
    return 0 if passed == len(results) else 1


if __name__ == "__main__":
    sys.exit(main())
