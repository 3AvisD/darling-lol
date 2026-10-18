#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>
using VkSemaphore = int;
struct VkSemaphoreWaitInfo { int sType, flags; size_t semaphoreCount; const VkSemaphore* pSemaphores; const uint64_t* pValues; };
struct VkSemaphoreSignalInfo { int sType; VkSemaphore semaphore; uint64_t value; };
constexpr int VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO=1, VK_SEMAPHORE_WAIT_ANY_BIT=1;
constexpr int VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO=2, VK_SUCCESS=0, VK_TIMEOUT=1;
static std::array<std::atomic<uint64_t>,512> counters {};
static std::atomic<int> signalCalls {0};
static std::atomic<uint64_t> lastTimeout {0};
static std::mutex kernelMutex;
static std::condition_variable kernelWake;
static std::function<void()> onWait, onCounter;
static bool lazy = true;
static bool lazyWakeupEnabled() { return lazy; }
static void noteWakeup(const char*) {}
struct TestMutex {
    std::mutex mutex;
    std::function<void()> afterUnlock;
    void lock() { mutex.lock(); }
    void unlock() {
        auto hook=std::move(afterUnlock);
        afterUnlock=nullptr;
        mutex.unlock();
        if(hook) hook();
    }
};
namespace DynamicVK {
int vkGetSemaphoreCounterValue(int, int semaphore, uint64_t* value) {
    auto hook=std::move(onCounter); onCounter=nullptr;
    if(hook) hook();
    *value=counters[semaphore].load(); return VK_SUCCESS;
}
int vkSignalSemaphore(int, const VkSemaphoreSignalInfo* info) {
    { std::lock_guard lock(kernelMutex); counters[info->semaphore]=info->value; ++signalCalls; }
    kernelWake.notify_all(); return VK_SUCCESS;
}
int vkWaitSemaphores(int, const VkSemaphoreWaitInfo* info, uint64_t timeout) {
    lastTimeout=timeout;
    auto hook=std::move(onWait); onWait=nullptr;
    if(hook) hook();
    auto ready=[&]{
        for(size_t i=0;i<info->semaphoreCount;++i)
            if(counters[info->pSemaphores[i]].load()>=info->pValues[i]) return true;
        return false;
    };
    if(ready()) return VK_SUCCESS;
    if(!timeout) return VK_TIMEOUT;
    std::unique_lock lock(kernelMutex);
    // A missed wakeup fails in a bounded interval instead of hanging the suite.
    assert(kernelWake.wait_for(lock,std::chrono::seconds(2),ready));
    return VK_SUCCESS;
}
}
namespace Indium {
class PrivateDevice {
public:
    std::mutex _pollingMutex;
    TestMutex _eventLoopMutex;
    int _device=0;
    uint64_t _completionSpinNanoseconds=0;
    std::atomic<uint64_t> _eventLoopRegistrations {0};
    std::atomic<unsigned> _eventLoopBlockedPollers {0};
    std::vector<VkSemaphore> _eventLoopSemaphores={1};
    std::vector<uint64_t> _eventLoopWaitValues={1};
    std::vector<std::function<void()>> _eventLoopCallbacks={nullptr};
    void pollEvents(uint64_t);
    void waitForSemaphore(VkSemaphore,uint64_t,std::function<void()>);
    void wakeupEventLoop();
};
}
/* ACTUAL_PATCHED_METHODS */
static void reset() {
    for(auto& value:counters) value=0;
    signalCalls=0; lastTimeout=0; lazy=true; onWait=nullptr; onCounter=nullptr;
}
int main() {
    // Registration before snapshot is included without a host-side wakeup.
    reset(); {
        Indium::PrivateDevice d; int done=0; counters[2]=1;
        d.waitForSemaphore(2,1,[&]{++done;}); d.pollEvents(UINT64_MAX);
        assert(done==1 && signalCalls==0 && d._eventLoopBlockedPollers==0);
    }
    // Registration after snapshot but before announcing a blocking wait.
    reset(); {
        Indium::PrivateDevice d; int done=0;
        d._eventLoopMutex.afterUnlock=[&]{ counters[2]=1; d.waitForSemaphore(2,1,[&]{++done;}); };
        d.pollEvents(UINT64_MAX);
        assert(lastTimeout==0 && signalCalls==0 && done==0 && d._eventLoopBlockedPollers==0);
        d.pollEvents(UINT64_MAX); assert(done==1);
    }
    // Registration after announcing a blocking wait must signal that wait.
    reset(); {
        Indium::PrivateDevice d; int done=0;
        onWait=[&]{ assert(d._eventLoopBlockedPollers==1); counters[2]=1; d.waitForSemaphore(2,1,[&]{++done;}); };
        d.pollEvents(UINT64_MAX);
        assert(signalCalls==1 && d._eventLoopBlockedPollers==0 && done==0);
        d.pollEvents(UINT64_MAX); assert(done==1);
    }
    // A registration during the spin ends it and causes a fresh snapshot.
    reset(); {
        Indium::PrivateDevice d; int done=0; d._completionSpinNanoseconds=1000000;
        d.waitForSemaphore(2,1,nullptr);
        onCounter=[&]{ counters[3]=1; d.waitForSemaphore(3,1,[&]{++done;}); };
        d.pollEvents(UINT64_MAX); assert(lastTimeout==0 && signalCalls==0);
        d.pollEvents(UINT64_MAX); assert(done==1);
    }
    // Zero-timeout polls never announce a blocking wait.
    reset(); {
        Indium::PrivateDevice d;
        onWait=[&]{ assert(d._eventLoopBlockedPollers==0); d.waitForSemaphore(2,1,nullptr); };
        d.pollEvents(0); assert(lastTimeout==0 && signalCalls==0);
    }
    // Opt-out restores unconditional registration wakeups; shutdown always wakes.
    reset(); {
        Indium::PrivateDevice d; lazy=false; d.waitForSemaphore(2,1,nullptr);
        assert(signalCalls==1); lazy=true; d.wakeupEventLoop(); assert(signalCalls==2);
    }
    // A blocked poller on another device cannot change this device's policy.
    reset(); {
        Indium::PrivateDevice a,b; b._eventLoopBlockedPollers=1;
        a.waitForSemaphore(2,1,nullptr); assert(signalCalls==0 && b._eventLoopRegistrations==0);
    }
    // Exercise real producer/poller concurrency with spinning enabled and disabled.
    for(uint64_t spin:{0ULL,1000000ULL}) {
        reset(); Indium::PrivateDevice d; d._completionSpinNanoseconds=spin;
        std::atomic<int> done {0}; constexpr int count=200;
        std::thread poller([&]{ while(done<count) d.pollEvents(UINT64_MAX); });
        for(int i=2;i<count+2;++i) {
            counters[i]=1;
            d.waitForSemaphore(i,1,[&]{ ++done; });
            if(i%3==0) std::this_thread::yield();
        }
        poller.join(); assert(done==count && d._eventLoopBlockedPollers==0);
    }
}
