"""Real cryptographic fixtures and compiled syscall contracts; no live policy changes."""
import hashlib
import importlib.util
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import unittest

import test_runtime_compatibility as compatibility
from test_runtime_compatibility import function

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('code_verifier', ROOT/'tools/verify-darling-code-signature.py')
verifier = importlib.util.module_from_spec(spec)
spec.loader.exec_module(verifier)


def fixture(signer=None, entitlements=None):
    # Fixed reservation means LC_CODE_SIGNATURE is sealed before CMS creation.
    prefix = struct.pack('<8I', 0xfeedfacf, 0x1000007, 3, 2, 1, 16, 0, 0)
    prefix += struct.pack('<4I', 0x1d, 16, 4096, 16384)
    prefix = prefix.ljust(4096, b'\0')
    identity = b'fixture.identifier\0'
    team = b'FIXTURE-TEAM\0'
    special = 5 if entitlements else 0
    hash_offset = 52 + len(identity) + len(team) + special * 32
    directory = struct.pack('>9I4BI2I', 0xfade0c02, hash_offset + 32, 0x20200,
                            0 if signer else 2, hash_offset, 52, special, 1, 4096,
                            32, 2, 0, 12, 0, 0, 52 + len(identity))
    directory += identity + team
    if special:
        directory += hashlib.sha256(entitlements).digest() + bytes(4 * 32)
    directory += hashlib.sha256(prefix).digest()
    blobs = [(0, directory)]
    if entitlements: blobs.append((5, entitlements))
    if signer:
        cms = signer(directory)
        blobs.append((0x10000, struct.pack('>2I', 0xfade0b01, len(cms)+8) + cms))
    length = 12 + len(blobs)*8 + sum(len(blob) for slot, blob in blobs)
    signature = struct.pack('>3I', 0xfade0cc0, length, len(blobs))
    offset = 12 + len(blobs)*8
    for slot, blob in blobs:
        signature += struct.pack('>2I', slot, offset)
        offset += len(blob)
    signature += b''.join(blob for slot, blob in blobs)
    assert len(signature) <= 16384
    return prefix + signature.ljust(16384, b'\0')


class SignatureChecks(unittest.TestCase):
    def test_adhoc_integrity_is_not_trusted_signing(self):
        result = verifier.verify(fixture())
        self.assertTrue(result['integrity_verified'])
        self.assertTrue(result['ad_hoc'])
        self.assertFalse(result['cms_verified'])
        self.assertFalse(result['anchor_verified'])
        self.assertEqual(result['identifier'], 'fixture.identifier')
        self.assertEqual(result['declared_team_id'], 'FIXTURE-TEAM')

    def test_tampered_code_and_unhashed_tail_fail(self):
        image = fixture()
        for bad in [image[:100]+b'X'+image[101:], image+b'X', image[:-1], image[:4096]+b'X'+image[4097:]]:
            with self.assertRaises(ValueError): verifier.verify(bad)

    def test_entitlements_must_be_sealed_and_untampered(self):
        entitlements = struct.pack('>2I', 0xfade7171, 16) + b'contents'
        image = fixture(entitlements=entitlements)
        self.assertTrue(verifier.verify(image)['integrity_verified'])
        position = image.index(b'contents')
        with self.assertRaises(ValueError): verifier.verify(image[:position]+b'X'+image[position+1:])

    def test_signature_slots_cannot_overlap_or_repeat(self):
        image = bytearray(fixture(entitlements=struct.pack('>2I',0xfade7171,8)))
        struct.pack_into('>I', image, 4096+20, 0)  # duplicate primary slot
        with self.assertRaises(ValueError): verifier.verify(bytes(image))
        image = bytearray(fixture(entitlements=struct.pack('>2I',0xfade7171,8)))
        struct.pack_into('>I', image, 4096+24, 28)  # overlapping CodeDirectory
        with self.assertRaises(ValueError): verifier.verify(bytes(image))

    def test_local_trust_is_exact_image_pin_not_universal_membership(self):
        image = fixture()
        cache = {'schema': 1, 'image_sha256': [hashlib.sha256(image).hexdigest()]}
        self.assertTrue(verifier.verify(image, trust_cache=cache)['local_trust_match'])
        self.assertFalse(verifier.verify(image, trust_cache={'schema':1,'image_sha256':[]})['local_trust_match'])
        self.assertFalse(verifier.verify(image, trust_cache=cache)['anchor_verified'])
        with self.assertRaises(ValueError): verifier.verify(image, trust_cache={'schema':True,'image_sha256':[]})

    @unittest.skipUnless(shutil.which('clang') and shutil.which('ld64.lld'), 'Darwin cross-linker required')
    def test_real_linker_produced_adhoc_signature(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            (root/'main.s').write_text('.globl _main\n_main:\n xorl %eax,%eax\n retq\n')
            subprocess.run(['clang','-target','x86_64-apple-macosx11','-c',str(root/'main.s'),'-o',str(root/'main.o')],check=True,capture_output=True)
            subprocess.run(['ld64.lld','-arch','x86_64','-platform_version','macos','11.0','11.0',
                            '-adhoc_codesign',str(root/'main.o'),'-o',str(root/'signed')],check=True,capture_output=True)
            image = (root/'signed').read_bytes()
            self.assertTrue(verifier.verify(image)['integrity_verified'])
            image = bytearray(image); image[1000] ^= 1
            with self.assertRaises(ValueError): verifier.verify(bytes(image))

    def test_malformed_input_is_rejected(self):
        for data in [b'', bytes(32), b'\xca\xfe\xba\xbe'+bytes(100), fixture()[:50]]:
            with self.assertRaises(ValueError): verifier.verify(data)


@unittest.skipUnless(shutil.which('openssl'), 'OpenSSL required')
class CMSSignatureChecks(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.root = Path(cls.temp.name)
        def run(*args):
            subprocess.run(['openssl', *args], cwd=cls.root, check=True, capture_output=True, timeout=30)
        cls.run_openssl = staticmethod(run)
        run('req','-x509','-newkey','rsa:2048','-nodes','-keyout','ca.key','-out','ca.pem',
            '-subj','/CN=Disposable Code Test CA','-days','1')
        run('req','-newkey','rsa:2048','-nodes','-keyout','signer.key','-out','signer.csr',
            '-subj','/CN=Disposable Code Signer')
        (cls.root/'extensions').write_text('basicConstraints=critical,CA:FALSE\nkeyUsage=digitalSignature\nextendedKeyUsage=codeSigning\n')
        run('x509','-req','-in','signer.csr','-CA','ca.pem','-CAkey','ca.key','-CAcreateserial',
            '-out','signer.pem','-days','1','-extfile','extensions')
        run('req','-x509','-newkey','rsa:2048','-nodes','-keyout','other.key','-out','other.pem',
            '-subj','/CN=Unrelated CA','-days','1')

    @classmethod
    def tearDownClass(cls): cls.temp.cleanup()

    def sign(self, directory):
        (self.root/'directory').write_bytes(directory)
        self.run_openssl('cms','-sign','-binary','-in','directory','-signer','signer.pem','-inkey','signer.key',
                         '-outform','DER','-out','cms','-md','sha256')
        return (self.root/'cms').read_bytes()

    def test_real_cms_signature_and_explicit_chain(self):
        image = fixture(self.sign)
        unanchored = verifier.verify(image)
        self.assertTrue(unanchored['cms_verified'])
        self.assertFalse(unanchored['anchor_verified'])
        anchored = verifier.verify(image, self.root/'ca.pem')
        self.assertTrue(anchored['anchor_verified'])
        self.assertIn('Disposable Code Signer', anchored['signer'])
        with self.assertRaises(ValueError): verifier.verify(image, self.root/'other.pem')

    def test_signed_identifier_tamper_is_detected_by_cms(self):
        image = fixture(self.sign)
        offset = image.index(b'fixture.identifier')
        changed = image[:offset] + b'X' + image[offset+1:]
        with self.assertRaises(ValueError): verifier.verify(changed, self.root/'ca.pem')

    def test_cms_bit_flip_rejected(self):
        image = fixture(self.sign)
        offset, blobs = verifier.signature_blobs(image)
        cms = blobs[0x10000]
        start = image.index(cms)
        changed = bytearray(image); changed[start+len(cms)-1] ^= 1
        with self.assertRaises(ValueError): verifier.verify(bytes(changed), self.root/'ca.pem')


class SecuritySyscallChecks(compatibility.CompatibilityChecks):
    # Avoid repeating the base class's compatibility tests in discovery.
    test_unsupported_mach_calls_return_errors_and_initialize_outputs = None
    test_ptrace_unknown_request_and_existing_requests = None

    def test_security_syscalls_do_not_invent_enforcement_or_identity(self):
        base = 'darling/src/libsystem_kernel/emulation/src/xnu_syscall/bsd/impl/misc/'
        helper = self.patched('xnu', base+'security_user_access.h')
        cs = self.patched('xnu', base+'csops.c')
        csr = self.patched('xnu', base+'csrctl.c')
        mac = self.patched('xnu', base+'mac_syscall.c')
        ops = ['STATUS','MARKINVALID','MARKHARD','MARKKILL','PIDPATH','CDHASH','PIDOFFSET',
               'ENTITLEMENTS_BLOB','MARKRESTRICT','SET_STATUS','BLOB','IDENTITY','CLEARINSTALLER',
               'CLEARPLATFORM','TEAMID','CLEAR_LV']
        constants = '\n'.join(f'#define CS_OPS_{name} {i}' for i,name in enumerate(ops))
        self.run_c('''
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <sys/syscall.h>
extern long syscall(long, ...);
#define LINUX_SYSCALL(...) syscall(__VA_ARGS__)
#define LINUX_SYSCALL0(nr) syscall(nr)
typedef struct { unsigned val[8]; } audit_token_t;
#define CSR_SYSCALL_CHECK 0
#define CSR_SYSCALL_GET_ACTIVE_CONFIG 1
#define CSR_VALID_FLAGS 0xffu
#define __simple_printf(...) ((void)0)
static int sys_getpid(void) { return 123; }
static int sys_kill(int pid, int sig, int posix) { return pid == 999999 ? -ESRCH : 0; }
''' + constants + '\n' + '\n'.join([function(helper,'darling_security_copy'),function(helper,'darling_security_read'),function(helper,'darling_security_write'),function(cs,'sys_csops'),function(cs,'sys_csops_audittoken'),
                                     function(csr,'sys_csrctl'),function(mac,'sys___mac_syscall')]) + '''
int main(void) {
    unsigned char flags[6]; memset(flags,0xa5,sizeof(flags));
    assert(sys_csops(0,CS_OPS_STATUS,flags+1,4)==0);
    assert(flags[0]==0xa5 && flags[5]==0xa5 && flags[1]==0 && flags[4]==0);
    assert(sys_csops(0,CS_OPS_STATUS,NULL,4)==-EFAULT);
    assert(sys_csops(0,CS_OPS_STATUS,(void*)1,4)==-EFAULT);
    flags[1]=0xa5; assert(sys_csops(0,CS_OPS_STATUS,flags+1,3)==-ERANGE && flags[1]==0xa5);
    assert(sys_csops(123,CS_OPS_STATUS,flags+1,4)==0);
    assert(sys_csops(999999,CS_OPS_STATUS,flags,4)==-ESRCH);
    assert(sys_csops(124,CS_OPS_STATUS,flags,4)==-ENOTSUP);
    assert(sys_csops(-1,CS_OPS_STATUS,flags,4)==-EINVAL);
    for(int op=1;op<=15;++op) assert(sys_csops(0,op,flags,4)<0);
    audit_token_t token = {{0}};
    assert(sys_csops_audittoken(0,0,flags,4,&token)==-ENOTSUP);
    assert(sys_csops_audittoken(0,0,flags,4,NULL)==-EFAULT);
    uint32_t value=0;
    assert(sys_csrctl(1,&value,4)==0 && value==CSR_VALID_FLAGS);
    assert(sys_csrctl(1,&value,8)==-EINVAL);
    assert(sys_csrctl(1,NULL,4)==-EFAULT);
    assert(sys_csrctl(0,(void*)1,4)==-EFAULT);
    value=1; assert(sys_csrctl(0,&value,4)==0);
    value=0x100; assert(sys_csrctl(0,&value,4)==-EPERM);
    assert(sys_csrctl(99,&value,4)==-ENOSYS);
    struct { unsigned char hash[20]; uint64_t result; } query={{0},123};
    assert(sys___mac_syscall("AMFI",95,&query)==-ENOTSUP && query.result==0);
    assert(sys___mac_syscall("AMFI",95,NULL)==-EFAULT);
    assert(sys___mac_syscall("AMFI",95,(void*)1)==-EFAULT);
    assert(sys___mac_syscall((void*)1,1,NULL)==-EFAULT);
    assert(sys___mac_syscall("AMFI",1,NULL)==-ENOTSUP);
    assert(sys___mac_syscall("unknown",1,NULL)==-ENOSYS);
    assert(sys___mac_syscall("Seatbelt",1,NULL)==0); // legacy compatibility, not enforcement
}
''')
