/*
 * If not stated otherwise in this file or this component's LICENSE file the
 * following copyright and licenses apply:
 *
 * Copyright 2020 Metrological
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#define MODULE "CheckpointServer"

#include "../common/Log.h"
#include "../hibernate.h"

#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <stdbool.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

typedef enum {
    MEMCR_CHECKPOINT = 100,
    MEMCR_RESTORE
} ServerRequestCode;

typedef enum {
    MEMCR_OK = 0,
    MEMCR_ERROR = -1,
    MEMCR_INVALID_PID = -2,
    MEMCR_SOCKET_READ_ERROR = -3
} ServerResponseCode;

typedef struct {
    ServerRequestCode reqCode;
    pid_t pid;
} __attribute__((packed)) ServerRequest;

typedef struct {
    ServerResponseCode respCode;
} __attribute__((packed)) ServerResponse;

static int64_t NowMs(void)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        return -1;
    }
    return (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

static bool Wait(int fd, short events, int64_t deadline)
{
    for (;;) {
        const int64_t now = NowMs();
        if ((now < 0) || (now >= deadline)) {
            errno = now < 0 ? EIO : ETIMEDOUT;
            return false;
        }
        const int64_t remaining = deadline - now;
        struct pollfd descriptor = { fd, events, 0 };
        const int result = poll(&descriptor, 1, remaining > INT_MAX ? INT_MAX : (int)remaining);
        if (result > 0) {
            // Let send/recv/getsockopt classify HUP and socket errors.
            return true;
        }
        if ((result < 0) && (errno != EINTR)) {
            return false;
        }
    }
}

static int Connect(const char* serverLocator, int64_t deadline)
{
    int cd;
    struct sockaddr_in addrIn = { 0 };
    struct sockaddr_un addrUn = { 0 };
    struct sockaddr* addr = NULL;
    size_t addrSize = 0;
    char host[64] = { 0 };
    char* port = NULL;
    int domain;
    if ((serverLocator == NULL) || (serverLocator[0] == '\0')) {
        errno = EINVAL;
        return -1;
    }

    if (serverLocator[0] == '/') {
        if (strlen(serverLocator) >= sizeof(addrUn.sun_path)) {
            errno = ENAMETOOLONG;
            return -1;
        }
        domain = PF_UNIX;
        addrUn.sun_family = PF_UNIX;
        strcpy(addrUn.sun_path, serverLocator);
        addr = (struct sockaddr*)&addrUn;
        addrSize = sizeof(struct sockaddr_un);
    } else {
        if (strlen(serverLocator) >= sizeof(host)) {
            errno = EINVAL;
            return -1;
        }
        strcpy(host, serverLocator);
        port = strchr(host, ':');
        if (port == NULL) {
            errno = EINVAL;
            return -1;
        }
        *port++ = '\0';
        char* end;
        errno = 0;
        unsigned long number = strtoul(port, &end, 10);
        if ((port[0] < '0') || (port[0] > '9') || (*end != '\0')
            || (errno != 0) || (number == 0) || (number > 65535)
            || (inet_pton(AF_INET, host, &addrIn.sin_addr) != 1)) {
            errno = EINVAL;
            return -1;
        }
        domain = AF_INET;
        addrIn.sin_family = AF_INET;
        addrIn.sin_port = htons((uint16_t)number);
        addr = (struct sockaddr*)&addrIn;
        addrSize = sizeof(struct sockaddr_in);
    }

    cd = socket(domain, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (cd < 0) {
        return -1;
    }
    if (connect(cd, addr, addrSize) != 0) {
        int error = errno;
        if ((error == EINPROGRESS) || (error == EINTR)) {
            if (Wait(cd, POLLOUT, deadline)) {
                socklen_t length = sizeof(error);
                if (getsockopt(cd, SOL_SOCKET, SO_ERROR, &error, &length) != 0) {
                    error = errno;
                }
            } else {
                error = errno;
            }
        }
        if (error != 0) {
            close(cd);
            errno = error;
            return -1;
        }
    }
    return cd;
}

static bool Transfer(int fd, void* buffer, size_t length, bool sending, int64_t deadline)
{
    size_t offset = 0;
    while (offset < length) {
        if (!Wait(fd, sending ? POLLOUT : POLLIN, deadline)) {
            return false;
        }
        ssize_t count = sending
            ? send(fd, (const char*)buffer + offset, length - offset, MSG_NOSIGNAL)
            : recv(fd, (char*)buffer + offset, length - offset, 0);
        if (count > 0) {
            offset += (size_t)count;
        } else if (count == 0) {
            errno = ECONNRESET;
            return false;
        } else if ((errno != EINTR) && (errno != EAGAIN) && (errno != EWOULDBLOCK)) {
            return false;
        }
    }
    return true;
}

static bool SendRcvCmd(const ServerRequest* cmd, ServerResponse* resp, uint32_t timeoutMs, const char* serverLocator)
{
    resp->respCode = MEMCR_ERROR;
    const int64_t now = NowMs();
    if (now < 0) {
        return false;
    }
    const int64_t deadline = now + timeoutMs;
    int cd = Connect(serverLocator, deadline);
    if (cd < 0) {
        if (errno == ETIMEDOUT) {
            resp->respCode = MEMCR_SOCKET_READ_ERROR;
        }
        return false;
    }
    ServerResponse received;
    const bool complete = Transfer(cd, (void*)cmd, sizeof(*cmd), true, deadline)
        && Transfer(cd, &received, sizeof(received), false, deadline);
    if (complete) {
        *resp = received;
    } else {
        resp->respCode = errno == ETIMEDOUT ? MEMCR_SOCKET_READ_ERROR : MEMCR_ERROR;
    }
    close(cd);
    return complete && (resp->respCode == MEMCR_OK);
}

// PUBLIC_INTERFACE
/** Request checkpoint of pid through the locator in data_dir within timeout milliseconds.
 * volatile_dir and storage are unused by this stateless backend. Returns a hibernate
 * success, timeout, or general-failure code; caller storage is never modified.
 */
uint32_t HibernateProcess(const uint32_t timeout, const pid_t pid, const char data_dir[], const char volatile_dir[] __attribute__((unused)), void** storage __attribute__((unused)))
{
    ServerRequest req = {
        .reqCode = MEMCR_CHECKPOINT,
        .pid = pid
    };
    ServerResponse resp;

    if (SendRcvCmd(&req, &resp, timeout, data_dir)) {
        LOGINFO("Hibernate process PID %d success", pid);
        return HIBERNATE_ERROR_NONE;
    } else if (resp.respCode == MEMCR_SOCKET_READ_ERROR) {
        LOGERR("Error Hibernate timeout process PID %d ret %d", pid, resp.respCode);
        return HIBERNATE_ERROR_TIMEOUT;
    } else {
        LOGERR("Error Hibernate process PID %d ret %d", pid, resp.respCode);
        return HIBERNATE_ERROR_GENERAL;
    }
}

// PUBLIC_INTERFACE
/** Request restoration of pid through data_dir within timeout milliseconds.
 * volatile_dir and storage are unused. Returns success (also for an absent PID),
 * timeout, or general failure without modifying caller storage.
 */
uint32_t WakeupProcess(const uint32_t timeout, const pid_t pid, const char data_dir[], const char volatile_dir[] __attribute__((unused)), void** storage __attribute__((unused)))
{
    ServerRequest req = {
        .reqCode = MEMCR_RESTORE,
        .pid = pid
    };
    ServerResponse resp;

    if (SendRcvCmd(&req, &resp, timeout, data_dir)) {
        LOGINFO("Wakeup process PID %d success", pid);
        return HIBERNATE_ERROR_NONE;
    } else if (resp.respCode == MEMCR_INVALID_PID) {
        LOGINFO("Wakeup process PID %d ret %d - INVALID PID, nothing to wakeup", pid, resp.respCode);
        return HIBERNATE_ERROR_NONE;
    } else if (resp.respCode == MEMCR_SOCKET_READ_ERROR) {
        return HIBERNATE_ERROR_TIMEOUT;
    }

    LOGERR("Error Wakeup process PID %d ret %d", pid, resp.respCode);
    return HIBERNATE_ERROR_GENERAL;
}
