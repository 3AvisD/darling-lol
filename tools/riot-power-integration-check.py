#!/usr/bin/env python3
"""Exercise IOKit sleep notifications in a disposable prefix against fake logind.

Requires a built iokitd, both power probes and an installed Darling server
with the empty OOL descriptor fix. Never contacts the real system bus.
"""
import argparse
import errno
import fcntl
import os
from pathlib import Path
import pty
import re
import select
import shutil
import subprocess
import tempfile
import termios
import time

ROOT = Path(__file__).resolve().parent.parent
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--open-workaround", type=Path,
                    help="development-only guest interposer for testing with an older server")
args = argparse.Namespace(open_workaround=None)


def wait_for(predicate, description, timeout=15):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        if predicate():
            return
        time.sleep(0.02)
    raise RuntimeError("timed out: " + description)


def stop_fixture_prefix(env):
    """Stop only this fixture's prefix and wait out Darling's asynchronous kill."""
    pid_file = Path(env["DPREFIX"]) / ".init.pid"
    try:
        pid_text = pid_file.read_text().strip()
        pid = int(pid_text)
    except (OSError, ValueError):
        pid_text, pid = None, None
    subprocess.run(["darling", "shutdown"], env=env, stdout=subprocess.DEVNULL,
                   stderr=subprocess.DEVNULL, timeout=10)
    if pid is not None:
        def exited():
            try:
                return Path(f"/proc/{pid}/stat").read_text().rsplit(")", 1)[1].split()[0] == "Z"
            except FileNotFoundError:
                return True
        wait_for(exited, "fixture Darling shutdown", timeout=10)
        # getInitProcess treats a zombie PID as live, although its mount namespace
        # is already gone. Forget that exited PID before the next fixture launch.
        try:
            if pid_file.read_text().strip() == pid_text:
                pid_file.unlink()
        except FileNotFoundError:
            pass


class Guest:
    def __init__(self, env, delay=None, *, executable=None, arguments=()):
        master, slave = pty.openpty()
        self.master = master
        self.anchor = delay is None and executable is None
        self.output = ""
        self.log_path = Path(env["DPREFIX"]).parent / f"guest-{time.monotonic_ns()}.log"
        command = ["darling", "shell"]
        if args.open_workaround:
            command += ["env", "DYLD_INSERT_LIBRARIES=/Volumes/SystemRoot" +
                        str(args.open_workaround.resolve())]
        if executable is not None:
            command += ["/Volumes/SystemRoot" + str(Path(executable).resolve()), *arguments]
        elif delay is None:
            command += ["sh", "-c", "printf 'ANCHOR_READY\\n'; exec sleep 120"]
        else:
            command += ["/Volumes/SystemRoot" + str(ROOT / "build/riot-power-notify-check"), str(delay), "30"]
        def terminal():
            os.setsid()
            fcntl.ioctl(0, termios.TIOCSCTTY, 0)
        self.process = subprocess.Popen(command, cwd="/tmp", env=env, stdin=slave,
                                        stdout=slave, stderr=slave, preexec_fn=terminal)
        os.close(slave)

    def read(self):
        while select.select([self.master], [], [], 0)[0]:
            try:
                data = os.read(self.master, 65536)
            except OSError as error:
                if error.errno == errno.EIO:
                    break
                raise
            if not data:
                break
            self.output += data.decode(errors="replace")
        self.log_path.write_text(self.output)
        return self.output

    def ready(self):
        output = self.read()
        if self.anchor:
            if self.process.poll() is not None:
                raise RuntimeError("anchor launcher exited:\n" + output[-2500:])
            return re.search(r"(?m)^ANCHOR_READY\r?$", output) is not None
        if "POWER_NOTIFY FAIL" in output or (self.process.poll() is not None and "READY " not in output):
            raise RuntimeError("guest registration failed:\n" + output[-2500:])
        return "READY " in output

    def finish(self):
        def finished():
            self.read()
            return self.process.poll() is not None
        wait_for(finished, "guest exit")
        output = self.read()
        if self.process.returncode or "POWER_NOTIFY PASS" not in output:
            raise RuntimeError("guest failed:\n" + output[-2500:])

    def close(self):
        # Closing the controlling terminal also releases a privileged launcher.
        os.close(self.master)
        try:
            self.process.wait(timeout=3)
        except subprocess.TimeoutExpired:
            try:
                self.process.terminate()
                self.process.wait(timeout=2)
            except (PermissionError, subprocess.TimeoutExpired):
                print("Launcher did not exit; fixture retained:", self.log_path, flush=True)


# Darling creates a root-owned overlay workdir. Keep fixture files as diagnostics;
# recursive deletion without root would mask the actual test result.
def main():
    global args
    args = parser.parse_args()
    directory = Path(tempfile.mkdtemp(prefix="power-integration-", dir=ROOT / "build"))
    temp = str(directory)
    print("Fixture:", directory, flush=True)
    prefix = directory / "prefix"
    fake = subprocess.Popen([str(ROOT / "build/riot-logind-sleep-check"), "--fake-logind", temp],
                            stdout=subprocess.DEVNULL)
    guests = []
    env = dict(os.environ, DPREFIX=str(prefix), EGL_PLATFORM="x11")
    # Do not pass caller-injected libraries or frameworks into the fixture.
    for name in ("DYLD_INSERT_LIBRARIES", "DYLD_LIBRARY_PATH", "DYLD_FRAMEWORK_PATH"):
        env.pop(name, None)
    def command(value):
        descriptor = os.open(directory / "control", os.O_WRONLY | os.O_NONBLOCK)
        try:
            os.write(descriptor, value.encode())
        finally:
            os.close(descriptor)

    try:
        wait_for(lambda: (directory / "address").exists(), "fake logind startup")
        env["DBUS_SYSTEM_BUS_ADDRESS"] = (directory / "address").read_text().strip()

        # Let the launcher initialize the empty prefix before adding overrides.
        subprocess.run(["darling", "shell", "true"], cwd="/tmp", env=env,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=45, check=True)
        stop_fixture_prefix(env)
        (prefix / "usr/sbin").mkdir(parents=True, exist_ok=True)
        shutil.copy2(ROOT / "build/darling-src/build/src/external/iokitd/iokitd", prefix / "usr/sbin/iokitd")

        def records():
            return (directory / "records").read_text()

        def new_guest(delay):
            guest = Guest(env, delay)
            guests.append(guest)
            try:
                wait_for(guest.ready, "IORegisterForSystemPower", timeout=40)
            except RuntimeError:
                print(guest.read()[-3000:], flush=True)
                raise
            return guest

        def current_lock():
            matches = re.findall(r"^Inhibit\t(\d+)\t", records(), re.M)
            return int(matches[-1]) if matches else 0

        def released(lock):
            match = re.search(rf"^Released\t{lock}\t(-?\d+)$", records(), re.M)
            return int(match[1]) if match else None

        # The bootstrap run can itself have taken and released an inhibitor.
        # Wait for a NEW lock from the anchor; an old record is not readiness.
        bootstrap_lock = current_lock()
        anchor = Guest(env, None)
        guests.append(anchor)
        wait_for(anchor.ready, "anchor shell startup", timeout=40)
        wait_for(lambda: current_lock() > bootstrap_lock, "anchor delay inhibitor")
        initial_lock = current_lock()

        # Two independent clients: wait for the slower acknowledgement.
        first, second = new_guest(-3), new_guest(0)
        wait_for(lambda: current_lock() > 0, "initial delay inhibitor")
        lock = current_lock()
        command("S")
        time.sleep(0.4)
        assert released(lock) is None, "inhibitor released before both clients acknowledged"
        wait_for(lambda: released(lock) is not None, "both acknowledgements")
        assert 1100 <= released(lock) < 3500, records()
        command("W")
        second.finish()
        wait_for(lambda: current_lock() > lock, "new inhibitor after resume")
        print("POWER_INTEGRATION multiple_clients=PASS", flush=True)

        # A client that does not acknowledge cannot hold up sleep indefinitely.
        lock = current_lock()
        command("S")
        time.sleep(0.5)
        assert released(lock) is None, "inhibitor deadline was bypassed"
        wait_for(lambda: released(lock) is not None, "acknowledgement timeout", timeout=7)
        assert 3500 <= released(lock) < 5500, records()
        command("W")
        wait_for(lambda: current_lock() > lock, "inhibitor after timeout/resume")
        print("POWER_INTEGRATION timeout=PASS", flush=True)

        # Dropping the last notification reference releases its pending ack.
        lock = current_lock()
        command("S")
        first.finish()
        wait_for(lambda: released(lock) is not None, "deregistered client's acknowledgement")
        assert 0 <= released(lock) < 2500, records()
        command("W")
        wait_for(lambda: current_lock() > lock, "inhibitor after deregistration")
        print("POWER_INTEGRATION deregister=PASS", flush=True)

        # No clients remain: stale registrations must not force a timeout.
        lock = current_lock()
        command("S")
        wait_for(lambda: released(lock) is not None, "sleep with no clients")
        assert 0 <= released(lock) < 1000, records()
        command("W")
        wait_for(lambda: current_lock() > lock, "inhibitor after empty cycle")
        assert current_lock() == initial_lock + 4, "iokitd restarted or acquired duplicate inhibitors:\n" + records()
        print("POWER_INTEGRATION no_clients=PASS", flush=True)
        print("POWER_INTEGRATION PASS" + (" (empty-OOL test workaround enabled)" if args.open_workaround else ""))
    finally:
        stop_fixture_prefix(env)
        for guest in guests:
            guest.close()
        if (directory / "control").exists():
            try:
                command("Q")
            except OSError:
                pass
        try:
            fake.wait(timeout=5)
        except subprocess.TimeoutExpired:
            fake.terminate()
            fake.wait(timeout=5)


if __name__ == "__main__":
    main()
