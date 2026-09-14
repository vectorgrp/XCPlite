// socket_recv_test - Socket receive fragmentation tests
// Tests the socketRecv loop with deterministic short reads.
//
// sockets.c is included directly to replace only the OS recv call and exercise
// every two-fragment split of a transport header and a maximum-size command.

#include <stdio.h>  // for printf, fprintf
#include <stdlib.h> // for exit
#include <string.h> // for memcpy, memset, memcmp

#include "sockets.h" // for socket handles and socketRecv
#include "xcplib.h"  // for XcpSetLogLevel

static int fragmented_recv(SOCKET socket, char *buffer, int size, int flags);

#define recv fragmented_recv
#include "sockets.c"
#undef recv

//-----------------------------------------------------------------------------------------------------
// Test fixture

// Unlike assert(), CHECK always evaluates commands and initialization in Release builds.
#define CHECK(condition)                                                                                                                                                           \
    do {                                                                                                                                                                           \
        if (!(condition)) {                                                                                                                                                        \
            fprintf(stderr, "%s:%d: %s failed\n", __FILE__, __LINE__, #condition);                                                                                                 \
            exit(EXIT_FAILURE);                                                                                                                                                    \
        }                                                                                                                                                                          \
    } while (0)

static uint8_t source[XCPTL_MAX_CTO_SIZE];
static size_t total;
static size_t offset;
static size_t first_fragment;
static unsigned receive_calls;

//-----------------------------------------------------------------------------------------------------
// Socket receive stub

static int fragmented_recv(SOCKET socket, char *buffer, int size, int flags) {
    CHECK(socket == 1);
    CHECK(flags == MSG_WAITALL);
    CHECK((size_t)size == total - offset);
    CHECK(receive_calls < 2);
    size_t n = (receive_calls++ == 0) ? first_fragment : size;
    memcpy(buffer, source + offset, n);
    offset += n;
    return (int)n;
}

//-----------------------------------------------------------------------------------------------------
// Regression tests

int main(void) {
    XcpSetLogLevel(0);
    for (size_t i = 0; i < sizeof(source); i++) {
        source[i] = (uint8_t)(i ^ 0x5a);
    }
#if defined(_LINUX) && defined(OPTION_SOCKET_HW_TIMESTAMPS)
    struct socket context = {.sock = 1};
    SOCKET_HANDLE socket = &context;
#else
    SOCKET_HANDLE socket = 1;
#endif
    const size_t lengths[] = {XCPTL_TRANSPORT_LAYER_HEADER_SIZE, XCPTL_MAX_CTO_SIZE};
    unsigned executed = 0;
    for (size_t i = 0; i < sizeof(lengths) / sizeof(lengths[0]); i++) {
        total = lengths[i];
        for (first_fragment = 1; first_fragment < total; first_fragment++) {
            uint8_t received[XCPTL_MAX_CTO_SIZE + 2];
            memset(received, 0xa5, sizeof(received));
            offset = 0;
            receive_calls = 0;
            CHECK(socketRecv(socket, received + 1, (uint16_t)total, true) == (int16_t)total);
            CHECK(receive_calls == 2);
            CHECK(offset == total);
            CHECK(memcmp(received + 1, source, total) == 0);
            CHECK((received[0] == 0xa5) && (received[total + 1] == 0xa5));
            executed++;
        }
    }
    printf("PASSED: %u deterministic header/payload splits\n", executed);
    return EXIT_SUCCESS;
}
