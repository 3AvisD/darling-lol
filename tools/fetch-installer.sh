#!/usr/bin/env bash
# Download Riot macOS installer. Usage: ./fetch-installer.sh [euw|na] [outdir]
set -euo pipefail
REGION="${1:-euw}"
OUT="${2:-build/installer}"
mkdir -p "$OUT"
URL="https://lol.secure.dyn.riotcdn.net/channels/public/x/installer/current/live.${REGION}.zip"
echo "GET $URL"
curl -L --fail -o "$OUT/live.${REGION}.zip" "$URL"
ls -lh "$OUT/live.${REGION}.zip"
unzip -l "$OUT/live.${REGION}.zip" | head -15
unzip -p "$OUT/live.${REGION}.zip" "Install League of Legends ${REGION}.app/Contents/Info.plist" | grep -A1 FileVersion
echo "saved to $OUT/live.${REGION}.zip"
