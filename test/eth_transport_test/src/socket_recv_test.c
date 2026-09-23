// socket_recv_test - Socket receive and allocation failure tests
// Tests fragmented reads and cleanup after socket handle allocation failures.
//
// sockets.c is included directly to control short reads and allocation failures.
// Allocation tests use real loopback sockets with Linux timestamp support enabled.

#include <stdio.h>  // for printf, fprintf
#include <stdlib.h> // for exit
#include <string.h> // for memcpy, memset, memcmp, strcmp

#include "sockets.h" // for socket handles and socketRecv
#include "xcplib.h"  // for XcpSetLogLevel

#include "../../support/check.h" // for the shared CHECK macro

static int fragmented_recv(SOCKET socket, char *buffer, int size, int flags);

#if defined(_LINUX) && defined(OPTION_SOCKET_HW_TIMESTAMPS)
#include <fcntl.h> // for fcntl, F_GETFD

static void *checked_malloc(size_t size);
static SOCKET tracked_socket(int domain, int type, int protocol);
static SOCKET tracked_accept(SOCKET socket, struct sockaddr *addr, socklen_t *size);

#define malloc checked_malloc
#define socket(domain, type, protocol) tracked_socket((domain), (type), (protocol))
#define accept(socket, addr, size) tracked_accept((socket), (addr), (size))
#endif

#define recv fragmented_recv
#include "sockets.c"
#undef recv
#if defined(_LINUX) && defined(OPTION_SOCKET_HW_TIMESTAMPS)
#undef malloc
#undef socket
#undef accept
#endif

//-----------------------------------------------------------------------------------------------------
// Test fixture

#define TEST_DATA_PATTERN 0x5A
#define TEST_GUARD_BYTE 0xA5

static uint8_t source[XCPTL_MAX_CTO_SIZE];
static size_t total;
static size_t offset;
static size_t first_fragment;
static bool repeat_fragments;
static unsigned receive_calls;

#if defined(_LINUX) && defined(OPTION_SOCKET_HW_TIMESTAMPS)
static bool fail_next_allocation;
static SOCKET allocation_socket;

// Track the descriptor acquired before the allocation that will fail.
static SOCKET tracked_socket(int domain, int type, int protocol) {
    SOCKET result = socket(domain, type, protocol);
    if (fail_next_allocation == true) {
        allocation_socket = result;
    }
    return result;
}

static SOCKET tracked_accept(SOCKET socket, struct sockaddr *addr, socklen_t *size) {
    SOCKET result = accept(socket, addr, size);
    if (fail_next_allocation == true) {
        allocation_socket = result;
    }
    return result;
}

static void *checked_malloc(size_t size) {
    if (fail_next_allocation == true) {
        CHECK(size == sizeof(struct socket));
        fail_next_allocation = false;
        errno = ENOMEM;
        return NULL;
    }
    return malloc(size);
}
#endif

//-----------------------------------------------------------------------------------------------------
// Socket receive stub

static int fragmented_recv(SOCKET socket, char *buffer, int size, int flags) {
    CHECK(socket == 1);
    CHECK(flags == MSG_WAITALL);
    CHECK((size_t)size == (total - offset));
    CHECK(size > 0);
    CHECK(receive_calls < total);
    size_t n = (size_t)size;
    if (((repeat_fragments == true) || (receive_calls == 0)) && (first_fragment < n)) {
        n = first_fragment;
    }
    receive_calls++;
    memcpy(buffer, source + offset, n);
    offset += n;
    return (int)n;
}

//-----------------------------------------------------------------------------------------------------
// Regression tests

static void check_receive(SOCKET_HANDLE socket, unsigned expected_calls) {
    uint8_t received[XCPTL_MAX_CTO_SIZE + 2];
    memset(received, TEST_GUARD_BYTE, sizeof(received));
    offset = 0;
    receive_calls = 0;
    CHECK(socketRecv(socket, received + 1, (uint16_t)total, true) == (int16_t)total);
    CHECK(receive_calls == expected_calls);
    CHECK(offset == total);
    CHECK(memcmp(received + 1, source, total) == 0);
    CHECK((received[0] == TEST_GUARD_BYTE) && (received[total + 1] == TEST_GUARD_BYTE));
}

static void test_fragmentation(void) {
    for (size_t i = 0; (i < sizeof(source)); i++) {
        source[i] = (uint8_t)(i ^ TEST_DATA_PATTERN);
    }
#if defined(_LINUX) && defined(OPTION_SOCKET_HW_TIMESTAMPS)
    struct socket context = {.sock = 1};
    SOCKET_HANDLE socket = &context;
#else
    SOCKET_HANDLE socket = 1;
#endif
    const size_t lengths[] = {XCPTL_TRANSPORT_LAYER_HEADER_SIZE, XCPTL_MAX_CTO_SIZE};
    unsigned executed = 0;
    for (size_t i = 0; (i < (sizeof(lengths) / sizeof(lengths[0]))); i++) {
        total = lengths[i];
        repeat_fragments = false;
        for (first_fragment = 1; (first_fragment < total); first_fragment++) {
            check_receive(socket, 2);
            executed++;
        }
        // Cover repeated short reads from byte-by-byte delivery to a single complete read.
        repeat_fragments = true;
        for (first_fragment = 1; (first_fragment <= total); first_fragment++) {
            check_receive(socket, (unsigned)((total + first_fragment - 1) / first_fragment));
            executed++;
        }
    }
    printf("PASSED: %u deterministic header/payload splits\n", executed);
}

#if defined(_LINUX) && defined(OPTION_SOCKET_HW_TIMESTAMPS)

static void check_allocation_cleanup(void) {
    CHECK(errno == ENOMEM);
    CHECK(fail_next_allocation == false);
    CHECK(allocation_socket != INVALID_SOCKET);
    CHECK(fcntl(allocation_socket, F_GETFD) == -1);
    CHECK(errno == EBADF);
}

static void test_socket_open_allocation_failure(void) {
    SOCKET_HANDLE socket = INVALID_SOCKET_HANDLE;
    allocation_socket = INVALID_SOCKET;
    fail_next_allocation = true;
    CHECK(socketOpen(&socket, SOCKET_MODE_TCP) == false);
    CHECK(socket == INVALID_SOCKET_HANDLE);
    check_allocation_cleanup();

    // A later allocation must still allow normal socket creation.
    CHECK(socketOpen(&socket, SOCKET_MODE_TCP));
    CHECK(socket != INVALID_SOCKET_HANDLE);
    CHECK(socketClose(&socket));
}

static void test_socket_accept_allocation_failure(void) {
    const uint8_t loopback[] = {127, 0, 0, 1};
    SOCKET_HANDLE listener = INVALID_SOCKET_HANDLE;
    CHECK(socketOpen(&listener, SOCKET_MODE_TCP));
    CHECK(socketBind(listener, loopback, 0));
    CHECK(socketListen(listener));
    CHECK(socketSetTimeout(listener, 1000));
    struct sockaddr_in addr;
    socklen_t size = sizeof(addr);
    CHECK(getsockname(SOCKET_FD(listener), (struct sockaddr *)&addr, &size) == 0);

    SOCKET peer = socket(AF_INET, SOCK_STREAM, 0);
    CHECK(peer != INVALID_SOCKET);
    CHECK(connect(peer, (struct sockaddr *)&addr, sizeof(addr)) == 0);
    allocation_socket = INVALID_SOCKET;
    fail_next_allocation = true;
    SOCKET_HANDLE accepted = socketAccept(listener, NULL);
    CHECK(accepted == INVALID_SOCKET_HANDLE);
    check_allocation_cleanup();
    CHECK(fcntl(SOCKET_FD(listener), F_GETFD) >= 0);
    CHECK(close(peer) == 0);

    // The allocation failure must not close the listener or prevent a later accept.
    peer = socket(AF_INET, SOCK_STREAM, 0);
    CHECK(peer != INVALID_SOCKET);
    CHECK(connect(peer, (struct sockaddr *)&addr, sizeof(addr)) == 0);
    uint8_t peer_addr[sizeof(loopback)];
    accepted = socketAccept(listener, peer_addr);
    CHECK(accepted != INVALID_SOCKET_HANDLE);
    CHECK(SOCKET_FD(accepted) != INVALID_SOCKET);
    CHECK(memcmp(peer_addr, loopback, sizeof(peer_addr)) == 0);
    CHECK(socketClose(&accepted));
    CHECK(close(peer) == 0);
    CHECK(socketClose(&listener));
}

#endif

//-----------------------------------------------------------------------------------------------------
// Test runner

static const struct {
    const char *name;
    void (*run)(void);
} cases[] = {
    {"fragmentation", test_fragmentation},
#if defined(_LINUX) && defined(OPTION_SOCKET_HW_TIMESTAMPS)
    {"socket_open_allocation_failure", test_socket_open_allocation_failure},
    {"socket_accept_allocation_failure", test_socket_accept_allocation_failure},
#endif
};

int main(int argc, char **argv) {
    CHECK(argc <= 2);
    XcpSetLogLevel(0);
    unsigned executed = 0;
    for (size_t i = 0; (i < (sizeof(cases) / sizeof(cases[0]))); i++) {
        if ((argc == 2) && (strcmp(argv[1], cases[i].name) != 0)) {
            continue;
        }
        printf("RUN %s\n", cases[i].name);
        fflush(stdout);
        cases[i].run();
        puts("  PASSED");
        executed++;
    }
    CHECK(executed != 0);
    return EXIT_SUCCESS;
}
