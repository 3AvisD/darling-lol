#!/bin/sh
# Build and run the host-side tests of the Indium deferred hand-off (Linux host, no guest, no GPU).
#
#   tools/test-deferred-handoff.cpp        the ordered worker's FIFO, the present-dispatch token, the wait counters
#   tools/test-deferred-encoder-model.cpp  the deferred blit/render op hand-off: order, resource lifetime, drop safety
#
# Both compile the real headers from deps/indium/private-include with the host compiler. The counter test writes its
# summary to /tmp/opencode/waits-test.log (DARLING_WAITS_LOG) and needs /tmp/metal-debug-waits for the length of the
# run; it removes the flag again if it created it (and first checks, in a forked child, that the counters stay off and
# write nothing without it).
#
# In the runtime the mechanisms exercised here are opt-in experiments (round-3 behaviour is the default): the narrow
# present-dispatch wait needs /tmp/metal-present-dispatch-wait, the deferred blit list /tmp/metal-deferred-blit, the
# counters /tmp/metal-debug-waits. The tests exercise the mechanisms themselves, independent of those flag files.
set -eu
project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
out_dir=${RIOT_TEST_OUT:-/tmp/opencode}
include="$project_dir/build/darling-src/src/external/metal/deps/indium/private-include"
mkdir -p "$out_dir"
for name in test-deferred-handoff test-deferred-encoder-model; do
	c++ -std=c++17 -O1 -pthread -I "$include" "$project_dir/tools/$name.cpp" -o "$out_dir/$name"
	echo "== $name"
	"$out_dir/$name"
done
echo "all host deferred-hand-off tests passed"
