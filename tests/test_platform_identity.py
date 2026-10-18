"""Disposable-prefix checks for explicit platform identity configuration."""
import importlib.util
import json
import os
from pathlib import Path
import plistlib
import stat
import subprocess
import sys
import tempfile
import unittest
import uuid

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / 'tools/configure-darling-platform.py'
spec = importlib.util.spec_from_file_location('platform_config', SCRIPT)
platform = importlib.util.module_from_spec(spec)
spec.loader.exec_module(platform)
UUID = '00112233-4455-4677-8899-AABBCCDDEEFF'


class PlatformIdentityChecks(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.prefix = self.root / 'prefix with spaces'

    def source(self, name, data):
        path = self.root / name
        path.write_bytes(data)
        return path

    def test_opencore_import_and_uuid_byte_order(self):
        source = self.source('OpenCore.plist', plistlib.dumps({'PlatformInfo': {'Generic': {
            'SystemProductName': 'ExampleModel1,1', 'SystemSerialNumber': 'MY-SERIAL',
            'SystemUUID': UUID, 'MLB': 'MY-BOARD', 'ROM': bytes.fromhex('0123456789AB'),
            'SpoofVendor': True}}}))
        result = platform.load(source)
        self.assertEqual(result['model'], result['product-name'])
        self.assertEqual(result['IOPlatformUUID'], UUID)
        self.assertEqual(result['system-id'].hex().upper(), '33221100554477468899AABBCCDDEEFF')
        self.assertEqual(result['ROM'], bytes.fromhex('0123456789AB'))
        self.assertNotIn('SpoofVendor', result)

    def test_gensmbios_partial_import_does_not_generate_missing_identity(self):
        source = self.source('identity.json', json.dumps({'Type': 'ExampleModel1,1', 'Serial': 'SUPPLIED'}).encode())
        result = platform.load(source)
        self.assertEqual(set(result), {'model', 'product-name', 'IOPlatformSerialNumber'})
        self.assertNotIn('IOPlatformUUID', result)

    def test_invalid_values_are_rejected(self):
        for properties in [
            {'IOPlatformUUID': 'not-a-uuid'}, {'IOPlatformUUID': str(uuid.UUID(int=0))},
            {'IOPlatformUUID': UUID, 'system-id': bytes(16)}, {'ROM': bytes(7)},
            {'ROM': 'xyz'}, {'model': 'bad\x00name'}, {'model': 'bad\nname'},
            {'model': 'é'}, {'model': 'X'*129}, {'IOPlatformSerialNumber': ''},
            {'IOPlatformSerialNumber': 123}, {'unknown': 'value'}, {'ROM': True},
        ]:
            with self.subTest(properties=properties), self.assertRaises((ValueError, TypeError)):
                platform.validate(properties)

    def test_schema_versions_types_and_unknown_fields(self):
        for payload in [{'schema': 2, 'properties': {}}, {'schema': True, 'properties': {}},
                        {'schema': 1, 'properties': {}, 'extra': 1}, {'schema': 1, 'properties': []}]:
            source = self.source('bad.json', json.dumps(payload).encode())
            with self.subTest(payload=payload), self.assertRaises(ValueError): platform.load(source)
        source = self.source('huge.json', b' ' * (platform.MAX_SIZE + 1))
        with self.assertRaises(ValueError): platform.load(source)

    def test_legacy_migration_never_executes_or_rewrites_source(self):
        marker = self.root / 'executed'
        raw = ('SMBIOS_MODEL="ExampleModel1,1"\nSMBIOS_SERIAL_NUMBER="SUPPLIED"\n'
               f'SMBIOS_CPU_SPEED="$(touch {marker})"\n').encode()
        source = self.source('darling-smbios.conf', raw)
        properties = platform.load(source)
        self.assertTrue(platform.save(self.prefix, properties))
        self.assertEqual(source.read_bytes(), raw)
        self.assertFalse(marker.exists())
        self.assertNotIn('SMBIOS_CPU_SPEED', properties)

    def test_stable_write_backup_permissions_and_roundtrip(self):
        first = {'IOPlatformSerialNumber': 'FIRST', 'IOPlatformUUID': UUID}
        self.assertTrue(platform.save(self.prefix, first))
        path = platform.destination(self.prefix)
        original = path.read_bytes()
        self.assertEqual(stat.S_IMODE(path.stat().st_mode), 0o600)
        self.assertFalse(platform.save(self.prefix, platform.load(path)))
        self.assertEqual(path.read_bytes(), original)
        self.assertTrue(platform.save(self.prefix, {'IOPlatformSerialNumber': 'SECOND'}))
        self.assertEqual(path.with_name(path.name+'.before-platform-change').read_bytes(), original)
        self.assertEqual(platform.load(path), {'IOPlatformSerialNumber': 'SECOND'})

    def test_prefix_isolation(self):
        other = self.root / 'other'
        platform.save(self.prefix, {'IOPlatformSerialNumber': 'FIRST'})
        platform.save(other, {'IOPlatformSerialNumber': 'SECOND'})
        self.assertEqual(platform.load(platform.destination(self.prefix))['IOPlatformSerialNumber'], 'FIRST')
        self.assertEqual(platform.load(platform.destination(other))['IOPlatformSerialNumber'], 'SECOND')

    def test_no_implicit_migration_without_an_override_file(self):
        env = dict(os.environ, DPREFIX=str(self.prefix))
        result = subprocess.run([sys.executable, str(SCRIPT)], env=env, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(json.loads(result.stdout)['properties'], {})
        self.assertFalse(self.prefix.exists())

    def test_preview_and_uuid_replacement_preserve_other_values(self):
        platform.save(self.prefix, {'IOPlatformSerialNumber': 'FIRST', 'IOPlatformUUID': UUID})
        replacement = '11112233-4455-4677-8899-AABBCCDDEEFF'
        command = [sys.executable, str(SCRIPT), '--prefix', str(self.prefix), '--set', 'IOPlatformUUID='+replacement]
        path = platform.destination(self.prefix)
        before = path.read_bytes()
        preview = subprocess.run(command, capture_output=True, text=True)
        self.assertEqual(preview.returncode, 0, preview.stderr)
        self.assertEqual(path.read_bytes(), before)
        written = subprocess.run([*command, '--write'], capture_output=True, text=True)
        self.assertEqual(written.returncode, 0, written.stderr)
        result = platform.load(path)
        self.assertEqual(result['IOPlatformSerialNumber'], 'FIRST')
        self.assertEqual(result['IOPlatformUUID'], replacement)
        self.assertEqual(result['system-id'], uuid.UUID(replacement).bytes_le)

    def test_symlink_escape_is_rejected(self):
        outside = self.root / 'outside'
        outside.mkdir()
        (self.prefix / 'private').mkdir(parents=True)
        (self.prefix / 'private/etc').symlink_to(outside, target_is_directory=True)
        with self.assertRaises(ValueError): platform.save(self.prefix, {})
        self.assertEqual(list(outside.iterdir()), [])

    def test_live_prefix_and_unknown_existing_schema_are_not_overwritten(self):
        self.prefix.mkdir()
        marker = self.prefix / '.init.pid'
        marker.write_text(str(os.getpid()))
        with self.assertRaises(ValueError): platform.save(self.prefix, {})
        marker.unlink()
        path = platform.destination(self.prefix)
        path.parent.mkdir(parents=True)
        bad = plistlib.dumps({'schema': 999, 'properties': {}})
        path.write_bytes(bad)
        with self.assertRaises(ValueError): platform.save(self.prefix, {})
        self.assertEqual(path.read_bytes(), bad)

    def test_duplicate_json_and_invalid_live_marker_are_rejected(self):
        duplicate = self.source('duplicate.json', b'{"schema":1,"schema":2,"properties":{}}')
        with self.assertRaises(ValueError): platform.load(duplicate)
        self.prefix.mkdir()
        (self.prefix / '.init.pid').write_text('0')
        with self.assertRaises(ValueError): platform.save(self.prefix, {})

    def test_malformed_xml_reports_an_error_without_a_traceback(self):
        source = self.source('bad.plist', b'<?xml version="1.0"?><broken>')
        result = subprocess.run([sys.executable, str(SCRIPT), '--prefix', str(self.prefix), '--import', str(source)], capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertNotIn('Traceback', result.stderr)
        self.assertFalse(self.prefix.exists())
