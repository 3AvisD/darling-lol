#!/usr/bin/env bash
set -euo pipefail

WORKSPACE="${XDG_DATA_HOME:-$HOME/.local/share}/darling-lol"
SHARE_DIR="@SHARE_DIR@"
FHS_BIN="@FHS_BIN@"
UNSHARE_BIN="@UNSHARE_BIN@"
# The store package is both a compatibility identity and a closure GC root.
export DARLING_PACKAGE_ID="$SHARE_DIR"
export DISPLAY="${DISPLAY:-:0}"
export EGL_PLATFORM="${EGL_PLATFORM:-x11}"
export DPREFIX="${DPREFIX:-$HOME/.darling}"
export DARLING_HOST_UID="$(id -u)"
export DARLING_HOST_GID="$(id -g)"
export DARLING_WORKSPACE="$WORKSPACE"

python3 - "$WORKSPACE" <<'PY'
import pathlib, sys
workspace = pathlib.Path(sys.argv[1])
for path in [workspace, *workspace.parents]:
    if path.is_symlink():
        raise SystemExit(f'Refusing symlink workspace ancestor: {path}')
if (workspace / '.launcher.lock').is_symlink():
    raise SystemExit('Refusing symlink workspace lock')
PY
mkdir -p "$WORKSPACE"
# One mutable base runtime per workspace; do not refresh scripts mid-session.
exec 9<>"$WORKSPACE/.launcher.lock"
flock -n 9 || { echo 'Another darling-lol workspace session is active; use its terminal.' >&2; exit 1; }
# Do not attempt to reconnect across separately created user namespaces. No kill,
# sudo, or shutdown is attempted here (the PID might even be stale/reused).
if [ -f "$DPREFIX/.init.pid" ]; then
  read -r server_pid < "$DPREFIX/.init.pid" || true
  if [[ "${server_pid:-}" =~ ^[1-9][0-9]*$ ]] && [ -d "/proc/$server_pid" ]; then
    state=$(awk '/^State:/ {print $2}' "/proc/$server_pid/status")
    if [ "$state" != Z ]; then
      echo 'Prefix has a live server marker; namespace reconnect is unsupported.' >&2
      echo 'Use the original darling-lol-shell terminal to run darling shutdown, then retry. No processes were stopped.' >&2
      exit 1
    fi
  fi
fi
python3 "$SHARE_DIR/scripts/nix-copy.py" "$SHARE_DIR" "$WORKSPACE"
mkdir -p "$WORKSPACE/usr-local"
cd "$WORKSPACE"
# Map subordinate IDs before bubblewrap sets no_new_privs. UID 0 is not host root.
fhs_command=("$UNSHARE_BIN" --user --map-auto --map-current-user --setuid=0 --setgid=0 "$FHS_BIN")
# Keep this supervisor alive to hold the lock: bubblewrap may close extra FDs.
# It never owns or signals game processes; foreground execution carries SIGINT.
case "${1:-}" in
  --shell) shift; "${fhs_command[@]}" bash "$@";;
  --update) shift; "${fhs_command[@]}" ./scripts/update.sh "$@";;
  --installer) shift; "${fhs_command[@]}" bash ./scripts/nix-session.sh --installer "$@";;
  *)
    if [ $# -gt 0 ]; then
      "${fhs_command[@]}" "$@"
    else
      "${fhs_command[@]}" bash ./scripts/nix-session.sh
    fi;;
esac
