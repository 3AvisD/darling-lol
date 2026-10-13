#include <darlingserver/message.hpp>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <stdexcept>

// Standalone host sockets only. Never connects to Darling, Riot or the game.
static bool countAllocations = false, failNextNew = false;
static unsigned allocations = 0;
extern "C" void* __real_malloc(size_t);
extern "C" void* __wrap_malloc(size_t n) {
    if (countAllocations) ++allocations;
    return __real_malloc(n);
}
void* operator new(size_t n) {
    if (failNextNew) { failNextNew = false; throw std::bad_alloc(); }
    if (countAllocations) ++allocations;
    if (void* p = __real_malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, size_t) noexcept { std::free(p); }

using DarlingServer::Message;
using DarlingServer::MessageQueue;
static void require(bool pass, const char* what) {
    if (!pass) throw std::runtime_error(what);
}
struct Sockets {
    int fd[2];
    Sockets() {
        require(socketpair(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0, fd) == 0, "socketpair");
        int enabled = 1;
        require(setsockopt(fd[1], SOL_SOCKET, SO_PASSCRED, &enabled, sizeof(enabled)) == 0, "credentials");
        sa_family_t family = AF_UNIX;
        require(bind(fd[0], reinterpret_cast<sockaddr*>(&family), sizeof(family)) == 0, "sender address");
    }
    ~Sockets() { close(fd[0]); close(fd[1]); }
};
static unsigned openFDs() {
    DIR* directory = opendir("/proc/self/fd");
    require(directory, "fd directory");
    unsigned n = 0;
    while (auto entry = readdir(directory)) if (entry->d_name[0] != '.') ++n;
    closedir(directory);
    return n;
}
static void sendPacket(int fd, unsigned tag, size_t size, unsigned descriptors = 0) {
    Message packet(size, descriptors);
    for (size_t i = 0; i < size; ++i) packet.data()[i] = (tag + i) & 255;
    for (unsigned i = 0; i < descriptors; ++i) {
        int pipeFDs[2]; require(pipe(pipeFDs) == 0, "pipe");
        unsigned char byte = tag + i;
        require(write(pipeFDs[1], &byte, 1) == 1, "pipe write"); close(pipeFDs[1]);
        packet.pushDescriptor(pipeFDs[0]);
    }
    packet.rawHeader().msg_name = nullptr;
    packet.rawHeader().msg_namelen = 0;
    require(sendmsg(fd, &packet.rawHeader(), 0) == static_cast<ssize_t>(size), "send packet");
}
static void checkPacket(MessageQueue& queue, unsigned tag, size_t size, unsigned descriptors, int sender, unsigned flags = 0) {
    auto packet = queue.pop(); require(packet.has_value(), "missing packet");
    require(packet->data().size() == size, "packet size");
    for (size_t i = 0; i < size; ++i) require(packet->data()[i] == ((tag + i) & 255), "packet payload");
    ucred credentials{};
    require(packet->copyCredentialsOut(credentials) && credentials.pid == getpid()
        && credentials.uid == getuid() && credentials.gid == getgid(), "packet credentials");
    require(packet->descriptors().size() == descriptors, "descriptor count");
    require((packet->rawHeader().msg_flags & flags) == flags, "receive flags");
    sockaddr_un address{}; socklen_t length = sizeof(address);
    require(getsockname(sender, reinterpret_cast<sockaddr*>(&address), &length) == 0, "sender name");
    auto receivedAddress = packet->address();
    require(receivedAddress.rawSize() == length && std::memcmp(&receivedAddress.raw(), &address, length) == 0, "sender preserved");
    for (unsigned i = 0; i < descriptors; ++i) {
        int fd = packet->extractDescriptorAtIndex(0);
        require(fd >= 0 && (fcntl(fd, F_GETFD) & FD_CLOEXEC), "descriptor CLOEXEC");
        unsigned char byte = 0;
        require(read(fd, &byte, 1) == 1 && byte == static_cast<unsigned char>(tag + i), "descriptor payload");
        close(fd);
    }
}
int main(int argc, char**) {
    try {
        const bool measureBaseline = argc > 1;
        Sockets sockets;
        unsigned baselineFDs = openFDs(), emptyAllocations = 0, oneAllocations = 0;
        {
            MessageQueue queue;
            unsigned callbacks = 0;
            queue.setMessageArrivalNotificationCallback([&] {
                ++callbacks;
                require(!queue.empty(), "callback reentrant lock");
            });
            require(!queue.receiveMany(sockets.fd[1]), "warm empty drain");
            allocations = 0; countAllocations = true;
            bool drained = !queue.receiveMany(sockets.fd[1]);
            countAllocations = false; emptyAllocations = allocations;
            require(drained && callbacks == 0, "empty drain notification");
            if (!measureBaseline) require(emptyAllocations == 0, "warm empty receive allocated");

            sendPacket(sockets.fd[0], 100, 19, 4);
            allocations = 0; countAllocations = true;
            drained = !queue.receiveMany(sockets.fd[1]);
            countAllocations = false; oneAllocations = allocations;
            require(drained && callbacks == 1, "single receive notification");
            checkPacket(queue, 100, 19, 4, sockets.fd[0]);
            if (!measureBaseline) require(oneAllocations <= 3, "single receive wasted allocations");

            // More than one batch, alternating descriptors and changing payload sizes.
            for (unsigned i = 0; i < 19; ++i) sendPacket(sockets.fd[0], 100 + i, i * 11, i % 2);
            require(!queue.receiveMany(sockets.fd[1]) && callbacks == 2, "multi batch notification");
            for (unsigned i = 0; i < 19; ++i) checkPacket(queue, 100 + i, i * 11, i % 2, sockets.fd[0]);
            require(queue.empty(), "extra packet");
            require(!queue.receiveMany(sockets.fd[1]) && callbacks == 2, "post drain notification");
            sendPacket(sockets.fd[0], 150, 300, 6);
            require(!queue.receiveMany(sockets.fd[1]), "truncated receive");
            checkPacket(queue, 150, 256, 4, sockets.fd[0], MSG_TRUNC | MSG_CTRUNC);
        }
        require(openFDs() == baselineFDs, "normal receive descriptor leak");
        {
            // A deque allocation failure after a kernel receive must close every
            // received descriptor, including the ones not yet visited by the loop.
            MessageQueue queue;
            require(!queue.receiveMany(sockets.fd[1]), "exception warmup");
            for (unsigned i = 0; i < 12; ++i) sendPacket(sockets.fd[0], 100 + i, 8, i % 4 + 1);
            failNextNew = true;
            bool threw = false;
            try { queue.receiveMany(sockets.fd[1]); } catch (const std::bad_alloc&) { threw = true; }
            failNextNew = false;
            require(threw, "failure injection did not throw");
            while (queue.pop()) {}
            if (!measureBaseline) {
                sendPacket(sockets.fd[0], 140, 256);
                require(!queue.receiveMany(sockets.fd[1]), "recovery drain");
                checkPacket(queue, 140, 256, 0, sockets.fd[0]);
            }
        }
        require(openFDs() == baselineFDs, "exception receive descriptor leak");
        std::printf("HOST_RECEIVE pass=1 warm_empty_allocations=%u single_packet_allocations=%u credentials=ok descriptors=ok batches=ok callback=ok exception_cleanup=ok\n", emptyAllocations, oneAllocations);
        return 0;
    } catch (const std::exception& error) {
        countAllocations = false;
        std::fprintf(stderr, "HOST_RECEIVE pass=0 error=%s\n", error.what());
        return 1;
    }
}
