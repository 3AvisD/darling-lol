"""Readiness regression fixtures: inert runtime bytes, no Darling execution."""
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class RuntimeIdentityTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='nix-identity-test-')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        for name in ('scripts', 'tools'):
            shutil.copytree(ROOT / name, self.root / name)
        self.prefix = self.root / 'prefix'
        self.guest = self.prefix / 'Users/tester'
        (self.guest / 'RuntimeLibraries').mkdir(parents=True)
        (self.guest / 'RuntimeFrameworks').mkdir()
        (self.guest / 'RuntimeLibraries/test.dylib').write_bytes(b'\xcf\xfa\xed\xfe' + b'fixture')
        (self.guest / 'libDarlingXShm.dylib').write_bytes(b'\xcf\xfa\xed\xfe' + b'fixture')
        audio = self.guest / 'Library/Audio/Plug-Ins/Components/DarlingRiotAudio.component/Contents/MacOS/CoreAudio'
        audio.parent.mkdir(parents=True)
        audio.write_bytes(b'\xcf\xfa\xed\xfe' + b'fixture')
        spec = importlib.util.spec_from_file_location('validation_fixture', self.root / 'tools/validate-riot-runtime.py')
        sys.path.insert(0, str(self.root / 'tools'))
        self.addCleanup(lambda: sys.path.remove(str(self.root / 'tools')))
        validation = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(validation)
        guest_req = getattr(validation, 'GUEST_REQUIRED', getattr(validation, 'REQUIRED', ()))
        prefix_req = getattr(validation, 'PREFIX_REQUIRED', ())
        for relative in guest_req:
            path = self.guest / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b'\xcf\xfa\xed\xfe' + b'inert fixture')
        for relative in prefix_req:
            path = self.prefix / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b'\xcf\xfa\xed\xfe' + b'inert fixture')
        for name in ('usr-local/bin/darling', 'usr-local/bin/darlingserver',
                     'usr-local/libexec/darling/usr/lib/libSystem.B.dylib'):
            path = self.root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text('inert runtime fixture\n')
            path.chmod(0o755)
        (self.root / 'build').mkdir()
        self.config = self.root / 'build/config.json'
        self.config.write_text(json.dumps({'prefix': str(self.prefix), 'guest_user': 'tester',
                                          'guest_home': '/Users/tester'}))
        self.env = dict(os.environ, DPREFIX=str(self.prefix),
                        DARLING_PACKAGE_ID='/nix/store/fixture-package/share/darling-lol')
        self.env.pop('RIOT_GUEST_HOME', None)
        for action in ('stamp-base', 'stamp'):
            result = self.state(action)
            self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.state('check').returncode, 0)

    def state(self, action, env=None):
        return subprocess.run([sys.executable, '-B', 'scripts/nix-state.py', action],
                              cwd=self.root, env=env or self.env, text=True,
                              capture_output=True, timeout=20)

    def test_host_guest_home_override_invalidates_readiness(self):
        env = dict(self.env, RIOT_GUEST_HOME=str(self.prefix / 'Users/alternate'))
        self.assertNotEqual(self.state('check', env).returncode, 0)

    def test_configured_guest_home_change_invalidates_readiness(self):
        config = json.loads(self.config.read_text())
        config['guest_home'] = '/Users/alternate'
        self.config.write_text(json.dumps(config))
        self.assertNotEqual(self.state('check').returncode, 0)

    def test_required_xshm_loss_invalidates_readiness(self):
        (self.guest / 'libDarlingXShm.dylib').unlink()
        self.assertNotEqual(self.state('check').returncode, 0)

    def test_audio_component_loss_invalidates_readiness(self):
        shutil.rmtree(self.guest / 'Library')
        self.assertNotEqual(self.state('check').returncode, 0)

    def test_launcher_execute_permission_loss_invalidates_readiness(self):
        (self.root / 'usr-local/bin/darling').chmod(0o644)
        self.assertNotEqual(self.state('check').returncode, 0)

    def test_base_runtime_tree_loss_invalidates_readiness(self):
        shutil.rmtree(self.root / 'usr-local/libexec')
        self.assertNotEqual(self.state('check').returncode, 0)

    def test_missing_base_server_cannot_be_restamped(self):
        (self.root / 'usr-local/bin/darlingserver').unlink()
        self.assertNotEqual(self.state('stamp-base').returncode, 0)

    def test_empty_prefix_cannot_be_stamped(self):
        shutil.rmtree(self.prefix)
        self.prefix.mkdir()
        self.assertNotEqual(self.state('stamp').returncode, 0)

    def test_configured_prefix_used_without_environment_override(self):
        env = dict(self.env)
        env.pop('DPREFIX')
        self.assertEqual(self.state('check', env).returncode, 0)

    def test_inventory_ignores_changes_inside_backup_directories(self):
        spec = importlib.util.spec_from_file_location('inventory_fixture', self.root / 'scripts/nix-state.py')
        state = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(state)
        runtime = self.root / 'usr-local/libexec/darling'
        for suffix in ('.before-test', '.rt-test', '.llvm-test'):
            backup = runtime / ('backup' + suffix) / 'nested'
            backup.mkdir(parents=True)
            saved = backup / 'saved.dylib'
            saved.write_text('old backup')
            before = state.inventory(runtime)
            saved.write_text('changed backup')
            self.assertEqual(state.inventory(runtime), before)

    def test_inventory_is_independent_of_filesystem_directory_order(self):
        from unittest.mock import patch
        spec = importlib.util.spec_from_file_location('inventory_fixture', self.root / 'scripts/nix-state.py')
        state = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(state)
        runtime = self.root / 'usr-local/libexec/darling'
        for name in ('a', 'b'):
            folder = runtime / name
            folder.mkdir()
            (folder / 'file').write_text(name)
        original_walk = os.walk
        def reversed_walk(*args, **kwargs):
            for directory, dirs, files in original_walk(*args, **kwargs):
                dirs.reverse()
                yield directory, dirs, files
        before = state.inventory(runtime)
        with patch.object(state.os, 'walk', reversed_walk):
            self.assertEqual(state.inventory(runtime), before)


if __name__ == '__main__':
    unittest.main()
