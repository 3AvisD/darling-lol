"""Regression tests for the Nix launcher; no root or game account required."""
import fcntl
import os
from pathlib import Path
import signal
import subprocess
import tempfile
import time
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
        shutil.copy(ROOT / 'scripts/nix-stop.py', self.share / 'scripts/nix-stop.py')
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

    def test_session_restores_signals_inside_the_namespace(self):
        result = self.launch()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.log.read_text().splitlines()[-5:],
                         [str(self.bin / 'fhs'), 'env', '--default-signal=INT,QUIT,HUP,TERM',
                          'bash', './scripts/nix-session.sh'])

    def test_interrupt_becomes_a_stop_request_and_namespace_survives_it(self):
        # Stands in for bubblewrap: it must outlive Ctrl+C and see the request.
        (self.bin / 'unshare').write_text(
            '#!/bin/sh\necho started > "$TEST_LOG"\n'
            'while [ ! -e .stop-request ]; do sleep 0.1; done\n'
            'echo stopped >> "$TEST_LOG"\n')
        launcher = subprocess.Popen(['bash', str(self.launcher)], env=self.env, text=True,
                                    stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                    start_new_session=True)
        for _ in range(100):
            if self.log.exists():
                break
            time.sleep(0.1)
        os.killpg(launcher.pid, signal.SIGINT)
        _, stderr = launcher.communicate(timeout=10)
        self.assertEqual(launcher.returncode, 0, stderr)
        self.assertEqual(self.log.read_text().splitlines(), ['started', 'stopped'])
        self.assertFalse((self.workspace / '.stop-request').exists())

    def test_stop_without_session_does_not_enter_the_namespace(self):
        result = self.launch('--stop')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('No darling-lol session is running', result.stdout)
        self.assertFalse(self.log.exists())

    def test_stop_asks_the_session_holding_the_lock_to_exit(self):
        self.workspace.mkdir(parents=True)
        lock = self.workspace / '.launcher.lock'
        session = subprocess.Popen(['flock', str(lock), 'sh', '-c',
                                    'while [ ! -e "$0/.stop-request" ]; do sleep 0.1; done',
                                    str(self.workspace)])
        self.addCleanup(session.kill)
        with open(lock, 'a+') as handle:
            for _ in range(100):
                try:
                    fcntl.flock(handle, fcntl.LOCK_EX | fcntl.LOCK_NB)
                    fcntl.flock(handle, fcntl.LOCK_UN)
                    time.sleep(0.1)
                except BlockingIOError:
                    break
        result = self.launch('--stop')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('Stopped.', result.stdout)
        self.assertEqual(session.wait(timeout=5), 0)
        self.assertFalse(self.log.exists())

    def test_saved_prefix_is_used_unless_dprefix_is_set(self):
        (self.bin / 'unshare').write_text('#!/bin/sh\nprintf "%s\\n" "$DPREFIX" "$@" > "$TEST_LOG"\n')
        (self.workspace / 'build').mkdir(parents=True)
        (self.workspace / 'build/config.json').write_text('{"prefix": "/mnt/games/League Of Legends"}')
        self.env.pop('DPREFIX', None)
        self.assertEqual(self.launch('true').returncode, 0)
        self.assertEqual(self.log.read_text().splitlines()[0], '/mnt/games/League Of Legends')
        self.env['DPREFIX'] = '/explicit'
        self.assertEqual(self.launch('true').returncode, 0)
        self.assertEqual(self.log.read_text().splitlines()[0], '/explicit')

    def test_set_prefix_resolves_the_path_from_the_callers_directory(self):
        result = subprocess.run(['bash', str(self.launcher), '--set-prefix', 'Games/League'],
                                env=self.env, cwd=self.folder, text=True, capture_output=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.log.read_text().splitlines()[-3:],
                         ['python3', './scripts/nix-prefix.py', str(self.folder.resolve() / 'Games/League')])
        self.assertEqual(self.launch('--set-prefix').returncode, 2)

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
