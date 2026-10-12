#!/bin/sh
# darling-lol M58F101: launch the real Riot chain with the argv log-dir shim.
#
# The Riot Foundation starts the UX product with a home-relative
# `--log-dir=Library/Logs/Riot Games/Riot Client`; the app hands that to
# Electron's app.setAppLogsPath(), which THROWS "Path must be absolute", so the
# UX stalls before app.whenReady()/createWindow() and never maps its window.
#
# The shim (tools/m58f101-logdir-shim.c) rewrites `--log-dir=<relative>` to
# `--log-dir=$HOME/<relative>` in argv at load time.  The product is NOT
# hardened, so DYLD_INSERT_LIBRARIES set on the launcher propagates through the
# Foundation to the product.
#
# One-time setup:
#   clang -target x86_64-apple-macosx11.0 -fuse-ld=lld -dynamiclib -nostdlib \
#     -nodefaultlibs -undefined dynamic_lookup -O2 -o /tmp/zzlogdir.dylib \
#     tools/m58f101-logdir-shim.c -Wl,-install_name,/tmp/zzlogdir.dylib
#   cp /tmp/zzlogdir.dylib "$HOME/.darling@GUEST_HOME@/zzlogdir.dylib"
#   # -> guest path @GUEST_HOME@/zzlogdir.dylib (outside the app bundle, so the
#   #    launcher's install-integrity check is unaffected)
#
# Then run this script (guest DISPLAY defaults to :1).
# Darling's native window backend supplies Xlib Display pointers to EGL.
# Select X11 explicitly so Mesa does not choose Wayland from the host session.
set -eu
# Native guest crashes created large system core files and exhausted this disk.
# Address-only diagnostics suffice; bound this runner and its new namespace.
if [ "${RCLOL_CORE_DUMPS:-0}" != 1 ]; then
 ulimit -c 0
fi
runner_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
python3 "$runner_dir/ensure-darling-runtime.py"
python3 "$runner_dir/ensure-darling-service-accounts.py"
python3 "$runner_dir/ensure-league-runtime-layout.py"
# Chromium's native macOS verifier stalls in Darling's Security/trustd path.
# Use its built-in verifier; certificate validation remains enabled.
# Keep the hardware-acceleration switch. The machine-id slot is Sentry
# metadata; reuse it for the verifier switch. Each replacement is one argv entry.
# Additional diagnostics can use RCLOL_APP_ARG2 with an explicit
# RCLOL_APP_ARG2_SLOT identifying an existing expendable argument.
RCLOL_APP_ARGS=${RCLOL_APP_ARGS-'--enable-features=ChromeRootStoreUsed,CertVerifierBuiltin:impl/4'}
RCLOL_APP_ARGS_SLOT=${RCLOL_APP_ARGS_SLOT-'--machine-id='}
export RCLOL_APP_ARGS RCLOL_APP_ARGS_SLOT
RCLOL_APP_ARG2=${RCLOL_APP_ARG2-'--disable-features=RemoteCoreAnimationAPI,AudioServiceOutOfProcess,UseVideoFrameSinkBundle'}
RCLOL_APP_ARG2_SLOT=${RCLOL_APP_ARG2_SLOT-'--crashpad-environment='}
export RCLOL_APP_ARG2 RCLOL_APP_ARG2_SLOT
# The startup/update hooks below connect to Electron's local main inspector.
# Reserve the telemetry session slot for that switch, as with the two existing
# compatibility switches. Bind it to loopback explicitly.
RCLOL_APP_ARG3=${RCLOL_APP_ARG3-'--inspect=127.0.0.1:9229'}
RCLOL_APP_ARG3_SLOT=${RCLOL_APP_ARG3_SLOT-'--session-id='}
export RCLOL_APP_ARG3 RCLOL_APP_ARG3_SLOT
# League's UI (LeagueClientUx, CEF) gets the Riot Client UI's tuning through the shim, applied to its
# GPU helper: the Darling-incompatible Chromium features off and frame pacing unthrottled. Each
# replacement takes over one existing, expendable switch of that helper's command line.
# RCLOL_LEAGUE_UX_TUNING=0 leaves the League UI untouched.
if [ "${RCLOL_LEAGUE_UX_TUNING:-1}" != 0 ]; then
 RCLOL_UX_ARG1='--disable-features=HardwareMediaKeyHandling,RemoteCoreAnimationAPI,UseVideoFrameSinkBundle'
 RCLOL_UX_ARG1_SLOT='--disable-features='
 RCLOL_UX_ARG2='--disable-frame-rate-limit'
 RCLOL_UX_ARG2_SLOT='--crash-environment='
 export RCLOL_UX_ARG1 RCLOL_UX_ARG1_SLOT RCLOL_UX_ARG2 RCLOL_UX_ARG2_SLOT
fi
# The paired runtime presents completed GPU textures without CPU readback.
# Explicit 0 or an empty value selects the CPU fallback for diagnostics.
for option in RCLOL_GPU_DIRECT_PRESENT RCLOL_GPU_DIRECT_ASYNC RCLOL_GPU_DIRECT_VSYNC DARLING_CA_PRESENT_AT_COMMIT DARLING_CA_SKIP_LOCAL_IOSURFACE_RENDER RCLOL_FORWARD_MOUSE_MOVED; do
 eval 'value=${'"$option"'-1}'
 case "$value" in 0|'') unset "$option";; *) export "$option=$value";; esac
done
# Bound software decode workers before the renderer is created. Native video
# playback remains enabled; avoid excess emulated worker/RPC synchronization.
# 0 restores Chromium's default thread policy for controlled comparisons.
decoder_threads=${RCLOL_DECODER_THREADS:-1}
case "$decoder_threads" in
 0|1|2|4) ;;
 *) echo "RCLOL_DECODER_THREADS must be 0, 1, 2, or 4" >&2; exit 1;;
esac
raster_threads=${RCLOL_RASTER_THREADS:-0}
case "$raster_threads" in
 0|1|2|4) ;;
 *) echo "RCLOL_RASTER_THREADS must be 0, 1, 2, or 4" >&2; exit 1;;
esac
if [ "${RCLOL_DISABLE_GPU_VSYNC:-0}" = 1 ]; then
 if [ "${RCLOL_UNTHROTTLED_BEGIN_FRAMES:-1}" = 1 ]; then
  python3 "$runner_dir/riot-decoder-scheduling.py" --threads "$decoder_threads" --raster-threads "$raster_threads" --unthrottled --disable-gpu-vsync > /tmp/riot-decoder-scheduling.log 2>&1 &
 else
  python3 "$runner_dir/riot-decoder-scheduling.py" --threads "$decoder_threads" --raster-threads "$raster_threads" --disable-gpu-vsync > /tmp/riot-decoder-scheduling.log 2>&1 &
 fi
elif [ "${RCLOL_UNTHROTTLED_BEGIN_FRAMES:-1}" = 1 ]; then
 python3 "$runner_dir/riot-decoder-scheduling.py" --threads "$decoder_threads" --raster-threads "$raster_threads" --unthrottled > /tmp/riot-decoder-scheduling.log 2>&1 &
elif [ "$decoder_threads" != 0 ] || [ "$raster_threads" != 0 ]; then
 python3 "$runner_dir/riot-decoder-scheduling.py" --threads "$decoder_threads" --raster-threads "$raster_threads" > /tmp/riot-decoder-scheduling.log 2>&1 &
fi
# This existing local main-process inspector exposes no account data here.
# Install a window hook once, then disconnect instead of polling the app.
if [ "${RCLOL_FOREGROUND_SCHEDULING:-1}" != 0 ]; then
 python3 "$runner_dir/riot-foreground-scheduling.py" > /tmp/riot-foreground-scheduling.log 2>&1 &
fi
# Recover accepted League Update actions that leave the legacy patch request empty.
if [ "${RCLOL_LEAGUE_UPDATE_HANDOFF:-1}" != 0 ]; then
 python3 "$runner_dir/riot-league-update.py" > /tmp/riot-league-update.log 2>&1 &
fi
SHIM_GUEST="@GUEST_HOME@/zzlogdir.dylib"
RIOT_INSERT_LIBRARIES="$SHIM_GUEST:@GUEST_HOME@/libRiotEmptyAutofill.dylib"
# Use the verified bridge when the paired host/guest artifacts are installed.
# Its UUID and interface guards retain Chromium's decoder on other builds.
native_vpx_default=0
if [ -f "$HOME/.darling@GUEST_HOME@/libRiotNativeVpx.dylib" ] && [ -f "$runner_dir/../build/libriot-native-vpx.so" ]; then
 native_vpx_default=1
fi
if [ "${RCLOL_NATIVE_VPX:-$native_vpx_default}" = 1 ]; then
 RIOT_INSERT_LIBRARIES="$RIOT_INSERT_LIBRARIES:@GUEST_HOME@/libRiotNativeVpx.dylib"
fi
A=${RIOT_LAUNCHER_HOST:-"$HOME/.darling/Applications/Riot Client.app/Contents/MacOS/RiotClientServices"}
# External XIM (for example Fcitx) consumes keys before Darling forwards them.
# Use Xlib's local input method; allow explicit testing of another modifier.
# Linux C.UTF-8 is not a supported codecvt locale in the macOS client.
# Keep this scoped to the guest; product region/locale arguments remain explicit.
exec env -u ELECTRON_RUN_AS_NODE EGL_PLATFORM=x11 \
  LANG="${RCLOL_GUEST_LOCALE:-en_US.UTF-8}" LC_ALL="${RCLOL_GUEST_LOCALE:-en_US.UTF-8}" \
  mesa_glthread="${RCLOL_MESA_GLTHREAD-${mesa_glthread-false}}" \
  XMODIFIERS="${RCLOL_XMODIFIERS:-@im=none}" DISPLAY="${DISPLAY:-:1}" \
  DYLD_LIBRARY_PATH="${DYLD_LIBRARY_PATH:-@GUEST_HOME@/RuntimeLibraries}" \
  DYLD_FRAMEWORK_PATH="${DYLD_FRAMEWORK_PATH:-@GUEST_HOME@/RuntimeFrameworks}" \
  DARLING_XSHM_LIBRARY="${DARLING_XSHM_LIBRARY:-@GUEST_HOME@/libDarlingXShm.dylib}" \
  DYLD_INSERT_LIBRARIES="$RIOT_INSERT_LIBRARIES" \
  darling "$A" --region="${RCLOL_REGION:-EUW}" --locale="${RCLOL_LOCALE:-en_GB}" \
    --app-root="/Applications/Riot Client.app" \
    --log-dir="@GUEST_HOME@/Library/Logs/Riot Games/Riot Client"
