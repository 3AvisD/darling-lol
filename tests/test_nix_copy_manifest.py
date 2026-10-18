"""Managed refresh regressions; all installations are disposable fixtures."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
COPY = ROOT / 'scripts/nix-copy.py'
MANIFEST = '.nix-managed-files.json'


class ManagedCopyTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(dir=os.environ.get('TMPDIR'))
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.source = self.root / 'source'
        self.destination = self.root / 'workspace'
        self.source.mkdir()
        self.destination.mkdir()
        self.put(self.source, 'scripts/current.sh', 'current')

    def put(self, root, name, content='user data'):
        path = root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content)
        return path

    def refresh(self, success=True, source=None, destination=None):
        result = subprocess.run([sys.executable, str(COPY), str(source or self.source),
                                 str(destination or self.destination)],
                                capture_output=True, text=True)
        if success:
            self.assertEqual(result.returncode, 0, result.stderr)
        else:
            self.assertNotEqual(result.returncode, 0)
        return result

    def manifest(self, files=(), directories=()):
        path = self.destination / MANIFEST
        path.write_text(json.dumps({'version': 1, 'files': list(files),
                                    'directories': list(directories)}))
        return path

    def test_refresh_prunes_removed_files_and_empty_directories(self):
        stale = self.put(self.source, 'scripts/obsolete/nested/old.sh')
        self.put(self.source, 'NOTICE.md')
        self.refresh()
        stale.unlink()
        stale.parent.rmdir()
        stale.parent.parent.rmdir()
        (self.source / 'NOTICE.md').unlink()
        self.refresh()
        self.assertFalse((self.destination / 'scripts/obsolete').exists())
        self.assertFalse((self.destination / 'NOTICE.md').exists())
        self.assertEqual((self.destination / 'scripts/current.sh').read_text(), 'current')
        state = json.loads((self.destination / MANIFEST).read_text())
        self.assertEqual(state['files'], ['scripts/current.sh'])
        self.assertEqual(state['directories'], ['scripts'])

    def test_removed_tree_retains_unmanaged_children_and_mutable_roots(self):
        self.put(self.source, 'docs/old.txt')
        self.refresh()
        (self.source / 'docs/old.txt').unlink()
        (self.source / 'docs').rmdir()
        preserved = ['docs/personal.txt', 'scripts/local.sh', 'build/cache',
                     'usr-local/bin/tool', 'prefix/game/save', 'other/data']
        for name in preserved:
            self.put(self.destination, name)
        self.refresh()
        self.assertFalse((self.destination / 'docs/old.txt').exists())
        for name in preserved:
            self.assertEqual((self.destination / name).read_text(), 'user data')

    def test_migration_preserves_unknown_legacy_files_then_tracks_new_files(self):
        self.put(self.destination, 'scripts/legacy.sh')
        self.refresh()
        self.assertTrue((self.destination / MANIFEST).is_file())
        (self.source / 'scripts/current.sh').unlink()
        self.refresh()
        self.assertTrue((self.destination / 'scripts/legacy.sh').is_file())
        self.assertFalse((self.destination / 'scripts/current.sh').exists())














if __name__ == '__main__':
    unittest.main()
