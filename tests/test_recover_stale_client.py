"""Test the opt-in partial client-tree recovery with fake /proc entries."""
import importlib.util
import io
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location(
    'recover_stale_client', ROOT / 'tools/recover-stale-client.py')
recover_stale_client = importlib.util.module_from_spec(spec)
spec.loader.exec_module(recover_stale_client)

GAME = '/Applications/League of Legends.app/Contents/LoL/LeagueofLegends'
CLIENT = '/Users/Shared/Riot Games/Riot Client.app/Contents/MacOS/RiotClientServices'


class RecoverStaleClientTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.proc = self.root / 'proc'
        self.proc.mkdir()
        self.prefix = self.root / '.darling'

    def process(self, pid, comm, argv, env=None, executable=None):
        folder = self.proc / str(pid)
        folder.mkdir()
        (folder / 'comm').write_text(comm + '\n')
        (folder / 'cmdline').write_bytes(
            (argv[0] if executable is None else executable).encode() + b'\0' +
            b''.join(arg.encode() + b'\0' for arg in argv[1:]))
        (folder / 'stat').write_text(f'{pid} ({comm}) S')
        if env is not None:
            (folder / 'environ').write_bytes(env)
        return folder

    def client_env(self, prefix=None):
        prefix = self.prefix if prefix is None else prefix
        return b'%s=%s\0' % (
            recover_stale_client.SOCK_VARIABLE,
            os.fsencode(prefix / recover_stale_client.SOCK_NAME),
        )

    def run_recover(self, window=False):
        killed = []

        def kill(pid, signum):
            killed.append((pid, signum))

        out = io.StringIO()
        with patch.object(recover_stale_client, 'visible_client_window',
                          return_value=window):
            result = recover_stale_client.recover(
                self.prefix, proc_root=self.proc, window_check=lambda: window,
                kill=kill, sleep=lambda _seconds: None, out=out)
        return result, killed, out.getvalue()

    def test_no_darlingserver_does_nothing(self):
        self.process(10, 'mldr', ['/sbin/launchd'])
        result, killed, output = self.run_recover()
        self.assertEqual((result, killed), (0, []))
        self.assertIn('no darlingserver', output)

    def test_game_is_never_touched(self):
        self.process(10, 'darlingserver', ['darlingserver'])
        self.process(11, 'mldr', [GAME])
        result, killed, output = self.run_recover()
        self.assertEqual((result, killed), (0, []))
        self.assertIn('game running', output)

    def test_visible_window_does_nothing(self):
        self.process(10, 'darlingserver', ['darlingserver'])
        self.process(11, 'mldr', [CLIENT], self.client_env())
        result, killed, output = self.run_recover(window=True)
        self.assertEqual((result, killed), (0, []))
        self.assertIn('client window present', output)

    def test_stale_client_is_killed_but_runtime_is_preserved(self):
        self.process(10, 'darlingserver', ['darlingserver'])
        self.process(11, 'mldr', ['/sbin/launchd'])
        self.process(12, 'mldr', [CLIENT], self.client_env())
        result, killed, output = self.run_recover()
        self.assertEqual(result, 0)
        self.assertEqual([pid for pid, _sig in killed], [12, 12])
        self.assertIn('stale client tree', output)

    def test_other_prefix_and_game_are_preserved(self):
        self.process(10, 'darlingserver', ['darlingserver'])
        self.process(11, 'mldr', [CLIENT], self.client_env(self.root / 'other'))
        self.process(12, 'mldr', [GAME], self.client_env())
        result, killed, output = self.run_recover()
        self.assertEqual((result, killed), (0, []))
        self.assertIn('game running', output)

    def test_runtime_process_names_are_not_killed(self):
        self.process(10, 'darlingserver', ['darlingserver'])
        self.process(11, 'mldr', ['/sbin/launchd'])
        self.process(12, 'mldr', [CLIENT], self.client_env())
        result, killed, _output = self.run_recover()
        self.assertEqual(result, 0)
        self.assertNotIn(10, [pid for pid, _sig in killed])
        self.assertNotIn(11, [pid for pid, _sig in killed])


if __name__ == '__main__':
    unittest.main()
