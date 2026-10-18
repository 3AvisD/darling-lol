"""darling-lol --set-prefix on temporary directories; no Darling or namespace."""
import errno
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]


class PrefixTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.folder = Path(self.tmp.name)
        self.workspace = self.folder / 'workspace'
        (self.workspace / 'scripts').mkdir(parents=True)
        (self.workspace / 'tools').mkdir()
        for name in ('nix-prefix.py', 'configure.py', 'common.py'):
            shutil.copy(ROOT / 'scripts' / name, self.workspace / 'scripts' / name)
        self.old = self.folder / 'home/.darling'
        (self.old / 'Volumes/SystemRoot').mkdir(parents=True)
        (self.old / 'Applications/League of Legends.app').mkdir(parents=True)
        (self.old / 'Applications/League of Legends.app/game').write_text('game data')
        (self.folder / 'home/.darling.workdir/work').mkdir(parents=True)

    def set_prefix(self, new):
        return subprocess.run([sys.executable, str(self.workspace / 'scripts/nix-prefix.py'), str(new)],
                              env=dict(os.environ, DPREFIX=str(self.old), HOME=str(self.folder / 'home'),
                                       PYTHONDONTWRITEBYTECODE='1'),
                              text=True, capture_output=True, timeout=30)

    def saved_prefix(self):
        return json.loads((self.workspace / 'build/config.json').read_text())['prefix']

    def test_moves_the_prefix_and_saves_it(self):
        new = self.folder / 'bulk/Games/League Of Legends'
        result = self.set_prefix(new)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual((new / 'Applications/League of Legends.app/game').read_text(), 'game data')
        self.assertFalse(self.old.exists())
        self.assertFalse((self.folder / 'home/.darling.workdir').exists())
        self.assertEqual(self.saved_prefix(), str(new))

    def test_copies_across_filesystems_and_removes_the_old_copy_last(self):
        spec = importlib.util.spec_from_file_location('nix_prefix', self.workspace / 'scripts/nix-prefix.py')
        prefix = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(prefix)
        rename = os.rename
        calls = []

        def cross_device(source, target):
            calls.append(Path(target).name)
            if len(calls) == 1:
                raise OSError(errno.EXDEV, 'cross-device link')
            self.assertTrue(self.old.exists(), 'old prefix removed before the copy finished')
            rename(source, target)

        new = self.folder / 'bulk/prefix'
        with patch.object(prefix.os, 'rename', cross_device):
            prefix.move(self.old, new)
        # A failed rename of the prefix, then the rename of the finished copy.
        self.assertEqual(calls, ['prefix', 'prefix'])
        self.assertEqual((new / 'Applications/League of Legends.app/game').read_text(), 'game data')
        self.assertFalse(self.old.exists())
        self.assertFalse((self.folder / 'bulk/prefix.partial').exists())

    def test_switches_to_an_existing_prefix_without_moving(self):
        other = self.folder / 'other'
        (other / 'Volumes/SystemRoot').mkdir(parents=True)
        result = self.set_prefix(other)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue((self.old / 'Applications/League of Legends.app/game').exists())
        self.assertEqual(self.saved_prefix(), str(other))

    def test_refuses_a_directory_with_other_files(self):
        games = self.folder / 'Games'
        games.mkdir()
        (games / 'other-game').mkdir()
        result = self.set_prefix(games)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('not a Darling prefix', result.stderr)
        self.assertTrue(self.old.exists())
        self.assertFalse((self.workspace / 'build/config.json').exists())

    def test_refuses_overlay_separators_and_nested_paths(self):
        for new in (self.folder / 'a,b', self.folder / 'a:b', self.old / 'inner'):
            result = self.set_prefix(new)
            self.assertNotEqual(result.returncode, 0, new)
        self.assertTrue(self.old.exists())


if __name__ == '__main__':
    unittest.main()
