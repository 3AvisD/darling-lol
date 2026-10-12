#include <indium/dynamic-vk.hpp>
#include <indium/retirement-queue.private.hpp>
#include <cassert>
#include <cstring>
#include <functional>
#include <future>
#include <iostream>
#include <thread>

VkInstance Indium::globalInstance = VK_NULL_HANDLE;
std::atomic<int> resolutions {0};
static int increment(int value) { return value + 1; }
static PFN_vkVoidFunction resolveMock(VkInstance, const char* name) {
    ++resolutions;
    return std::strcmp(name, "increment") == 0 ? reinterpret_cast<PFN_vkVoidFunction>(increment) : nullptr;
}
PFN_vkGetInstanceProcAddr Indium::DynamicVK::vkGetInstanceProcAddr = resolveMock;

int main() {
    Indium::DynamicVK::DynamicFunction<int(*)(int)> function("increment");
    std::vector<std::thread> threads;
    for (int i = 0; i < 16; ++i) threads.emplace_back([&] {
        for (int j = 0; j < 10000; ++j) assert(function(j) == j + 1);
    });
    for (auto& thread : threads) thread.join();
    assert(resolutions == 1);
    Indium::DynamicVK::DynamicFunction<int(*)(int)> missing("missing");
    assert(!missing.isAvailable() && !missing.isAvailable());
    try { missing(0); assert(false); } catch (const std::runtime_error&) {}
    assert(resolutions == 2);

    using Job = std::function<void()>;
    Indium::RetirementQueue<Job, 2> queue;
    std::promise<void> started, release;
    auto gate = release.get_future();
    auto mainThread = std::this_thread::get_id();
    std::atomic<int> completed {0};
    assert(queue.tryPush(Job([&] {
        assert(std::this_thread::get_id() != mainThread);
        started.set_value(); gate.wait(); ++completed;
    })));
    started.get_future().wait();
    assert(queue.tryPush(Job([&] { ++completed; })));
    assert(queue.tryPush(Job([&] { ++completed; })));
    Job fallback = [&] { ++completed; };
    assert(!queue.tryPush(std::move(fallback)));
    assert(fallback); // Full queues must not consume the caller's job.
    fallback();
    release.set_value();
    queue.shutdown();
    assert(completed == 4);
    Job afterShutdown = [&] { ++completed; };
    assert(!queue.tryPush(std::move(afterShutdown)) && afterShutdown);

    // Concurrent producers, bounded fallback, and resource lifetime through
    // worker completion. Every submitted or fallback job runs exactly once.
    Indium::RetirementQueue<Job, 8> concurrent;
    completed = 0; threads.clear();
    for (int i = 0; i < 4; ++i) threads.emplace_back([&] {
        for (int j = 0; j < 1000; ++j) {
            auto owner = std::make_shared<int>(j);
            Job job = [&, owner] { assert(*owner >= 0); ++completed; };
            if (!concurrent.tryPush(std::move(job))) job();
        }
    });
    for (auto& thread : threads) thread.join();
    concurrent.shutdown();
    assert(completed == 4000);
    std::cout << "cached dispatch and bounded retirement concurrency tests passed\n";
}
