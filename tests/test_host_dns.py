"""Exercise the base resolver install and actual vchroot code without DNS traffic."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest

import test_runtime_compatibility as sources


@unittest.skipUnless(shutil.which('cc') and shutil.which('patch'), 'C compiler and patch required')
class HostDNSChecks(unittest.TestCase):
    patched = sources.CompatibilityChecks.patched

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='host-dns-')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.prefix = self.root / 'guest'
        (self.prefix / 'private/etc').mkdir(parents=True)
        (self.prefix / 'Volumes/SystemRoot').mkdir(parents=True)
        (self.prefix / 'etc').symlink_to('private/etc')
        self.resolver = self.prefix / 'private/etc/resolv.conf'

    def build_vchroot(self):
        path = 'darling/src/libsystem_kernel/emulation/'
        source = self.patched('xnu', path + 'src/linux_premigration/vchroot_userspace.c')
        header = self.root / 'darling/emulation/linux_premigration/vchroot_expand.h'
        header.parent.mkdir(parents=True)
        header.write_text(self.patched('xnu', path + 'include/linux_premigration/vchroot_expand.h'))
        # Use upstream's Linux TEST mode, replacing only its CLI and RPC setup.
        fixture = r'''
#define _GNU_SOURCE
#define TEST
#include <stdint.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#define LC_C_LOCALE 0
#define strncasecmp_l(a,b,n,l) strncasecmp(a,b,n)
#define __simple_printf printf
#define __simple_abort abort
static int dserver_rpc_vchroot_path(char *p, unsigned long n, uint64_t *len) { abort(); }
#define main upstream_main
'''
        fixture += source + r'''
#undef main
int main(int argc, const char **argv) {
    if (argc != 3) return 2;
    int fd = open(argv[1], O_RDONLY | O_DIRECTORY);
    if (fd < 0 || __darling_vchroot(fd)) return 3;
    close(fd);
    struct vchroot_expand_args args = {.dfd = -100, .flags = VCHROOT_FOLLOW};
    strcpy(args.path, argv[2]);
    int result = vchroot_expand(&args);
    if (result) return 4;
    FILE *file = fopen(args.path, "r");
    if (!file) return 5;
    char buf[4096];
    while (fgets(buf, sizeof(buf), file)) fputs(buf, stdout);
    return fclose(file) != 0;
}
'''
        file = self.root / 'check.c'
        file.write_text(fixture)
        self.binary = self.root / 'check'
        result = subprocess.run(['cc', '-std=gnu11', '-I', str(self.root), str(file),
                                 '-o', str(self.binary)], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)

    def read_guest(self, path='/etc/resolv.conf'):
        result = subprocess.run([str(self.binary), str(self.prefix), path],
                                capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stderr)
        return result.stdout

    def test_host_symlink_chains_updates_and_guest_override(self):
        self.build_vchroot()
        host = self.root / 'host'
        host.mkdir()
        data = host / 'stub-resolv.conf'
        data.write_text('nameserver 127.0.0.53\nsearch tether.example\n')
        self.resolver.symlink_to('/Volumes/SystemRoot' + str(data))
        self.assertEqual(self.read_guest(), data.read_text())
        self.resolver.unlink()
        # An absolute host link followed by a relative one models resolved and
        # NetworkManager layouts. Never write to the real host resolver.
        (host / 'relative').symlink_to(data.name)
        (host / 'resolv.conf').symlink_to(host / 'relative')
        self.resolver.symlink_to('/Volumes/SystemRoot' + str(host / 'resolv.conf'))
        self.assertEqual(self.read_guest(), data.read_text())
        # Network managers replace files atomically; a bind mount or snapshot
        # would keep reading the old resolver inode.
        replacement = host / 'next'
        replacement.write_text('nameserver 192.0.2.53\n')
        os.replace(replacement, data)
        self.assertEqual(self.read_guest(), data.read_text())
        other = host / 'other'
        other.write_text('nameserver 192.0.2.54\n')
        (host / 'resolv.conf').unlink()
        (host / 'resolv.conf').symlink_to(other)
        self.assertEqual(self.read_guest(), other.read_text())
        self.resolver.unlink()
        self.resolver.write_text('nameserver 127.0.0.1\n')
        self.assertEqual(self.read_guest(), self.resolver.read_text())
        # Absolute links originating inside the guest must retain guest scope.
        (self.prefix / 'guest-link').symlink_to('/private/etc/resolv.conf')
        self.assertEqual(self.read_guest('/guest-link'), self.resolver.read_text())

    @unittest.skipUnless(shutil.which('cmake'), 'CMake required')
    def test_base_install_replaces_google_file_with_host_link(self):
        cmake = self.patched('darling', 'CMakeLists.txt')
        start = cmake.index('# Follow the host resolver')
        end = cmake.index('\n', cmake.index('InstallSymlink(', start))
        macro = self.patched('darling', 'cmake/InstallSymlink.cmake')
        project = self.root / 'project'
        project.mkdir()
        (project / 'CMakeLists.txt').write_text(
            'cmake_minimum_required(VERSION 3.16)\nproject(dns NONE)\n' + macro + '\n' + cmake[start:end] + '\n')
        base = self.root / 'base'
        target = base / 'libexec/darling/private/etc/resolv.conf'
        target.parent.mkdir(parents=True)
        for packaging in ('OFF', 'ON'):
            target.unlink(missing_ok=True)
            target.write_text('nameserver 8.8.8.8\nnameserver 8.8.4.4\n')
            build = self.root / ('build-' + packaging)
            subprocess.run(['cmake', '-S', str(project), '-B', str(build),
                            '-DCMAKE_INSTALL_PREFIX=' + str(base),
                            '-DBINARY_PACKAGING_MODE=' + packaging], check=True, capture_output=True)
            subprocess.run(['cmake', '--install', str(build)], check=True, capture_output=True)
            self.assertTrue(target.is_symlink())
            self.assertEqual(os.readlink(target), '/Volumes/SystemRoot/etc/resolv.conf')
