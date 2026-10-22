"""Exercise the in-match client window hide with fake /proc entries and X11."""
import importlib.util
import io
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location(
    'hide_in_game', ROOT / 'tools/hide-league-client-in-game.py')
hide_in_game = importlib.util.module_from_spec(spec)
spec.loader.exec_module(hide_in_game)

GAME = '/Applications/League of Legends.app/Contents/LoL/LeagueofLegends'
MAIN = '/Applications/League of Legends.app/Contents/LoL/LeagueClientUx.app/Contents/MacOS/LeagueClientUx'
RIOT = '/Applications/Riot Client.app/Contents/MacOS/RiotClientServices'


class FakeX11:
    def __init__(self):
        self.unmapped = []
        self.shown = []

    def unmap(self, window):
        self.unmapped.append(window)

    def show(self, window):
        self.shown.append(window)


class HideClientInGameTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.proc = self.root / 'proc'
        self.proc.mkdir()
        self.prefix = self.root / '.darling'
        self.env_patch = patch.dict(os.environ, {'DPREFIX': str(self.prefix)})
        self.env_patch.start()
        self.addCleanup(self.env_patch.stop)

    def environment(self, prefix):
        return b'%s=%s\0' % (
            hide_in_game.SOCK_VARIABLE,
            os.fsencode(prefix / hide_in_game.SOCK_NAME),
        )

    def process(self, pid, executable=GAME, prefix='own', environ=None, args=('--flag',)):
        folder = self.proc / str(pid)
        folder.mkdir()
        (folder / 'cmdline').write_bytes(
            executable.encode() + b'\0' +
            b''.join(arg.encode() + b'\0' for arg in args))
        if environ is None:
            environ = self.environment(self.prefix if prefix == 'own' else prefix)
        (folder / 'environ').write_bytes(environ)
        (folder / 'stat').write_text(f'{pid} (game) S')
        return folder

    def test_game_running_for_own_prefix(self):
        self.process(123)
        self.assertTrue(hide_in_game.game_running(self.prefix, self.proc))

    def test_no_game_is_false(self):
        self.process(123, executable=MAIN)
        self.assertFalse(hide_in_game.game_running(self.prefix, self.proc))

    def test_game_of_another_prefix_is_false(self):
        self.process(123, prefix=self.root / 'other')
        self.assertFalse(hide_in_game.game_running(self.prefix, self.proc))

    def test_game_without_socket_is_false(self):
        self.process(123, environ=b'')
        self.assertFalse(hide_in_game.game_running(self.prefix, self.proc))

    def test_unreadable_environment_is_unknown(self):
        folder = self.process(123)
        original = Path.read_bytes

        def read(path):
            if path.name == 'environ':
                raise PermissionError()
            return original(path)

        with patch.object(Path, 'read_bytes', read):
            self.assertIsNone(hide_in_game.game_running(self.prefix, self.proc))

    def test_target_windows_ignores_game_class(self):
        with patch.object(hide_in_game, 'xprop_root',
                          return_value=['0x10', '0x20', '0x30']), \
             patch.object(hide_in_game, 'window_class',
                          side_effect=[
                              'wm_class(string) = "leagueclientux"',
                              'wm_class(string) = "leagueoflegends"',
                              'wm_class(string) = "riot client"',
                          ]):
            self.assertEqual(hide_in_game.target_windows(), [0x10, 0x30])

    def test_hides_then_shows_on_match_end(self):
        states = iter([True, True, False])
        x11 = FakeX11()
        output = io.StringIO()
        watcher = hide_in_game.Watcher(
            self.prefix, x11,
            window_source=lambda: [0xabc],
            state_source=lambda _prefix: next(states),
            sleep=lambda _seconds: None,
            out=output,
        )
        watcher.run(max_steps=3)
        self.assertEqual(x11.unmapped, [0xabc])
        self.assertEqual(x11.shown, [0xabc])
        self.assertIn('hidden 0xabc', output.getvalue())
        self.assertIn('the match ended', output.getvalue())
        self.assertIn('shown 0xabc', output.getvalue())

    def test_unreadable_state_restores_the_window(self):
        states = iter([True, None])
        x11 = FakeX11()
        output = io.StringIO()
        watcher = hide_in_game.Watcher(
            self.prefix, x11,
            window_source=lambda: [0xdef],
            state_source=lambda _prefix: next(states),
            sleep=lambda _seconds: None,
            out=output,
        )
        watcher.run(max_steps=2)
        self.assertEqual(x11.unmapped, [0xdef])
        self.assertEqual(x11.shown, [0xdef])
        self.assertIn('unreadable /proc state', output.getvalue())

    def test_no_game_does_not_touch_windows(self):
        x11 = FakeX11()
        watcher = hide_in_game.Watcher(
            self.prefix, x11,
            window_source=lambda: [0xabc],
            state_source=lambda _prefix: False,
            sleep=lambda _seconds: None,
            out=io.StringIO(),
        )
        watcher.run(max_steps=1)
        self.assertEqual(x11.unmapped, [])
        self.assertEqual(x11.shown, [])

    def test_main_is_disabled_by_default(self):
        with patch.dict(os.environ, {
            'RCLOL_HIDE_CLIENT_IN_GAME': '',
            'RCLOL_CLOSE_CLIENT_IN_GAME': '',
        }):
            self.assertEqual(hide_in_game.main([]), 0)

    def test_close_mode_takes_precedence(self):
        with patch.dict(os.environ, {
            'RCLOL_HIDE_CLIENT_IN_GAME': '1',
            'RCLOL_CLOSE_CLIENT_IN_GAME': '1',
        }):
            self.assertEqual(hide_in_game.main([]), 0)


if __name__ == '__main__':
    unittest.main()
