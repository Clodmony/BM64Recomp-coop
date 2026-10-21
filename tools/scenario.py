#!/usr/bin/env python3
"""Run test scenarios of the game unattended and check their logs.

    tools/scenario.py [--version us|eu] [--timeout SECONDS] [--env NAME=VALUE]...
                      [--exe PATH] [--exe-client PATH] [--jobs N] SCENARIO...

--exe runs another build (run.sh's --exe; a .exe in Wine), --exe-client
another build as a session's client only (#29: the Windows build against the
Linux one).

--jobs runs that many scenarios at once (#71; default a third of the
machine's cores, at most 6, and 1 with --exe, where Wine's single prefix is
shared). Each run already has its own run folder and a free port, and the
game's own pacing, not the CPU, sets how long a run takes, so the lanes do
not change a check's result; the output of each scenario is printed in one
block, in the order they were named.

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
    #check nearer PROBE camera A B  the point the camera looks at is nearer
                                 the player in slot A than the one in B, along
                                 the floor (x and z), at PROBE
    #check camera PROBE NAME OP VALUE
                                 the camera's yaw, pitch (degrees) or
                                 distance at PROBE, e.g. yaw = 45
    #check field PROBE SLOT NAME OP VALUE
                                 a field of the player's probe line, OP one of
                                 = != < > <= >=, e.g. hearts < 2
    #check file PATH             the run left a file PATH (relative to its
                                 run folder, a glob: saves/bm64_*.bin)
    #check no-file PATH          ... did not
    #check contains PATH REGEX   a file PATH (a glob) the run left has a line
                                 matching REGEX

A session scenario (#21) runs two instances of the game, a host and a client
of an online session (src/coop/session.cpp) on a free local port, with these
header lines besides:

    #session                     the scenario is a session's
    #launcher                    both start it from the launcher's Co-op
                                 menu (BM64_COOP_LAUNCHER) instead of
                                 BM64_COOP_SESSION
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
    #prev-log-host TEXT          a line of the host's last run's log
                                 (coop.prev.log, which a session sends to the
                                 other side; also #prev-log-client)

Both logs are cut at their "coop: input end": once one game is stopped the
other loses the session. Both scripts should end at the same frame.

SCENARIO is a file or a name in tools/scenarios/ (without .txt). Each runs
with tools/run.sh --input SCENARIO --until-log "coop: input end" in its own
run folder under ~/dev-scratch/BM64Recomp-coop/runs/. The result is one line
per check, "N/M checks passed" and a RESULT line; exit 0 only if every check
of every scenario passed. A log line the checks read (a probe's player or
camera line, a hash line) that does not parse, as when two games wrote into
one log, is a failed check "log line parses" that names it; a check or a
scenario that raises is a failed check with the error, and the next scenario
still runs (#46; tools/test_scenario.py). --env adds environment after the
scenario's own, so it overrides a scenario's #env (e.g. --env BM64_COOP_P2=0
for a run without player 2).
"""
import argparse
import concurrent.futures
import math
import operator
import os
import re
import shlex
import socket
import subprocess
import sys
import threading
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OPS = {"=": operator.eq, "!=": operator.ne, "<": operator.lt, ">": operator.gt,
       "<=": operator.le, ">=": operator.ge}


def parse_fields(text):
    """The key=value fields of a probe line: numbers, (x, y, z) for pos, eye
    and angles (tenths), a word (actor=none). ValueError for anything else,
    such as another game's line written into this one (#46)."""
    fields = {}
    for token in text.split(" "):
        key, eq, value = token.partition("=")
        if not eq or not re.fullmatch(r"\w+", key):
            raise ValueError(f"not key=value: {token!r}")
        if key in ("pos", "eye", "angles"):
            parts = value.split(",")
            if len(parts) != 3 or not all(re.fullmatch(r"-?\d+", v) for v in parts):
                raise ValueError(f"not x,y,z: {token!r}")
            fields[key] = tuple(int(v) / 10 for v in parts)
        elif re.fullmatch(r"-?\d+", value):
            fields[key] = int(value)
        elif re.fullmatch(r"0x[0-9A-Fa-f]+", value):
            fields[key] = int(value, 16)
        elif re.fullmatch(r"[a-z]+", value):
            fields[key] = value
        else:
            raise ValueError(f"not a value: {token!r}")
    return fields


def parse_probes(lines, bad=None):
    """{label: {slot or "camera": {field: int or (x, y, z)}}} from the probe
    lines. A player or camera line that does not parse is left out and
    appended to `bad`."""
    probes, current = {}, None
    for line in lines:
        m = re.match(r"coop: probe (\S+)$", line)
        if m:
            current = probes.setdefault(m.group(1), {})
            continue
        # "coop: camera follows slot=N" is not a probe's camera line
        m = re.match(r"coop: (?:player slot=(\d+) |camera (?=eye=))(.*)", line)
        if m and current is not None:
            try:
                fields = parse_fields(m.group(2))
            except ValueError:
                if bad is not None:
                    bad.append(line)
                continue
            current[int(m.group(1)) if m.group(1) else "camera"] = fields
    return probes


def parse_hashes(lines, bad=None):
    """{frame: hash} from the "coop: hash" lines; one that does not parse is
    left out and appended to `bad`."""
    hashes = {}
    for line in lines:
        if line.startswith("coop: hash "):
            m = re.fullmatch(r"coop: hash frame=(\d+) hash=([0-9A-F]{8})", line)
            if m:
                hashes[m.group(1)] = m.group(2)
            elif bad is not None:
                bad.append(line)
    return hashes


def camera_target(camera):
    """The point the camera looks at: its distance from the eye, along its yaw
    and pitch (read in room 0x29: yaw 90, pitch 45, distance 2000, the eye
    +x and +y of its player)."""
    (ex, ey, ez), (yaw, pitch, dist) = camera["eye"], camera["angles"]
    yaw, pitch = math.radians(yaw), math.radians(pitch)
    flat = dist * math.cos(pitch)
    return (ex - flat * math.sin(yaw), ey - dist * math.sin(pitch), ez - flat * math.cos(yaw))


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
    if kind == "nearer":
        label, a, b = args[0], int(args[2]), int(args[3])
        if label not in probes or "camera" not in probes[label]:
            raise LookupError(f"no camera at probe {label}")
        at = camera_target(probes[label]["camera"])
        da, db = (math.dist((at[0], at[2]), (player(probes, label, s)["pos"][0], player(probes, label, s)["pos"][2]))
                  for s in (a, b))
        return da < db, f"{da:.1f} and {db:.1f} units"
    if kind == "camera":
        label, name, op, value = args[0], args[1], args[2], float(args[3])
        if label not in probes or "camera" not in probes[label]:
            raise LookupError(f"no camera at probe {label}")
        got = probes[label]["camera"]["angles"][("yaw", "pitch", "distance").index(name)]
        return OPS[op](got, value), f"{name}={got:g}"
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
    if kind == "contains":
        if folder is None:
            return False, "no run folder"
        files = list(folder.glob(args[0]))
        hits = [f for f in files if any(re.search(args[1], line)
                                        for line in f.read_text(errors="replace").splitlines())]
        return bool(hits), f"{len(files)} files, {len(hits)} matching"
    raise ValueError(f"unknown check {kind}")


EXES = {}  # the executable of "host" (any game but a session's client) and "client"


def run_cmd(script, version, timeout, envs, name=None, saves=None, prev_log=None):
    cmd = [str(ROOT / "tools/run.sh"), "--input", str(script), "--until-log", "coop: input end",
           "--timeout", str(timeout)]
    exe = EXES.get("client" if name == "client" else "host")
    if exe:
        cmd += ["--exe", exe]
    if version:
        cmd += ["--version", version]
    if name:
        cmd += ["--name", name]
    if saves:
        cmd += ["--saves", str(saves)]
    if prev_log:
        cmd += ["--prev-log", str(prev_log)]
    for env in envs:
        cmd += ["--env", env]
    return cmd


def read_run(proc, label, results, folder=None, said=None):
    """The run's game log, cut at the script's end in a session; a failed
    run is a failed check. Its run folder is appended to `folder`, its lines
    of output to `said` (printed as one block, #71)."""
    out = proc.stdout.read() if hasattr(proc.stdout, "read") else proc.stdout
    summary = out.strip().splitlines()[-1] if out.strip() else ""
    said.append(f"{label}: {summary}")
    m = re.search(r"folder (\S+)$", summary)
    if proc.returncode != 0 or not m:
        results.append((False, "run", f"run.sh exit {proc.returncode}"))
    lines = []
    if m:
        try:
            lines = (Path(m.group(1)) / "game.log").read_text(errors="replace").splitlines()
        except OSError as err:
            results.append((False, "run", f"no game.log: {err}"))
    if m and folder is not None:
        folder.append(Path(m.group(1)))
    errors = [line for line in lines if line.startswith("coop: input error")]
    if errors:
        results.append((False, "input script", errors[0]))
    return lines


PORTS = (threading.Lock(), set())  # the ports handed out, so two lanes never
                                   # get the same one (#71): the bind is let go
                                   # before the game takes the port


def free_port():
    lock, taken = PORTS
    with lock:
        for _ in range(100):
            with socket.socket() as sock:
                sock.bind(("127.0.0.1", 0))
                port = sock.getsockname()[1]
            if port not in taken:
                taken.add(port)
                return port
    raise RuntimeError("no free port")


def same_hashes(host, client):
    a, b = parse_hashes(host), parse_hashes(client)
    both = sorted(set(a) & set(b), key=int)
    if not both:
        return False, "no frame hashed by both"
    differ = [f for f in both if a[f] != b[f]]
    if differ:
        return False, f"{len(differ)}/{len(both)} frames differ, first {differ[0]}"
    return True, f"{len(both)} frames"


def unparsable(lines, results, side=""):
    """Each line the checks read that does not parse (#46: two games wrote
    into one log) is a failed check that names it."""
    bad = []
    parse_probes(lines, bad)
    parse_hashes(lines, bad)
    for line in bad:
        results.append((False, f"{side}log line parses", line if len(line) <= 200 else line[:200] + " ..."))


def run_checks(checks, lines, probes, results, other=None, folder=None):
    for text in checks:
        try:
            if text == "same-hashes":
                ok, detail = same_hashes(lines, other)
            else:
                ok, detail = check(text, lines, probes, folder)
        except (LookupError, ValueError, IndexError, re.error) as err:
            ok, detail = False, str(err)
        except Exception as err:  # a check that raises fails; the run goes on (#46)
            ok, detail = False, f"{type(err).__name__}: {err}"
        results.append((ok, text, detail))


def until_end(lines):
    end = next((i for i, line in enumerate(lines) if line.startswith("coop: input end")), len(lines))
    return lines[:end + 1]


def scenario_path(name):
    path = Path(name)
    return path if path.is_file() else ROOT / "tools/scenarios" / f"{name}.txt"


def run_scenario(path, version, timeout, extra_envs, folder=None, said=None):
    envs, checks = [], []
    session, host_envs, client_envs, client_steps, client_checks = False, [], [], [], []
    switch = "BM64_COOP_SESSION"
    saves_from, prev_logs = {}, {"host": [], "client": []}
    for line in path.read_text().splitlines():
        if line.startswith("#env "):
            envs.append(line[5:].strip())
        elif line.startswith("#check "):
            checks.append(line[7:].strip())
        elif line.strip() == "#session":
            session = True
        elif line.strip() == "#launcher":
            switch = "BM64_COOP_LAUNCHER"
        elif line.startswith("#env-host "):
            host_envs.append(line[10:].strip())
        elif line.startswith("#env-client "):
            client_envs.append(line[12:].strip())
        elif line.startswith("#client "):
            client_steps.append(line[8:].strip())
        elif line.startswith("#check-client "):
            client_checks.append(line[14:].strip())
        elif line.startswith(("#prev-log-host ", "#prev-log-client ")):
            side, _, text = line[10:].partition(" ")
            prev_logs[side].append(text)
        elif line.startswith(("#saves-host ", "#saves-client ")):
            side, _, name = line[7:].partition(" ")
            saves_from[side] = name.strip()
    results, saves = [], {}
    said, printing = (said, False) if said is not None else ([], True)
    for side, name in saves_from.items():
        # the scenario that leaves the saves runs first, with its checks
        before, ran = scenario_path(name), []
        if not before.is_file():
            results.append((False, f"#saves-{side} {name}", "no such scenario"))
            continue
        results += [(ok, f"{before.stem}: {text}", detail)
                    for ok, text, detail in run_scenario(before, version, timeout, extra_envs, ran, said)]
        if ran and (ran[0] / "saves").is_dir():
            saves[side] = ran[0] / "saves"
        else:
            results.append((False, f"#saves-{side} {name}", "its run left no saves/ folder"))
    own = len(results)  # the earlier scenarios printed their own
    if not session:
        proc = subprocess.run(run_cmd(path, version, timeout, envs + extra_envs, saves=saves.get("host")),
                              capture_output=True, text=True)
        ran = folder if folder is not None else []
        lines = read_run(proc, path.stem, results, ran, said)
        unparsable(lines, results)
        run_checks(checks, lines, parse_probes(lines), results, folder=ran[-1] if ran else None)
    else:
        port = free_port()
        runs = Path.home() / "dev-scratch/BM64Recomp-coop/runs"
        runs.mkdir(parents=True, exist_ok=True)
        client_script = runs / f".{path.stem}-client-{port}.txt"
        client_script.write_text("\n".join(client_steps) + "\n")
        prev_files = {}
        for side, lines in prev_logs.items():
            if lines:
                prev_files[side] = runs / f".{path.stem}-{side}-prev-{port}.log"
                prev_files[side].write_text("\n".join(lines) + "\n")
        procs = []
        for name, script, own_envs, session_env in (
                ("host", path, host_envs, f"{switch}=host:{port}"),
                ("client", client_script, client_envs, f"{switch}=join:127.0.0.1:{port}")):
            cmd = run_cmd(script, version, timeout, envs + own_envs + [session_env] + extra_envs, name,
                          saves.get(name), prev_files.get(name))
            procs.append(subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True))
        for proc in procs:
            proc.wait()
        client_script.unlink()
        for prev in prev_files.values():
            prev.unlink()
        host_dir, client_dir = [], []
        host = until_end(read_run(procs[0], f"{path.stem} host", results, host_dir, said))
        client = until_end(read_run(procs[1], f"{path.stem} client", results, client_dir, said))
        unparsable(host, results)
        unparsable(client, results, "client: ")
        run_checks(checks, host, parse_probes(host), results, client, host_dir[0] if host_dir else None)
        client_results = []
        run_checks(client_checks, client, parse_probes(client), client_results, host,
                   client_dir[0] if client_dir else None)
        results += [(ok, f"client: {text}", detail) for ok, text, detail in client_results]
    for ok, text, detail in results[own:]:
        said.append(f"  {'ok    ' if ok else 'FAILED'} {text}{'  (' + detail + ')' if detail else ''}")
    if printing:
        print("\n".join(said), flush=True)
    return results


def default_jobs(other_exe):
    """How many scenarios to run at once (#71). A run mostly waits for the
    game's own 50 frames a second, but each game still wants a core or two
    and a session scenario starts two: a third of the machine's cores, at
    most 6. --jobs raises it (on 16 cores 8 lanes still passed every check,
    docs/coop/building.md). Another build (--exe: Wine, one prefix) runs
    alone."""
    if other_exe:
        return 1
    return max(1, min(6, (os.cpu_count() or 1) // 3))


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--version", choices=["us", "eu"])
    parser.add_argument("--timeout", type=int, default=120)
    parser.add_argument("--env", action="append", default=[])
    parser.add_argument("--exe")
    parser.add_argument("--exe-client")
    parser.add_argument("--jobs", type=int, default=0, help="scenarios at once (0: pick one)")
    parser.add_argument("scenarios", nargs="+")
    args = parser.parse_args()
    EXES["host"] = args.exe
    EXES["client"] = args.exe_client or args.exe
    jobs = args.jobs or default_jobs(bool(args.exe or args.exe_client))
    paths, results = [], []
    for name in args.scenarios:
        path = scenario_path(name)
        if not path.is_file():
            print(f"FAILED no scenario {name}")
            results.append((False, name, ""))
            continue
        paths.append(path)
    if jobs > 1:
        print(f"{len(paths)} scenarios, {jobs} at a time", flush=True)
    # one lane per scenario, but the output of each is printed whole and in
    # the order they were named (#71)
    with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as pool:
        running = [(pool.submit(run_scenario, path, args.version, args.timeout, args.env, None, said), path, said)
                   for path, said in ((path, []) for path in paths)]
        for future, path, said in running:
            try:
                results += future.result()
            except Exception as err:  # this lane fails, the other scenarios still run (#46)
                said.append(f"{path.stem}:\n  FAILED scenario.py  ({type(err).__name__}: {err})")
                results.append((False, f"{path.stem}: scenario.py", f"{type(err).__name__}: {err}"))
            print("\n".join(said), flush=True)
    passed = sum(ok for ok, _, _ in results)
    print(f"{passed}/{len(results)} checks passed")
    print(f"RESULT {'ok' if passed == len(results) else 'FAILED'}")
    return 0 if passed == len(results) else 1


if __name__ == "__main__":
    sys.exit(main())
