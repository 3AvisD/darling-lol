"""Run the install shutdown hook with process and privilege commands mocked."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
SCRIPT = Path(os.environ.get('DARLING_SHUTDOWN_SCRIPT',
                             ROOT / 'build/darling-src/tools/shutdown-user.sh'))


@unittest.skipUnless(SCRIPT.is_file(), 'prepare the pinned Darling sources first')
class ShutdownUserTests(unittest.TestCase):
    def run_hook(self, processes, mode='stop'):
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            state = folder / 'state.json'
            state.write_text(json.dumps({'processes': processes, 'calls': 0}))
            driver = folder / 'driver'
            driver.write_text(f'#!{sys.executable}\n' + '''
import json, os, sys
from pathlib import Path
state = Path(os.environ['TEST_STATE'])
data = json.loads(state.read_text())
command = Path(sys.argv[0]).name
if command == 'whoami':
    print('root')
elif command == 'ps':
    if '-eo' in sys.argv:
        for pid, status, args in data['processes']:
            print(pid, status, args)
    elif os.environ['TEST_MODE'] != 'vanished':
        print('test-user')
    else:
        data['processes'] = []
        state.write_text(json.dumps(data))
elif command == 'timeout':
    data['calls'] += 1
    if os.environ['TEST_MODE'] == 'stop':
        data['processes'] = [p for p in data['processes'] if p[2] != '/sbin/launchd']
    state.write_text(json.dumps(data))
    if os.environ['TEST_MODE'] == 'failed':
        sys.exit(1)
    if os.environ['TEST_MODE'] == 'timed-out':
        sys.exit(124)
elif command != 'sleep':
    raise RuntimeError('Unexpected command: ' + command)
''')
            driver.chmod(0o755)
            for command in ('whoami', 'ps', 'timeout', 'sleep'):
                (folder / command).symlink_to(driver)
            env = dict(os.environ, PATH=str(folder) + os.pathsep + os.environ['PATH'],
                       TEST_STATE=str(state), TEST_MODE=mode)
            result = subprocess.run(['sh', str(SCRIPT)], env=env, capture_output=True,
                                    text=True, timeout=10)
            return result, json.loads(state.read_text())

    def test_no_instances(self):
        result, state = self.run_hook([])
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(state['calls'], 0)

    def test_ignores_mentions_and_zombies(self):
        result, state = self.run_hook([
            [123, 'S', 'editor launchd.txt'], [124, 'Z', '/sbin/launchd'],
            [125, 'S', 'bash -c echo launchd'],
        ])
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(state['calls'], 0)

    def test_normal_shutdown(self):
        result, state = self.run_hook([[123, 'S', '/sbin/launchd']])
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(state['calls'], 1)

    def test_bounded_failure(self):
        for mode in ('failed', 'timed-out', 'stuck'):
            with self.subTest(mode=mode):
                result, state = self.run_hook([[123, 'S', '/sbin/launchd']], mode)
                self.assertEqual(result.returncode, 1, result.stderr)
                self.assertIn('launchd PID 123 remains alive', result.stderr)
                self.assertEqual(state['calls'], 5)

    def test_process_exits_before_owner_query(self):
        result, state = self.run_hook([[123, 'S', '/sbin/launchd']], 'vanished')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(state['calls'], 0)


if __name__ == '__main__':
    unittest.main()
