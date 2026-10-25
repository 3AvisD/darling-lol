#!/usr/bin/env python3
"""Check host versions the build and the installed runtime need, before the expensive steps.

Required checks exit 1 with one actionable line each; optional checks only warn.
Set RCLOL_SKIP_PREFLIGHT=1 in the environment to bypass the gate. Nothing here
touches the prefix, the guest or the network.
"""
import argparse
import json
import os
import platform
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / 'tools'))
import llvm_tools

MIN_PYTHON = (3, 10)
MIN_LLVM = 16          # LLVMGetVersion, checked by Iridium at startup, appeared in LLVM 16.
TESTED_LLVM = 22       # The patch tracks LLVM 22/23.
MIN_CMAKE = (3, 20)    # The bundled indium CMakeLists requires 3.20.
MIN_LFS = (2, 13)      # Debian 11 ships 2.13, the oldest release the setup is tested with.
MIN_KERNEL = (5, 14)   # MADV_POPULATE_WRITE.
MIN_VULKAN = (1, 3)    # Indium rejects older devices and needs timeline semaphores.

SOURCE = Path(os.environ.get('RCLOL_PREFLIGHT_SOURCE') or ROOT / 'build/darling-src')
POINTER = b'version https://git-lfs.github.com/spec/v1'
LIBRARY_SUFFIXES = ('.dylib', '.a', '.so', '.o')


def version(text):
    """(major, minor) from the first x.y in text, or None."""
    match = re.search(r'(\d+)\.(\d+)', text or '')
    return (int(match.group(1)), int(match.group(2))) if match else None


def dotted(value):
    return '.'.join(str(part) for part in value) if value else 'unknown'


def output(command, timeout=30):
    """stdout of a command, or None when it is missing, fails or times out."""
    try:
        result = subprocess.run(command, capture_output=True, text=True, errors='replace',
                                timeout=timeout)
    except (OSError, subprocess.SubprocessError):
        return None
    return result.stdout if result.returncode == 0 else None


def check_python(report):
    found = platform.python_version()
    report('python', sys.version_info[:2] >= MIN_PYTHON, found,
           f'Python {dotted(MIN_PYTHON)} or newer is required; older releases lack standard '
           'library calls the helpers use. Ubuntu 22.04 ships 3.10, which is enough.')


def check_cmake(report):
    text = output(['cmake', '--version'])
    found = version(text.splitlines()[0]) if text else None
    report('cmake', found is not None and found >= MIN_CMAKE, dotted(found),
           f'CMake {dotted(MIN_CMAKE)} or newer is required by the configured build.')


def check_git_lfs(report):
    text = output(['git', 'lfs', 'version'])
    found = version(text)
    report('git-lfs', found is not None and found >= MIN_LFS, dotted(found),
           f'Git LFS {dotted(MIN_LFS)} or newer is required: the pinned submodules ship '
           'libraries as LFS objects and pointer text crashes the guest.')


def lfs_pointers(source):
    """Small library-like files under source that are still Git LFS pointer text."""
    found = []
    for folder, directories, files in os.walk(source):
        directories[:] = [name for name in directories if name not in ('.git', '__pycache__')]
        if Path(folder) == source:
            directories[:] = [name for name in directories if name != 'build']
        for name in files:
            if not name.endswith(LIBRARY_SUFFIXES):
                continue
            path = Path(folder) / name
            # A pointer is ~130 bytes. Skip everything else before opening it, so a
            # prepared checkout adds about a second instead of opening every file.
            try:
                if path.is_symlink() or path.stat().st_size > 1024:
                    continue
                with path.open('rb') as handle:
                    if handle.read(len(POINTER)) == POINTER:
                        found.append(path)
            except OSError:
                continue
    return found


def check_lfs_pointers(report):
    if not SOURCE.is_dir():
        # The first build runs this before prepare.py fetches the pinned sources.
        report('lfs-pointer', True, 'no prepared checkout yet')
        return
    pointers = lfs_pointers(SOURCE)
    shown = ', '.join(str(path.relative_to(SOURCE)) for path in pointers[:3])
    if len(pointers) > 3:
        shown += f' (+{len(pointers) - 3} more)'
    report('lfs-pointer', not pointers,
           f'{len(pointers)} library file(s) still contain pointer text: {shown}'
           if pointers else 'no pointer text in the prepared libraries',
           'Git LFS did not fetch these objects. Install git-lfs and re-run '
           './scripts/build.sh (or git lfs pull in that checkout); a pointer file links but '
           'crashes the guest at run time.')


def check_kernel(report):
    release = os.environ.get('RCLOL_PREFLIGHT_KERNEL_RELEASE') or platform.release()
    found = version(release)
    report('kernel', found is not None and found >= MIN_KERNEL, release,
           f'Linux {dotted(MIN_KERNEL)} or newer is required for MADV_POPULATE_WRITE, which '
           "Darwin's allocator uses when it reuses pages.")


def check_llvm(report):
    configuration = llvm_tools.find('llvm-config')
    text = output([configuration, '--version']) if configuration else None
    found = version(text.splitlines()[0]) if text else None
    major = found[0] if found else None
    report('llvm', major is not None and major >= MIN_LLVM,
           f'{dotted(found)} ({configuration or "llvm-config not found"})',
           f'LLVM {MIN_LLVM} or newer is required: Iridium calls LLVMGetVersion at startup and '
           'LLVM 15 does not export it. LLVM 22 or 23 is what the patches are tested with. '
           'Install the development package (for example llvm-dev or llvm23-dev).')
    if major is not None and MIN_LLVM <= major < TESTED_LLVM:
        report('llvm-tested', False, dotted(found),
               f'LLVM {major} builds, but the patch tracks LLVM {TESTED_LLVM}/23 and older '
               'majors are untested; use 22 or 23 to be safe.', required=False)


def check_llvm_tools(report):
    tools = {name: llvm_tools.find(name) for name in ('llvm-objdump', 'llvm-nm')}
    missing = [name for name, path in tools.items() if not path]
    present = ', '.join(f'{name}={path}' for name, path in tools.items() if path)
    report('llvm-tools', not missing, present or 'none found',
           f'Install the LLVM tools package (for example llvm-dev or llvm23-tools): {", ".join(missing)} '
           'not found as an unversioned name, as llvm-<tool>-<N> or in `llvm-config --bindir`. '
           'The runtime install validates Darwin binaries with them.')


def vulkan_devices():
    """(highest apiVersion, device names) from vulkaninfo, or None when it is not available."""
    summary = output(['vulkaninfo', '--summary'])
    if summary is None:
        return None
    api = [version(found) for found in re.findall(r'apiVersion\s*=\s*(\d+\.\d+)', summary)]
    api = [value for value in api if value]
    names = re.findall(r'deviceName\s*=\s*(.+)', summary)
    return (max(api) if api else None, [name.strip() for name in names])


def check_vulkan(report):
    devices = vulkan_devices()
    if devices is None and shutil.which('vulkaninfo'):
        report('vulkan', False, 'vulkaninfo did not finish',
               'vulkaninfo is installed but exited with an error or timed out. Run '
               '`vulkaninfo --summary` for the loader or ICD error, then fix or reinstall the '
               'driver: the runtime needs Vulkan 1.3 and timeline semaphores.')
        return
    if devices is None:
        report('vulkan', False, 'vulkaninfo not available',
               'Install vulkan-tools to check the device. The runtime needs Vulkan 1.3 and '
               'timeline semaphores; without a suitable device the game cannot render.',
               required=False)
        return
    api, names = devices
    detail = f'apiVersion {dotted(api)}' + (f' ({", ".join(names[:2])})' if names else '')
    report('vulkan', api is not None and api >= MIN_VULKAN, detail,
           f'Vulkan {dotted(MIN_VULKAN)} or newer is required: Indium rejects older devices. '
           'Update Mesa or the vendor driver, or check `vulkaninfo --summary`.')
    feature = output(['vulkaninfo'], timeout=60)
    if feature is None:
        return
    values = re.findall(r'timelineSemaphore\s*=\s*(true|false)', feature)
    if values and 'true' not in values:
        report('vulkan-timeline', False, 'timelineSemaphore = false',
               'No Vulkan device reports timeline semaphores, which the Metal wrapper needs; '
               'update the driver or select another ICD.')


def check_display(report):
    display = os.environ.get('DISPLAY')
    if not display:
        report('x11', False, 'DISPLAY unset',
               'Without X11/Xwayland the runtime boots but the game cannot present; expected on '
               'a headless host that is only used to build.', required=False)
        return
    if not shutil.which('xdpyinfo'):
        report('x11', False, f'DISPLAY={display}, xdpyinfo not installed',
               'Install x11-utils to check the display before a launch.', required=False)
        return
    if output(['xdpyinfo'], timeout=15) is None:
        report('x11', False, f'DISPLAY={display}',
               f'Cannot open {display}. On servers, SSH and sudo sessions export XAUTHORITY '
               'with a cookie the user running Darling can read; otherwise iokitd runs without '
               'X11/EGL and the game cannot render. Builds still work without it.',
               required=False)
        return
    vendor = ''
    for probe in (['glxinfo', '-B'], ['eglinfo']):
        text = output(probe, timeout=20)
        if text:
            found = re.search(r'(?:OpenGL vendor string|EGL_VENDOR):\s*(.+)', text)
            if found:
                vendor = f', {probe[0]} vendor {found.group(1).strip()}'
            break
    report('x11', True, f'DISPLAY={display} (xdpyinfo){vendor}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--json', action='store_true', help='machine-readable output')
    args = parser.parse_args()

    checks = []

    def report(name, ok, detail, hint=None, required=True):
        checks.append({'check': name, 'ok': bool(ok), 'required': required,
                       'detail': detail, 'hint': None if ok else hint})

    for check in (check_python, check_cmake, check_git_lfs, check_lfs_pointers, check_kernel,
                  check_llvm, check_llvm_tools, check_vulkan, check_display):
        check(report)

    failed = [check for check in checks if check['required'] and not check['ok']]
    warned = [check for check in checks if not check['required'] and not check['ok']]
    passed = [check for check in checks if check['ok']]
    if args.json:
        print(json.dumps({'ok': not failed, 'checks': checks}, indent=2))
    else:
        print('preflight: host checks the build and runtime need')
        for check in checks:
            state = 'ok  ' if check['ok'] else ('FAIL' if check['required'] else 'warn')
            print(f'  {state} {check["check"]:<13} {check["detail"]}')
            if check['hint'] and not check['ok']:
                print(f'       {check["hint"]}')
        summary = f'preflight: {len(passed)} checks passed'
        if warned:
            summary += f', {len(warned)} warning' + ('s' if len(warned) != 1 else '')
        if failed:
            summary += (f', {len(failed)} required check'
                        + ('s' if len(failed) != 1 else '') + ' failed')
        print(summary)
    if failed:
        if not args.json:
            print('preflight: fix the required checks above, or set RCLOL_SKIP_PREFLIGHT=1 to bypass',
                  file=sys.stderr)
        raise SystemExit(1)


if __name__ == '__main__':
    main()
