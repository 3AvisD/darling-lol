#!/usr/bin/env bash
# Host + Darling dependency check. Supports Arch / Debian-Ubuntu / Fedora.
set -u
ok=1
need() { command -v "$1" >/dev/null 2>&1 || { echo "MISSING: $1"; ok=0; }; }
echo "== host =="
uname -a
need clang; need cmake; need git; need git-lfs; need curl; need unzip; need file
[ "$(uname -m)" = x86_64 ] || { echo "WARN: Darling officially needs x86_64 host (got $(uname -m))"; }
if [ -f /etc/arch-release ]; then
  echo "distro: arch"
  for p in fuse libudev cairo mesa glu egl tiff freetype libxml2 fontconfig libbsd libxrandr libxcursor libgiflib pulseaudio ffmpeg dbus libxkbfile openssl llvm libcap vulkan-headers libcurl libedit; do :; done
  echo "(install: sudo pacman -S cmake clang bison flex fuse libudev cairo mesa glu egl tiff freetype libxml2 fontconfig libbsd libxrandr libxcursor libgiflib pulseaudio ffmpeg dbus libxkbfile openssl llvm libcap vulkan-headers libcurl libedit git-lfs)"
elif [ -f /etc/debian_version ]; then echo "distro: debian/ubuntu (see darling docs/build-instructions.html)";
elif [ -f /etc/fedora-release ]; then echo "distro: fedora/rhel";
else echo "distro: unknown"; fi
echo "== darling =="
command -v darling >/dev/null && darling --version 2>&1 || echo "darling not installed yet (expected at M0)"
[ -d "$HOME/.darling" ] && echo "prefix ~/.darling exists" || echo "prefix ~/.darling absent (created on first 'darling shell')"
echo "== gpu =="
ls /dev/dri 2>/dev/null || echo "no /dev/dri (Vulkan render needs GPU or lavapipe)"
command -v vulkaninfo >/dev/null && vulkaninfo --summary 2>&1 | head -20 || echo "vulkaninfo absent"
[ "$ok" -eq 1 ] && echo "OK" || { echo "FAIL: install missing tools"; exit 1; }
