#!/bin/sh
# Build every overlay of the optimized library batch, in order.
#
# Each builder recompiles the component's own objects at -O2 with the flags
# the configured build gives them and relinks with the configured link line;
# none of them runs a full cmake build. The builders stop while a League game
# process is running, because a compile job would skew the FPS the user is
# measuring. Install the artifacts with
# python3 build/runtime/tools/install-riot-libs-runtime.py.
set -eu
project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$project_dir"
python3 tools/build-riot-libc-runtime.py
python3 tools/build-riot-cxx-runtime.py
python3 tools/build-riot-platform-strings.py
