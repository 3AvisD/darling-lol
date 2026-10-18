"""Exercise the in-match client close with fake /proc entries and mocked signals only."""
import importlib.util
import io
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('in_game', ROOT / 'tools/close-league-client-in-game.py')
in_game = importlib.util.module_from_spec(spec)
spec.loader.exec_module(in_game)

MAIN = '/Applications/League of Legends.app/Contents/LoL/LeagueClientUx.app/Contents/MacOS/LeagueClientUx'
HELPER = ('/Applications/League of Legends.app/Contents/LoL/LeagueClientUx Helper.app'
          '/Contents/MacOS/LeagueClientUx Helper')
GAME = '/Applications/LeagueofLegends.app/Contents/MacOS/LeagueofLegends'
BACKEND = '/Applications/League of Legends.app/Contents/LoL/LeagueClient.app/Contents/MacOS/LeagueClient'
RIOT = '/Applications/Riot Client.app/Contents/MacOS/RiotClientServices'
CRASH_HANDLER = ('/Applications/League of Legends.app/Contents/LoL/LeagueClient.app'
                 '/Contents/MacOS/LeagueCrashHandler')
# A live crash handler carries the client UI name in its arguments only.
CRASH_ARGUMENTS = ('--attachment=/Applications/League of Legends.app/Contents/LoL/Logs/'
                   'LeagueClientUx Logs/2026-10-18T09-49-13_659_LeagueClientUx.log',
                   '--handshake-fd=52')


class InGameCloseTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.proc = self.root / 'proc'
        self.proc.mkdir()
        (self.proc / 'uptime').write_text('1000 0\n')
        self.home = self.root / 'home'
        self.home_patch = patch.object(Path, 'home', return_value=self.home)
        self.home_patch.start()
        self.addCleanup(self.home_patch.stop)
        self.prefix = self.home / '.darling'
        self.env_patch = patch.dict(os.environ, {'DPREFIX': str(self.prefix)})
        self.env_patch.start()
        self.addCleanup(self.env_patch.stop)

    def environment(self, prefix):
        if prefix is None:
            return b''
        return b'%s=%s\0' % (in_game.SOCK_VARIABLE, os.fsencode(prefix / in_game.SOCK_NAME))

    def process(self, pid, executable, prefix='own', age=120, environ=None, args=('--flag',)):
        folder = self.proc / str(pid)
        folder.mkdir()
        (folder / 'cmdline').write_bytes(executable.encode() + b'\0'
                                         + b''.join(arg.encode() + b'\0' for arg in args))
        if environ is None:
            environ = self.environment(self.prefix if prefix == 'own' else prefix)
        (folder / 'environ').write_bytes(environ)
        fields = ['S'] + ['0'] * 18 + [str(int((1000 - age) * os.sysconf('SC_CLK_TCK')))]
        (folder / 'stat').write_text(str(pid) + ' (LeagueClientUx (helper)) ' + ' '.join(fields))
        return folder

    def client_ui(self, pid, prefix='own', age=120, environ=None):
        return self.process(pid, MAIN if pid % 2 else HELPER, prefix=prefix, age=age, environ=environ)

    def run_once(self, dry_run=False):
        self.output = io.StringIO()
        with patch.object(in_game.time, 'sleep'), patch.object(in_game.os, 'kill') as kill:
            result = in_game.supervise(self.proc, once=True, dry_run=dry_run, out=self.output)
        return result, [call.args for call in kill.call_args_list]

    def test_client_ui_without_a_match_is_preserved(self):
        self.client_ui(123)
        result, calls = self.run_once()
        self.assertEqual((result, calls), (0, []))
        self.assertIn('match=absent client_ui=1', self.output.getvalue())

    def test_running_match_closes_only_the_client_ui(self):
        self.client_ui(123)
        self.process(124, HELPER)
        self.process(125, GAME)
        self.process(126, BACKEND)
        self.process(127, RIOT)
        self.process(128, MAIN, prefix=self.root / 'other-prefix')
        result, calls = self.run_once()
        self.assertEqual(result, 0)
        self.assertEqual(sorted(calls), sorted([(123, in_game.signal.SIGTERM),
                                                (124, in_game.signal.SIGTERM),
                                                (123, in_game.signal.SIGKILL),
                                                (124, in_game.signal.SIGKILL)]))
        self.assertIn('closed the client UI for the running match (2 processes: 123,124)',
                      self.output.getvalue())

    def test_match_soon_after_boot_still_closes_the_client_ui(self):
        # time.monotonic() counts from boot; a fresh machine (or CI runner) is
        # still inside the first cooldown window.
        self.client_ui(123)
        self.process(125, GAME)
        with patch.object(in_game.time, 'monotonic', return_value=5.0):
            result, calls = self.run_once()
        self.assertEqual(result, 0)
        self.assertEqual(sorted(calls), sorted([(123, in_game.signal.SIGTERM),
                                                (123, in_game.signal.SIGKILL)]))

    def test_marker_in_arguments_does_not_make_a_process_client_ui(self):
        self.client_ui(123)
        self.process(124, CRASH_HANDLER, args=CRASH_ARGUMENTS)
        self.process(125, GAME)
        result, calls = self.run_once()
        self.assertEqual(result, 0)
        self.assertEqual(sorted(calls), sorted([(123, in_game.signal.SIGTERM),
                                                (123, in_game.signal.SIGKILL)]))
        self.assertIn('closed the client UI for the running match (1 processes: 123)',
                      self.output.getvalue())

    def test_game_argument_naming_the_client_ui_is_still_the_game(self):
        self.process(125, GAME, args=('--parent=LeagueClientUx',))
        self.process(126, CRASH_HANDLER, args=CRASH_ARGUMENTS)
        result, calls = self.run_once()
        self.assertEqual((result, calls), (0, []))
        self.assertIn('match=running client_ui=0', self.output.getvalue())

    def test_ui_identity_rejects_a_related_process_with_the_marker(self):
        folder = self.client_ui(123)
        self.assertIsNotNone(in_game.ui_identity(self.proc, '123'))
        (folder / 'cmdline').write_bytes(CRASH_HANDLER.encode() + b'\0'
                                         + b'\0'.join(a.encode() for a in CRASH_ARGUMENTS) + b'\0')
        self.assertIsNone(in_game.ui_identity(self.proc, '123'))
        with patch.object(in_game.time, 'sleep'), patch.object(in_game.os, 'kill') as kill:
            in_game.close_ui(self.proc, {'123': 1})
        self.assertEqual(kill.call_args_list, [])

    def test_match_in_another_install_is_preserved(self):
        self.client_ui(123)
        self.process(125, GAME, prefix=self.root / 'other-prefix')
        self.assertEqual(self.run_once()[1], [])

    def test_game_without_an_install_socket_is_preserved(self):
        self.client_ui(123)
        self.process(125, GAME, environ=b'')
        self.assertEqual(self.run_once()[1], [])

    def test_young_client_ui_is_preserved(self):
        self.client_ui(123, age=in_game.MIN_AGE_SECONDS - 1)
        self.process(125, GAME)
        self.assertEqual(self.run_once()[1], [])

    def test_client_ui_of_another_install_is_preserved(self):
        self.client_ui(123, prefix=self.root / 'other-prefix')
        self.process(125, GAME)
        self.assertEqual(self.run_once()[1], [])

    def test_client_ui_without_an_install_socket_is_preserved(self):
        self.client_ui(123, environ=b'')
        self.process(125, GAME)
        self.assertEqual(self.run_once()[1], [])

    def test_missing_prefix_prevents_any_action(self):
        self.client_ui(123)
        self.process(125, GAME)
        with patch.dict(os.environ, {'DPREFIX': 'relative/path'}):
            self.assertIsNone(in_game.target_prefix())
            self.assertEqual(self.run_once()[1], [])

    def test_foreign_owned_process_is_ignored(self):
        self.client_ui(123)
        self.process(125, GAME)
        with patch.object(in_game.os, 'getuid', return_value=os.getuid() + 1):
            result, calls = self.run_once()
        self.assertEqual((result, calls), (0, []))

    def test_unreadable_environment_prevents_action(self):
        self.client_ui(123)
        self.process(125, GAME)
        original = Path.read_bytes
        def read(path):
            if path.name == 'environ':
                raise PermissionError()
            return original(path)
        with patch.object(Path, 'read_bytes', read):
            self.assertEqual(self.run_once()[1], [])

    def test_malformed_or_missing_age_prevents_action(self):
        proc = self.client_ui(123)
        self.process(125, GAME)
        for value in ('garbage', '123 (worker) S'):
            (proc / 'stat').write_text(value)
            self.assertEqual(self.run_once()[1], [])
        (proc / 'stat').unlink()
        self.assertEqual(self.run_once()[1], [])

    def test_dry_run_reports_without_signalling(self):
        self.client_ui(123)
        self.process(125, GAME)
        self.assertEqual(self.run_once(dry_run=True)[1], [])
        self.assertIn('would close the client UI (1 processes: 123)', self.output.getvalue())

    def test_exited_or_reused_processes_are_preserved(self):
        folder = self.client_ui(123)
        identity = in_game.ui_identity(self.proc, '123')
        self.assertIsNotNone(identity)
        (folder / 'cmdline').write_bytes(b'/usr/bin/editor\0')
        self.assertIsNone(in_game.ui_identity(self.proc, '123'))
        with patch.object(in_game.time, 'sleep'), patch.object(in_game.os, 'kill') as kill:
            in_game.close_ui(self.proc, {'123': identity, '124': 1})
        self.assertEqual(kill.call_args_list, [])

    def test_idle_clock_starts_only_after_a_client_or_match(self):
        empty = {'game': False, 'ui': {}}
        client = {'game': False, 'ui': {'10': 5}}
        match = {'game': True, 'ui': {}}
        # Before any client (long patch or login): never idle, however long it takes.
        self.assertEqual(in_game.update_idle(empty, False, None, 5000.0), (False, None))
        # A client or a match marks the session as seen and keeps the clock stopped.
        self.assertEqual(in_game.update_idle(client, False, None, 10.0), (True, None))
        self.assertEqual(in_game.update_idle(match, True, 50.0, 60.0), (True, None))
        # Afterwards the clock starts once and keeps its start time.
        self.assertEqual(in_game.update_idle(empty, True, None, 70.0), (True, 70.0))
        self.assertEqual(in_game.update_idle(empty, True, 70.0, 900.0), (True, 70.0))

    def test_close_decision_covers_limits_and_cooldown(self):
        state = {'game': True, 'ui': {'123': 1}}
        self.assertEqual(in_game.close_decision(state, 0, None, 100.0), 'close')
        self.assertEqual(in_game.close_decision(state, 0, None, 5.0), 'close')
        self.assertIsNone(in_game.close_decision(state, 0, 90.0, 100.0))
        self.assertEqual(in_game.close_decision(state, in_game.MAX_CLOSES, 0.0, 100.0), 'limit')
        self.assertIsNone(in_game.close_decision({'game': False, 'ui': {'123': 1}}, 0, 0.0, 100.0))
        self.assertIsNone(in_game.close_decision({'game': True, 'ui': {}}, 0, 0.0, 100.0))


if __name__ == '__main__':
    unittest.main()
