// Host-side microbenchmark of the reply path with the real MessageQueue:
//   direct - MessageQueue::pushAndSend, the producer sends its own reply
//   loop   - MessageQueue::push + eventfd wakeup + a separate "main loop" thread
//            calls MessageQueue::sendMany
// Both endpoints (guest, worker) and, for `loop`, the main loop are threads of
// this one process, so this measures the hand-off difference, not a real guest.
//
// Standalone host sockets only. Never connects to Darling, Riot or the game.
#include <darlingserver/message.hpp>
#include <sys/socket.h>
#include <sys/eventfd.h>
#include <poll.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <chrono>
#include <algorithm>

using DarlingServer::Message;
using DarlingServer::MessageQueue;

static constexpr uint64_t stopTag = 0xdead;
static constexpr size_t requestSize = 48, replySize = 24;

static uint64_t nowNs() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1000000000ull + ts.tv_nsec;
}

struct Result { double average = 0, p50 = 0, p99 = 0, maximum = 0; };

static Result summarize(std::vector<double>& samples) {
    std::sort(samples.begin(), samples.end());
    Result result;
    double sum = 0;
    for (double sample : samples) sum += sample;
    result.average = sum / samples.size();
    result.p50 = samples[samples.size() / 2];
    result.p99 = samples[(samples.size() * 99) / 100];
    result.maximum = samples.back();
    return result;
}

static Result run(bool direct, unsigned iterations) {
    int request[2] = { -1, -1 }, reply[2] = { -1, -1 };
    if (socketpair(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0, request) != 0) throw std::runtime_error("request socketpair");
    if (socketpair(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0, reply) != 0) throw std::runtime_error("reply socketpair");

    MessageQueue queue;
    int wakeup = -1;
    if (!direct) {
        wakeup = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
        if (wakeup < 0) throw std::runtime_error("eventfd");
        queue.setMessageArrivalNotificationCallback([&] { eventfd_write(wakeup, 1); });
    }

    std::atomic<bool> stopping{false};
    std::thread worker([&] {
        // blocks until the guest sends a request
        while (true) {
            Message requestMessage(requestSize, 0);
            struct msghdr header{};
            struct iovec iov{ requestMessage.data().data(), requestMessage.data().size() };
            header.msg_iov = &iov;
            header.msg_iovlen = 1;
            ssize_t received = recvmsg(request[0], &header, 0);
            if (received < 0) throw std::runtime_error("worker recvmsg");
            uint64_t tag = 0;
            memcpy(&tag, requestMessage.data().data(), sizeof(tag));
            if (tag == stopTag) return;

            Message replyMessage(replySize, 0);
            memcpy(replyMessage.data().data(), &tag, sizeof(tag));
            if (direct) {
                queue.pushAndSend(std::move(replyMessage), reply[0]);
            } else {
                queue.push(std::move(replyMessage));
            }
        }
    });

    std::thread mainLoop;
    if (!direct) {
        mainLoop = std::thread([&] {
            while (!stopping.load()) {
                struct pollfd pollFD{ wakeup, POLLIN, 0 };
                if (poll(&pollFD, 1, 20) <= 0) continue;
                eventfd_t value;
                eventfd_read(wakeup, &value);
                queue.sendMany(reply[0]);
            }
        });
    }

    auto cycle = [&]() {
        uint64_t tag = 1;
        if (send(request[1], &tag, sizeof(tag), 0) != static_cast<ssize_t>(sizeof(tag))) throw std::runtime_error("guest send");
        char replyBuffer[replySize];
        if (recv(reply[1], replyBuffer, sizeof(replyBuffer), 0) != static_cast<ssize_t>(sizeof(replyBuffer))) throw std::runtime_error("guest recv");
    };

    for (unsigned i = 0; i < 200; ++i) cycle(); // warm up

    std::vector<double> samples;
    samples.reserve(iterations);
    for (unsigned i = 0; i < iterations; ++i) {
        uint64_t start = nowNs();
        cycle();
        samples.push_back(nowNs() - start);
    }

    uint64_t tag = stopTag;
    if (send(request[1], &tag, sizeof(tag), 0) != static_cast<ssize_t>(sizeof(tag))) throw std::runtime_error("guest stop");
    worker.join();
    stopping.store(true);
    if (mainLoop.joinable()) {
        // wake the main loop for its final pass, then wait for it to notice
        eventfd_write(wakeup, 1);
        mainLoop.join();
    }
    while (!queue.empty()) queue.sendMany(reply[0]);

    close(request[0]); close(request[1]); close(reply[0]); close(reply[1]);
    if (wakeup >= 0) close(wakeup);
    return summarize(samples);
}

int main(int argc, char** argv) {
    try {
        unsigned iterations = (argc > 1) ? static_cast<unsigned>(strtoul(argv[1], nullptr, 10)) : 2000;
        auto loop = run(false, iterations);
        auto direct = run(true, iterations);
        std::printf("HOST_REPLY_BENCH same_process=1 n=%u loop_avg=%.2fus loop_p50=%.2fus loop_p99=%.2fus loop_max=%.2fus direct_avg=%.2fus direct_p50=%.2fus direct_p99=%.2fus direct_max=%.2fus\n",
            iterations, loop.average / 1000.0, loop.p50 / 1000.0, loop.p99 / 1000.0, loop.maximum / 1000.0,
            direct.average / 1000.0, direct.p50 / 1000.0, direct.p99 / 1000.0, direct.maximum / 1000.0);
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "HOST_REPLY_BENCH pass=0 error=%s\n", error.what());
        return 1;
    }
}
