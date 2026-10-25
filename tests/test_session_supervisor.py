"""Session supervisor: attribution, idle decisions and scoped cleanup."""
import importlib.util
import io
import os
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location(
    'session_supervisor', ROOT / 'tools/session-supervisor.py')
supervisor = importlib.util.module_from_spec(spec)
spec.loader.exec_module(supervisor)


def fake_process(root, pid, ppid=1, state='S', comm='mldr', environ=b'', ns=None,
                 uid=None):
    proc = root / str(pid)
    (proc / 'ns').mkdir(parents=True)
    fields = [state, str(ppid)] + ['0'] * 17 + ['100']
    (proc / 'stat').write_text('%d (%s) %s\n' % (pid, comm, ' '.join(fields)))
    (proc / 'comm').write_text(comm + '\n')
    (proc / 'cmdline').write_bytes(comm.encode() + b'\0')
    if environ is not None:
        (proc / 'environ').write_bytes(environ)
    if ns is not None:
        os.symlink(ns, proc / 'ns/pid')
    return proc


class AttributionTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.prefix = self.root / '.darling'
        self.socket = self.prefix / '.darlingserver.sock'
        self.ns = 'pid:[4026532000]'

    def process(self, pid, **kwargs):
        return fake_process(self.root, pid, ns=kwargs.pop('ns', self.ns), **kwargs)

    def test_socket_names_the_owner_prefix(self):
        proc = self.process(10, comm='RiotClientServices',
                            environ=b'__mldr_sockpath=%s\0' % os.fsencode(self.socket))
        self.assertEqual(supervisor.socket_prefix(proc), self.prefix.resolve())
        self.assertEqual(supervisor.prefix_namespace(self.prefix, self.root), self.ns)

    def test_another_prefix_is_not_owned(self):
        proc = self.process(10, comm='RiotClientServices',
                            environ=b'__mldr_sockpath=/elsewhere/.darlingserver.sock\0')
        self.assertFalse(supervisor.owned_by_prefix(proc, self.prefix.resolve()))

    def test_own_namespace_is_refused(self):
        proc = self.process(10)
        self.assertEqual(
            supervisor.namespace_members(self.ns, self.root, own_namespace=self.ns), [])
        self.assertEqual(
            [pid for pid, _proc in supervisor.namespace_members(self.ns, self.root,
                                                                'pid:[1]')], [10])

    def test_unreadable_environ_is_not_owned(self):
        proc = self.process(10, environ=None)
        self.assertFalse(supervisor.owned_by_prefix(proc, self.prefix.resolve()))

    def test_game_is_matched_through_the_prefix_socket(self):
        proc = self.process(10, comm='LeagueofLegends',
                            environ=b'__mldr_sockpath=%s\0' % os.fsencode(self.socket))
        (proc / 'cmdline').write_bytes(b'/Applications/LeagueofLegends\0')
        self.assertTrue(supervisor.game_running(self.prefix, self.root))
        other = self.root / 'other'
        other.mkdir()
        self.assertFalse(supervisor.game_running(other, self.root))


class CleanupTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.prefix = self.root / '.darling'
        self.ns = 'pid:[4026532000]'
        self.own = 'pid:[4026531000]'
        self.output = io.StringIO()

    def process(self, pid, ppid=1):
        return fake_process(self.root, pid, ppid=ppid, ns=self.ns)

    def test_cleanup_shuts_down_then_terminates_namespace_members(self):
        self.process(10)
        self.process(11, ppid=10)
        calls = []
        killed = []

        def run(command, **_kwargs):
            calls.append(command)

        def kill(pid, signum):
            killed.append((pid, signum))
            if signum == supervisor.signal.SIGTERM:
                import shutil
                shutil.rmtree(self.root / str(pid))

        remaining = supervisor.cleanup(
            self.prefix, '/usr/bin/darling', self.ns, proc_root=self.root,
            run=run, kill=kill, sleep=lambda _seconds: None, out=self.output,
            own_namespace=self.own, term_grace=0, kill_grace=0)
        self.assertEqual(remaining, [])
        self.assertEqual(calls, [['/usr/bin/darling', 'shutdown']])
        # Deepest process first: the child (11) before its parent (10).
        self.assertEqual(killed, [(11, supervisor.signal.SIGTERM),
                                  (10, supervisor.signal.SIGTERM)])
        self.assertIn('guest session stopped', self.output.getvalue())

    def test_cleanup_does_nothing_without_attribution(self):
        output = io.StringIO()
        remaining = supervisor.cleanup(
            self.prefix, '/usr/bin/darling', None, proc_root=self.root,
            run=lambda *_a, **_k: self.fail('must not run darling shutdown'),
            kill=lambda *_a: self.fail('must not kill'), sleep=lambda _s: None,
            out=output, own_namespace=self.own, term_grace=0, kill_grace=0)
        self.assertEqual(remaining, [])
        self.assertIn('gone', output.getvalue())

    def test_live_survivor_is_reported(self):
        self.process(10)
        output = io.StringIO()
        remaining = supervisor.cleanup(
            self.prefix, None, self.ns, proc_root=self.root,
            run=lambda *_a, **_k: None, kill=lambda *_a: None,
            sleep=lambda _s: None, out=output, own_namespace=self.own, term_grace=0, kill_grace=0)
        self.assertEqual([pid for pid, _proc in remaining], [10])
        self.assertIn('still present: 10', output.getvalue())


class RuntimeMarkerTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.proc = self.root / 'proc'
        self.proc.mkdir()
        self.prefix = self.root / '.darling'
        self.prefix.mkdir()

    def server(self, pid=10, argv=None, state='S'):
        proc = fake_process(self.proc, pid, state=state, comm='darlingserver')
        (proc / 'cmdline').write_bytes(
            b'\0'.join(os.fsencode(part) for part in
                       (argv or ['/usr/libexec/darlingserver', str(self.prefix)])) + b'\0')
        (self.prefix / '.init.pid').write_text('%d\n' % pid)
        return proc

    def test_live_server_of_this_prefix_is_recognised(self):
        self.server()
        self.assertTrue(supervisor.runtime_alive(self.prefix, self.proc))

    def test_missing_marker_is_not_a_runtime(self):
        self.assertFalse(supervisor.runtime_alive(self.prefix, self.proc))

    def test_reused_pid_is_not_a_runtime(self):
        self.server()
        proc = self.proc / '10'
        (proc / 'cmdline').write_bytes(b'/usr/bin/other\0\0')
        self.assertFalse(supervisor.runtime_alive(self.prefix, self.proc))

    def test_other_prefix_is_not_ours(self):
        self.server(argv=['/usr/libexec/darlingserver', '/elsewhere/.darling'])
        self.assertFalse(supervisor.runtime_alive(self.prefix, self.proc))

    def test_zombie_server_is_not_a_runtime(self):
        self.server(state='Z')
        self.assertFalse(supervisor.runtime_alive(self.prefix, self.proc))


class SupervisorModeTests(unittest.TestCase):
    """run_supervised must not end a runtime that another session started."""

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.prefix = self.root / '.darling'
        self.prefix.mkdir()
        self.proc = self.root / 'proc'
        self.proc.mkdir()

    def run_supervisor(self):
        output = io.StringIO()
        code = supervisor.run_supervised(
            ['true'], self.prefix, None, proc_root=self.proc, poll=0,
            run=lambda *_a, **_k: None, kill=lambda *_a: None,
            sleep=lambda _s: None, out=output)
        self.assertEqual(code, 0)
        return output.getvalue()

    def test_own_session_is_cleaned_up(self):
        self.assertIn('gone', self.run_supervisor())

    def test_attached_session_is_left_running(self):
        proc = fake_process(self.proc, 10, comm='darlingserver')
        (proc / 'cmdline').write_bytes(
            b'/usr/libexec/darlingserver\0' + os.fsencode(self.prefix) + b'\0')
        (self.prefix / '.init.pid').write_text('10\n')
        output = self.run_supervisor()
        self.assertIn('another session owns this runtime', output)
        self.assertNotIn('guest session stopped', output)


class IdleDecisionTests(unittest.TestCase):
    def test_window_present_resets_the_timer(self):
        self.assertEqual(supervisor.idle_decision(True, 100.0, 200.0, True, False, 60),
                         (True, None, False))

    def test_window_gone_starts_and_then_fires(self):
        seen, since, stop = supervisor.idle_decision(True, None, 100.0, False, False, 60)
        self.assertEqual((seen, since, stop), (True, 100.0, False))
        self.assertEqual(supervisor.idle_decision(True, since, 150.0, False, False, 60),
                         (True, 100.0, False))
        self.assertEqual(supervisor.idle_decision(True, since, 161.0, False, False, 60),
                         (True, 100.0, True))

    def test_game_resets_the_timer(self):
        self.assertEqual(supervisor.idle_decision(True, 100.0, 200.0, False, True, 60),
                         (True, None, False))

    def test_unknown_window_state_never_stops(self):
        self.assertEqual(supervisor.idle_decision(True, 100.0, 200.0, None, False, 60),
                         (True, None, False))

    def test_client_that_was_never_seen_never_stops(self):
        self.assertEqual(supervisor.idle_decision(False, None, 200.0, False, False, 60),
                         (False, None, False))

    def test_disabled_never_stops(self):
        self.assertEqual(supervisor.idle_decision(True, 100.0, 999.0, False, False, 0),
                         (True, None, False))


class ManagedWindowTests(unittest.TestCase):
    def test_client_class_is_detected(self):
        def run(command, **_kwargs):
            output = ('_NET_CLIENT_LIST(WINDOW): window id # 0x1' if '_NET_CLIENT_LIST' in command
                      else 'WM_CLASS(STRING) = "riot client", "Riot Client"')

            class Result:
                stdout = output
            return Result()

        self.assertTrue(supervisor.managed_client_window(run))

    def test_missing_property_is_unknown(self):
        def run(_command, **_kwargs):
            class Result:
                stdout = 'not found'
            return Result()

        self.assertIsNone(supervisor.managed_client_window(run))


if __name__ == '__main__':
    unittest.main()
