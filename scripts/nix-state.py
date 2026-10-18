#!/usr/bin/env python3
"""Per-prefix rootless readiness; no Darling execution or prefix writes."""
import hashlib
import json
import os
from pathlib import Path
import pwd
import sys

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from runtime_paths import runtime_paths


def selected_paths():
    config_path = ROOT / 'build/config.json'
    config = json.loads(config_path.read_text()) if config_path.exists() else {}
    guest = config.get('guest_user') or pwd.getpwuid(os.getuid()).pw_name
    config.setdefault('prefix', str(Path.home() / '.darling'))
    config.setdefault('guest_home', '/Users/' + guest)
    configured_prefix = Path(os.environ.get('DPREFIX') or config['prefix']).resolve()
    env_prefix = os.environ.get('DPREFIX')
    try:
        os.environ['DPREFIX'] = str(configured_prefix)
        prefix, home = runtime_paths(config)
    finally:
        if env_prefix is None:
            os.environ.pop('DPREFIX', None)
        else:
            os.environ['DPREFIX'] = env_prefix
    return prefix.resolve(), home.resolve(), guest


def guest_user():
    return selected_paths()[2]


def digest(paths):
    result = hashlib.sha256()
    for path in sorted(paths):
        if path.is_file():
            result.update(str(path.relative_to(ROOT)).encode() + b'\0' + path.read_bytes())
    return result.hexdigest()


def base_identity():
    runtime_tree = ROOT / 'usr-local/libexec/darling'
    runtime_modes = {name: (ROOT / 'usr-local/bin' / name).stat().st_mode & 0o777
                     if (ROOT / 'usr-local/bin' / name).exists() else None
                     for name in ('darling', 'darlingserver')}
    return {'schema': 2, 'package': os.environ.get('DARLING_PACKAGE_ID', ''),
            'runtime_modes': runtime_modes,
            'runtime_tree': inventory(runtime_tree),
            'sources': digest([* (ROOT / 'patches').glob('*.patch'),
                               ROOT / 'upstream/manifest.json',
                               * (ROOT / 'scripts').glob('*.py'),
                               * (ROOT / 'scripts').glob('*.sh'),
                               * (ROOT / 'tools').glob('*')]),
            'runtime': digest([ROOT / 'usr-local/bin/darling',
                               ROOT / 'usr-local/bin/darlingserver'])}


def inventory(root):
    """Include all required trees and modes without hashing large installed binaries."""
    if not root.is_dir():
        return None
    result = hashlib.sha256()
    def enumeration_error(error):
        raise error

    for directory, dirs, files in os.walk(root, followlinks=False,
                                         onerror=enumeration_error):
        excluded = ('.before-', '.rt-', '.llvm')
        dirs[:] = sorted(name for name in dirs if not any(token in name for token in excluded))
        for name in sorted(dirs + files):
            if any(token in name for token in excluded):
                continue
            path = Path(directory) / name
            info = path.lstat()
            target_str = ''
            if path.is_symlink():
                try:
                    target_str = f':{os.readlink(path)}'
                except OSError:
                    pass
            result.update(str(path.relative_to(root)).encode() + b'\0')
            result.update(f'{info.st_mode}:{info.st_size}:{info.st_mtime_ns}{target_str}'.encode())
    return result.hexdigest()


def overlay_identity(home):
    result: dict[str, object] = {name: inventory(home / name) for name in
              ('RuntimeLibraries', 'RuntimeFrameworks',
               'Library/Audio/Plug-Ins/Components/DarlingRiotAudio.component')}
    helper = home / 'libDarlingXShm.dylib'
    result['xshm'] = ([helper.stat().st_mode, helper.stat().st_size,
                       helper.stat().st_mtime_ns] if helper.is_file() else None)
    return result


def identity():
    prefix, home, guest = selected_paths()
    return dict(base_identity(), prefix=str(prefix), guest=guest, guest_home=str(home),
                overlays=overlay_identity(home), uid=os.getuid(), gid=os.getgid())


def read(path):
    try:
        return json.loads(path.read_text())
    except (OSError, ValueError):
        return None


def atomic(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix('.tmp')
    temporary.write_text(json.dumps(value, sort_keys=True) + '\n')
    temporary.replace(path)


def main():
    action = sys.argv[1]
    state = identity()
    key = hashlib.sha256(state['prefix'].encode()).hexdigest()
    marker = ROOT / 'build/nix-ready' / (key + '.json')
    base_marker = ROOT / 'build/nix-base.json'
    if action == 'guest':
        print(state['guest'])
    elif action == 'check-base':
        return 0 if read(base_marker) == base_identity() else 1
    elif action == 'check':
        return 0 if read(marker) == state and read(base_marker) == base_identity() else 1
    elif action == 'stamp-base':
        if (not all((ROOT / 'usr-local/bin' / name).is_file() and
                    os.access(ROOT / 'usr-local/bin' / name, os.X_OK)
                    for name in ('darling', 'darlingserver')) or
                not (ROOT / 'usr-local/libexec/darling').is_dir() or not state['package']):
            raise SystemExit('Cannot stamp readiness without executable base runtime, SDK and package identity')
        atomic(base_marker, base_identity())
    elif action == 'stamp':
        if read(base_marker) != base_identity():
            raise SystemExit('Base runtime readiness is missing; run darling-lol --update in a terminal.')
        home = Path(str(state['guest_home']))
        if not home.is_dir() or not any((home / 'RuntimeLibraries').glob('*')) or not any((home / 'RuntimeFrameworks').glob('*')):
            raise SystemExit('Cannot stamp an empty or missing prefix runtime')
        # Reuse the complete installation gate; readiness must never mint an
        # apparently valid manifest for partial overlays, even when called directly.
        import importlib.util
        spec = importlib.util.spec_from_file_location('runtime_validation', ROOT / 'tools/validate-riot-runtime.py')
        assert spec is not None and spec.loader is not None
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        module.inputs(Path(str(state['prefix'])), home)
        atomic(marker, state)
    elif action in ('invalidate', 'invalidate-base'):
        marker.unlink(missing_ok=True)
        if action == 'invalidate-base':
            base_marker.unlink(missing_ok=True)
    else:
        raise SystemExit(f'Unknown state action: {action}')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
