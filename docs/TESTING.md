# Testing on another PC

Build and install the base/runtime first. Native probes are built by the incremental runtime scripts. With the default guest username, run a probe like this (replace `me` and the host checkout path):

```sh
darling shell env \
  DYLD_FRAMEWORK_PATH=/Users/me/RuntimeFrameworks \
  DYLD_LIBRARY_PATH=/Users/me/RuntimeLibraries \
  /Volumes/SystemRoot/path/to/darling-lol/build/riot-appkit-pump-check
```

Build additional probes when needed, for example:

```sh
python3 build/runtime/tools/build-riot-metal-concurrency-check.py
```

Useful probes:

- `riot-appkit-pump-check`: every queued callback must arrive in its own event poll; expected `events=200 callbacks=200 late_polls=0`.
- `riot-event-coalescing-check`: preserve key events and latest mouse motion.
- `riot-metal-presentation-check`: Vulkan clear reaches the GL texture, including late window/context attachment.
- `riot-metal-concurrency-check`: submission ordering, resource lifetime and concurrent recording.

`RIOT_PRESENTATION_STRESS=1` makes the presentation probe run 120 frames. Force async encoding for a non-game probe with the guest flag `/tmp/metal-async-encode`, then remove it after testing. The game enables async encoding by default. The tested old path leaked 120 synchronization FDs over 120 frames; the fixed path leaked zero. Inspect the host process's `/proc/PID/fd` for `anon_inode:syncobj_file` if reproducing that test.

For gameplay, start a practice game through Riot. Check sound, keyboard/mouse buttons, fullscreen confinement, camera motion and returning to the client. Use the real UI; an unknown version/layout is a reason to stop automation and inspect it.

The rendered `baron-setup.py` is for a fresh practice game. `baron-reset.py` is for an already-configured scene and deliberately preserves refresh toggles. The helpers expect a focused 1920x1080 game at global 0,0 and identify the two verified panel sizes before clicking. Auto-refresh settings are toggles; visually verify them rather than blindly repeating setup on an existing match.

Passive captures are opt-in. `CYCLE_FRAME_INTERVALS=1 sh build/runtime/tools/profile-cycle.sh` restarts the runtime and creates/reconnects to practice; do not run it during a real match. It enables bounded timing blocks after a 90-second warmup. Native capture files under the guest `/tmp` contain request/render/flush timestamps, not physical scanout times. Re-arm through the guest `/tmp/metal-frame-interval-repeat` flag. The current bounded recorder writes a block synchronously; its file-write boundary can distort isolated worst frames. Treat it as diagnostic instrumentation.

Compare the same scene, ability activity and camera behavior with the correct charger connected. Do not report a paused, dead, unfocused or empty scene as a fight benchmark. Report p99 frame time as milliseconds; the reciprocal is a percentile-equivalent FPS, not the mean of the slowest 1% of frames.

The CPU symbolizer offers `--leaf-only` because the host DWARF unwinder can drop guest Mach-O stacks. Successfully unwound samples alone are not a representative profile. Local API lockfiles, account data and raw logs must stay outside version control.
