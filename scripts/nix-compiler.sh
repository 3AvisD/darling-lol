# Invoked under clang or clang++: keep the driver's original name.
driver=${0##*/}
for arg in "$@"; do
    case "$arg" in
        *darwin*|*apple*|*macosx*) exec "$unwrapped/$driver" "$@" ;;
    esac
done
exec "$wrapped/$driver" -idirafter /usr/include "$@"
