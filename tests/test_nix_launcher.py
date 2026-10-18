"""Regression tests for the Nix launcher; no root or game account required."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class LauncherTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.folder = Path(self.tmp.name)
        self.home = self.folder / 'home'
        self.home.mkdir()
        self.share = self.folder / 'share'
        (self.share / 'scripts').mkdir(parents=True)
        (self.share / 'scripts/run.sh').write_text('new packaged script\n')
        import shutil
        shutil.copy(ROOT / 'scripts/nix-copy.py', self.share / 'scripts/nix-copy.py')
        self.workspace = self.home / '.local/share/darling-lol'
        self.log = self.folder / 'calls'
        self.bin = self.folder / 'bin'
        self.bin.mkdir()
        for name in ['unshare', 'fhs']:
            executable = self.bin / name
            executable.write_text('#!/bin/sh\nprintf "%s\\n" "$0" "$@" > "$TEST_LOG"\n')
            executable.chmod(0o755)
        self.launcher = self.folder / 'launcher'
        self.launcher.write_text((ROOT / 'darling-lol-launcher.sh').read_text()
                                 .replace('@SHARE_DIR@', str(self.share))
                                 .replace('@FHS_BIN@', str(self.bin / 'fhs'))
                                 .replace('@UNSHARE_BIN@', str(self.bin / 'unshare')))
        self.env = dict(os.environ, HOME=str(self.home), XDG_DATA_HOME=str(self.home / '.local/share'),
                        TEST_LOG=str(self.log), PATH=str(self.bin) + ':' + os.environ['PATH'])

    def launch(self, *args):
        return subprocess.run(['bash', str(self.launcher), *args], env=self.env,
                              text=True, capture_output=True, timeout=10)

    def test_package_upgrade_refreshes_tools_without_touching_build(self):
        (self.workspace / 'scripts').mkdir(parents=True)
        (self.workspace / 'scripts/run.sh').write_text('stale script\n')
        (self.workspace / 'build').mkdir()
        binary = self.workspace / 'build/keep-me'
        binary.write_text('compiled runtime')
        result = self.launch('true')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual((self.workspace / 'scripts/run.sh').read_text(), 'new packaged script\n')
        self.assertEqual(binary.read_text(), 'compiled runtime')

    def test_fhs_enters_subuid_namespace_before_bubblewrap(self):
        result = self.launch('echo', 'hello world')
        self.assertEqual(result.returncode, 0, result.stderr)
        calls = self.log.read_text().splitlines()
        self.assertEqual(calls[0], str(self.bin / 'unshare'))
        self.assertIn('--map-auto', calls)
        self.assertIn('--map-current-user', calls)
        self.assertIn('--setuid=0', calls)
        self.assertIn('--setgid=0', calls)
        self.assertEqual(calls[-3:], [str(self.bin / 'fhs'), 'echo', 'hello world'])

    def test_setup_never_makes_a_user_writable_runtime_setuid(self):
        result = self.launch('--installer')
        self.assertEqual(result.returncode, 0, result.stderr)
        setup = (ROOT / 'scripts/nix-session.sh').read_text() + (ROOT / 'scripts/update.sh').read_text().split('if [ ! -d build/darling-src/build ]; then')[0]
        self.assertNotIn('    sudo ', setup)
        self.assertNotIn('\nsudo ', setup)
        self.assertNotIn('chmod 4755', setup)
        self.assertIn('install-runtime.sh', setup)


class RuntimeEntryTests(unittest.TestCase):
    def test_entry_preserves_host_identity_and_uses_namespace_only_privileges(self):
        entry = ROOT / 'scripts/fhs-entry.sh'
        self.assertTrue(entry.is_file(), 'missing rootless FHS entrypoint')
        text = entry.read_text()
        self.assertIn('DARLING_HOST_UID', text)
        self.assertIn('DARLING_HOST_GID', text)
        self.assertIn('--ambient-caps', text)
        shim = ROOT / 'scripts/darling-rootless.sh'
        self.assertTrue(shim.is_file(), 'missing namespace-local Darling shim')
        self.assertIn('setpriv', shim.read_text())
        self.assertIn('--euid', shim.read_text())


if __name__ == '__main__':
    unittest.main()
