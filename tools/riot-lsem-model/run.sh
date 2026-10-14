#!/bin/sh
# Host model of the process-local Mach semaphores: extracts the shipped code from the patched tree, builds it with
# shims for the syscalls and server RPCs, and runs the stress scenarios. Linux only; never runs a Mach-O binary.
set -e
here=$(cd "$(dirname "$0")" && pwd)
out=$(cd "$here/../.." && pwd)/build/riot-lsem-model
python3 "$here/extract-lsem.py" "$@"
cc -O2 -pthread -Wall -I"$out" -o "$out/lsem-model" "$here/lsem-model.c"
"$out/lsem-model" | tee "$out/lsem-model-output.txt" | grep -E "FAIL|LSEM_MODEL checks"
