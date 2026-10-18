"""Compile the patched native-thread signal bridge and completion budget policy."""
import shutil
import subprocess
from pathlib import Path
import tempfile
import unittest
import test_runtime_compatibility as sources


class NativeThreadChecks(unittest.TestCase):
    patched = sources.CompatibilityChecks.patched
    run_c = sources.CompatibilityChecks.run_c

    def test_resize_forwarding_is_limited_to_foreign_threads_and_successful_delivery(self):
        source = self.patched('xnu', 'darling/src/libsystem_kernel/emulation/src/linux_premigration/signal/sigexc.c')
        bridge = sources.function(source, 'forward_native_resize_signal')
        self.run_c('''
#include <assert.h>
#define LINUX_SIGWINCH 28
#define __NR_getpid 1
#define __NR_gettid 2
#define __NR_tgkill 3
static long tid=101, result=0;
static int sends;
static long fake_syscall(int number, ...) {
    if (number==__NR_getpid) return 100;
    if (number==__NR_gettid) return tid;
    ++sends; return result;
}
#define LINUX_SYSCALL(...) fake_syscall(__VA_ARGS__)
''' + bridge + '''
int main(void) {
    assert(forward_native_resize_signal(28,-32)==1 && sends==1);
    assert(forward_native_resize_signal(28,0)==0 && sends==1);
    assert(forward_native_resize_signal(28,-9)==0 && sends==1);
    for(int sig=1;sig<=64;++sig)
        if(sig!=28) assert(forward_native_resize_signal(sig,-32)==0);
    assert(sends==1);
    tid=100; assert(forward_native_resize_signal(28,-32)==0 && sends==1);
    tid=101; result=-3;
    assert(forward_native_resize_signal(28,-32)==0 && sends==2);
}
''')

    def test_native_thread_without_rpc_socket_returns_an_error_instead_of_aborting(self):
        source = self.patched('darling', 'src/startup/mldr/elfcalls/threads.c')
        self.run_c('''
#include <assert.h>
#include <stdlib.h>
#define SYS_gettid 1
static int t_server_socket=-1, __dserver_main_thread_socket_fd=71;
static int tid=101;
static int getpid(void) { return 100; }
static int syscall(int number) { return tid; }
''' + sources.function(source, '__darling_thread_rpc_socket') + '''
int main(void) {
    assert(__darling_thread_rpc_socket()==-1);
    t_server_socket=73; assert(__darling_thread_rpc_socket()==73);
    t_server_socket=-1; tid=100; assert(__darling_thread_rpc_socket()==71);
}
''')

    def test_cpu_readback_skips_cached_surfaces_but_preserves_mixed_metal_trees(self):
        source = self.patched('cocotron', 'QuartzCore/CALayerContext.m')
        self.run_c('''#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdlib.h>
typedef int BOOL;
typedef struct { int metal, full; } CALayer;
typedef struct { int width, height; } CGSize;
static BOOL hasMetalLayer(CALayer* layer) { return layer->metal; }
static BOOL hasFullWindowSurface(CALayer* layer, CGSize size) { return layer->full; }
''' + sources.function(source, 'skipLocalRender') + '''
int main(void) {
    CGSize size={16,16};
    assert(!skipLocalRender(NULL,size));
    for(int shortcut=0;shortcut<=1;++shortcut)
    for(int direct=0;direct<=1;++direct)
    for(int metal=0;metal<=1;++metal)
    for(int full=0;full<=1;++full) {
        CALayer layer={metal,full};
        if(shortcut) setenv("DARLING_CA_SKIP_LOCAL_IOSURFACE_RENDER","1",1);
        else unsetenv("DARLING_CA_SKIP_LOCAL_IOSURFACE_RENDER");
        if(direct) setenv("RCLOL_GPU_DIRECT_PRESENT","1",1);
        else unsetenv("RCLOL_GPU_DIRECT_PRESENT");
        assert(!!skipLocalRender(&layer,size)==(shortcut && !metal && (!direct || full)));
    }
}
''')


@unittest.skipUnless(shutil.which('c++'), 'C++ compiler required')
class CompletionBudgetChecks(unittest.TestCase):
    patched = sources.CompatibilityChecks.patched

    def test_vendor_defaults_disable_clamping_and_malformed_overrides(self):
        header = self.patched('metal--deps--indium', 'private-include/indium/completion-spin.private.hpp')
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder)
            (path / 'budget.hpp').write_text(header)
            (path / 'check.cpp').write_text('''
#include "budget.hpp"
#include <cassert>
#include <string>
int main() {
    using Indium::completionSpinBudget;
    assert(completionSpinBudget(0x1002,nullptr)==0);
    assert(completionSpinBudget(0x10de,nullptr)==2000000);
    assert(completionSpinBudget(0x1002,nullptr)==0);
    assert(completionSpinBudget(0x10de,"0")==0);
    assert(completionSpinBudget(0x1002,"15")==15000);
    assert(completionSpinBudget(0x10de,"10001")==10000000);
    const std::string huge(10000,'9');
    assert(completionSpinBudget(0x10de,huge.c_str())==10000000);
    for(const char* invalid : {"", "-1", "1ms", "18446744073709551616x"}) {
        assert(completionSpinBudget(0x10de,invalid)==2000000);
        assert(completionSpinBudget(0x1002,invalid)==0);
    }
}
''')
            subprocess.run(['c++','-std=c++17',str(path/'check.cpp'),'-o',str(path/'check')], check=True, capture_output=True)
            subprocess.run([str(path/'check')], check=True, capture_output=True, timeout=10)

    def test_polling_preserves_idle_blocking_callbacks_and_timeout_budget(self):
        source = self.patched('metal--deps--indium', 'src/indium/device.cpp')
        poll = sources.function(source, 'Indium::PrivateDevice::pollEvents')
        harness = '''
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <mutex>
#include <vector>
using VkSemaphore = int;
struct VkSemaphoreWaitInfo {
    int sType, flags;
    size_t semaphoreCount;
    const VkSemaphore* pSemaphores;
    const uint64_t* pValues;
};
constexpr int VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO=1, VK_SEMAPHORE_WAIT_ANY_BIT=1;
constexpr int VK_SUCCESS=0, VK_TIMEOUT=1;
static int counterQueries, waitResult=VK_TIMEOUT;
static uint64_t lastTimeout;
static bool ready;
namespace DynamicVK {
int vkGetSemaphoreCounterValue(int, int semaphore, uint64_t* count) {
    ++counterQueries; *count=ready && semaphore==2 ? 1 : 0; return VK_SUCCESS;
}
int vkWaitSemaphores(int, const VkSemaphoreWaitInfo*, uint64_t timeout) {
    lastTimeout=timeout; return waitResult;
}
}
namespace Indium {
class PrivateDevice {
public:
    std::mutex _pollingMutex, _eventLoopMutex;
    int _device=0;
    uint64_t _completionSpinNanoseconds=1000000;
    std::atomic<uint64_t> _eventLoopRegistrations {0};
    std::atomic<unsigned> _eventLoopBlockedPollers {0};
    std::vector<VkSemaphore> _eventLoopSemaphores={1};
    std::vector<uint64_t> _eventLoopWaitValues={1};
    std::vector<std::function<void()>> _eventLoopCallbacks={nullptr};
    void pollEvents(uint64_t timeoutNanoseconds);
};
}
''' + poll + '''
int main() {
    Indium::PrivateDevice device;
    device.pollEvents(2000000);
    assert(counterQueries==0 && lastTimeout==2000000);
    int callbacks=0;
    device._eventLoopSemaphores.push_back(2);
    device._eventLoopWaitValues.push_back(1);
    device._eventLoopCallbacks.push_back([&]{++callbacks;});
    device.pollEvents(2000000);
    assert(counterQueries>0 && lastTimeout<2000000 && callbacks==0);
    ready=true; waitResult=VK_SUCCESS;
    device.pollEvents(2000000);
    assert(lastTimeout==0 && callbacks==1);
    assert(device._eventLoopSemaphores.size()==1);
    counterQueries=0; waitResult=VK_TIMEOUT;
    device._eventLoopSemaphores.push_back(2);
    device._eventLoopWaitValues.push_back(1);
    device._eventLoopCallbacks.push_back(nullptr);
    device.pollEvents(0);
    assert(counterQueries==0 && lastTimeout==0);
    device._completionSpinNanoseconds=0;
    device.pollEvents(2000000);
    assert(counterQueries==0 && lastTimeout==2000000);
}
'''
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder)
            (path / 'poll.cpp').write_text(harness)
            subprocess.run(['c++', '-std=c++17', '-pthread', str(path/'poll.cpp'), '-o', str(path/'poll')],
                           check=True, capture_output=True)
            subprocess.run([str(path/'poll')], check=True, capture_output=True, timeout=10)
