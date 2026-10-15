#!/usr/bin/env python3
"""Test IOPM assertions against fake logind/ScreenSaver in a disposable prefix."""
import argparse
import importlib.util
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location("power_fixture", ROOT / "tools/riot-power-integration-check.py")
fixture = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fixture)
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--iokitd", type=Path,
                    default=ROOT / "build/darling-src/build/src/external/iokitd/iokitd")
args = parser.parse_args()


def main():
    directory = Path(tempfile.mkdtemp(prefix="assertion-integration-", dir=ROOT / "build"))
    prefix = directory / "prefix"
    print("Fixture:", directory, flush=True)
    fake = subprocess.Popen([str(ROOT / "build/riot-logind-sleep-check"), "--fake-logind", str(directory)],
                            stdout=subprocess.DEVNULL)
    env = dict(os.environ, DPREFIX=str(prefix), EGL_PLATFORM="x11")
    for name in ("DYLD_INSERT_LIBRARIES", "DYLD_LIBRARY_PATH", "DYLD_FRAMEWORK_PATH"):
        env.pop(name, None)
    guests = []

    def records():
        return (directory / "records").read_text()

    def state():
        locks, screens = {}, set()
        for line in records().splitlines():
            fields = line.split("\t")
            if fields[0] == "Inhibit": locks[int(fields[1])] = (fields[2], fields[-1])
            elif fields[0] == "Released": locks.pop(int(fields[1]), None)
            elif fields[0] == "ScreenInhibit": screens.add(int(fields[1]))
            elif fields[0] == "ScreenRelease": screens.discard(int(fields[1]))
        blocking = [what for what, mode in locks.values() if mode == "block"]
        assert all(what == "idle" for what in blocking), "assertions must not block manual suspend"
        return len(blocking), len(screens)

    def expect_state(idle, screens):
        try:
            fixture.wait_for(lambda: state() == (idle, screens), f"inhibitors idle={idle} screen={screens}")
        except RuntimeError:
            print(records(), flush=True)
            raise

    def probe(*arguments):
        guest = fixture.Guest(env, executable=ROOT / "build/riot-power-assertion-check", arguments=arguments)
        guests.append(guest)
        return guest

    def stage(guest, label):
        def arrived():
            output = guest.read()
            if "FAIL:" in output or guest.process.poll() is not None:
                raise RuntimeError("assertion guest failed:\n" + output[-1800:])
            return re.search(rf"(?m)^STAGE {re.escape(label)}\r?$", output) is not None
        fixture.wait_for(arrived, "assertion stage " + label)

    def advance(guest):
        os.write(guest.master, b"\n")

    def finished(guest, marker=None):
        def exited():
            guest.read()
            return guest.process.poll() is not None
        fixture.wait_for(exited, "assertion guest exit")
        assert guest.process.returncode == 0, guest.output[-1800:]
        if marker: assert marker in guest.output, guest.output[-1800:]

    try:
        fixture.wait_for(lambda: (directory / "address").exists(), "fake power services")
        address = (directory / "address").read_text().strip()
        env["DBUS_SYSTEM_BUS_ADDRESS"] = address
        env["DBUS_SESSION_BUS_ADDRESS"] = address
        subprocess.run(["darling", "shell", "true"], cwd="/tmp", env=env,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=45, check=True)
        fixture.stop_fixture_prefix(env)
        (prefix / "usr/sbin").mkdir(parents=True, exist_ok=True)
        shutil.copy2(args.iokitd, prefix / "usr/sbin/iokitd")
        previous_locks = records().count("Inhibit\t")
        anchor = fixture.Guest(env)
        guests.append(anchor)
        fixture.wait_for(anchor.ready, "anchor shell", timeout=40)
        fixture.wait_for(lambda: records().count("Inhibit\t") > previous_locks, "anchor power monitor")

        main_guest = probe()
        stage(main_guest, "idle-held")
        expect_state(1, 0)
        print("POWER_ASSERTION_INTEGRATION aliases_and_retain=PASS", flush=True)

        worker = probe("--exit-held")
        stage(worker, "orphan-held")
        expect_state(2, 1)  # Both applications share the system-idle backend.
        advance(worker)
        finished(worker)
        expect_state(1, 0)  # Dead child's display assertion is removed; parent stays held.
        print("POWER_ASSERTION_INTEGRATION process_exit=PASS", flush=True)

        advance(main_guest)
        stage(main_guest, "idle-released")
        expect_state(0, 0)
        print("POWER_ASSERTION_INTEGRATION release=PASS", flush=True)

        for label, idle, screens in [("display-held", 1, 1), ("display-off", 0, 0),
                                     ("display-on", 1, 1), ("display-released", 0, 0),
                                     ("idle-off", 0, 0), ("idle-on", 1, 0), ("finished", 0, 0)]:
            advance(main_guest)
            stage(main_guest, label)
            expect_state(idle, screens)
        advance(main_guest)
        finished(main_guest, "POWER_ASSERTION PASS failures=0")
        print("POWER_ASSERTION_INTEGRATION display_and_levels=PASS", flush=True)
        print("POWER_ASSERTION_INTEGRATION PASS", flush=True)
    finally:
        fixture.stop_fixture_prefix(env)
        for guest in guests: guest.close()
        try:
            control = os.open(directory / "control", os.O_WRONLY | os.O_NONBLOCK)
            os.write(control, b"Q")
            os.close(control)
        except OSError:
            pass
        try: fake.wait(timeout=5)
        except subprocess.TimeoutExpired:
            fake.terminate()
            fake.wait(timeout=5)


if __name__ == "__main__":
    main()
