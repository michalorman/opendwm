"""Mocked setup.sh tests against a copied tree; no repo or system changes."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


WAYLAND = Path(__file__).resolve().parents[1]

HYPRCTL = '#!/bin/sh\n[ "$1" = version ] && echo "Tag: v0.56.2" || exit 0\n'
HYPRLAND = '#!/bin/sh\n[ "$1" = --version ] && echo "Hyprland 0.56.2 (mock)" || exit 0\n'
QUICKSHELL = '#!/bin/sh\n[ "$1" = --version ] && echo "quickshell 0.3.1" || exit 0\n'
QUICKSHELL_OLD = '#!/bin/sh\n[ "$1" = --version ] && echo "quickshell 0.2.0" || exit 0\n'
QUICKSHELL_REPEATED = '#!/bin/sh\n[ "$1" = --version ] && echo "quickshell 0.3.3" || exit 0\n'
NOOP = '#!/bin/sh\nexit 0\n'
PGREP_DEAD = '#!/bin/sh\nexit 1\n'
SUDO = ('#!/bin/sh\n'
        '[ "$1" = "-v" ] && exit 0\n'
        'mode=$(cat "$MOCK_ROOT/sudo-mode" 2>/dev/null || echo ok)\n'
        'case $mode in\n'
        '  fail-mv) [ "$1" = mv ] && exit 1 ;;\n'
        '  fail-rm) [ "$1" = rm ] && exit 1 ;;\n'
        '  fail-install-launcher)\n'
        '    [ "$1" = install ] && [ "$4" = "$OPENDWM_SETUP_BIN_DIR/start-opendwm-wayland" ] && exit 1 ;;\n'
        '  fail-install-desktop)\n'
        '    [ "$1" = install ] && [ "$4" = "$OPENDWM_SETUP_SESSION_DIR/opendwm-wayland.desktop" ] && exit 1 ;;\n'
        'esac\n'
        'exec "$@"\n')


class SetupTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)

        # Run setup from an isolated copy so tests never touch the repo.
        self.repo = self.root / "repo"
        for name in ("hyprland", "scripts", "session"):
            shutil.copytree(WAYLAND / name, self.repo / name)
        shutil.copy2(WAYLAND / "setup.sh", self.repo / "setup.sh")
        # Point the copied config at the copied scripts, so plain installs
        # need no path confirmation.
        conf = self.repo / "hyprland/hyprland.lua"
        conf.write_text(conf.read_text().replace(
            'local OPENDWM = "/home/snq/Code/opendwm/wayland/scripts"',
            f'local OPENDWM = "{self.repo}/scripts"'))

        self.home = self.root / "home"
        self.home.mkdir()
        self.config = self.home / ".config"
        self.hypr = self.config / "hypr"
        self.bin = self.root / "mock-bin"
        self.bin.mkdir()
        for name, body in (("hyprctl", HYPRCTL), ("Hyprland", HYPRLAND),
                           ("quickshell", QUICKSHELL), ("sudo", SUDO),
                           ("start-hyprland", NOOP), ("wpctl", NOOP),
                           ("dmenu", NOOP), ("dmenu_run", NOOP),
                           ("j4-dmenu-desktop", NOOP), ("pgrep", PGREP_DEAD)):
            path = self.bin / name
            path.write_text(body)
            path.chmod(0o700)
        (self.root / "sudo-mode").write_text("ok")
        self.prefix_bin = self.root / "prefix-bin"
        self.prefix_bin.mkdir()
        self.prefix_sessions = self.root / "prefix-sessions"
        self.prefix_sessions.mkdir()
        self.env = dict(
            os.environ,
            PATH=f"{self.bin}:{os.environ['PATH']}",
            HOME=str(self.home),
            XDG_CONFIG_HOME=str(self.config),
            XDG_RUNTIME_DIR=str(self.root / "runtime"),
            OPENDWM_SETUP_BIN_DIR=str(self.prefix_bin),
            OPENDWM_SETUP_SESSION_DIR=str(self.prefix_sessions),
            MOCK_ROOT=str(self.root),
        )
        (self.root / "runtime").mkdir(mode=0o700)
        self.link = self.hypr / "hyprland.lua"
        self.legacy = self.hypr / "hyprland.conf"
        self.manifest = self.hypr / ".opendwm-session-manifest"
        self.launcher = self.prefix_bin / "start-opendwm-wayland"
        self.desktop = self.prefix_sessions / "opendwm-wayland.desktop"

    def run_setup(self, *args, success=True, stdin=None, env=None):
        result = subprocess.run(["sh", str(self.repo / "setup.sh"), *args],
                                env=env or self.env, text=True, capture_output=True,
                                input=stdin, timeout=30)
        self.assertEqual(result.returncode == 0, success,
                         f"{result.stdout}\n{result.stderr}")
        return result

    def limited_env(self, exclude=()):
        # Real tools plus all mocks, minus excluded commands: deterministic
        # even when the host has the real programs installed.
        limited = self.root / ("limited-" + "-".join(sorted(exclude)) if exclude else "limited")
        limited.mkdir(exist_ok=True)
        tools = ["jq", "flock", "timeout", "python3", "stat", "readlink",
                 "sh", "bash", "install", "date", "sed", "ls", "mv", "ln", "rm",
                 "mkdir", "id", "cat", "cp", "grep", "head", "dirname", "pwd",
                 "sha256sum", "awk", "basename", "fc-match", "cmp"]
        for tool in tools:
            src = shutil.which(tool)
            if src and tool not in exclude:
                (limited / tool).symlink_to(src)
        for mock in self.bin.iterdir():
            if mock.name not in exclude:
                target = limited / mock.name
                if not target.exists():
                    target.symlink_to(mock)
        return dict(self.env, PATH=str(limited))

    # --- install/check ------------------------------------------------------

    def test_fresh_install_and_check(self):
        self.run_setup()
        self.assertTrue(self.link.is_symlink())
        self.assertEqual(self.link.resolve(),
                         (self.repo / "hyprland/hyprland.lua").resolve())
        self.assertTrue(self.launcher.stat().st_mode & 0o111)
        self.assertTrue(self.desktop.is_file())
        self.assertTrue(self.manifest.is_file())
        self.run_setup("--check")
        # Idempotent re-run keeps the same state.
        self.run_setup()
        self.assertFalse(list(self.hypr.glob("hyprland.lua.bak-*")))

    def test_outdated_managed_session_files_are_updated(self):
        self.run_setup()
        # Simulate a repo update: change the copied source, not the install.
        source = self.repo / "session/start-opendwm-wayland"
        source.write_text(source.read_text() + "# v2\n")
        self.run_setup("--check", success=False)
        self.run_setup()
        self.assertTrue(self.launcher.read_text().endswith("# v2\n"))
        self.run_setup("--check")

    def test_modified_session_files_are_foreign_not_outdated(self):
        self.run_setup()
        self.launcher.write_text("# user edit\n")
        # A user-modified file is not a managed upgrade: aborts without force.
        result = self.run_setup(success=False, stdin="")
        self.assertIn("aborted", result.stderr)
        self.assertEqual(self.launcher.read_text(), "# user edit\n")
        # With force it is backed up, then replaced.
        self.run_setup("--force")
        self.assertEqual(self.launcher.read_text(),
                         (self.repo / "session/start-opendwm-wayland").read_text())
        self.assertTrue(list(self.prefix_bin.glob("start-opendwm-wayland.bak-*")))
        # Uninstall removes managed files and never touches the backup.
        self.run_setup("--uninstall")
        self.assertFalse(self.launcher.exists())
        self.assertTrue(list(self.prefix_bin.glob("start-opendwm-wayland.bak-*")))

    def test_nonexecutable_launcher_is_repaired(self):
        self.run_setup()
        self.launcher.chmod(0o644)
        self.run_setup("--check", success=False)
        self.run_setup()
        self.assertTrue(self.launcher.stat().st_mode & 0o111)

    def test_existing_config_is_backed_up_with_force(self):
        self.hypr.mkdir(parents=True)
        self.link.write_text("# my own config\n")
        self.run_setup("--force")
        backups = list(self.hypr.glob("hyprland.lua.bak-*"))
        self.assertEqual(len(backups), 1)
        self.assertEqual(backups[0].read_text(), "# my own config\n")
        self.assertTrue(self.link.is_symlink())
        self.run_setup("--uninstall")
        self.assertFalse(self.link.is_symlink())
        self.assertEqual(self.link.read_text(), "# my own config\n")
        self.assertFalse(self.launcher.exists())
        self.assertFalse(self.desktop.exists())

    def test_foreign_config_needs_confirmation(self):
        self.hypr.mkdir(parents=True)
        self.link.write_text("# mine\n")
        result = self.run_setup(success=False, stdin="")
        self.assertIn("aborted", result.stderr)
        self.assertEqual(self.link.read_text(), "# mine\n")
        self.assertFalse(self.link.is_symlink())

    # --- legacy .conf migration ----------------------------------------------

    def make_legacy_link(self):
        self.hypr.mkdir(parents=True, exist_ok=True)
        # Positively identifiable as the previous opendwm install.
        self.legacy.symlink_to(self.repo / "hyprland/hyprland.conf")

    def test_legacy_link_removed_and_conf_backup_restored(self):
        self.make_legacy_link()
        self.legacy.with_name("hyprland.conf.bak-20200101-000000").write_text("# old conf\n")
        self.run_setup()
        self.assertFalse(self.legacy.exists() or self.legacy.is_symlink())
        self.assertTrue(self.link.is_symlink())
        self.run_setup("--uninstall")
        self.assertEqual(self.legacy.read_text(), "# old conf\n")

    def test_foreign_dangling_conf_link_is_preserved(self):
        self.hypr.mkdir(parents=True, exist_ok=True)
        self.legacy.symlink_to("/mnt/configs/my-hyprland.conf")  # dangling
        self.run_setup()
        self.assertTrue(self.legacy.is_symlink())
        self.run_setup("--uninstall")
        self.assertTrue(self.legacy.is_symlink())

    # --- validation -----------------------------------------------------------

    def test_missing_dependency_reported(self):
        result = self.run_setup("--check", success=False,
                                env=self.limited_env(exclude=("quickshell",)))
        self.assertIn("quickshell", result.stderr)
        self.assertIn("pacman", result.stdout)
        self.assertFalse(self.link.exists())

    def test_missing_start_hyprland_reported(self):
        result = self.run_setup("--check", success=False,
                                env=self.limited_env(exclude=("start-hyprland",)))
        self.assertIn("start-hyprland", result.stderr)

    def test_old_versions_rejected(self):
        (self.bin / "Hyprland").write_text(HYPRLAND.replace("0.56.2", "0.55.0"))
        (self.bin / "Hyprland").chmod(0o700)
        result = self.run_setup(success=False)
        self.assertIn("0.56", result.stderr)
        (self.bin / "Hyprland").write_text(HYPRLAND)
        (self.bin / "Hyprland").chmod(0o700)
        (self.bin / "quickshell").write_text(QUICKSHELL_OLD)
        (self.bin / "quickshell").chmod(0o700)
        result = self.run_setup(success=False)
        self.assertIn("0.3.1", result.stderr)

    def test_repeated_version_components_accepted(self):
        # 0.3.3 satisfies >= 0.3.1; equal minor/patch must not parse as 0.3.0.
        (self.bin / "quickshell").write_text(QUICKSHELL_REPEATED)
        (self.bin / "quickshell").chmod(0o700)
        self.run_setup()
        self.run_setup("--check")

    def test_declined_confirmation_changes_nothing(self):
        # Wrong OPENDWM path: declining must abort BEFORE any mutation.
        conf = self.repo / "hyprland/hyprland.lua"
        conf.write_text(conf.read_text().replace(
            f'local OPENDWM = "{self.repo}/scripts"', 'local OPENDWM = "/wrong/path"'))
        result = self.run_setup(success=False, stdin="")
        self.assertIn("nothing was changed", result.stderr)
        self.assertFalse(self.link.exists() or self.link.is_symlink())
        self.assertFalse(self.manifest.exists())
        self.assertFalse(self.launcher.exists())

    def test_failed_session_backup_aborts_before_replace(self):
        # Foreign session files + failing sudo mv: originals must survive
        # and nothing may be installed.
        self.launcher.write_text("# foreign launcher\n")
        self.desktop.write_text("# foreign entry\n")
        (self.root / "sudo-mode").write_text("fail-mv")
        result = self.run_setup("--force", success=False)
        self.assertIn("backup", result.stderr)
        self.assertEqual(self.launcher.read_text(), "# foreign launcher\n")
        self.assertEqual(self.desktop.read_text(), "# foreign entry\n")
        self.assertFalse(self.manifest.exists())

    def test_failed_first_install_restores_originals(self):
        self.launcher.write_text("# original launcher\n")
        self.desktop.write_text("# original entry\n")
        (self.root / "sudo-mode").write_text("fail-install-launcher")
        self.run_setup("--force", success=False)
        self.assertEqual(self.launcher.read_text(), "# original launcher\n")
        self.assertEqual(self.desktop.read_text(), "# original entry\n")
        self.assertFalse(self.manifest.exists())

    def test_failed_first_install_removes_partial_without_predecessor(self):
        (self.root / "sudo-mode").write_text("fail-install-launcher")
        self.run_setup(success=False)
        # No predecessor: the partial launcher must be removed, and the
        # newly activated config link must be rolled back too.
        self.assertFalse(self.launcher.exists())
        self.assertFalse(self.desktop.exists())
        self.assertFalse(self.link.exists() or self.link.is_symlink())

    def test_failed_desktop_install_restores_everything(self):
        (self.root / "sudo-mode").write_text("fail-install-desktop")
        self.run_setup(success=False)
        self.assertFalse(self.launcher.exists())
        self.assertFalse(self.desktop.exists())
        self.assertFalse(self.link.exists() or self.link.is_symlink())
        self.assertFalse(self.manifest.exists())

    def test_manifest_failure_restores_previous_files(self):
        self.launcher.write_text("# original launcher\n")
        self.desktop.write_text("# original entry\n")
        self.hypr.mkdir(parents=True)
        (self.hypr / ".opendwm-session-manifest.tmp").mkdir()  # write must fail
        self.run_setup("--force", success=False)
        self.assertEqual(self.launcher.read_text(), "# original launcher\n")
        self.assertEqual(self.desktop.read_text(), "# original entry\n")
        self.assertFalse(self.manifest.exists())

    def test_same_second_backups_do_not_collide(self):
        fixed_date = self.root / "fixed-date-bin"
        fixed_date.mkdir()
        (fixed_date / "date").write_text("#!/bin/sh\necho 20240101-000000\n")
        (fixed_date / "date").chmod(0o700)
        env = dict(self.env, PATH=f"{fixed_date}:{self.env['PATH']}")
        self.launcher.write_text("# first foreign\n")
        self.desktop.write_text("# first foreign entry\n")
        self.run_setup("--force", env=env)
        first_backup = self.prefix_bin / "start-opendwm-wayland.bak-20240101-000000"
        self.assertEqual(first_backup.read_text(), "# first foreign\n")
        # User edits make the files foreign again; same second, same stamp.
        self.launcher.write_text("# second foreign\n")
        self.desktop.write_text("# second foreign entry\n")
        self.run_setup("--force", env=env)
        self.assertEqual(first_backup.read_text(), "# first foreign\n")
        second_backup = self.prefix_bin / "start-opendwm-wayland.bak-20240101-000000-1"
        self.assertEqual(second_backup.read_text(), "# second foreign\n")

    def test_opendwm_edit_restored_on_session_failure(self):
        conf = self.repo / "hyprland/hyprland.lua"
        wrong = conf.read_text().replace(
            f'local OPENDWM = "{self.repo}/scripts"', 'local OPENDWM = "/wrong"')
        conf.write_text(wrong)
        (self.root / "sudo-mode").write_text("fail-install-launcher")
        self.run_setup("--force", success=False)
        # Rollback restores the PRE-INSTALL content (with /wrong), undoing
        # the OPENDWM path fix that ran before the session failure.
        self.assertEqual(conf.read_text(), wrong)
        self.assertFalse(self.link.exists() or self.link.is_symlink())

    def test_dangling_foreign_lua_link_restored_on_failure(self):
        self.hypr.mkdir(parents=True)
        self.link.symlink_to("/nonexistent/target.conf")
        (self.root / "sudo-mode").write_text("fail-install-launcher")
        self.run_setup("--force", success=False)
        self.assertTrue(self.link.is_symlink())
        self.assertEqual(os.readlink(self.link), "/nonexistent/target.conf")

    def test_existing_manifest_preserved_on_write_failure(self):
        self.run_setup()
        original_manifest = self.manifest.read_text()
        source = self.repo / "session/start-opendwm-wayland"
        source.write_text(source.read_text() + "# v2\n")
        self.manifest.with_suffix(".tmp").symlink_to(self.manifest)
        self.run_setup(success=False)
        self.assertEqual(self.manifest.read_text(), original_manifest)
        self.assertFalse(self.launcher.read_text().endswith("# v2\n"))

    def test_failed_session_removal_keeps_manifest(self):
        self.run_setup()
        (self.root / "sudo-mode").write_text("fail-rm")
        self.run_setup("--uninstall", success=False)
        self.assertTrue(self.manifest.exists())
        self.assertTrue(self.launcher.exists())
        # Recovery: removing the failure lets uninstall complete.
        (self.root / "sudo-mode").write_text("ok")
        self.run_setup("--uninstall")
        self.assertFalse(self.manifest.exists())

    def test_backup_restore_uses_name_timestamp_not_mtime(self):
        self.run_setup()
        older = self.hypr / "hyprland.lua.bak-20200101-000000"
        newer = self.hypr / "hyprland.lua.bak-20210101-000000"
        older.write_text("# older by name, newer mtime\n")
        newer.write_text("# newer by name, older mtime\n")
        subprocess.run(["touch", "-d", "2020-01-01", str(newer)], check=True)
        # The name-newer backup has the OLDER mtime: mtime sort would pick
        # the wrong file; name sort must win.
        self.run_setup("--uninstall")
        self.assertEqual(self.link.read_text(), "# newer by name, older mtime\n")

    def test_purge_refused_while_compositor_running(self):
        self.run_setup()
        (self.root / "runtime" / "opendwm-bar-abc").touch()
        (self.bin / "pgrep").write_text("#!/bin/sh\nexit 0\n")
        (self.bin / "pgrep").chmod(0o700)
        result = self.run_setup("--uninstall", "--purge", success=False)
        self.assertIn("running", result.stderr)
        self.assertTrue((self.root / "runtime" / "opendwm-bar-abc").exists())
        # Refusal happens before uninstall mutations.
        self.assertTrue(self.link.is_symlink())
        self.assertTrue(self.manifest.exists())

    def test_purge_refused_on_pgrep_error(self):
        (self.bin / "pgrep").write_text("#!/bin/sh\nexit 2\n")
        (self.bin / "pgrep").chmod(0o700)
        result = self.run_setup("--uninstall", "--purge", success=False)
        self.assertIn("cannot determine", result.stderr)

    def test_purge_refused_without_pgrep(self):
        result = self.run_setup("--uninstall", "--purge", success=False,
                                env=self.limited_env(exclude=("pgrep",)))
        self.assertIn("pgrep", result.stderr)

    def test_malformed_versions_rejected(self):
        for bad in ("quickshell 0.4.", "quickshell 0..2"):
            with self.subTest(bad=bad):
                (self.bin / "quickshell").write_text(
                    f'#!/bin/sh\n[ "$1" = --version ] && echo "{bad}" || exit 0\n')
                (self.bin / "quickshell").chmod(0o700)
                self.run_setup(success=False)

    def test_wrong_opendwm_path_detected_and_fixed(self):
        conf = self.repo / "hyprland/hyprland.lua"
        conf.write_text(conf.read_text().replace(
            f'local OPENDWM = "{self.repo}/scripts"', 'local OPENDWM = "/wrong/path"'))
        self.run_setup("--check", success=False)
        self.run_setup("--force")
        self.run_setup("--check")

    def test_repo_path_with_shell_and_lua_specials(self):
        # Spaces, &, |, quotes and backslashes must survive both the sed
        # replacement and the Lua string encoding.
        special = self.root / 'repo with spaces & pipe|quote"back\\slash'
        shutil.copytree(self.repo, special)
        conf = special / "hyprland/hyprland.lua"
        conf.write_text(conf.read_text().replace(
            f'local OPENDWM = "{self.repo}/scripts"', 'local OPENDWM = "/wrong"'))
        env = dict(self.env)
        result = subprocess.run(["sh", str(special / "setup.sh"), "--force"],
                                env=env, text=True, capture_output=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stderr)
        written = next(line for line in conf.read_text().splitlines()
                       if line.startswith("local OPENDWM = "))
        expected = ('local OPENDWM = "'
                    + str(special).replace("\\", "\\\\").replace('"', '\\"')
                    + '/scripts"')
        self.assertEqual(written, expected)
        result = subprocess.run(["sh", str(special / "setup.sh"), "--check"],
                                env=env, text=True, capture_output=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stderr)

    # --- purge -----------------------------------------------------------------

    def test_purge_cleans_runtime_state(self):
        self.run_setup()
        for name in ("opendwm-bar-abc", "opendwm-scratchpad-sig-term.pending",
                     "opendwm-gaps-sig.lock"):
            (self.root / "runtime" / name).touch()
        self.run_setup("--uninstall", "--purge")
        self.assertEqual(list((self.root / "runtime").iterdir()), [])


if __name__ == "__main__":
    unittest.main()
