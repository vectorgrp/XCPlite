// socket_recv_test - Socket receive fragmentation tests
// Tests the socketRecv loop with deterministic short reads.
//
// sockets.c is included directly to replace only the OS recv call and exercise
// two-fragment splits and repeated short reads of a header and maximum-size command.

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

#define TEST_DATA_PATTERN 0x5A
#define TEST_GUARD_BYTE 0xA5

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
static bool repeat_fragments;
static unsigned receive_calls;

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

int main(void) {
    XcpSetLogLevel(0);
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
    return EXIT_SUCCESS;
}
