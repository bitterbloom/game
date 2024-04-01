#ifdef __linux__
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <poll.h>

#include "./sockets.h"
#include "../util.h"

static char error_buffer[1024];

#define FAIL(string) { \
    if (sizeof string > 1024) \
        EXIT_PRINT("Error message too long for buffer"); \
    memcpy(error_buffer, string, sizeof string); \
    return false; \
}

#define FAIL_AND_GET_ERROR(string) { \
    snprintf(error_buffer, 1024, string " (code: %d, '%s')", errno, strerror(errno)); \
    return false; \
}

char *sockets_get_error() {
    return error_buffer;
}

bool socket_init_udp(Socket *const s) {
    s->socket = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, AF_UNSPEC);
    if (s->socket == -1)
        FAIL_AND_GET_ERROR("Failed to initialize socket");
    return true;
}

bool socket_close(Socket const s) {
    if (close(s.socket) == -1)
        FAIL_AND_GET_ERROR("Failed to close socket");
    return true;
}

bool socket_bind(Socket const s, struct sockaddr_in *const addr) {
    if (bind(s.socket, (struct sockaddr *) addr, sizeof (struct sockaddr_in)) == -1)
        FAIL_AND_GET_ERROR("Failed to bind socket");
    return true;
}

bool socket_sendto_inet(Socket const s, void const *const buf, int const len, struct sockaddr_in const *const dest) {
    int const n = sendto(s.socket, buf, len, 0, (struct sockaddr *) dest, sizeof (struct sockaddr_in));

    if (n == -1)
        FAIL_AND_GET_ERROR("Failed to send data");

    if (n < len)
        FAIL("Did not send all data");

    return true;
}

bool socket_recvfrom_inet(Socket const s, void *const buf, int const len, int *const read, struct sockaddr_in *const src) {
    socklen_t addr_len = sizeof (struct sockaddr_in);
    int const n = recvfrom(s.socket, buf, len, 0, (struct sockaddr *) src, &addr_len);

    if (n == -1)
        FAIL_AND_GET_ERROR("Failed to receive data");

    if (addr_len != sizeof (struct sockaddr_in) || src->sin_family != AF_INET)
        FAIL("Received data from non-IPv4 source");

    *read = n;
    return true;
}

// This function blocks execution.
bool socket_poll(Socket const s, short const ev_req, short *const ev_ret, int const timeout) {
    struct pollfd pfd = {
        .fd = s.socket,
        .events = ev_req,
    };
    int const nready = poll(&pfd, 1, timeout);

    if (nready == -1)
        FAIL_AND_GET_ERROR("Failed to poll");

    if (pfd.revents & POLLERR)
        FAIL("Error occurred on socket");

    if (pfd.revents & POLLHUP)
        FAIL("Hangup occurred on socket");

    if (pfd.revents & POLLNVAL)
        FAIL("Invalid request on socket");

    *ev_ret = pfd.revents;
    return true;
}

#endif

#ifdef _WIN64
#error TODO
#endif
