#!/usr/bin/env python3
"""Check launch behavior using stub commands; never start or contact a client."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parent.parent


def check():
    with tempfile.TemporaryDirectory(prefix="riot-launcher-offline-") as directory:
        folder = Path(directory)
        launcher = folder / "launch.sh"
        launcher.write_text((ROOT / "tools/run-riot-ux-with-logdir-shim.sh").read_text().replace("@GUEST_HOME@", "/Users/offline-check"))
        binary_folder = folder / "bin"
        binary_folder.mkdir()
        log = folder / "calls.jsonl"
        stub = f'''#!{sys.executable}
import json, os, sys
record = {{"command": os.path.basename(sys.argv[0]), "args": sys.argv[1:]}}
if record["command"] == "darling":
    record["flags"] = {{key: os.environ.get(key) for key in
        ("RCLOL_APP_ARG3", "RCLOL_APP_ARG3_SLOT", "RCLOL_UX_ARG2", "RCLOL_UX_ARG2_SLOT", "RCLOL_UX_ARG1")}}
with open({str(log)!r}, "a") as output:
    output.write(json.dumps(record) + "\\n")
'''
        for name in ("python3", "darling"):
            path = binary_folder / name
            path.write_text(stub)
            path.chmod(0o755)
        env = dict(os.environ)
        env.pop("RIOT_LAUNCHER_HOST", None)
        env.pop("RCLOL_LAUNCH_PRODUCT", None)
        env["PATH"] = str(binary_folder) + os.pathsep + env.get("PATH", "")
        env["RCLOL_NATIVE_VPX"] = "0"
        env["RCLOL_LEAGUE_UX_TUNING"] = "1"
        # Inherited legacy settings must not re-enable inspection or unlimited frames.
        env.update({
            "RCLOL_APP_ARG3": "--inspect=127.0.0.1:9229",
            "RCLOL_APP_ARG3_SLOT": "--session-id=",
            "RCLOL_UX_ARG2": "--disable-frame-rate-limit",
            "RCLOL_UX_ARG2_SLOT": "--crash-environment=",
            "RCLOL_UNTHROTTLED_BEGIN_FRAMES": "1",
            "RCLOL_FOREGROUND_SCHEDULING": "1",
            "RCLOL_LEAGUE_UPDATE_HANDOFF": "1",
            "RCLOL_OPEN_URL_BRIDGE": "1",
            "RCLOL_DISABLE_GPU_VSYNC": "1",
        })
        subprocess.run(["sh", str(launcher)], env=env, check=True, capture_output=True, text=True)
        records = [json.loads(line) for line in log.read_text().splitlines()]
        maintenance = [r for r in records if r["command"] == "python3"]
        assert [Path(r["args"][0]).name for r in maintenance] == [
            "ensure-darling-runtime.py", "ensure-darling-service-accounts.py", "ensure-league-runtime-layout.py"
        ], "unexpected automatic helper"
        launches = [r for r in records if r["command"] == "darling"]
        assert len(launches) == 1, "unexpected launch count"
        launch = launches[0]
        assert all(launch["flags"][key] is None for key in (
            "RCLOL_APP_ARG3", "RCLOL_APP_ARG3_SLOT", "RCLOL_UX_ARG2", "RCLOL_UX_ARG2_SLOT"
        )), "inherited inspector/unlimited-frame argument survived"
        assert launch["flags"]["RCLOL_UX_ARG1"], "feature compatibility setting lost"
        assert "--launch-product=league_of_legends" in launch["args"], "product launch lost"
        assert "--app-root=/Applications/Riot Client.app" in launch["args"], "application root lost"
        assert "--log-dir=/Users/offline-check/Library/Logs/Riot Games/Riot Client" in launch["args"], "absolute log directory lost"
        print("LAUNCHER_OFFLINE pass=1 automatic_inspector_helpers=0 inspector_argument=absent unlimited_frames=absent compatibility=preserved")


if __name__ == "__main__":
    check()
