#!/bin/sh
# Host models of the Metal wrapper's bind deduplication (the shipped helpers, extracted verbatim) and of the Indium
# blit cast change. Linux only; never runs a Mach-O binary.
set -e
here=$(cd "$(dirname "$0")" && pwd)
out=$(cd "$here/../.." && pwd)/build/riot-metal-bind-model
python3 "$here/extract-bind-tracking.py"
c++ -std=c++17 -O2 -Wall -I"$out" -o "$out/bind-dedup-model" "$here/bind-dedup-model.cpp"
c++ -std=c++17 -O2 -Wall -o "$out/blit-cast-model" "$here/blit-cast-model.cpp"
"$out/bind-dedup-model"
"$out/blit-cast-model"
