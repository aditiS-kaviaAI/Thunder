#include <gtest/gtest.h>
#include "../../../Source/addons/hibernate/hibernate.h"
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>
#include <chrono>
#include <cstring>
#include <thread>
#include <filesystem>
#include <poll.h>
#include <cerrno>

namespace {
thread_local int interruptPoll = 0;
thread_local int interruptSend = 0;
thread_local int interruptRecv = 0;
thread_local unsigned injected = 0;
}

extern "C" int __real_poll(struct pollfd*, nfds_t, int);
extern "C" ssize_t __real_send(int, const void*, size_t, int);
extern "C" ssize_t __real_recv(int, void*, size_t, int);

extern "C" int __wrap_poll(struct pollfd* descriptors, nfds_t count, int timeout)
{
    if (interruptPoll != 0) {
        if (interruptPoll > 0) --interruptPoll;
        ++injected;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        errno = EINTR;
        return -1;
    }
    return __real_poll(descriptors, count, timeout);
}

extern "C" ssize_t __wrap_send(int fd, const void* bytes, size_t count, int flags)
{
    if (interruptSend > 0) {
        --interruptSend;
        ++injected;
        errno = EINTR;
        return -1;
    }
    return __real_send(fd, bytes, count, flags);
}

extern "C" ssize_t __wrap_recv(int fd, void* bytes, size_t count, int flags)
{
    if (interruptRecv > 0) {
        --interruptRecv;
        ++injected;
        errno = EINTR;
        return -1;
    }
    return __real_recv(fd, bytes, count, flags);
}

extern "C" ssize_t __real___recv_chk(int, void*, size_t, size_t, int);
extern "C" ssize_t __wrap___recv_chk(int fd, void* bytes, size_t count, size_t capacity, int flags)
{
    if (interruptRecv > 0) {
        --interruptRecv;
        ++injected;
        errno = EINTR;
        return -1;
    }
    return __real___recv_chk(fd, bytes, count, capacity, flags);
}

namespace {
enum class Reply { Fragmented, Eof, Stall };

static uint32_t Exchange(Reply reply, uint32_t timeout, bool descriptorZero = false)
{
    const std::string path = "/tmp/thunder-p1-" + std::to_string(getpid()) + ".sock";
    unlink(path.c_str());
    int listener = socket(AF_UNIX, SOCK_STREAM, 0);
    EXPECT_GE(listener, 0);
    if (listener < 0) return HIBERNATE_ERROR_GENERAL;
    sockaddr_un address {};
    address.sun_family = AF_UNIX;
    std::strncpy(address.sun_path, path.c_str(), sizeof(address.sun_path) - 1);
    if (bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0
        || listen(listener, 1) != 0) {
        ADD_FAILURE() << "Cannot bind disposable checkpoint fixture";
        close(listener);
        unlink(path.c_str());
        return HIBERNATE_ERROR_GENERAL;
    }
    std::thread server([listener, reply]() {
        const int client = accept(listener, nullptr, nullptr);
        if (client >= 0) {
            char request[sizeof(int) + sizeof(pid_t)];
            size_t count = 0;
            while (count < sizeof(request)) {
                const ssize_t received = recv(client, request + count, sizeof(request) - count, 0);
                if (received <= 0) break;
                count += static_cast<size_t>(received);
            }
            if (reply == Reply::Fragmented) {
                const int success = 0;
                const char* bytes = reinterpret_cast<const char*>(&success);
                for (size_t i = 0; i < sizeof(success); ++i) {
                    send(client, bytes + i, 1, MSG_NOSIGNAL);
                    std::this_thread::sleep_for(std::chrono::milliseconds(2));
                }
            } else if (reply == Reply::Stall) {
                std::this_thread::sleep_for(std::chrono::milliseconds(150));
            }
            close(client);
        }
    });
    const auto started = std::chrono::steady_clock::now();
    void* storage = nullptr;
    const int saved = descriptorZero ? dup(STDIN_FILENO) : -1;
    if (descriptorZero) close(STDIN_FILENO);
    const uint32_t result = HibernateProcess(timeout, getpid(), path.c_str(), "", &storage);
    const auto elapsed = std::chrono::steady_clock::now() - started;
    if (interruptPoll < 0) {
        EXPECT_GE(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count(), timeout - 5);
        EXPECT_LT(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count(), timeout + 100);
    }
    server.join();
    if (descriptorZero) {
        EXPECT_EQ(fcntl(STDIN_FILENO, F_GETFD), -1);
        if (saved >= 0) {
            EXPECT_EQ(dup2(saved, STDIN_FILENO), STDIN_FILENO);
            close(saved);
        }
    }
    close(listener);
    unlink(path.c_str());
    return result;
}
}

TEST(CheckpointP1, InvalidLocatorDoesNotCloseStdin)
{
    const int original = fcntl(STDIN_FILENO, F_GETFD);
    void* storage = nullptr;
    for (unsigned i = 0; i < 100; ++i) {
        EXPECT_EQ(HibernateProcess(50, getpid(), "invalid", "", &storage), HIBERNATE_ERROR_GENERAL);
    }
    EXPECT_EQ(fcntl(STDIN_FILENO, F_GETFD), original);
    EXPECT_EQ(storage, nullptr);
}

TEST(CheckpointP1, FragmentedResponseCompletes)
{
    EXPECT_EQ(Exchange(Reply::Fragmented, 500), HIBERNATE_ERROR_NONE);
}

TEST(CheckpointP1, DescriptorZeroIsOwnedAndClosed)
{
    EXPECT_EQ(Exchange(Reply::Fragmented, 500, true), HIBERNATE_ERROR_NONE);
}

TEST(CheckpointP1, FailedConnectionsDoNotLeak)
{
    const auto count = [] {
        return std::distance(std::filesystem::directory_iterator("/proc/self/fd"),
            std::filesystem::directory_iterator());
    };
    const auto before = count();
    void* storage = nullptr;
    for (unsigned i = 0; i < 100; ++i) {
        EXPECT_EQ(HibernateProcess(50, getpid(), "/tmp/thunder-p1-missing/socket", "", &storage),
            HIBERNATE_ERROR_GENERAL);
    }
    EXPECT_EQ(count(), before);
}

TEST(CheckpointP1, EofIsNotTimeout)
{
    EXPECT_EQ(Exchange(Reply::Eof, 500), HIBERNATE_ERROR_GENERAL);
}

TEST(CheckpointP1, OverallDeadline)
{
    EXPECT_EQ(Exchange(Reply::Stall, 30), HIBERNATE_ERROR_TIMEOUT);
}

TEST(CheckpointP1, InterruptedPollSendAndReceiveComplete)
{
    injected = 0;
    interruptPoll = interruptSend = interruptRecv = 2;
    EXPECT_EQ(Exchange(Reply::Fragmented, 500), HIBERNATE_ERROR_NONE);
    EXPECT_EQ(injected, 6u);
    EXPECT_EQ(interruptPoll + interruptSend + interruptRecv, 0);
    interruptPoll = interruptSend = interruptRecv = 0;
}

TEST(CheckpointP1, RepeatedInterruptionsKeepOverallDeadline)
{
    injected = 0;
    interruptPoll = -1;
    EXPECT_EQ(Exchange(Reply::Stall, 30), HIBERNATE_ERROR_TIMEOUT);
    EXPECT_GT(injected, 1u);
    interruptPoll = 0;
}
