#!/usr/bin/env python3
"""Self-test of tools/scenario.py without the game (#46).

    python3 -I tools/test_scenario.py

Fake runs hand scenario.py the logs below in place of tools/run.sh: garbled
ones (two games writing one game.log) must become failed checks that name the
line, and the scenarios after them must still be checked. tools/verify.sh
runs it in its `harness` stage. Its run folders are made under
~/dev-scratch/BM64Recomp-coop/runs/ and removed again.
"""
import contextlib
import io
import shutil
import sys
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent))
import scenario  # noqa: E402

RUNS = Path.home() / "dev-scratch/BM64Recomp-coop/runs"

PROBE = [
    "coop: probe start",
    "coop: camera eye=-10342,18142,15000 angles=900,450,20000",
    "coop: player slot=0 controlType=1 hearts=1 bombs=2 fire=2 flags=0x120 pos=3000,3000,15000 ghost=0 alpha=255",
]
# what #46 saw: another game's line written into the middle of a probe line
GARBLED = ("coop: player slot=0 controlType=1 hearts=1 bombs=2 fire=2 flags=0x120 "
           "pos=2400,3000,5coop: overlay 0x29")
END = ["coop: input end"]


class FakeRuns:
    """Stands in for tools/run.sh: each run gets a folder with the game.log
    given for its script (by file name; a session's client by "client")."""

    def __init__(self, logs, folder):
        self.logs, self.folder, self.count = logs, folder, 0

    def _run(self, cmd):
        name = cmd[cmd.index("--name") + 1] if "--name" in cmd else None
        script = Path(cmd[cmd.index("--input") + 1]).stem
        log = self.logs.get("client" if name == "client" else script)
        self.count += 1
        run = self.folder / f"run{self.count}"
        run.mkdir()
        if log is not None:
            (run / "game.log").write_text("\n".join(log) + "\n")
        return f"run eu: \"coop: input end\" logged after 1.0 s; overlays: 0x20; folder {run}\n"

    def run(self, cmd, **_):
        return SimpleNamespace(returncode=0, stdout=self._run(cmd))

    def popen(self, cmd, **_):
        out = self._run(cmd)
        return SimpleNamespace(returncode=0, stdout=io.StringIO(out), wait=lambda: 0)


class ScenarioTest(unittest.TestCase):
    def setUp(self):
        RUNS.mkdir(parents=True, exist_ok=True)
        self.folder = Path(tempfile.mkdtemp(prefix=".test-scenario-", dir=RUNS))
        self.addCleanup(shutil.rmtree, self.folder, ignore_errors=True)

    def scenario(self, name, text):
        path = self.folder / f"{name}.txt"
        path.write_text(text)
        return str(path)

    def main(self, logs, *scenarios):
        """scenario.py's main on the scenarios, with fake runs: (exit
        status, output)."""
        fake = FakeRuns(logs, self.folder)
        out = io.StringIO()
        argv = ["scenario.py", "--version", "eu", *scenarios]
        with mock.patch.object(sys, "argv", argv), \
                mock.patch.object(scenario.subprocess, "run", fake.run), \
                mock.patch.object(scenario.subprocess, "Popen", fake.popen), \
                contextlib.redirect_stdout(out):
            status = scenario.main()
        return status, out.getvalue()

    def test_garbled_probe_line_fails_and_the_next_scenario_runs(self):
        first = self.scenario("garbled", "#check log ^coop: probe start$\n#check field start 0 hearts = 1\n")
        second = self.scenario("clean", "#check log ^coop: probe start$\n#check field start 0 hearts = 1\n")
        status, out = self.main({"garbled": PROBE + ["coop: probe later", GARBLED] + END,
                                 "clean": PROBE + END}, first, second)
        self.assertEqual(status, 1, out)
        self.assertRegex(out, r"FAILED log line parses .*5coop: overlay 0x29", out)
        # the first scenario's own checks and the second scenario's still ran
        self.assertEqual(out.count("ok     field start 0 hearts = 1"), 2, out)
        self.assertIn("4/5 checks passed", out)
        self.assertIn("RESULT FAILED", out)

    def test_garbled_values_are_unparsable_lines(self):
        for line in (GARBLED,
                     "coop: player slot=0 controlType=1 hearts=1 bombs=2 fircoop: overlay 0x29",
                     "coop: player slot=0 controlType=1 hearts=05x pos=1,2,3",
                     "coop: camera eye=1,2 angles=900,450,20000",
                     "coop: hash frame=12 hash=0A1Bcoop: input end",
                     "coop: hash frame=1coop: probe x"):
            bad = []
            scenario.parse_probes(["coop: probe p", line], bad)
            scenario.parse_hashes([line], bad)
            self.assertEqual(bad, [line])
        for line in PROBE + ["coop: player slot=4 controlType=2 hearts=0 actor=none",
                             "coop: camera follows slot=4", "coop: hash frame=12 hash=0A1B2C3D"]:
            bad = []
            scenario.parse_probes(PROBE[:1] + [line], bad)
            scenario.parse_hashes([line], bad)
            self.assertEqual(bad, [], line)

    def test_camera_follows_line_is_not_the_probes_camera(self):
        probes = scenario.parse_probes(PROBE + ["coop: camera follows slot=4"])
        self.assertEqual(probes["start"]["camera"]["angles"], (90.0, 45.0, 2000.0))

    def test_check_that_raises_is_a_failed_check(self):
        # actor=none is text: comparing it with a number raises TypeError
        path = self.scenario("text-field", "#check field start 4 actor < 3\n#check log ^coop: input end$\n")
        status, out = self.main({"text-field": PROBE + ["coop: player slot=4 controlType=2 hearts=0 actor=none"]
                                 + END}, path)
        self.assertEqual(status, 1, out)
        self.assertRegex(out, r"FAILED field start 4 actor < 3  \(TypeError: ")
        self.assertIn("1/2 checks passed", out)

    def test_session_garbled_hash_line(self):
        hashes = [f"coop: hash frame={i} hash=0000000{i}" for i in range(3)]
        path = self.scenario("session", "#session\n#check same-hashes\n#check-client log ^coop: input end$\n")
        status, out = self.main({"session": hashes + END,
                                 "client": hashes[:2] + ["coop: hash frame=2 hash=0000coop: overlay 0x29"] + END},
                                path)
        self.assertEqual(status, 1, out)
        self.assertIn("FAILED client: log line parses  (coop: hash frame=2 hash=0000coop: overlay 0x29)", out)
        self.assertIn("ok     same-hashes  (2 frames)", out)
        self.assertIn("ok     client: log ^coop: input end$", out)

    def test_run_without_game_log_fails_and_the_next_scenario_runs(self):
        first = self.scenario("nolog", "#check log ^coop: input end$\n")
        second = self.scenario("clean", "#check log ^coop: input end$\n")
        status, out = self.main({"clean": END}, first, second)
        self.assertEqual(status, 1, out)
        self.assertIn("FAILED run  (no game.log", out)
        self.assertIn("ok     log ^coop: input end$", out)

    def test_scenario_that_crashes_the_checker_is_a_failed_check(self):
        first = self.scenario("crash", "#check log ^coop: input end$\n")
        second = self.scenario("clean", "#check log ^coop: input end$\n")
        real = scenario.run_checks

        def broken(checks, *args, **kwargs):
            if checks == ["log ^coop: input end$"] and not broken.done:
                broken.done = True
                raise RuntimeError("a bug in scenario.py")
            return real(checks, *args, **kwargs)
        broken.done = False
        with mock.patch.object(scenario, "run_checks", broken):
            status, out = self.main({"crash": END, "clean": END}, first, second)
        self.assertEqual(status, 1, out)
        self.assertIn("FAILED scenario.py  (RuntimeError: a bug in scenario.py)", out)
        self.assertIn("ok     log ^coop: input end$", out)


if __name__ == "__main__":
    unittest.main()
