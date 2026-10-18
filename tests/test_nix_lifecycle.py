"""Inert fixtures: no real Darling execution or live prefix modification."""
import importlib.util
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from unittest.mock import MagicMock

ROOT = Path(__file__).resolve().parents[1]


class IdleTests(unittest.TestCase):
    def test_unreadable_maps_fail_closed_but_disappearing_process_is_skipped(self):
        spec = importlib.util.spec_from_file_location('nix_idle_fixture', ROOT / 'scripts/nix-idle.py')
        assert spec is not None and spec.loader is not None
        idle = importlib.util.module_from_spec(spec)
        # Older versions run on import: point even those at an inert prefix.
        with tempfile.TemporaryDirectory() as folder:
            from unittest.mock import patch
            with patch.dict(os.environ, DPREFIX=folder):
                spec.loader.exec_module(idle)
        process = MagicMock()
        process.name = '123'
        process.stat.return_value.st_uid = os.getuid()
        (process / 'maps').read_text.side_effect = PermissionError('denied')
        with self.assertRaisesRegex(RuntimeError, 'Cannot inspect'):
            idle.client_active(Path('/fixture/prefix'), processes=[process])
        (process / 'maps').read_text.side_effect = FileNotFoundError()
        self.assertFalse(idle.client_active(Path('/fixture/prefix'), processes=[process]))


class LifecycleTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        scripts = self.root / 'scripts'
        scripts.mkdir()
        for name in ('nix-session.sh', 'nix-lifecycle.py'):
            if (ROOT / 'scripts' / name).exists():
                shutil.copy(ROOT / 'scripts' / name, scripts / name)
        self.log = self.root / 'log'
        self.env = dict(os.environ, DARLING_WORKSPACE=str(self.root),
                        DPREFIX=str(self.root / 'prefix'), TEST_LOG=str(self.log))
        (self.root / 'prefix/Applications/Riot Client.app').mkdir(parents=True)
        (scripts / 'nix-state.py').write_text('raise SystemExit(0)\n')
        (scripts / 'nix-idle.py').write_text('''import os
calls = 0
def client_active(prefix, **kwargs):
    global calls
    calls += 1
    with open(os.environ['TEST_LOG'], 'a') as f:
        f.write('scan\\n')
    return calls < 2
''')
        (scripts / 'darling-rootless.sh').write_text('''#!/bin/bash
echo "shutdown:$*" >> "$TEST_LOG"
if [ "${SHUTDOWN_RC:-0}" != 0 ] && [ ! -e "$TEST_LOG.retried" ]; then
  touch "$TEST_LOG.retried"
  exit "$SHUTDOWN_RC"
fi
exit 0
''')
        client = scripts / 'run.sh'
        client.write_text('''#!/usr/bin/env bash
echo client-finished >> "$TEST_LOG"
exit "${CLIENT_RC:-0}"
'''.replace('#!/usr/bin/env bash', '#!' + str(shutil.which('bash'))))
        client.chmod(0o755)

    def run_session(self, **environment):
        return subprocess.run(['bash', str(self.root / 'scripts/nix-session.sh')],
                              env=dict(self.env, **environment), text=True,
                              capture_output=True, timeout=10)

    def test_exit_waits_for_command_and_clients_before_shutdown(self):
        result = self.run_session()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.log.read_text().splitlines(),
                         ['client-finished', 'scan', 'scan', 'shutdown:shutdown'])

    def test_command_failure_preserved_and_shutdown_failure_reported(self):
        result = self.run_session(CLIENT_RC='17', SHUTDOWN_RC='23')
        self.assertEqual(result.returncode, 17, result.stderr)
        self.assertIn('shutdown failed', result.stderr)
        self.assertIn('23', result.stderr)
        self.assertEqual(self.log.read_text().splitlines().count('shutdown:shutdown'), 2)
        self.assertIn('shutdown:shutdown', self.log.read_text())
