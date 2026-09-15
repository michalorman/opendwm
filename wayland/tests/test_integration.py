"""Session argv and QML JavaScript logic tests; not a live Qt/compositor test."""
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest


WAYLAND = Path(__file__).resolve().parents[1]
QMLTESTRUNNER = shutil.which("qmltestrunner6") or (
    "/usr/lib/qt6/bin/qmltestrunner" if Path("/usr/lib/qt6/bin/qmltestrunner").exists() else None)


class IntegrationTests(unittest.TestCase):
    def fullscreen_command(self):
        # Reassemble the embedded shell command from the QML source so the
        # test exercises the actual pipeline, not a copy of it.
        qml = (WAYLAND / "quickshell/shell.qml").read_text()
        block = re.search(r'command: \["sh", "-c",(.*?)\]\n', qml, re.S)
        self.assertIsNotNone(block)
        segments = re.findall(r'"((?:[^"\\]|\\.)*)"', block.group(1))
        self.assertGreaterEqual(len(segments), 2)
        return "".join(segments).replace('\\"', '"')

    def run_fullscreen_command(self, root, state):
        mock = root / "hyprctl"
        mock.write_text(
            '#!/usr/bin/env python3\n'
            'import json, os, sys\n'
            'state = json.load(open(os.environ["MOCK_STATE"]))\n'
            'key = "monitors" if "monitors" in sys.argv else "clients"\n'
            'item = state[key]\n'
            'sys.stdout.write(item["out"])\n'
            'sys.exit(item.get("status", 0))\n')
        mock.chmod(0o700)
        (root / "state.json").write_text(json.dumps(state))
        env = dict(os.environ, PATH=f"{root}:{os.environ['PATH']}",
                   MOCK_STATE=str(root / "state.json"))
        return subprocess.run(["sh", "-c", self.fullscreen_command()],
                              env=env, capture_output=True, text=True, timeout=10)

    def test_fullscreen_command_sentinel(self):
        ok_clients = json.dumps([{
            "mapped": True, "hidden": False, "fullscreen": 2, "monitor": 0,
            "workspace": {"id": 1}, "class": "x", "title": "t",
        }])
        ok_monitors = json.dumps([{
            "id": 0, "name": "DP-1", "focused": True,
            "specialWorkspace": {"id": 0, "name": ""},
            "activeWorkspace": {"id": 1, "name": "1"},
        }])
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            cases = [
                ("ok", {"clients": {"out": ok_clients}, "monitors": {"out": ok_monitors}}, True),
                ("clients fail", {"clients": {"out": "", "status": 1},
                                  "monitors": {"out": ok_monitors}}, False),
                ("monitors fail", {"clients": {"out": ok_clients},
                                   "monitors": {"out": "", "status": 1}}, False),
                ("trailing garbage", {"clients": {"out": ok_clients + "garbage"},
                                      "monitors": {"out": ok_monitors}}, False),
                ("timeout status", {"clients": {"out": ok_clients, "status": 124},
                                    "monitors": {"out": ok_monitors}}, False),
            ]
            for name, state, expect_marker in cases:
                with self.subTest(name):
                    result = self.run_fullscreen_command(root, state)
                    self.assertEqual(result.stdout.endswith("__OPENDWM_OK__\n"),
                                     expect_marker, result.stderr)
                    if expect_marker:
                        body = result.stdout[:-len("__OPENDWM_OK__\n")]
                        data = json.loads(body)
                        self.assertEqual(len(data["clients"]), 1)
                        self.assertEqual(len(data["monitors"]), 1)

    def test_session_selects_lua_explicitly(self):
        with tempfile.TemporaryDirectory(prefix="opendwm session ") as directory:
            root = Path(directory)
            config = root / "hypr"
            config.mkdir()
            # A legacy .conf must not shadow the explicit Lua selection.
            (config / "hyprland.conf").touch()
            (config / "hyprland.lua").touch()
            launcher = root / "start-hyprland"
            launcher.write_text('#!/bin/sh\nprintf "%s\\n" "$@"\n')
            launcher.chmod(0o700)
            env = dict(os.environ, XDG_CONFIG_HOME=str(root), PATH=f"{root}:{os.environ['PATH']}")
            result = subprocess.run(
                ["sh", str(WAYLAND / "session/start-opendwm-wayland")],
                env=env, capture_output=True, text=True, check=True,
            )
            self.assertEqual(result.stdout.splitlines(),
                             ["--", "--config", str(config / "hyprland.lua")])
            (config / "hyprland.lua").unlink()
            result = subprocess.run(
                ["sh", str(WAYLAND / "session/start-opendwm-wayland")],
                env=env, capture_output=True, text=True,
            )
            self.assertNotEqual(result.returncode, 0)

    @unittest.skipUnless(QMLTESTRUNNER, "Qt 6 Quick Test required")
    def test_yawc_layout_and_startup(self):
        qml = (WAYLAND / "quickshell/yawc.qml").read_text()
        # Exercise the actual functions, timers, ListView and image delegates
        # in Qt, without starting Quickshell or changing the desktop wallpaper.
        state = qml[qml.index('    property string wallpaperDir:'):
                    qml.index('    readonly property string cacheDir:')]
        view = qml[qml.index('        ListView {'):
                   qml.index('\n        Text {', qml.index('        ListView {'))]
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            image = folder / "wallpaper.svg"
            image.write_text('<svg xmlns="http://www.w3.org/2000/svg" width="160" '
                             'height="100"><rect width="160" height="100" fill="blue"/></svg>')
            state = state.replace('Quickshell.env("HOME")', json.dumps(directory))
            harness = ('import QtQuick\nimport QtTest\nItem {\n'
                       'id: root; width: 1200; height: 800\n'
                       'property var thumbMap: ({})\n'
                       + state + '\nItem { id: mainScope; anchors.fill: parent\n'
                       + view + '\n}\nTestCase {\n'
                       'name: "YawcLayout"; when: windowShown\n'
                       'function initTestCase() {\n'
                       f'  for (let i = 0; i < 23; ++i) wallpaperModel.append({{path: {json.dumps(str(image))}}})\n'
                       '  compare(carousel.count, 0)\n'
                       '  root.scanFinished = true\n'
                       '  root.beginEntry()\n'
                       '  tryCompare(root, "entryReady", true, 10000)\n'
                       '}\n' + r'''
function centered() {
    const card = carousel.currentItem
    return card && card.isCurrent && card.width === root.focusWidth
        && Math.abs(card.mapToItem(carousel, card.width / 2, 0).x - carousel.width / 2) < 1
}
function test_startup_and_navigation() {
    tryVerify(centered, 3000)
    compare(carousel.interactive, false)
    compare(carousel.highlightMoveDuration, 220)
    verify(carousel.currentItem.contentReady)
    tryCompare(root, "previewReady", true, 3000)
    verify(carousel.currentItem.loadFullRes)
    verify(!carousel.itemAtIndex(carousel.currentIndex + 1).loadFullRes)
    for (const delta of [1, 1, -1, -1, 7, -7]) {
        root.navigateTo(carousel.currentIndex + delta)
        compare(root.previewReady, false)
        tryVerify(centered, 3000)
    }
    for (let i = 0; i < 10; ++i) root.navigateTo(carousel.currentIndex + 1)
    tryVerify(centered, 3000)
    const chosen = carousel.currentIndex
    root.beginEntry()
    wait(600)
    compare(carousel.currentIndex, chosen)
    verify(centered())
}
function test_failed_thumbnail_falls_back_to_full_image() {
    const path = carousel.currentItem.itemPath
    const map = {}; map[path] = path + ".missing"
    root.thumbMap = map
    wait(200)
    tryVerify(() => carousel.currentItem.contentReady, 5000)
    verify(centered())
    root.thumbMap = ({})
}
function test_unreadable_images_do_not_stall_startup() {
    root.entryReady = false
    root.previewReady = false
    root.viewInitialized = false
    root.scanFinished = false
    wait(0)
    wallpaperModel.clear()
    wallpaperModel.append({path: "/nonexistent/opendwm-wallpaper.png"})
    root.scanFinished = true
    root.beginEntry()
    tryCompare(root, "entryReady", true, 5000)
    tryVerify(centered, 3000)
    verify(carousel.currentItem.imageFailed)
    verify(!carousel.currentItem.contentReady)
}
} }
''')
            (folder / "tst_yawc.qml").write_text(harness)
            result = subprocess.run(
                [QMLTESTRUNNER, "-input", directory, "-platform", "offscreen", "-o", "-,txt"],
                env=dict(os.environ, QT_QUICK_BACKEND="software", QT_FORCE_STDERR_LOGGING="1"),
                capture_output=True, text=True, timeout=40)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    @unittest.skipUnless(shutil.which("node"), "Node.js required to exercise QML JavaScript")
    def test_fullscreen_modes_and_monitor_recovery(self):
        qml = (WAYLAND / "quickshell/shell.qml").read_text()
        functions = []
        for name in ("actualFullscreen", "hasActualFullscreen", "monitorForScreen",
                     "activeWorkspaceId", "validClientRecord", "validMonitorRecord"):
            match = re.search(r"  function " + name + r"\([^)]*\) \{.*?\n  \}", qml, re.S)
            self.assertIsNotNone(match, name)
            functions.append(match.group())
        code = "\n".join(functions) + """
const assert = require('node:assert/strict');
const workspace = {id: 1};
for (const [mode, expected] of [[0, false], [1, false], [2, true], [3, true]]) {
  assert.equal(actualFullscreen({mapped: true, hidden: false, fullscreen: mode, workspace}), expected);
}
assert.equal(actualFullscreen({mapped: false, hidden: false, fullscreen: 2, workspace}), false);
assert.equal(actualFullscreen({mapped: true, hidden: true, fullscreen: 2, workspace}), false);
let fullscreenState = {clients: [{monitor: 1, workspace}], specials: {}, active: {1: 1}};
assert.equal(hasActualFullscreen({id: 1}), true);
assert.equal(hasActualFullscreen({id: 2}), false);
assert.equal(hasActualFullscreen(null), false);
// A swapped/stale active-workspace mapping changes the verdict.
fullscreenState = {clients: [{monitor: 1, workspace}], specials: {}, active: {1: 2}};
assert.equal(hasActualFullscreen({id: 1}), false);
// Fullscreen on the monitor's displayed special workspace (negative ids).
fullscreenState = {clients: [{monitor: 1, workspace: {id: -5}}], specials: {}, active: {1: 1}};
assert.equal(hasActualFullscreen({id: 1}), false);
fullscreenState = {clients: [{monitor: 1, workspace: {id: -5}}], specials: {1: -5}, active: {1: 1}};
assert.equal(hasActualFullscreen({id: 1}), true);
// A closed special workspace must not suppress the bar.
fullscreenState = {clients: [{monitor: 1, workspace: {id: -5}}], specials: {}, active: {1: 1}};
assert.equal(hasActualFullscreen({id: 1}), false);
// Highlight source: polled snapshot wins over the event model; the model
// is only the pre-snapshot fallback.
fullscreenState = {clients: [], specials: {}, active: {}};
assert.equal(activeWorkspaceId({id: 1}, {id: 7}), 7);
fullscreenState = {clients: [], specials: {}, active: {1: 3}};
assert.equal(activeWorkspaceId({id: 1}, {id: 7}), 3);
assert.equal(activeWorkspaceId({id: 2}, {id: 7}), 7);
assert.equal(activeWorkspaceId(null, {id: 7}), -1);
const Hyprland = {monitors: {values: []}};
const screen = {name: 'DP-1'};
assert.equal(monitorForScreen(screen), null);
const monitor = {id: 1, name: 'DP-1'};
Hyprland.monitors.values = [monitor];
assert.equal(monitorForScreen(screen), monitor);
Hyprland.monitors.values = [];
assert.equal(monitorForScreen(screen), null);
// Snapshot record validation: malformed entries must reject the response.
const validClient = {mapped: true, hidden: false, fullscreen: 2, monitor: 1, workspace};
assert.equal(validClientRecord(validClient), true);
for (const bad of [{}, null, {mapped: true},
                   {...validClient, monitor: '1'},
                   {...validClient, workspace: {}},
                   {...validClient, hidden: undefined},
                   {...validClient, fullscreen: '2'},
                   {...validClient, fullscreen: 2.5}]) {
  assert.equal(validClientRecord(bad), false, JSON.stringify(bad));
}
assert.equal(validMonitorRecord({id: 1, specialWorkspace: {id: 0}, activeWorkspace: {id: 1}}), true);
assert.equal(validMonitorRecord({id: 1, specialWorkspace: {id: -5}, activeWorkspace: {id: 1}}), true);
for (const bad of [{id: 1}, {id: '1', specialWorkspace: {id: 0}, activeWorkspace: {id: 1}},
                   {id: 1, specialWorkspace: {id: '-5'}, activeWorkspace: {id: 1}},
                   {id: 1, specialWorkspace: null, activeWorkspace: {id: 1}},
                   {id: 1, specialWorkspace: {id: 0}},
                   {id: 1, specialWorkspace: {id: 0}, activeWorkspace: {}},
                   {id: 1, specialWorkspace: {id: 0}, activeWorkspace: {id: '1'}}]) {
  assert.equal(validMonitorRecord(bad), false, JSON.stringify(bad));
}
"""
        subprocess.run(["node", "-e", code], check=True, capture_output=True)


if __name__ == "__main__":
    unittest.main()
