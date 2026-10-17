#!/usr/bin/env python3
"""Preview or explicitly save per-prefix IOKit platform overrides; defaults stay unchanged."""
import argparse
import json
import os
from pathlib import Path
import plistlib
import shlex
import tempfile
import uuid
from xml.parsers.expat import ExpatError

MAX_SIZE = 65536
STRINGS = {'IOPlatformSerialNumber': 64, 'manufacturer': 128, 'model': 128,
           'product-name': 128, 'board-id': 128, 'mlb-serial-number': 64}
BYTES = {'ROM': 6, 'system-id': 16}
ALIASES = {
    'SystemProductName': 'model', 'Type': 'model', 'SMBIOS_MODEL': 'model',
    'SMBIOS_PRODUCT_NAME': 'product-name', 'SMBIOS_MANUFACTURER': 'manufacturer',
    'SMBIOS_BOARD_ID': 'board-id', 'SystemSerialNumber': 'IOPlatformSerialNumber',
    'Serial': 'IOPlatformSerialNumber', 'SMBIOS_SERIAL_NUMBER': 'IOPlatformSerialNumber',
    'SystemUUID': 'IOPlatformUUID', 'SmUUID': 'IOPlatformUUID', 'SMBIOS_PLATFORM_UUID': 'IOPlatformUUID',
    'MLB': 'mlb-serial-number', 'Board Serial': 'mlb-serial-number', 'SMBIOS_MLB': 'mlb-serial-number',
    'SMBIOS_ROM': 'ROM', 'SMBIOS_SYSTEM_ID': 'system-id',
}


def validate(properties):
    if not isinstance(properties, dict) or len(properties) > 9:
        raise ValueError('properties must be a dictionary of supported identity fields')
    result = {}
    for key, value in properties.items():
        if key in STRINGS:
            if not isinstance(value, str) or not 1 <= len(value) <= STRINGS[key] or any(not 32 <= ord(c) <= 126 for c in value):
                raise ValueError('Invalid printable string for ' + key)
        elif key == 'IOPlatformUUID':
            if not isinstance(value, str):
                raise ValueError('IOPlatformUUID must be a UUID string')
            parsed = uuid.UUID(value)
            if not parsed.int:
                raise ValueError('IOPlatformUUID must not be zero')
            value = str(parsed).upper()
        elif key in BYTES:
            if isinstance(value, str):
                value = bytes.fromhex(value.replace(':', '').replace('-', ''))
            if not isinstance(value, bytes) or len(value) != BYTES[key]:
                raise ValueError(key + ' has an invalid byte length')
        else:
            raise ValueError('Unsupported identity field: ' + str(key))
        result[key] = value
    if 'model' in result:
        result.setdefault('product-name', result['model'])
    if 'IOPlatformUUID' in result:
        system_id = uuid.UUID(result['IOPlatformUUID']).bytes_le
        if 'system-id' in result and result['system-id'] != system_id:
            raise ValueError('system-id does not match IOPlatformUUID')
        result['system-id'] = system_id
    return result


def load(source):
    with Path(source).open("rb") as stream:
        raw = stream.read(MAX_SIZE + 1)
    if len(raw) > MAX_SIZE:
        raise ValueError('Identity input exceeds 64 KiB')
    if str(source).endswith('.conf'):
        payload = {}
        for line in raw.decode().splitlines():
            if not line.strip() or line.lstrip().startswith('#'):
                continue
            key, sep, value = line.partition('=')
            if not sep:
                raise ValueError('Malformed legacy identity line')
            values = shlex.split(value, comments=True)
            if len(values) != 1 or key.strip() in payload:
                raise ValueError('Malformed or duplicate legacy identity field')
            payload[key.strip()] = values[0]
    else:
        def unique_keys(pairs):
            result = {}
            for key, value in pairs:
                if key in result: raise ValueError('Duplicate JSON identity field: ' + key)
                result[key] = value
            return result
        if raw.lstrip()[:1] in (b'{', b'['):
            payload = json.loads(raw, object_pairs_hook=unique_keys)
        else:
            payload = plistlib.loads(raw)
    if not isinstance(payload, dict):
        raise ValueError('Identity input must be a dictionary')
    if 'schema' in payload:
        if set(payload) != {'schema', 'properties'} or type(payload['schema']) is not int or payload['schema'] != 1:
            raise ValueError('Unsupported platform identity schema')
        return validate(payload['properties'])
    if 'PlatformInfo' in payload:
        payload = payload['PlatformInfo'].get('Generic') if isinstance(payload['PlatformInfo'], dict) else None
    elif 'Generic' in payload:
        payload = payload['Generic']
    if not isinstance(payload, dict):
        raise ValueError('Missing OpenCore PlatformInfo/Generic dictionary')
    result = {}
    for key, value in payload.items():
        name = ALIASES.get(key, key)
        if name not in {*STRINGS, *BYTES, 'IOPlatformUUID'}:
            continue # OpenCore includes unrelated boot, firmware and CPU settings.
        if name in result and result[name] != value:
            raise ValueError('Conflicting imports for ' + name)
        result[name] = value
    if not result:
        raise ValueError('No supported identity fields found')
    return validate(result)


def destination(prefix):
    prefix = Path(prefix).expanduser()
    if not prefix.is_absolute():
        raise ValueError('Prefix must be absolute')
    path = prefix / 'private/etc/darling-platform.plist'
    if path.is_symlink() or not path.resolve().is_relative_to(prefix.resolve()):
        raise ValueError('Identity destination must remain inside the selected prefix')
    return path


def save(prefix, properties):
    path = destination(prefix)
    prefix = Path(prefix)
    # A host write behind Darling's mounted overlay may not be visible. Never
    # stop a user's session implicitly or write a second copy behind its back.
    marker = prefix / '.init.pid'
    if marker.exists():
        try:
            text = marker.read_text().strip()
            if not text.isdecimal() or int(text) <= 1:
                raise ValueError('Invalid .init.pid; verify the selected prefix is stopped before writing')
            pid = int(text)
            os.kill(pid, 0)
        except ProcessLookupError:
            pass
        else:
            raise ValueError('Shut down the selected Darling prefix before writing platform identity')
    encoded = plistlib.dumps({'schema': 1, 'properties': validate(properties)}, sort_keys=True)
    if path.exists() and path.read_bytes() == encoded:
        return False
    path.parent.mkdir(parents=True, exist_ok=True)
    # Recheck after creating parents, before opening a temporary file.
    path = destination(prefix)
    if path.exists():
        load(path) # refuse to silently overwrite an unknown schema
        backup = path.with_name(path.name + '.before-platform-change')
        if not backup.exists():
            fd = os.open(backup, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
            with os.fdopen(fd, 'wb') as stream:
                stream.write(path.read_bytes())
    fd, temporary = tempfile.mkstemp(prefix='.darling-platform-', dir=path.parent)
    try:
        with os.fdopen(fd, 'wb') as stream:
            stream.write(encoded)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    finally:
        Path(temporary).unlink(missing_ok=True)
    return True


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--prefix', type=Path)
    parser.add_argument('--import', dest='source', type=Path, help='OpenCore plist, GenSMBIOS JSON, legacy .conf, or schema-1 plist/JSON')
    parser.add_argument('--set', action='append', default=[], metavar='KEY=VALUE')
    parser.add_argument('--write', action='store_true', help='Write validated overrides to a stopped prefix (otherwise preview only)')
    args = parser.parse_args(argv)
    config = Path(__file__).resolve().parents[1] / 'build/config.json'
    saved = json.loads(config.read_text()) if config.exists() else {}
    prefix = args.prefix or Path(os.environ.get('DPREFIX') or saved.get('prefix', str(Path.home() / '.darling')))
    try:
        path = destination(prefix)
        properties = load(path) if path.exists() else {}
        if args.source:
            incoming = load(args.source)
            # Replacing a UUID also replaces its previously derived byte form.
            if 'IOPlatformUUID' in incoming:
                properties.pop('system-id', None)
            properties.update(incoming)
        for assignment in args.set:
            key, sep, value = assignment.partition('=')
            if not sep:
                raise ValueError('--set requires KEY=VALUE')
            if key == 'IOPlatformUUID': properties.pop('system-id', None)
            if key == 'model': properties.pop('product-name', None)
            properties[key] = value
        properties = validate(properties)
        if args.write:
            changed = save(prefix, properties)
            print('Platform identity ' + ('saved' if changed else 'unchanged') + '; restart iokitd to apply.')
        else:
            print(json.dumps({'schema': 1, 'properties': properties}, default=lambda value: value.hex().upper(), indent=2))
    except (OSError, ValueError, TypeError, plistlib.InvalidFileException, ExpatError) as error:
        parser.exit(1, str(error) + '\n')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
