#!/usr/bin/env python3
"""Compare the logs of a session's two sides, one per machine.

usage: python3 -I tools/compare_logs.py <host log> <client log>

tools/scenario.py drives both games itself and compares their logs in one run
(#check same-hashes), which only works for two games on one machine. A session
across two machines (#29) or a playtest (docs/coop/playtest.md) leaves one log
on each PC instead; this reads the two files and says whether the two games
agreed. Either side's game.log, coop.log or the captured stderr will do, with
Unix or Windows line ends.

What it reports, and what makes it fail (exit 1):

- the build identity each side logged in the hello (differ: FAILED, since a
  session refuses such a peer);
- the state hash of every frame both logged, with the first frame that differs
  (needs BM64_COOP_LOG_HASH=N on both sides; neither side logging any is a
  note, not a failure, since a playtest build runs without it);
- coop: session desync, lost or refused before the scripted run ended
  (FAILED); a desync names the parts of the state that differ (#75);
- the snapshot lines both sides logged for the same frame (each side logs its
  values of the first desynced frame, or every N frames with
  BM64_COOP_LOG_STATE=N): the first frame whose lines differ, and those
  lines, side by side (#75);
- per side, for the record: the overlay loads, and from coop: frame lines
  (BM64_COOP_LOG_FRAMES=N) the frames a second and the audio skips.

Both logs are cut at their "coop: input end" as scenario.py does: once one
game is stopped the other loses the session, which is not a fault of the run.
Standard library only.
"""
import argparse
import re
import sys
from pathlib import Path

HASH = re.compile(r"coop: hash frame=(\d+) hash=(\S+)\s*$")
BUILD = re.compile(r"coop: session build (0x[0-9A-Fa-f]+)")
OVERLAY = re.compile(r"coop: overlay (0x[0-9A-Fa-f]+)")
EVENT = re.compile(r"coop: session (?:desync|lost|refused)\b.*")
FRAME = re.compile(r"coop: frame (\d+) (\d+) (\d+) (\d+) (\d+) (\d+)\s*$")
SNAPSHOT = re.compile(r"coop: snapshot frame=(\d+) (.*?)\s*$")


def read(path):
    lines = Path(path).read_text(errors="replace").splitlines()
    end = next((i for i, line in enumerate(lines) if line.startswith("coop: input end")), len(lines) - 1)
    return [line.rstrip("\r") for line in lines[: end + 1]]


def found(pattern, lines):
    return [match for match in map(pattern.search, lines) if match]


def rate(lines):
    frames = found(FRAME, lines)
    if len(frames) < 2:
        return "no coop: frame lines"
    first, last = frames[0], frames[-1]
    passed = int(last.group(2)) - int(first.group(2))
    count = int(last.group(1)) - int(first.group(1))
    skips = int(last.group(6)) - int(first.group(6))
    speed = f"{count * 1000 / passed:.1f}" if passed else "?"
    return f"{count} frames in {passed / 1000:.1f} s, {speed} a second, {skips} audio skips"


def snapshots(lines):
    """The snapshot lines by frame, each frame's in the order logged."""
    frames = {}
    for match in found(SNAPSHOT, lines):
        frames.setdefault(int(match.group(1)), []).append(match.group(2))
    return frames


def compare_snapshots(host, client):
    """The first frame both sides took a snapshot of whose lines differ, and
    those lines: what parted the two games (#75)."""
    sides = [snapshots(host), snapshots(client)]
    both = sorted(set(sides[0]) & set(sides[1]))
    if not both:
        return ["snapshots: none of the same frame on both sides"], []
    for frame in both:
        mine, theirs = sides[0][frame], sides[1][frame]
        if mine == theirs:
            continue
        out = [f"snapshots: {len(both)} frames on both sides, first that differs {frame}:"]
        for line in mine:
            if line not in theirs:
                out.append(f"  host   {line}")
        for line in theirs:
            if line not in mine:
                out.append(f"  client {line}")
        return out, [f"snapshot of frame {frame} differs"]
    return [f"snapshots: {len(both)} frames on both sides, all equal"], []


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("host")
    parser.add_argument("client")
    args = parser.parse_args()
    sides = {"host": read(args.host), "client": read(args.client)}
    failed = []

    builds = {}
    for name, lines in sides.items():
        ids = ["0x%08X" % int(match.group(1), 16) for match in found(BUILD, lines)]
        builds[name] = ids[0] if ids else None
        loads = [match.group(1) for match in found(OVERLAY, lines)]
        print(f"{name}: {len(lines)} lines, build {builds[name] or '(none logged: no hello)'}")
        print(f"{name}: {len(loads)} overlay loads {' '.join(loads)}")
        print(f"{name}: {rate(lines)}")
    if builds["host"] and builds["client"] and builds["host"] != builds["client"]:
        failed.append(f"build identity {builds['host']} against {builds['client']}")

    hashes = [dict(match.groups() for match in map(HASH.match, lines) if match) for lines in sides.values()]
    both = sorted(set(hashes[0]) & set(hashes[1]), key=int)
    if not both:
        print("hashes: no frame hashed by both (BM64_COOP_LOG_HASH off on a side)")
    else:
        differ = [frame for frame in both if hashes[0][frame] != hashes[1][frame]]
        print(f"hashes: {len(both)} frames hashed by both ({both[0]}..{both[-1]}), "
              + (f"{len(differ)} differ, first frame {differ[0]} "
                 f"({hashes[0][differ[0]]} against {hashes[1][differ[0]]})" if differ else "all equal"))
        if differ:
            failed.append(f"{len(differ)} frames differ, first {differ[0]}")

    events = [match.group(0) for lines in sides.values() for match in found(EVENT, lines)]
    print("events: " + ("; ".join(events) if events else "no desync, lost or refused"))
    failed += events

    report, differ = compare_snapshots(sides["host"], sides["client"])
    print("\n".join(report))
    failed += differ

    for line in failed:
        print(f"FAILED {line}")
    print(f"RESULT {'ok' if not failed else 'FAILED'}")
    return 0 if not failed else 1


if __name__ == "__main__":
    sys.exit(main())
