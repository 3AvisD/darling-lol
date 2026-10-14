// Host-side checks for the reply path (MessageQueue::pushAndSend): direct sends
// from the producing thread, FIFO order under concurrency and EAGAIN, wakeup
// notification rules, descriptor ownership and dead-peer handling.
//
// Standalone host sockets only. Never connects to Darling, Riot or the game.
#include <darlingserver/message.hpp>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <system_error>
#include <string>
#include <thread>
#include <atomic>
#include <chrono>
#include <vector>
#include <algorithm>

using DarlingServer::Address;
using DarlingServer::Message;
using DarlingServer::MessageQueue;

static void require(bool pass, const char* what) {
    if (!pass) throw std::runtime_error(what);
}

static unsigned openFDs() {
    DIR* directory = opendir("/proc/self/fd");
    require(directory, "fd directory");
    unsigned n = 0;
    while (auto entry = readdir(directory)) if (entry->d_name[0] != '.') ++n;
    closedir(directory);
    return n;
}

// A guest-side socket with its own bound address, like a guest RPC endpoint.
struct Peer {
    int fd = -1;
    std::string path;
    explicit Peer(unsigned index) {
        fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
        require(fd >= 0, "peer socket");
        path = "/tmp/opencode/riot-host-reply-check-" + std::to_string(getpid()) + "-" + std::to_string(index);
        unlink(path.c_str());
        sockaddr_un address{};
        address.sun_family = AF_UNIX;
        std::strncpy(address.sun_path, path.c_str(), sizeof(address.sun_path) - 1);
        require(bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0, "peer bind");
    }
    ~Peer() {
        if (fd >= 0) close(fd);
        if (!path.empty()) unlink(path.c_str());
    }
    sockaddr_un address() const {
        sockaddr_un address{};
        address.sun_family = AF_UNIX;
        std::strncpy(address.sun_path, path.c_str(), sizeof(address.sun_path) - 1);
        return address;
    }
};

// The server's listener socket; a small send buffer forces EAGAIN quickly.
static int serverSocket(int sendBuffer = 0) {
    int fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    require(fd >= 0, "server socket");
    if (sendBuffer) {
        require(setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &sendBuffer, sizeof(sendBuffer)) == 0, "send buffer");
    }
    return fd;
}

static Message packetFor(const Peer& peer, uint64_t tag, size_t size = 64, unsigned descriptors = 0) {
    Message message(size, descriptors);
    std::memcpy(message.data().data(), &tag, sizeof(tag));
    message.setAddress(Address(peer.address()));
    return message;
}

struct Incoming {
    bool valid = false;
    uint64_t tag = 0;
    size_t size = 0;
    std::vector<int> fds;
};

static Incoming receive(int fd) {
    Incoming incoming;
    unsigned char buffer[512];
    unsigned char control[CMSG_SPACE(sizeof(int) * 8)];
    struct iovec iov{ buffer, sizeof(buffer) };
    struct msghdr header{};
    header.msg_iov = &iov;
    header.msg_iovlen = 1;
    header.msg_control = control;
    header.msg_controllen = sizeof(control);
    ssize_t received = recvmsg(fd, &header, 0);
    if (received < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) return incoming;
        throw std::system_error(errno, std::generic_category(), "recvmsg");
    }
    incoming.valid = true;
    incoming.size = static_cast<size_t>(received);
    if (received >= static_cast<ssize_t>(sizeof(incoming.tag))) std::memcpy(&incoming.tag, buffer, sizeof(incoming.tag));
    for (auto cmsg = CMSG_FIRSTHDR(&header); cmsg; cmsg = CMSG_NXTHDR(&header, cmsg)) {
        if (cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_RIGHTS) {
            size_t count = (cmsg->cmsg_len - CMSG_LEN(0)) / sizeof(int);
            for (size_t i = 0; i < count; ++i) {
                int descriptor = -1;
                std::memcpy(&descriptor, CMSG_DATA(cmsg) + (sizeof(int) * i), sizeof(int));
                incoming.fds.push_back(descriptor);
            }
        }
    }
    return incoming;
}

// Reads everything currently queued on the peer, in arrival order.
static void receiveAll(int fd, std::vector<uint64_t>& tags, std::vector<std::vector<int>>& descriptors) {
    while (true) {
        auto incoming = receive(fd);
        if (!incoming.valid) return;
        tags.push_back(incoming.tag);
        descriptors.push_back(std::move(incoming.fds));
    }
}

// The main loop's job: send what the queue can send, then let the peer consume it.
static void drainQueue(MessageQueue& queue, int serverFD, int peerFD, std::vector<uint64_t>& tags, std::vector<std::vector<int>>& descriptors, size_t expected) {
    for (unsigned spin = 0; tags.size() < expected; ++spin) {
        require(spin < 20000, "drain made no progress");
        queue.sendMany(serverFD);
        receiveAll(peerFD, tags, descriptors);
        if (tags.size() < expected) std::this_thread::sleep_for(std::chrono::microseconds(50));
    }
}

static void checkWakeupRules() {
    int server = serverSocket(4096);
    Peer peer(0);
    MessageQueue queue;
    unsigned callbacks = 0;
    queue.setMessageArrivalNotificationCallback([&] {
        ++callbacks;
        require(!queue.empty(), "callback fired on an empty queue");
    });

    // A direct send that drains the queue must not wake the main loop.
    queue.pushAndSend(packetFor(peer, 1000), server);
    require(callbacks == 0, "drained direct send notified");
    require(queue.empty(), "drained direct send left messages");
    auto first = receive(peer.fd);
    require(first.valid && first.tag == 1000, "direct send did not arrive");

    // The legacy push path still notifies unconditionally.
    queue.push(packetFor(peer, 1001));
    require(callbacks == 1, "push did not notify");
    require(!queue.empty(), "push did not queue");

    // Under EAGAIN the queue remains and the main loop is notified.
    std::vector<uint64_t> tags;
    std::vector<std::vector<int>> descriptors;
    for (uint64_t i = 0; i < 64; ++i) queue.pushAndSend(packetFor(peer, 2000 + i), server);
    require(callbacks >= 2, "EAGAIN did not notify");
    require(!queue.empty(), "EAGAIN did not leave messages queued");
    unsigned afterEagain = callbacks;

    drainQueue(queue, server, peer.fd, tags, descriptors, 64 + 1);
    require(queue.empty(), "drain left messages");
    require(tags.size() == 65, "wrong message count");
    require(tags[0] == 1001, "queued message lost");
    for (size_t i = 0; i < 64; ++i) require(tags[i + 1] == 2000 + i, "EAGAIN retry reordered or lost a message");
    require(callbacks == afterEagain, "drain notified without a push");

    close(server);
}

static void checkConcurrentPushers() {
    int server = serverSocket(4096); // small buffer: mix direct sends and the retry path
    Peer peer(1);
    MessageQueue queue;
    std::atomic<unsigned> callbacks{0};

    constexpr unsigned pusherCount = 4;
    constexpr unsigned perPusher = 2000;
    constexpr size_t total = pusherCount * perPusher;
    std::atomic<unsigned> ready{0};
    std::vector<std::thread> pushers;
    // Under concurrency the queue may be drained between the notification and this
    // callback, so only its firing is observable here (like the server's eventfd).
    queue.setMessageArrivalNotificationCallback([&] { ++callbacks; });
    for (unsigned p = 0; p < pusherCount; ++p) {
        pushers.emplace_back([&, p] {
            ++ready;
            for (uint64_t i = 0; i < perPusher; ++i) {
                queue.pushAndSend(packetFor(peer, (static_cast<uint64_t>(p) << 32) | i), server);
            }
        });
    }
    while (ready.load() != pusherCount) std::this_thread::yield();

    // The main loop's role: send whatever the pushers had to leave behind.
    std::vector<unsigned> nextSequence(pusherCount, 0);
    size_t received = 0;
    for (unsigned spin = 0; received < total; ++spin) {
        require(spin < 400000, "concurrent drain made no progress");
        queue.sendMany(server);
        while (true) {
            auto incoming = receive(peer.fd);
            if (!incoming.valid) break;
            unsigned p = incoming.tag >> 32;
            unsigned sequence = incoming.tag & 0xffffffff;
            require(p < pusherCount, "unknown pusher");
            require(sequence == nextSequence[p], "message reordered for one pusher");
            ++nextSequence[p];
            ++received;
        }
        if (received < total) std::this_thread::sleep_for(std::chrono::microseconds(50));
    }

    for (auto& pusher : pushers) pusher.join();
    require(received == total, "lost or duplicated messages");
    for (unsigned p = 0; p < pusherCount; ++p) require(nextSequence[p] == perPusher, "incomplete pusher sequence");

    close(server);
}

static void checkDescriptors() {
    int server = serverSocket(65536);
    Peer peer(2);
    MessageQueue queue;
    unsigned baseline = openFDs();

    for (uint64_t i = 0; i < 8; ++i) {
        Message message = packetFor(peer, 3000 + i, 64, 2);
        for (unsigned d = 0; d < 2; ++d) {
            int pipeFDs[2];
            require(pipe(pipeFDs) == 0, "pipe");
            unsigned char byte = static_cast<unsigned char>(i * 2 + d);
            require(write(pipeFDs[1], &byte, 1) == 1, "pipe write");
            close(pipeFDs[1]);
            message.pushDescriptor(pipeFDs[0]);
        }
        queue.pushAndSend(std::move(message), server);
        // the queue drops its message (and its descriptors) right after the send
        require(openFDs() == baseline, "descriptor not closed on the sender");
    }

    std::vector<uint64_t> tags;
    std::vector<std::vector<int>> descriptors;
    receiveAll(peer.fd, tags, descriptors);
    require(tags.size() == 8, "wrong descriptor message count");
    for (size_t i = 0; i < tags.size(); ++i) {
        require(tags[i] == 3000 + i, "descriptor message order");
        require(descriptors[i].size() == 2, "descriptor count");
        for (unsigned d = 0; d < 2; ++d) {
            unsigned char byte = 0;
            require(read(descriptors[i][d], &byte, 1) == 1, "descriptor read");
            require(byte == static_cast<unsigned char>(i * 2 + d), "descriptor payload");
            close(descriptors[i][d]);
        }
    }
    require(openFDs() == baseline, "received descriptors leaked");

    close(server);
}

static void checkDeadPeer() {
    int server = serverSocket(0);
    MessageQueue queue;
    Peer live(3);
    unsigned baseline = openFDs();

    // A peer that unbound its address makes the kernel report ENOENT.
    {
        Peer dead(4);
        sockaddr_un address = dead.address();
        close(dead.fd);
        dead.fd = -1;
        unlink(dead.path.c_str());
        Message message(64, 0);
        message.setAddress(Address(address));
        queue.pushAndSend(std::move(message), server); // must not throw
    }

    // A peer that closed its (still-bound) address reports ECONNREFUSED.
    {
        Peer dead(5);
        sockaddr_un address = dead.address();
        Message message(64, 0);
        message.setAddress(Address(address));
        close(dead.fd);
        dead.fd = -1;
        queue.pushAndSend(std::move(message), server); // must not throw
    }

    require(queue.empty(), "dead peer wedged the queue");
    require(openFDs() == baseline, "dead peer leaked descriptors");

    // Two dropped messages in front of a live one must not block or reorder it.
    sockaddr_un deadAddress{};
    {
        Peer dead(6);
        deadAddress = dead.address();
        close(dead.fd);
        dead.fd = -1;
    }
    for (unsigned i = 0; i < 2; ++i) {
        Message message(64, 0);
        message.setAddress(Address(deadAddress));
        queue.pushAndSend(std::move(message), server);
    }
    require(queue.empty(), "dead peer messages remained queued");
    queue.pushAndSend(packetFor(live, 4000), server);

    std::vector<uint64_t> tags;
    std::vector<std::vector<int>> descriptors;
    receiveAll(live.fd, tags, descriptors);
    require(tags.size() == 1 && tags[0] == 4000, "live message after dead peers");

    close(server);
}

int main() {
    try {
        checkWakeupRules();
        checkConcurrentPushers();
        checkDescriptors();
        checkDeadPeer();
        std::printf("HOST_REPLY pass=1 direct_send=ok eagain_retry=ok fifo=ok concurrency=ok notify=ok descriptors=ok dead_peer=ok\n");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "HOST_REPLY pass=0 error=%s\n", error.what());
        return 1;
    }
}
