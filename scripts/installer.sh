#!/bin/sh
set -eu
repo=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$repo"
region=${1:-euw}
case "$region" in euw|eune|na|br|kr|jp|lan|las|oce|ru|tr) ;; *) echo 'Use a Riot installer region code' >&2; exit 1;; esac
python3 scripts/configure.py >/dev/null
bash build/runtime/tools/fetch-installer.sh "$region" build/installer
unzip -q -o "build/installer/live.$region.zip" -d build/installer
RIOT_LAUNCHER_HOST="$repo/build/installer/Install League of Legends $region.app/Contents/MacOS/RiotClientServices"
RCLOL_REGION=$(printf '%s' "$region" | tr '[:lower:]' '[:upper:]')
export RIOT_LAUNCHER_HOST RCLOL_REGION
exec sh scripts/run.sh
