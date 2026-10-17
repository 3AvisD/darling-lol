#!/usr/bin/env python3
"""Verify a thin x86_64 Mach-O's code hashes and optional CMS/explicit local trust.

This checks a file snapshot, not process pages, bundle resources, SIP or Apple's
platform authorization policy. It never grants runtime entitlements or CS flags.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import tempfile

MAX_IMAGE = 256 * 1024 * 1024
MAX_SIGNATURE = 16 * 1024 * 1024
HASHES = {1: ('sha1', 20), 2: ('sha256', 32), 3: ('sha256', 20), 4: ('sha384', 48)}


def u32(data, offset):
    if offset < 0 or offset + 4 > len(data):
        raise ValueError('Truncated signature integer')
    return struct.unpack_from('>I', data, offset)[0]


def bounded_file(path, maximum):
    with Path(path).open('rb') as stream:
        data = stream.read(maximum + 1)
    if len(data) > maximum:
        raise ValueError('File exceeds supported size limit')
    return data


def cstring(data, offset, end):
    if not 0 < offset < end:
        raise ValueError('Invalid CodeDirectory string offset')
    stop = data.find(b'\0', offset, end)
    if stop < 0:
        raise ValueError('Unterminated CodeDirectory string')
    return data[offset:stop].decode('utf-8', errors='strict')


def signature_blobs(image):
    if len(image) < 32:
        raise ValueError('Truncated Mach-O header')
    magic, cpu, subtype, kind, count, size, flags, reserved = struct.unpack_from('<8I', image)
    if magic != 0xfeedfacf or cpu != 0x01000007:
        raise ValueError('Only thin little-endian x86_64 Mach-O is supported; extract fat slices first')
    end = 32 + size
    if end > len(image) or count > size // 8:
        raise ValueError('Invalid Mach-O load command bounds')
    position, signature = 32, None
    for _ in range(count):
        if position + 8 > end:
            raise ValueError('Truncated Mach-O load command')
        command, length = struct.unpack_from('<II', image, position)
        if length < 8 or length % 8 or position + length > end:
            raise ValueError('Invalid Mach-O load command length')
        if command == 0x1d:
            if signature is not None or length != 16:
                raise ValueError('Duplicate or malformed LC_CODE_SIGNATURE')
            signature = struct.unpack_from('<II', image, position + 8)
        position += length
    if position != end or signature is None:
        raise ValueError('Missing signature or inconsistent load command size')
    offset, size = signature
    if offset < end or size < 12 or size > MAX_SIGNATURE or offset + size != len(image):
        raise ValueError('Signature must be bounded at the end of the image')
    data = image[offset:offset + size]
    if u32(data, 0) != 0xfade0cc0:
        raise ValueError('Expected an embedded signature SuperBlob')
    length, count = u32(data, 4), u32(data, 8)
    table_end = 12 + count * 8
    if not table_end <= length <= len(data) or any(data[length:]):
        raise ValueError('Invalid SuperBlob length or nonzero trailing bytes')
    blobs, ranges = {}, []
    for i in range(count):
        slot, start = u32(data, 12 + i * 8), u32(data, 16 + i * 8)
        if slot in blobs or start < table_end or start + 8 > length:
            raise ValueError('Duplicate or invalid signature slot')
        size = u32(data, start + 4)
        stop = start + size
        if size < 8 or stop > length or any(start < b and a < stop for a, b in ranges):
            raise ValueError('Overlapping or out-of-bounds signature blob')
        ranges.append((start, stop))
        blobs[slot] = data[start:stop]
    if 0 not in blobs:
        raise ValueError('Missing primary CodeDirectory')
    return offset, blobs


def verify_directory(image, signature_offset, directory, blobs, external):
    if len(directory) < 44 or u32(directory, 0) != 0xfade0c02 or u32(directory, 4) != len(directory):
        raise ValueError('Malformed CodeDirectory')
    version, flags, hash_offset, identity_offset, special, count, limit = struct.unpack_from('>7I', directory, 8)
    hash_size, hash_type, platform, page_shift = directory[36:40]
    if version < 0x20001 or version > 0x20500:
        raise ValueError('Unsupported CodeDirectory version')
    minimum = 96 if version >= 0x20500 else 88 if version >= 0x20400 else 64 if version >= 0x20300 else 52 if version >= 0x20200 else 48 if version >= 0x20100 else 44
    if len(directory) < minimum:
        raise ValueError('Truncated versioned CodeDirectory header')
    if version >= 0x20100 and u32(directory, 44):
        raise ValueError('Scatter CodeDirectories are not supported')
    if version >= 0x20300:
        extended = struct.unpack_from('>Q', directory, 56)[0]
        if limit == 0 and extended:
            limit = extended
        elif extended:
            raise ValueError('Ambiguous extended code limit')
    if version >= 0x20500 and u32(directory, 92):
        raise ValueError('Pre-encrypt hash arrays are not supported')
    if hash_type not in HASHES or HASHES[hash_type][1] != hash_size or page_shift > 30:
        raise ValueError('Unsupported hash or page size')
    if limit != signature_offset:
        raise ValueError('CodeDirectory must seal the entire prefix before the signature')
    page_size = (1 << page_shift) if page_shift else limit
    if not page_size or count != (limit + page_size - 1) // page_size:
        raise ValueError('Code slot count does not match the sealed image')
    if hash_offset - special * hash_size < minimum or hash_offset + count * hash_size > len(directory):
        raise ValueError('CodeDirectory hash table out of bounds')
    string_end = hash_offset - special * hash_size
    if identity_offset < minimum:
        raise ValueError('Identity string overlaps CodeDirectory header')
    identity = cstring(directory, identity_offset, string_end)
    team_offset = u32(directory, 48) if version >= 0x20200 else 0
    if team_offset and team_offset < minimum:
        raise ValueError('Team string overlaps CodeDirectory header')
    team = cstring(directory, team_offset, string_end) if team_offset else None
    name = HASHES[hash_type][0]
    def digest(value):
        return hashlib.new(name, value).digest()[:hash_size]
    for i in range(count):
        expected = directory[hash_offset + i * hash_size:hash_offset + (i + 1) * hash_size]
        if digest(image[i * page_size:min(limit, (i + 1) * page_size)]) != expected:
            raise ValueError('Code page hash mismatch at slot ' + str(i))
    for slot in range(1, special + 1):
        expected = directory[hash_offset - slot * hash_size:hash_offset - (slot - 1) * hash_size]
        value = blobs.get(slot, external.get(slot))
        if value is None:
            if any(expected):
                raise ValueError('Missing sealed special slot ' + str(slot))
        elif digest(value) != expected:
            raise ValueError('Special slot hash mismatch at slot ' + str(slot))
    # An embedded entitlement/requirements blob must actually be sealed.
    for slot in (2, 5, 7):
        if slot in blobs and (slot > special or not any(directory[hash_offset-slot*hash_size:hash_offset-(slot-1)*hash_size])):
            raise ValueError('Unsealed security metadata at slot ' + str(slot))
    return {'identifier': identity, 'declared_team_id': team, 'declared_flags': flags,
            'ad_hoc': bool(flags & 2), 'hash_type': hash_type, 'sealed_pages': count,
            'cdhash': hashlib.new(name, directory).digest()[:20].hex(),
            'code_directory_sha256': hashlib.sha256(directory).hexdigest()}


def verify_cms(directory, blob, ca_file):
    if u32(blob, 0) != 0xfade0b01 or u32(blob, 4) != len(blob):
        raise ValueError('Malformed CMS wrapper')
    with tempfile.TemporaryDirectory() as folder:
        root = Path(folder)
        (root / 'directory').write_bytes(directory)
        (root / 'cms').write_bytes(blob[8:])
        command = ['openssl', 'cms', '-verify', '-binary', '-inform', 'DER', '-in', str(root/'cms'),
                   '-content', str(root/'directory'), '-out', str(root/'verified'), '-signer', str(root/'signer')]
        command += ['-CAfile', str(ca_file), '-no-CApath', '-no-CAstore', '-purpose', 'any'] if ca_file else ['-noverify']
        result = subprocess.run(command, capture_output=True, timeout=15)
        if result.returncode or (root/'verified').read_bytes() != directory:
            raise ValueError('CMS signature or supplied certificate-chain verification failed')
        certificate = subprocess.run(['openssl', 'x509', '-in', str(root/'signer'), '-noout', '-subject',
                                      '-fingerprint', '-sha256'], capture_output=True, text=True, check=True, timeout=15)
    return certificate.stdout.strip()


def verify(image, ca_file=None, external=None, trust_cache=None):
    offset, blobs = signature_blobs(image)
    directories = {slot: blob for slot, blob in blobs.items() if slot == 0 or 0x1000 <= slot <= 0x1005}
    result = verify_directory(image, offset, directories[0], blobs, external or {})
    for slot, directory in directories.items():
        if slot:
            verify_directory(image, offset, directory, blobs, external or {})
    result.update({'integrity_verified': True, 'cms_verified': False, 'anchor_verified': False,
                   'local_trust_match': False, 'image_sha256': hashlib.sha256(image).hexdigest(),
                   'scope': 'file snapshot; no bundle, running-process, Apple policy, or SIP enforcement'})
    if 0x10000 in blobs:
        if result['ad_hoc']:
            raise ValueError('Ad-hoc CodeDirectory unexpectedly contains CMS')
        result['signer'] = verify_cms(directories[0], blobs[0x10000], ca_file)
        result['cms_verified'] = True
        result['anchor_verified'] = ca_file is not None
    elif not result['ad_hoc']:
        raise ValueError('Non-ad-hoc signature has no CMS')
    if trust_cache is not None:
        if (not isinstance(trust_cache, dict) or set(trust_cache) != {'schema', 'image_sha256'} or
            type(trust_cache['schema']) is not int or trust_cache['schema'] != 1 or
            not isinstance(trust_cache['image_sha256'], list) or
            any(not isinstance(value, str) or len(value) != 64 or any(c not in '0123456789abcdef' for c in value)
                for value in trust_cache['image_sha256'])):
            raise ValueError('Invalid local trust cache; expected schema 1 and lowercase image_sha256 list')
        result['local_trust_match'] = result['image_sha256'] in trust_cache['image_sha256']
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('image', type=Path)
    parser.add_argument('--ca-file', type=Path, help='Explicit trust anchors; checks CMS chain, not Apple authorization policy')
    parser.add_argument('--local-trust-cache', type=Path, help='Explicit schema-1 file of exact image SHA256 pins; not an Apple trust cache')
    parser.add_argument('--info-plist', type=Path)
    parser.add_argument('--resources-plist', type=Path, help='Seal only; does not verify files referenced by CodeResources')
    parser.add_argument('--allow-adhoc', action='store_true', help='Accept integrity-only ad-hoc checks without asserting trusted signer')
    args = parser.parse_args()
    try:
        external = {slot: bounded_file(path, MAX_SIGNATURE) for slot, path in
                    [(1, args.info_plist), (3, args.resources_plist)] if path}
        cache = json.loads(bounded_file(args.local_trust_cache, MAX_SIGNATURE)) if args.local_trust_cache else None
        result = verify(bounded_file(args.image, MAX_IMAGE), args.ca_file, external, cache)
        accepted = result['anchor_verified'] or result['local_trust_match'] or (args.allow_adhoc and result['ad_hoc'])
        result['accepted_by_requested_check'] = accepted
        print(json.dumps(result, indent=2))
        return 0 if accepted else 1
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        print(json.dumps({'integrity_verified': False, 'accepted_by_requested_check': False, 'error': str(error)}))
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
