#!/usr/bin/env bash
# Run setup and the client in the SAME namespace. Never reconnect by killing it.
set -euo pipefail
cd "${DARLING_WORKSPACE:?start with darling-lol}"
ready() { python3 scripts/nix-state.py check; }
interactive() {
  if [ ! -t 0 ]; then
    echo 'Setup needs an interactive terminal. Run darling-lol --installer in a terminal (or use the desktop entry with Terminal=true).' >&2
    exit 1
  fi
}
if ! ready; then
  interactive
  echo "Prefix: $DPREFIX"
  if ! python3 scripts/nix-state.py check-base; then
    echo 'Base runtime missing or incompatible; building requires about 40–50 GB.'
    read -r -p 'Build/update the rootless runtime? [y/N] ' reply || exit 1
    case "$reply" in y|Y) ;; *) exit 1;; esac
    ./scripts/update.sh
  else
    ./scripts/install-runtime.sh
  fi
fi
# Ready existing installations never rebuild merely because the launcher opens.
if [ "${1:-}" = --installer ] ||
   { [ ! -e "$DPREFIX/Applications/Riot Client.app" ] &&
     [ ! -d "$DPREFIX/Users/Shared/Riot Games/Riot Client.app" ]; }; then
  interactive
  read -r -p 'Region [euw]: ' region || exit 1
  exec python3 scripts/nix-lifecycle.py ./scripts/installer.sh "${region:-euw}"
fi
exec python3 scripts/nix-lifecycle.py ./scripts/run.sh
