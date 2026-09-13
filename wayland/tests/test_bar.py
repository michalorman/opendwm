"""Mock-only regression tests: python3 -m unittest discover -s wayland/tests -p test_bar.py."""

import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import time
import unittest


SCRIPTS = Path(__file__).resolve().parents[1] / "scripts"
MOCK = r'''#!/usr/bin/env python3
import json, os, pathlib, signal, sys, time
root = pathlib.Path(os.environ["MOCK_ROOT"])
args = sys.argv[1:]
with (root / "calls").open("a") as log:
    log.write(json.dumps([os.getpid(), args]) + "\n")
if args[0] == "--path":
    descriptors = []
    for fd in pathlib.Path("/proc/self/fd").iterdir():
        try:
            descriptors.append(os.readlink(fd))
        except FileNotFoundError:
            pass
    (root / (str(os.getpid()) + ".fds")).write_text(json.dumps(descriptors))
    inherited = sorted(k for k in os.environ if k.startswith("QS_"))
    (root / (str(os.getpid()) + ".env")).write_text(json.dumps(inherited))
    if (root / "mode").read_text() == "stubborn":
        signal.signal(signal.SIGTERM, signal.SIG_IGN)
    # Readiness is published only after initialization; IPC honors it.
    (root / (str(os.getpid()) + ".ready")).write_text("ready")
    while True:
        time.sleep(1)
mode = (root / "mode").read_text()
if args[0] == "ipc" and "--pid" in args:
    target = args[args.index("--pid") + 1]
    if not (root / (target + ".ready")).exists():
        sys.exit(0)
if mode == "hang" or (mode == "toggle-hang" and args[-1] == "toggle"):
    time.sleep(30)
if mode == "silent":
    sys.exit(0)
if mode == "wrong":
    print("not-the-bar")
elif args[-1] == "ping":
    print("opendwm-bar")
elif mode != "toggle-fail":
    print("hidden")
sys.exit(1)
'''


class BarTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        mock = self.root / "quickshell"
        mock.write_text(MOCK)
        mock.chmod(0o700)
        self.mode("ok")
        self.env = dict(os.environ, PATH=f"{self.root}:{os.environ['PATH']}",
                        XDG_RUNTIME_DIR=str(self.root), MOCK_ROOT=str(self.root),
                        HYPRLAND_INSTANCE_SIGNATURE="session/one")

    def tearDown(self):
        for pid in self.mock_pids():
            try:
                os.kill(pid, signal.SIGKILL)  # stubborn children must not leak
            except ProcessLookupError:
                pass
        self.temp.cleanup()

    def mock_pids(self):
        # The publication-failure path may kill the child before the mock
        # interpreter logs anything, so process state is the reliable signal.
        alive = []
        marker = str(self.root / "quickshell").encode()
        for entry in Path("/proc").iterdir():
            if not entry.name.isdigit():
                continue
            try:
                cmdline = entry.joinpath("cmdline").read_bytes()
            except OSError:
                continue
            if marker in cmdline:
                alive.append(int(entry.name))
        return alive

    def mode(self, mode):
        (self.root / "mode").write_text(mode)

    def calls(self):
        path = self.root / "calls"
        return [json.loads(line) for line in path.read_text().splitlines()] if path.exists() else []

    def run_bar(self, toggle=False, success=True, env=None):
        command = (["sh", str(SCRIPTS / "toggle-bar")] if toggle else
                   [sys.executable, str(SCRIPTS / "start-bar")])
        result = subprocess.run(command, env=env or self.env, capture_output=True, timeout=7)
        self.assertEqual(result.returncode == 0, success, result.stderr.decode())
        return result

    def starts(self):
        return [(pid, args) for pid, args in self.calls() if args[0] == "--path"]

    def test_start_closes_lock_and_targets_pid_despite_bad_exit_status(self):
        self.run_bar()
        pid, args = self.starts()[0]
        self.assertEqual(args, ["--path", str((SCRIPTS.parent / "quickshell/shell.qml").resolve())])
        fds = json.loads((self.root / f"{pid}.fds").read_text())
        self.assertFalse(any(fd.endswith("/lock") for fd in fds))
        self.run_bar()
        self.run_bar(toggle=True)
        self.assertEqual(len(self.starts()), 1)
        for _, args in self.calls():
            if args[0] == "ipc":
                self.assertEqual(args[:5], ["ipc", "--pid", str(pid), "call", "bar"])
        self.assertEqual(sum(args[-1] == "toggle" for _, args in self.calls()), 1)

    def test_sessions_have_distinct_instances(self):
        self.run_bar()
        other = dict(self.env, HYPRLAND_INSTANCE_SIGNATURE="session/two")
        self.run_bar(env=other)
        self.run_bar(toggle=True, env=other)
        self.assertEqual(len(self.starts()), 2)
        self.assertEqual(self.calls()[-1][1][2], str(self.starts()[1][0]))

    def test_ipc_failures_never_start_duplicates_or_retry_toggle(self):
        self.run_bar()
        for mode in ("silent", "wrong", "hang", "toggle-fail", "toggle-hang"):
            with self.subTest(mode=mode):
                self.mode(mode)
                before = len(self.calls())
                began = time.monotonic()
                self.run_bar(toggle=True, success=False)
                self.assertLess(time.monotonic() - began, 5)
                self.assertEqual(len(self.starts()), 1)
                self.assertLessEqual(sum(args[-1] == "toggle" for _, args in self.calls()[before:]), 1)

    def test_failed_start_readiness_retains_pid(self):
        self.mode("silent")
        self.run_bar(success=False)
        self.mode("ok")
        self.run_bar()
        self.assertEqual(len(self.starts()), 1)

    def test_reused_pid_is_not_targeted(self):
        self.run_bar()
        state = next(self.root.glob("opendwm-bar-*/instance.json"))
        record = json.loads(state.read_text())
        record.update(pid=os.getpid(), starttime="0")
        state.write_text(json.dumps(record))
        self.run_bar()
        self.assertEqual(len(self.starts()), 2)
        self.assertFalse(any(args[:3] == ["ipc", "--pid", str(os.getpid())] for _, args in self.calls()))

    def test_wrong_scope_fails_closed(self):
        self.run_bar()
        state = next(self.root.glob("opendwm-bar-*/instance.json"))
        record = json.loads(state.read_text())
        record["signature"] = "another-session"
        state.write_text(json.dumps(record))
        self.run_bar(toggle=True, success=False)
        self.assertEqual(len(self.starts()), 1)

    def test_live_unrelated_pid_fails_closed(self):
        self.run_bar()
        state = next(self.root.glob("opendwm-bar-*/instance.json"))
        record = json.loads(state.read_text())
        starttime = Path("/proc/self/stat").read_text().rsplit(")", 1)[1].split()[19]
        record.update(pid=os.getpid(), starttime=starttime)
        state.write_text(json.dumps(record))
        before = self.calls()
        self.run_bar(toggle=True, success=False)
        self.assertEqual(self.calls(), before)

    def test_concurrent_starts_are_serialized(self):
        command = [sys.executable, str(SCRIPTS / "start-bar")]
        children = [subprocess.Popen(command, env=self.env, stdout=subprocess.PIPE,
                                     stderr=subprocess.PIPE) for _ in range(4)]
        for child in children:
            _, error = child.communicate(timeout=7)
            self.assertEqual(child.returncode, 0, error.decode())
        self.assertEqual(len(self.starts()), 1)

    def test_missing_session_fails_without_launch(self):
        self.run_bar(success=False, env=dict(self.env, HYPRLAND_INSTANCE_SIGNATURE=""))
        self.assertEqual(self.starts(), [])

    def test_quickshell_env_options_are_not_inherited(self):
        env = dict(self.env, QS_CONFIG_PATH="/elsewhere", QS_CONFIG_NAME="other",
                   QS_MANIFEST="/manifest", QS_KEEP="also-removed")
        self.run_bar(env=env)
        pid, _ = self.starts()[0]
        inherited = json.loads((self.root / f"{pid}.env").read_text())
        self.assertEqual(inherited, ["QS_KEEP"])

    def test_publication_failure_terminates_new_child(self):
        import hashlib
        directory = self.root / ("opendwm-bar-" +
                                 hashlib.sha256(b"session/one").hexdigest())
        directory.mkdir()
        (directory / "instance.tmp").mkdir()  # makes state publication fail
        self.run_bar(success=False)
        # The started child must not survive a failed state publication.
        self.assertEqual(self.mock_pids(), [])
        # A retry after fixing the directory starts exactly one new bar.
        (directory / "instance.tmp").rmdir()
        self.run_bar()
        self.assertEqual(len(self.mock_pids()), 1)

    def test_sigterm_during_publication_kills_new_child(self):
        env = dict(self.env, OPENDWM_BAR_TEST_HOOK="1", OPENDWM_BAR_START_DELAY="5")
        helper = subprocess.Popen([sys.executable, str(SCRIPTS / "start-bar")],
                                  env=env, stdout=subprocess.PIPE,
                                  stderr=subprocess.PIPE)
        deadline = time.monotonic() + 5
        while not self.mock_pids() and time.monotonic() < deadline:
            time.sleep(0.05)
        self.assertTrue(self.mock_pids(), "child was never started")
        helper.send_signal(signal.SIGTERM)
        _, error = helper.communicate(timeout=10)
        self.assertNotEqual(helper.returncode, 0)
        self.assertIn(b"signal", error)
        self.assertEqual(self.mock_pids(), [])

    def test_stubborn_child_is_killed_and_reaped(self):
        # Synchronize on the child's readiness before forcing publication
        # failure: the child has provably installed SIG_IGN, so cleanup must
        # escalate through the full 2s SIGTERM wait to SIGKILL.
        self.mode("stubborn")
        flag = self.root / "fail-publish"
        env = dict(self.env, OPENDWM_BAR_TEST_HOOK="1", OPENDWM_BAR_START_DELAY="10",
                   OPENDWM_BAR_FAIL_PUBLISH=str(flag))
        helper = subprocess.Popen([sys.executable, str(SCRIPTS / "start-bar")],
                                  env=env, stdout=subprocess.PIPE,
                                  stderr=subprocess.PIPE)
        deadline = time.monotonic() + 5
        while not list(self.root.glob("*.ready")) and time.monotonic() < deadline:
            time.sleep(0.05)
        ready = list(self.root.glob("*.ready"))
        self.assertTrue(ready, "child never became ready")
        child_pid = int(ready[0].stem)

        # Sample the child's process state concurrently. Without the
        # post-SIGKILL child.wait(), the child remains a zombie of the
        # still-alive helper until helper exit; with it, no zombie is
        # observable in the final phase. cmdline scans cannot see zombies.
        import threading
        states = []
        halt = threading.Event()

        def sample():
            while not halt.is_set():
                try:
                    stat = Path(f"/proc/{child_pid}/stat").read_text()
                    states.append((time.monotonic(), stat.rsplit(")", 1)[1].split()[0]))
                except (FileNotFoundError, ProcessLookupError, IndexError):
                    states.append((time.monotonic(), "gone"))
                time.sleep(0.005)

        sampler = threading.Thread(target=sample, daemon=True)
        sampler.start()
        flag.touch()
        began = time.monotonic()
        _, error = helper.communicate(timeout=15)
        elapsed = time.monotonic() - began
        exited = time.monotonic()
        halt.set()
        sampler.join(timeout=2)

        self.assertNotEqual(helper.returncode, 0, error.decode())
        # Cleanup began at the flag, so ~2s proves the full SIGTERM wait;
        # immediate SIGKILL would finish far sooner.
        self.assertGreaterEqual(elapsed, 1.5)
        self.assertLess(elapsed, 8)
        # Reaping proof: no zombie in the 0.5s before the helper exited.
        late_zombies = [state for stamp, state in states
                        if stamp > exited - 0.5 and state == "Z"]
        self.assertEqual(late_zombies, [])
        self.assertEqual(self.mock_pids(), [])


if __name__ == "__main__":
    unittest.main()
