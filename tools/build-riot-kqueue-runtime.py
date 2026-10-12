#!/usr/bin/env python3
"""Build the queue-lifetime repair and its descriptor-leak regression probe."""
from pathlib import Path
import os
import shlex
import subprocess

project = Path(__file__).resolve().parent.parent
build = project / 'build/darling-src/build'
kernel=build/'src/external/xnu/darling/src/libsystem_kernel'
objects=shlex.split((kernel/'CMakeFiles/system_kernel.dir/objects1.rsp').read_text())
import re
# The saved CMake flags name the clang resource directory of the compiler that configured the tree (22); the system
# compiler is 23 now and <stdbool.h> disappears. Follow the installed clang.
resource_dir=subprocess.check_output(['clang','-print-resource-dir'],text=True).strip()
def configured_flags(directory):
    result={}
    for line in (directory/'flags.make').read_text().splitlines():
        if line.startswith('C_') and ' = ' in line:
            key,value=line.split(' = ',1)
            result[key]=[re.sub(r'/usr/lib/clang/\d+/include',resource_dir+'/include',v) for v in shlex.split(value)]
    return result
kqueue_flags=configured_flags(build/'src/external/libkqueue/CMakeFiles/kqueue.dir')
emulation_flags=configured_flags(kernel/'emulation/CMakeFiles/emulation.dir')
syscall_flags=configured_flags(kernel/'libsyscall/CMakeFiles/libsyscall.dir')
# Build every configured kqueue object with optimization; the configured
# upstream target explicitly uses -O0 even when the rest of the runtime is
# optimized. Preserve its definitions, debug checks, and source selection.
kqueue_flags={**kqueue_flags,'C_FLAGS':[*kqueue_flags['C_FLAGS'],'-O2','-fno-strict-aliasing']}
kqueue_sources=[value.split('/kqueue.dir/',1)[1][:-2] for value in objects if '/kqueue.dir/' in value and value.endswith('.c.o')]
assert kqueue_sources, 'Configured kqueue objects missing'
repairs=[(source,kqueue_flags,project/'build/darling-src/src/external/libkqueue') for source in kqueue_sources]
repairs += [('src/xnu_syscall/bsd/bsd_syscall_table.c',emulation_flags,project/'build/darling-src/src/external/xnu/darling/src/libsystem_kernel/emulation')]
repairs += [('src/common/base.c',emulation_flags,project/'build/darling-src/src/external/xnu/darling/src/libsystem_kernel/emulation')]
# Avoid a redundant absolute-time read when the commpage timestamp is unpublished.
repairs += [('wrappers/__commpage_gettimeofday.c',syscall_flags,project/'build/darling-src/src/external/xnu/libsyscall')]
repairs += [('src/xnu_syscall/bsd/impl/'+name,emulation_flags,
    project/'build/darling-src/src/external/xnu/darling/src/libsystem_kernel/emulation')
    for name in ('bsdthread/bsdthread_register.c','bsdthread/pthread_canceled.c','kqueue/kevent_qos.c','bsdthread/workq_kernreturn.c','psynch/psynch_cvwait.c','psynch/psynch_mutexwait.c','psynch/psynch_mutexdrop.c','psynch/ulock_wait.c',
                 # select busy-loop trace (opt-in) and the recvfrom flag conversion
                 'select/select.c','network/recvfrom.c','network/recvmsg.c','unistd/read.c','unistd/close.c','process/fork.c')]
# RPC guards use the full native signal mask. Rebuild both users of the
# configured forced-include header, without changing unrelated kernel objects.
emulation_root=project/'build/darling-src/src/external/xnu/darling/src/libsystem_kernel/emulation'
rpc_flags={**emulation_flags,'C_FLAGS':[*emulation_flags['C_FLAGS'],'-O2','-fno-omit-frame-pointer']}
# The fault reporter also writes /tmp/lolsig.log (return addresses and module map)
repairs += [('src/linux_premigration/signal/sigexc.c',emulation_flags,emulation_root)]
repairs += [('src/xnu_syscall/mach/impl/mach_traps.c',rpc_flags,emulation_root)]
# User-space clock (per-CPU TSC calibration), its gettimeofday entry and the cached kern.boottime.
repairs += [('src/linux_premigration/ext/clock_gettime.c',rpc_flags,emulation_root)]
repairs += [('src/xnu_syscall/bsd/impl/time/gettimeofday.c',rpc_flags,emulation_root)]
# sysctl_kern.c takes the emulated OS identity from per-source definitions (emulation/CMakeLists.txt)
identity=['-DEMULATED_SYSNAME="Darwin"','-DEMULATED_RELEASE="23.6.0"',
    '-DEMULATED_VERSION="Darwin Kernel Version 23.6.0: Mon Jul 29 21:14:30 PDT 2024; root:xnu-10063.141.2~1/RELEASE_X86_64"',
    '-DEMULATED_OSVERSION="23H124"','-DEMULATED_OSPRODUCTVERSION="14.7"']
repairs += [('src/xnu_syscall/bsd/helper/misc/sysctl_kern.c',{**emulation_flags,'C_FLAGS':[*emulation_flags['C_FLAGS'],*identity]},emulation_root)]
repairs += [('src/linux_premigration/resources/dserver-rpc-defs.c',rpc_flags,emulation_root)]
forced_rpc_flags={**rpc_flags,'C_FLAGS':[*rpc_flags['C_FLAGS'],'-include',str(emulation_root/'include/linux_premigration/resources/dserver-rpc-defs.h')]}
repairs += [('src/rpc.c',forced_rpc_flags,build/'src/external/darlingserver')]
# Process-local Mach semaphores (mach_traps.c) wrap semaphore_create/semaphore_destroy: the MIG stubs get private names.
mig_flags=configured_flags(kernel/'libsyscall/CMakeFiles/libsyscall_64.dir')
mig_obj=project/'build/riot-kernel-task-x86_64-User.o'
subprocess.run(['clang',*mig_flags['C_DEFINES'],*mig_flags['C_INCLUDES'],*mig_flags['C_FLAGS'],
    '-Dsemaphore_create=__darling_rpc_semaphore_create','-Dsemaphore_destroy=__darling_rpc_semaphore_destroy',
    '-c',str(kernel/'libsyscall/mach/task-x86_64-User.c'),'-o',str(mig_obj)],check=True,cwd=kernel/'libsyscall')
matches=[i for i,value in enumerate(objects) if value.endswith('mach/task-x86_64-User.c.o')]
assert len(matches)==1,matches
objects[matches[0]]=str(mig_obj)
# mach_vm_region_recurse on the own task is answered in the process (mach_traps.c); the MIG stub gets a private name.
vm_obj=project/'build/riot-kernel-mach_vm-x86_64-User.o'
subprocess.run(['clang',*mig_flags['C_DEFINES'],*mig_flags['C_INCLUDES'],*mig_flags['C_FLAGS'],
    '-Dmach_vm_region_recurse=__darling_rpc_mach_vm_region_recurse',
    '-c',str(kernel/'libsyscall/mach/mach_vm-x86_64-User.c'),'-o',str(vm_obj)],check=True,cwd=kernel/'libsyscall')
matches=[i for i,value in enumerate(objects) if value.endswith('mach/mach_vm-x86_64-User.c.o')]
assert len(matches)==1,matches
objects[matches[0]]=str(vm_obj)
for source,settings,root in repairs:
    obj=project/('build/riot-kernel-'+source.replace('/','-')+'.o')
    subprocess.run(['clang',*settings['C_DEFINES'],*settings['C_INCLUDES'],*settings['C_FLAGS'],
        '-c',str(root/source),'-o',str(obj)],check=True)
    matches=[i for i,value in enumerate(objects) if value.endswith('/'+source+'.o')]
    assert len(matches)==1,(source,matches)
    objects[matches[0]]=str(obj)
rsp=project/'build/riot-kernel-objects.rsp'
rsp.write_text(shlex.join(objects))
link=shlex.split((kernel/'CMakeFiles/system_kernel.dir/link.txt').read_text())
link[link.index('@CMakeFiles/system_kernel.dir/objects1.rsp')]='@'+str(rsp)
link[link.index('-o')+1]=str(project/'build/riot-libsystem_kernel.dylib')
subprocess.run(link,cwd=kernel,check=True)
flags = {}
for line in (build / 'src/frameworks/CoreVideo/CMakeFiles/CoreVideo.dir/flags.make').read_text().splitlines():
    if line.startswith('C_') and ' = ' in line:
        name, value = line.split(' = ', 1)
        flags[name] = shlex.split(value)
for probe in ('riot-task-info-check','riot-kqueue-lifetime-check','riot-kqueue-stack-check','riot-workqueue-reuse-check','riot-dispatch-fd-check','riot-kqueue-dispatch2-check','riot-syscall-fastpath-check','riot-select-backoff-check','riot-vnode-close-check','riot-rpc-signal-mask-check','riot-fileport-lifetime-check','riot-kqueue-timer-check','riot-cancel-rpc-check'):
    obj = project / f'build/{probe}.o'
    subprocess.run(['clang', *flags['C_DEFINES'], *flags['C_INCLUDES'], *flags['C_FLAGS'],
                    '-O2', '-c', str(project / f'tools/{probe}.c'), '-o', str(obj)], check=True)
    subprocess.run(['clang', '-target', 'x86_64-apple-macosx11.0', '-fuse-ld=lld', '-nostdlib',
                    '-isysroot', '@DARLING_SDK@', str(obj), '-lSystem', '-o',
                    str(project / f'build/{probe}')], check=True)
