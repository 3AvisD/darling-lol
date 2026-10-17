// Exercise the real worker with overlapping drains and producers. A frame's
// drain must finish even while later work is blocked indefinitely.
#include <indium/submit-thread.private.hpp>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <future>
#include <thread>

using namespace std::chrono_literals;
using namespace Indium;

static void awaitDrains(size_t count) {
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    for (;;) {
        {
            auto& s = SubmitThread::state();
            std::lock_guard<std::mutex> lock(s.mutex);
            if (s.drainWaiters == count) return;
        }
        assert(std::chrono::steady_clock::now() < deadline);
        std::this_thread::yield();
    }
}

int main() {
    std::promise<void> releaseFirst, releaseSecond, releaseThird;
    auto first = releaseFirst.get_future().share();
    auto second = releaseSecond.get_future().share();
    auto third = releaseThird.get_future().share();
    std::atomic<unsigned> completed{0};
    SubmitThread::post([&] { first.wait(); ++completed; });
    auto drain1 = std::async(std::launch::async, [] { SubmitThread::drain(); });
    awaitDrains(1); // drain1 has taken its snapshot before the next post
    SubmitThread::post([&] { second.wait(); ++completed; });
    auto drain2 = std::async(std::launch::async, [] { SubmitThread::drain(); });
    awaitDrains(2);
    SubmitThread::post([&] { third.wait(); ++completed; });
    releaseFirst.set_value();
    assert(drain1.wait_for(2s) == std::future_status::ready);
    drain1.get();
    assert(completed == 1);
    assert(drain2.wait_for(0s) == std::future_status::timeout);
    releaseSecond.set_value();
    assert(drain2.wait_for(2s) == std::future_status::ready);
    drain2.get();
    assert(completed == 2);
    assert(SubmitThread::pending());
    releaseThird.set_value();
    SubmitThread::drain();
    assert(completed == 3);
    // The worker itself cannot wait for a snapshot containing its active task.
    SubmitThread::post([] { SubmitThread::drain(); });
    SubmitThread::drain();
    // Repeated idle/active transitions exercise notification registration races.
    for (unsigned i=0; i<100000; ++i) {
        SubmitThread::post([&] { ++completed; });
        SubmitThread::drain();
    }
    assert(completed == 100003);
    std::puts("PASS submit drain: overlapping snapshots, blocked later producer, reentrant drain, 100000 handoffs");
    std::fflush(stdout);
    // The production worker is detached and process-lived.
    std::_Exit(0);
}
