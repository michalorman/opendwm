"""Run with python3 -m unittest discover -s wayland/tests -v. No compositor used."""
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile
import time
import unittest


SCRIPTS = Path(__file__).resolve().parents[1] / "scripts"
MOCK = Path(__file__).with_name("mock_hyprctl.py").resolve()


def client(workspace="special:term", mapped=True):
    # Hyprland clears the workspace on unmap and reports an empty name.
    if not mapped and workspace == "special:term":
        workspace = ""
    return [{"class": "test'class", "address": "0xabc", "mapped": mapped,
             "workspace": {"name": workspace}}]


class ScriptsTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        (self.root / "hyprctl").symlink_to(MOCK)
        self.state = self.root / "state.json"
        self.env = dict(os.environ, PATH=f"{self.root}:{os.environ['PATH']}",
                        XDG_RUNTIME_DIR=str(self.root),
                        HYPRLAND_INSTANCE_SIGNATURE="test_123", MOCK_STATE=str(self.state))

    def configure(self, **state):
        self.state.write_text(json.dumps(state))

    def run_script(self, script, *args, success=True):
        result = subprocess.run([str(SCRIPTS / script), *args], env=self.env,
                                text=True, capture_output=True, timeout=10)
        self.assertEqual(result.returncode == 0, success, result.stderr)
        return result

    def calls(self):
        return json.loads(self.state.read_text()).get("calls", [])

    def mutations(self):
        # Normalize Lua-mode calls back to short semantic names so the
        # assertions stay readable.
        out = []
        for call in self.calls():
            if call[0] == "eval":
                if "exec_cmd" in call[1]:
                    out.append(["dispatch", "exec", call[1]])
                else:
                    out.append(["eval", call[1]])
            elif call[0] == "dispatch":
                expr = call[1]
                if "toggle_special" in expr:
                    out.append(["dispatch", "togglespecialworkspace", expr])
                elif "follow=false" in expr:
                    out.append(["dispatch", "movetoworkspacesilent", expr])
                else:
                    out.append(["dispatch", "movetoworkspace", expr])
            elif call[0] == "keyword":
                out.append(call)
        return out

    def test_delta_grammar(self):
        for delta in ("--1", "1-2", "+2", "01", "1001", "-1001", "999999999999999999999", ""):
            with self.subTest(delta=delta):
                self.configure()
                self.run_script("adjust-gaps", delta, success=False)
                self.assertEqual(self.calls(), [])

    def test_gap_policy_call(self):
        for delta in ("-2", "0", "2"):
            with self.subTest(delta=delta):
                self.configure()
                self.run_script("adjust-gaps", delta)
                self.assertEqual(self.mutations(),
                                 [["eval", f"opendwm_adjust_gaps({delta})"]])

    def test_rejected_gap_policy_call(self):
        for response in ({"reply": "error"}, {"reply": "ok", "status": 1},
                          {"reply": "ok\nok"}):
            self.configure(responses=[response])
            self.run_script("adjust-gaps", "1", success=False)
            self.assertEqual(len(self.mutations()), 1)

    def scratch(self, success=True, *args):
        return self.run_script("scratchpad", "term", "test'class", "app", *args, success=success)

    def test_bad_query_never_spawns(self):
        for data in ({"raw": "broken"}, {"raw": "[] []"}, {"raw": "[]", "status": 1}, {},
                     [{"class": "test'class"}], client() * 2):
            with self.subTest(data=data):
                self.configure(queries=[data])
                self.scratch(False)
                self.assertEqual(self.mutations(), [])

    def test_mapping_failure_and_pending_retry(self):
        self.configure(queries=[[], {"raw": "bad data"}])
        self.scratch(False)
        self.assertEqual([c[1] for c in self.mutations()], ["exec"])
        self.configure(queries=[[]])
        self.scratch(False)
        self.assertEqual(self.mutations(), [])
        self.configure(queries=[client()])
        self.scratch()
        self.assertFalse(list(self.root.glob("*.pending")))

    def test_mapping_deadline(self):
        self.configure(queries=[[], client(mapped=False)])
        start = time.monotonic()
        self.scratch(False)
        self.assertLess(time.monotonic() - start, 8)
        self.assertEqual([c[1] for c in self.mutations()], ["exec"])

    def test_slow_query_deadline(self):
        self.configure(queries=[[], {"raw": "[]", "delay": 20}])
        start = time.monotonic()
        self.scratch(False)
        self.assertLess(time.monotonic() - start, 8)
        self.assertEqual([c[1] for c in self.mutations()], ["exec"])

    def test_pending_marker_never_created_without_dispatch(self):
        # The initial query consumes the budget: the script must fail before
        # creating a pending marker for a launch it never attempted. 5.5s is
        # past the 2s minimum-budget guard regardless of SECONDS rounding.
        self.configure(queries=[{"raw": "[]", "delay": 5.5}])
        self.scratch(False)
        self.assertEqual(self.mutations(), [])
        self.assertFalse(list(self.root.glob("*.pending")))
        self.configure(queries=[client()])
        self.scratch()
        self.assertEqual([c[1] for c in self.mutations()], ["togglespecialworkspace"])

    def test_launch_into_open_workspace_does_not_hide_it(self):
        # Special workspace already displayed: launching must ensure
        # visibility, never toggle it closed.
        self.configure(queries=[[], client()], special_visible=True)
        self.scratch()
        self.assertEqual([c[1] for c in self.mutations()], ["exec"])
        self.assertTrue(json.loads(self.state.read_text())["special_visible"])

    def test_recovery_into_open_workspace_does_not_hide_it(self):
        self.configure(queries=[client("1"), client()], special_visible=True)
        self.scratch()
        self.assertEqual(self.mutations(), [
            ["dispatch", "movetoworkspacesilent", "hl.dsp.window.move({workspace=\"special:term\", follow=false, window=\"address:0xabc\"})"]])
        self.assertTrue(json.loads(self.state.read_text())["special_visible"])

    def test_unmapped_client_is_not_serialized(self):
        # `clients -a` crashes Hyprland 0.56.2 when an unmapped client has an
        # idle-inhibitor surface. Ignore it and recover the newly mapped client.
        self.configure(queries=[client(mapped=False), client("1"), client()])
        self.scratch()
        self.assertEqual([c[1] for c in self.mutations()],
                         ["exec", "movetoworkspacesilent", "togglespecialworkspace"])
        self.assertEqual(self.mutations()[1],
                          ["dispatch", "movetoworkspacesilent", "hl.dsp.window.move({workspace=\"special:term\", follow=false, window=\"address:0xabc\"})"])
        self.assertTrue(all("-a" not in call for call in self.calls()))

    def test_visibility_query_failure_never_toggles(self):
        # A failed monitor query must not be read as "not visible": the
        # already-open workspace must stay open and the run must fail.
        self.configure(queries=[[], client()], special_visible=True,
                       monitors={"raw": "boom", "status": 1})
        self.scratch(False)
        self.assertEqual([c[1] for c in self.mutations()], ["exec"])
        self.assertTrue(json.loads(self.state.read_text())["special_visible"])

    def test_dispatch_failure_status_keeps_pending_marker(self):
        # timeout propagates the invoked command's status: any nonzero exec
        # result is uncertain, so the marker must block duplicate launches.
        self.configure(queries=[[]], responses=[{"reply": "ok", "status": 90}])
        self.scratch(False)
        self.assertEqual([c[1] for c in self.mutations()], ["exec"])
        self.assertTrue(list(self.root.glob("*.pending")))
        self.configure(queries=[[]])
        self.scratch(False)
        self.assertEqual(self.mutations(), [])

    def test_rejection_clears_pending_and_allows_retry(self):
        self.configure(queries=[[]], responses=[{"reply": "error"}])
        self.scratch(False)
        self.assertFalse(list(self.root.glob("*.pending")))
        self.configure(queries=[[], client()])
        self.scratch()
        self.assertEqual([c[1] for c in self.mutations()][:2], ["exec", "togglespecialworkspace"])

    def test_pending_cleared_when_mapping_confirmed_before_failed_recovery(self):
        # Uncertain launch leaves a marker; the client then maps on a normal
        # workspace. Mapping confirms the outcome, so the marker must be
        # cleared even though the recovery dispatch is rejected.
        self.configure(queries=[[]], responses=[{"reply": "ok", "status": 124}])
        self.scratch(False)
        self.assertTrue(list(self.root.glob("*.pending")))
        self.configure(queries=[client("1")], responses=[{"reply": "error"}])
        self.scratch(False)
        self.assertFalse(list(self.root.glob("*.pending")))
        # Same guarantee when recovery's readback (not the dispatch) fails.
        self.configure(queries=[[]], responses=[{"reply": "ok", "status": 124}])
        self.scratch(False)
        self.assertTrue(list(self.root.glob("*.pending")))
        self.configure(queries=[client("1")])
        self.scratch(False)  # readback never shows the special workspace
        self.assertFalse(list(self.root.glob("*.pending")))

    def test_malformed_visibility_data_never_toggles(self):
        # Successful transport with malformed specialWorkspace data must
        # fail safely, never read as "not visible" and toggle.
        bad = [
            [{"id": 0, "focused": True}],
            [{"id": 0, "focused": True, "specialWorkspace": None}],
            [{"id": 0, "focused": True, "specialWorkspace": {"id": -5}}],
            [{"id": 0, "focused": True, "specialWorkspace": {"id": -5, "name": None}}],
            [{"id": 0, "focused": True, "specialWorkspace": {"id": -5, "name": 5}}],
        ]
        for monitors in bad:
            with self.subTest(monitors=monitors):
                self.configure(queries=[[], client()], special_visible=True,
                               monitors={"raw": json.dumps(monitors)})
                self.scratch(False)
                self.assertEqual([c[1] for c in self.mutations()], ["exec"])
                self.assertTrue(json.loads(self.state.read_text())["special_visible"])
        # Explicit empty name remains a valid "not visible" report.
        self.configure(queries=[[], client()],
                       monitors={"raw": json.dumps([
                           {"id": 0, "focused": True,
                            "specialWorkspace": {"id": 0, "name": ""}}])})
        self.scratch()
        self.assertEqual([c[1] for c in self.mutations()], ["exec", "togglespecialworkspace"])

    def test_recovery(self):
        self.configure(queries=[client("1"), client()])
        self.scratch()
        self.assertEqual(self.mutations(), [
            ["dispatch", "movetoworkspacesilent", "hl.dsp.window.move({workspace=\"special:term\", follow=false, window=\"address:0xabc\"})"],
            ["dispatch", "togglespecialworkspace", "hl.dsp.workspace.toggle_special(\"term\")"]])
        for queries, responses in (([client("1")], [{"reply": "error"}]),
                                   ([client("1")], []),
                                   ([client("1"), {"raw": "bad"}], [])):
            self.configure(queries=queries, responses=responses)
            self.scratch(False)
            self.assertEqual(len(self.mutations()), 1)

    def test_launch_quotes(self):
        args = ("", "a'b", "with spaces", "$(touch nope);", "line\n", "\\", '"quoted"')
        self.configure(queries=[[], client()])
        self.scratch(True, *args)
        entry = self.mutations()[0][2]
        self.assertTrue(entry.startswith("hl.exec_cmd('") and entry.endswith("')"))
        # Unwrap the Lua string (reverse of the script's escaping), then the
        # shell quoting must reproduce the original argument vector.
        command = (entry[len("hl.exec_cmd('"):-2]
                   .replace("\\\\", "\0")
                   .replace("\\n", "\n").replace("\\r", "\r").replace("\\'", "'")
                   .replace("\0", "\\"))
        self.assertEqual(shlex.split(command), ["app", *args])
        parsed = subprocess.check_output(
            ["sh", "-c", 'set -- ' + command + '; printf "%s\\0" "$@"'])
        self.assertEqual(parsed.decode().split("\0")[:-1], ["app", *args])
        self.assertEqual(self.mutations()[-1][1], "togglespecialworkspace")

    @unittest.skipUnless(shutil.which("lua"), "lua required to parse generated code")
    def test_generated_lua_is_valid_and_preserves_arguments(self):
        args = ("a'b", "with spaces", "line\n", "carriage\rreturn", "\\", '"quoted"')
        self.configure(queries=[[], client()])
        self.scratch(True, *args)
        entry = self.mutations()[0][2]
        # A real Lua interpreter evaluates the expression: syntax must be
        # valid and the delivered shell command must be exactly the script's
        # single-quote encoding of the original argument vector.
        def shq(value):
            return "'" + value.replace("'", "'\\''") + "'"
        expected = " ".join(shq(a) for a in ["app", *args])
        script = ('local got; hl = {exec_cmd = function(s) got = s end}; '
                  + entry + '; io.write(got)')
        out = subprocess.check_output(["lua", "-e", script])
        self.assertEqual(out.decode(), expected)

    def test_exec_rejection_never_toggles(self):
        self.configure(queries=[[]], responses=[{"reply": "error"}])
        self.scratch(False)
        self.assertEqual([c[1] for c in self.mutations()], ["exec"])

    def test_runtime_and_names(self):
        self.configure(queries=[[]])
        self.run_script("scratchpad", "bad;name", "class", "app", success=False)
        self.assertEqual(self.calls(), [])
        self.env.pop("XDG_RUNTIME_DIR")
        self.scratch(False)
        self.run_script("adjust-gaps", "1", success=False)
        self.assertEqual(self.calls(), [])

    def test_launch_normal_targets_normal_workspace(self):
        self.configure()
        self.run_script("launch-normal", "ghostty", "--title=a b")
        evals = [c for c in self.calls() if c[0] == "eval"]
        self.assertEqual(len(evals), 1)
        # Single quotes from the shell quoting are escaped for Lua.
        self.assertEqual(evals[0][1],
                         "hl.exec_cmd('\\'ghostty\\' \\'--title=a b\\'', {workspace=1})")
        if shutil.which("lua"):
            entry = evals[0][1]
            script = ('local got; hl = {exec_cmd = function(s, r) got = s; ws = r.workspace end}; '
                      + entry + '; io.write(got .. "|" .. tostring(ws))')
            out = subprocess.check_output(["lua", "-e", script])
            self.assertEqual(out.decode(), "'ghostty' '--title=a b'|1")

    def test_move_validation_and_response(self):
        for args in ((), ("0",), ("1;exec",), ("1", "2"), ("999999999999",)):
            self.configure()
            self.run_script("move-to-workspace", *args, success=False)
            self.assertEqual(self.calls(), [])
        self.configure()
        self.run_script("move-to-workspace", "3")
        for response in ({"reply": "error"}, {"reply": "ok", "status": 1}):
            self.configure(responses=[response])
            self.run_script("move-to-workspace", "3", success=False)


if __name__ == "__main__":
    unittest.main()
