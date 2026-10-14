#!/bin/sh
# Host models of the Metal wrapper's bind deduplication (the shipped helpers, extracted verbatim), of the
# vertex-descriptor reset marks (ditto), of the Indium blit cast change and of the render-encoder resource
# arguments (const references + the deferred op's single reference). Linux only; never runs a Mach-O binary.
set -e
here=$(cd "$(dirname "$0")" && pwd)
out=$(cd "$here/../.." && pwd)/build/riot-metal-bind-model
python3 "$here/extract-bind-tracking.py"
python3 "$here/extract-vertex-reset.py"
c++ -std=c++17 -O2 -Wall -I"$out" -o "$out/bind-dedup-model" "$here/bind-dedup-model.cpp"
c++ -std=c++17 -O2 -Wall -o "$out/blit-cast-model" "$here/blit-cast-model.cpp"
c++ -std=c++17 -O2 -Wall -I"$out" -o "$out/vertex-reset-model" "$here/vertex-reset-model.cpp"
c++ -std=c++17 -O2 -Wall -o "$out/bind-refcount-model" "$here/bind-refcount-model.cpp"
"$out/bind-dedup-model"
"$out/blit-cast-model"
"$out/vertex-reset-model"
"$out/bind-refcount-model"
