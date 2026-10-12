# Snapshot status — 2026-10-12

This source snapshot includes current local modifications, including experiments that have not all been compared against a fresh clean build. It preserves a development runtime, not an upstream-ready patch series.

Confirmed fixes include a lost wake in kernel workqueue semaphore handling, callback-safe CoreAudio stop, AppKit event handling/post-game controls, and delivering ready main-queue callbacks on every event poll while keeping expensive CF run-loop work throttled.

The presentation callback regression changed from 180 callbacks during 200 polls to 200 callbacks with zero late polls. Native pixel and input checks passed. The user subsequently reported no visible stutter, but still reported sharp fight FPS drops.

A separate presentation defect exported a GPU synchronization FD on the deferred worker while the GL context belonged to the main thread. The live game accumulated over 45,000 syncobj FDs. The corrected import runs on the compositor thread; native stress leaked zero handles over 120 frames, versus 120 on the old build. The installed game was checked at zero leaked syncobj handles and about 111 total open files.

A post-fix, limited-ability Baron run measured 99.6 FPS average and 16.11 ms p99 (62.1 reciprocal FPS). Another spell-heavy attempt measured 85.7 FPS average and 23.51 ms p99 (42.5 reciprocal FPS); practice refresh/camera differences prevent treating these as a clean improvement comparison. These are short practice measurements, not an ARAM guarantee or a 90 FPS p99 result. Historical comparisons using a reset helper that alternated refresh toggles are unreliable.

The development system uses an AMD Barcelo integrated GPU, RADV, Xwayland/Hyprland and two 60 Hz displays without active VRR. Driver capabilities, CPU power sharing and display refresh can differ on another PC. No CPU-affinity or compositor configuration is installed automatically: the legacy `pin-game.sh` assumes this laptop's CPUs and must be adapted before use elsewhere.
