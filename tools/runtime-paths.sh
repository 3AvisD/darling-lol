# Sourced by configured runtime installers; RIOT_GUEST_HOME is a host path.
# Keep these defaults aligned with runtime_paths.py.
prefix_dir=${DPREFIX:-"@DPREFIX@"}
guest_home=${RIOT_GUEST_HOME:-"$prefix_dir@GUEST_HOME@"}
case "$prefix_dir" in
 /*) : ;;
 *) echo "DPREFIX must be an absolute host path" >&2; exit 1 ;;
esac
prefix_dir=${prefix_dir%/}
case "$guest_home" in
 "$prefix_dir"/*) : ;;
 *) echo "RIOT_GUEST_HOME must be inside DPREFIX" >&2; exit 1 ;;
esac
case "$guest_home/" in
 */../*|*/./*) echo "RIOT_GUEST_HOME must not contain dot components" >&2; exit 1 ;;
esac
export DPREFIX="$prefix_dir" RIOT_GUEST_HOME="$guest_home"
python3 "@PROJECT_ROOT@/build/runtime/tools/runtime_paths.py" "$prefix_dir" || exit 1
