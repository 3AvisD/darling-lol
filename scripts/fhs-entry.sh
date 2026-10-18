#!/usr/bin/env bash
set -euo pipefail
: "${DARLING_HOST_UID:?start this environment with darling-lol}"
: "${DARLING_HOST_GID:?start this environment with darling-lol}"
# Carry only namespace-scoped capabilities into the ordinary-user process.
# Darling's shim temporarily switches effective IDs; real IDs stay the user's.
exec setpriv --ruid "$DARLING_HOST_UID" --euid "$DARLING_HOST_UID" \
  --rgid "$DARLING_HOST_GID" --egid "$DARLING_HOST_GID" --keep-groups \
  --inh-caps +all --ambient-caps +all "$@"
