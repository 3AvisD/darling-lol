#!/bin/sh
# Install the rebuilt AppKit overlay and its paired X11 backend.
# Manually exit the game and both clients, then shut down Darling before use.
set -eu
project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
. "$project_dir/tools/runtime-paths.sh"
framework="$guest_home/RuntimeFrameworks/AppKit.framework/Versions/C"
backup_dir=${RIOT_APPKIT_BACKUP_DIR:-$project_dir/build/appkit-runtime-backups}
# Preflight both artifacts and destinations before replacing either binary.
for name in AppKit X11; do
  case "$name" in
    AppKit) artifact=${RIOT_APPKIT_ARTIFACT:-$project_dir/build/riot-AppKit}; binary="$framework/AppKit";;
    X11) artifact=${RIOT_X11_ARTIFACT:-$project_dir/build/riot-X11}; binary="$framework/Resources/Backends/X11.backend/Contents/MacOS/X11";;
  esac
  [ -f "$artifact" ] || { echo "Build $name first: $artifact" >&2; exit 1; }
  [ -f "$binary" ] && [ ! -L "$binary" ] || {
    echo "Expected an existing $name overlay: $binary" >&2; exit 1;
  }
done
mkdir -p "$backup_dir"
for name in AppKit X11; do
  case "$name" in
    AppKit) artifact=${RIOT_APPKIT_ARTIFACT:-$project_dir/build/riot-AppKit}; binary="$framework/AppKit";;
    X11) artifact=${RIOT_X11_ARTIFACT:-$project_dir/build/riot-X11}; binary="$framework/Resources/Backends/X11.backend/Contents/MacOS/X11";;
  esac
  if cmp -s "$artifact" "$binary"; then
    echo "$name is already current."
    continue
  fi
  old_hash=$(sha256sum "$binary" | cut -d ' ' -f 1)
  backup="$backup_dir/$name-$old_hash"
  if [ -e "$backup" ]; then
    cmp -s "$binary" "$backup" || { echo "Backup verification failed: $backup" >&2; exit 1; }
  else
    cp -p "$binary" "$backup"
  fi
  staged=$(mktemp "$binary.staged.XXXXXX")
  trap 'rm -f "$staged"' EXIT HUP INT TERM
  install -m755 "$artifact" "$staged"
  cmp -s "$artifact" "$staged" || { echo "Staged $name verification failed" >&2; exit 1; }
  mv -f "$staged" "$binary"
  echo "$name installed. Backup: $backup"
done
echo "Launch scripts/run.sh to use the new runtime."
