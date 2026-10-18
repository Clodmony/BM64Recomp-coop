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
    #check moved P1 P2 SLOT [MIN]   the player in SLOT is at least MIN world
                                 units (default 10) from where it was between
                                 the probes labelled P1 and P2
    #check still P1 P2 SLOT [MAX]   ... at most MAX (default 1) units
    #check delta P1 P2 SLOT AXIS OP VALUE
                                 the player's move along AXIS (x, y or z)
                                 from probe P1 to P2, e.g. z < -100
    #check field PROBE SLOT NAME OP VALUE
                                 a field of the player's probe line, OP one of
                                 = != < > <= >=, e.g. hearts < 2

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


def check(text, lines, probes):
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
    if kind == "after":
        first = next((i for i, line in enumerate(lines) if re.search(args[0], line)), None)
        if first is None:
            return False, "first pattern not logged"
        return any(re.search(args[1], line) for line in lines[first + 1:]), ""
    if kind in ("moved", "still"):
        a, b, slot = args[0], args[1], int(args[2])
        limit = float(args[3]) if len(args) > 3 else (10.0 if kind == "moved" else 1.0)
        pa, pb = player(probes, a, slot)["pos"], player(probes, b, slot)["pos"]
        dist = math.dist(pa, pb)
        ok = dist >= limit if kind == "moved" else dist <= limit
        return ok, f"{dist:.1f} units"
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
    raise ValueError(f"unknown check {kind}")


def run_scenario(path, version, timeout, extra_envs):
    envs, checks = [], []
    for line in path.read_text().splitlines():
        if line.startswith("#env "):
            envs.append(line[5:].strip())
        elif line.startswith("#check "):
            checks.append(line[7:].strip())
    cmd = [str(ROOT / "tools/run.sh"), "--input", str(path), "--until-log", "coop: input end",
           "--timeout", str(timeout)]
    if version:
        cmd += ["--version", version]
    for env in envs + extra_envs:
        cmd += ["--env", env]
    proc = subprocess.run(cmd, capture_output=True, text=True)
    summary = proc.stdout.strip().splitlines()[-1] if proc.stdout.strip() else proc.stderr.strip()
    print(f"{path.stem}: {summary}")
    results = []
    m = re.search(r"folder (\S+)$", summary)
    if proc.returncode != 0 or not m:
        results.append((False, "run", f"run.sh exit {proc.returncode}"))
        lines = (Path(m.group(1)) / "game.log").read_text(errors="replace").splitlines() if m else []
    else:
        lines = (Path(m.group(1)) / "game.log").read_text(errors="replace").splitlines()
    errors = [line for line in lines if line.startswith("coop: input error")]
    if errors:
        results.append((False, "input script", errors[0]))
    probes = parse_probes(lines)
    for text in checks:
        try:
            ok, detail = check(text, lines, probes)
        except (LookupError, ValueError, IndexError, re.error) as err:
            ok, detail = False, str(err)
        results.append((ok, text, detail))
    for ok, text, detail in results:
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
        path = Path(name)
        if not path.is_file():
            path = ROOT / "tools/scenarios" / f"{name}.txt"
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
