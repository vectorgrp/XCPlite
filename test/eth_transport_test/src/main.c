// eth_transport_test - Ethernet command framing and receive recovery tests
// Tests TCP and UDP command handling with real loopback sockets.
//
// xcpethtl.c is included directly to inspect transport state and discover the
// ephemeral listening port without adding a test-only production API.

#include <errno.h>  // for errno and error codes
#include <stdio.h>  // for printf, fprintf, fflush, puts
#include <stdlib.h> // for exit
#include <string.h> // for strcmp

#ifndef _WIN32
#include <netinet/in.h> // for sockaddr_in
#include <sys/socket.h> // for socket, connect, send, recv, shutdown
#include <sys/time.h>   // for timeval
#include <unistd.h>     // for close
#endif

#include "sockets.h" // for socket handles and platform helpers
#include "xcplib.h"  // for the public XCPlite API

// Instrument transport receive calls while retaining the real socket implementation.

static int16_t checked_socket_recv(SOCKET_HANDLE socket, uint8_t *buffer, uint16_t size, bool wait_all);
static int16_t checked_socket_recv_from(SOCKET_HANDLE socket, uint8_t *buffer, uint16_t size, uint8_t *addr, uint16_t *port, uint64_t *time);

#define socketRecv checked_socket_recv
#define socketRecvFrom checked_socket_recv_from
#include "xcpethtl.c"
#undef socketRecv
#undef socketRecvFrom

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

static tQueueHandle test_queue;
static SOCKET client;
static bool use_tcp;
static bool threaded;
static struct sockaddr_in server_addr;
static unsigned header_only_reads;
static bool expect_header_only;
static int receive_error;

//-----------------------------------------------------------------------------------------------------
// Socket helpers

static void set_receive_error(int error) {
#ifdef _WIN
    WSASetLastError(error);
#else
    errno = error;
#endif
}

static void set_stale_error(void) {
#ifdef _WIN
    set_receive_error(WSAEINVAL);
#else
    set_receive_error(EINVAL);
#endif
}

static void close_client(void) {
#ifdef _WIN
    CHECK(closesocket(client) == 0);
#else
    CHECK(close(client) == 0);
#endif
    client = INVALID_SOCKET;
}

static void shutdown_client_send(void) {
#ifdef _WIN
    CHECK(shutdown(client, SD_SEND) == 0);
#else
    CHECK(shutdown(client, SHUT_WR) == 0);
#endif
}

static void expect_no_response(void) {
    fd_set readable;
    FD_ZERO(&readable);
    FD_SET(client, &readable);
    struct timeval timeout = {0};
#ifdef _WIN
    CHECK(select(0, &readable, NULL, NULL, &timeout) == 0);
#else
    CHECK(select(client + 1, &readable, NULL, NULL, &timeout) == 0);
#endif
}

static int16_t checked_socket_recv(SOCKET_HANDLE socket, uint8_t *buffer, uint16_t size, bool wait_all) {
    if (expect_header_only == true) {
        CHECK(++header_only_reads == 1);
        CHECK(size == XCPTL_TRANSPORT_LAYER_HEADER_SIZE);
    }
    return socketRecv(socket, buffer, size, wait_all);
}

static int16_t checked_socket_recv_from(SOCKET_HANDLE socket, uint8_t *buffer, uint16_t size, uint8_t *addr, uint16_t *port, uint64_t *time) {
    if (receive_error != 0) {
        set_receive_error(receive_error);
        receive_error = 0;
        return -1;
    }
    return socketRecvFrom(socket, buffer, size, addr, port, time);
}

static void open_client(void) {
    client = socket(AF_INET, (use_tcp == true) ? SOCK_STREAM : SOCK_DGRAM, 0);
    CHECK(client != INVALID_SOCKET);
#ifdef _WIN
    DWORD timeout = 1000;
#else
    struct timeval timeout = {.tv_sec = 1, .tv_usec = 0};
#endif
    CHECK(setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout, sizeof(timeout)) == 0);
    CHECK(connect(client, (struct sockaddr *)&server_addr, sizeof(server_addr)) == 0);
}

static void setup(bool tcp) {
    const uint8_t loopback[] = {127, 0, 0, 1};
    use_tcp = tcp;
    if (threaded == true) {
        CHECK(XcpEthServerInit(loopback, 0, tcp, 16 * 1024));
    } else {
        CHECK(XcpEthTlInit(loopback, 0, tcp, test_queue));
    }
    socklen_t size = sizeof(server_addr);
    CHECK(getsockname(SOCKET_FD((tcp == true) ? gXcpTl.listen_socket : gXcpTl.socket), (struct sockaddr *)&server_addr, &size) == 0);
    open_client();
}

static void teardown(void) {
    if (threaded == true) {
        close_client();
        CHECK(XcpEthServerShutdown());
        return;
    }
    XcpDisconnect();
    close_client();
    XcpEthTlShutdown();
}

static void send_bytes(const uint8_t *data, size_t size) {
    size_t sent = 0;
    do {
        ssize_t n = send(client, (const char *)data + sent, (int)(size - sent), 0);
        if ((n < 0) && (socketGetLastError() == SOCKET_ERROR_INTR)) {
            continue;
        }
        CHECK(n >= 0);
        if (use_tcp == false) {
            CHECK((size_t)n == size);
            return;
        }
        CHECK(n > 0);
        sent += (size_t)n;
    } while (sent < size);
}

static void receive_bytes(uint8_t *data, size_t size) {
    size_t received = 0;
    while (received < size) {
        ssize_t n = recv(client, (char *)data + received, (int)(size - received), 0);
        if ((n < 0) && (socketGetLastError() == SOCKET_ERROR_INTR)) {
            continue;
        }
        CHECK(n > 0);
        received += (size_t)n;
    }
}

//-----------------------------------------------------------------------------------------------------
// XCP command helpers

static void send_command(uint8_t command, uint16_t size) {
    uint8_t frame[XCPTL_TRANSPORT_LAYER_HEADER_SIZE + XCPTL_MAX_CTO_SIZE] = {0};
    CHECK((size > 0) && (size <= XCPTL_MAX_CTO_SIZE));
    frame[0] = (uint8_t)size;
    frame[1] = (uint8_t)(size >> 8);
    frame[4] = command;
    send_bytes(frame, XCPTL_TRANSPORT_LAYER_HEADER_SIZE + size);
}

static uint16_t expect_response(uint8_t pid) {
    uint8_t frame[XCPTL_TRANSPORT_LAYER_HEADER_SIZE + XCPTL_MAX_CTO_SIZE];
    ssize_t n;
    if (use_tcp == true) {
        receive_bytes(frame, 4);
        uint16_t size = frame[0] | (uint16_t)frame[1] << 8;
        CHECK((size > 0) && (size <= XCPTL_MAX_CTO_SIZE));
        receive_bytes(frame + 4, size);
    } else {
        n = recv(client, (char *)frame, sizeof(frame), 0);
        CHECK(n >= 5);
        CHECK(n == (4 + (frame[0] | ((uint16_t)frame[1] << 8))));
    }
    CHECK(frame[4] == pid);
    return frame[0] | (uint16_t)frame[1] << 8;
}

static void connect_xcp(void) {
    send_command(CC_CONNECT, 2);
    if (threaded == false) {
        CHECK(XcpEthTlHandleCommands());
    }
    CHECK(expect_response(PID_RES) == CRM_CONNECT_LEN);
    if (threaded == false) {
        CHECK(XcpIsConnected());
    }
}

static void check_status(void) {
    send_command(CC_GET_STATUS, 1);
    if (threaded == false) {
        CHECK(XcpEthTlHandleCommands());
    }
    CHECK(expect_response(PID_RES) == CRM_GET_STATUS_LEN);
}

static void expect_tcp_recovery(void) {
    CHECK(gXcpTl.socket == INVALID_SOCKET_HANDLE);
    CHECK(gXcpTl.listen_socket != INVALID_SOCKET_HANDLE);
    CHECK(!XcpIsConnected());
    close_client();
    open_client();
    connect_xcp();
    check_status();
}

//-----------------------------------------------------------------------------------------------------
// TCP regression tests

// Reject an oversized command before CONNECT and allow a new client to connect.
static void test_tcp_oversized(void) {
    setup(true);
    uint8_t frame[4 + XCPTL_MAX_CTO_SIZE + 1] = {0};
    uint16_t size = XCPTL_MAX_CTO_SIZE + 1;
    frame[0] = (uint8_t)size;
    frame[1] = (uint8_t)(size >> 8);
    send_bytes(frame, sizeof(frame));
    CHECK(XcpEthTlHandleCommands());
    expect_tcp_recovery();
    teardown();
}

static void test_tcp_invalid_lengths(void) {
    const uint16_t lengths[] = {0, XCPTL_MAX_CTO_SIZE + 1, UINT16_MAX};
    for (unsigned connected = 0; (connected < 2); connected++) {
        for (size_t i = 0; (i < (sizeof(lengths) / sizeof(lengths[0]))); i++) {
            setup(true);
            if (connected != 0) {
                connect_xcp();
            }
            const uint8_t header[] = {(uint8_t)lengths[i], (uint8_t)(lengths[i] >> 8), 0, 0};
            // No payload: reject the length before trying to receive it.
            send_bytes(header, sizeof(header));
            header_only_reads = 0;
            expect_header_only = true;
            CHECK(XcpEthTlHandleCommands());
            expect_header_only = false;
            CHECK(header_only_reads == 1);
            expect_tcp_recovery();
            teardown();
        }
    }
}

static void test_tcp_payload_timeout(void) {
    setup(true);
    connect_xcp();
    const uint8_t header[] = {2, 0, 0, 0};
    send_bytes(header, sizeof(header));
    CHECK(XcpEthTlHandleCommands());
    expect_tcp_recovery();
    teardown();
}

static void test_tcp_idle_timeout(void) {
    setup(true);
    connect_xcp();
    CHECK(XcpEthTlHandleCommands());
    CHECK(XcpIsConnected());
    check_status();
    teardown();
}

static void test_tcp_partial_frames(void) {
    const uint8_t frame[] = {2, 0, 0, 0, CC_CONNECT};
    const size_t sizes[] = {1, 3, 5};
    for (unsigned eof = 0; (eof < 2); eof++) {
        for (size_t i = 0; (i < (sizeof(sizes) / sizeof(sizes[0]))); i++) {
            setup(true);
            connect_xcp();
            send_bytes(frame, sizes[i]);
            if (eof != 0) {
                shutdown_client_send();
            }
            CHECK(XcpEthTlHandleCommands());
            expect_tcp_recovery();
            teardown();
        }
    }
}

static void test_tcp_eof_stale_error(void) {
    const uint8_t frame[] = {2, 0, 0, 0, CC_CONNECT};
    const size_t sizes[] = {0, 1, 3, 4, 5};
    for (size_t i = 0; (i < (sizeof(sizes) / sizeof(sizes[0]))); i++) {
        setup(true);
        connect_xcp();
        if (sizes[i] != 0) {
            send_bytes(frame, sizes[i]);
        }
        shutdown_client_send();
        set_stale_error();
        CHECK(XcpEthTlHandleCommands());
        expect_tcp_recovery();
        teardown();
    }
}

static void test_socket_eof_status(void) {
    for (unsigned wait_all = 0; (wait_all < 2); wait_all++) {
        setup(true);
        connect_xcp();
        shutdown_client_send();
        uint8_t byte;
        set_stale_error();
        CHECK(socketRecv(gXcpTl.socket, &byte, 1, wait_all != 0) < 0);
        CHECK(socketGetLastError() == SOCKET_ERROR_NOTCONN);
        CHECK(socketIsClosed(socketGetLastError()));
        teardown();
    }
}

//-----------------------------------------------------------------------------------------------------
// UDP regression tests

static void test_udp_receive_errors(void) {
    setup(false);
    connect_xcp();
    // A datagram size error is recoverable; other socket errors remain fatal.
    receive_error = SOCKET_ERROR_MSGSIZE;
    CHECK(XcpEthTlHandleCommands());
    CHECK(receive_error == 0);
    CHECK(XcpIsConnected());
    check_status();
#ifdef _WIN
    receive_error = WSAEFAULT;
#else
    receive_error = EIO;
#endif
    CHECK(!XcpEthTlHandleCommands());
    CHECK(receive_error == 0);
    teardown();
}

static void expect_udp_drop(const uint8_t *frame, size_t size) {
    bool connected = XcpIsConnected();
    send_bytes(frame, size);
    CHECK(XcpEthTlHandleCommands());
    CHECK(XcpIsConnected() == connected);
    expect_no_response();
    if (connected == true) {
        check_status();
    } else {
        connect_xcp();
        XcpDisconnect();
    }
}

static const struct {
    size_t size;
    uint8_t data[8];
} malformed_datagrams[] = {
    {0, {0}},                            // Zero-length datagram.
    {1, {1}},                            // Incomplete length field.
    {2, {1, 0}},                         // Incomplete transport header.
    {3, {1, 0, 0}},                      // Incomplete counter field.
    {4, {0, 0, 0, 0}},                   // Empty command.
    {5, {2, 0, 0, 0, CC_CONNECT}},       // Payload shorter than declared.
    {6, {1, 0, 0, 0, CC_CONNECT, 0}},    // Payload longer than declared.
    {5, {0xFF, 0xFF, 0, 0, CC_CONNECT}}, // Oversized declaration.
    {6, {1, 0, 0, 0, CC_DISCONNECT, 0}}, // Invalid frame must not disconnect.
};

static void test_udp_malformed(void) {
    for (unsigned connected = 0; (connected < 2); connected++) {
        setup(false);
        if (connected != 0) {
            connect_xcp();
        }
        for (size_t i = 0; (i < (sizeof(malformed_datagrams) / sizeof(malformed_datagrams[0]))); i++) {
            expect_udp_drop(malformed_datagrams[i].data, malformed_datagrams[i].size);
        }
        teardown();
    }
}

static void test_udp_oversized(void) {
    setup(false);
    connect_xcp();
    uint8_t frame[4 + XCPTL_MAX_CTO_SIZE + 64] = {0};
    // A valid maximum-size command prefix must not be accepted after truncation.
    frame[0] = (uint8_t)XCPTL_MAX_CTO_SIZE;
    frame[1] = (uint8_t)(XCPTL_MAX_CTO_SIZE >> 8);
    frame[4] = CC_GET_STATUS;
    expect_udp_drop(frame, 4 + XCPTL_MAX_CTO_SIZE + 1);
    expect_udp_drop(frame, sizeof(frame));
    teardown();
}

static void test_udp_other_peer(void) {
    setup(false);
    connect_xcp();
    SOCKET master = client;
    open_client();
    const uint8_t malformed[] = {2, 0, 0, 0, CC_CONNECT};
    send_bytes(malformed, sizeof(malformed));
    CHECK(XcpEthTlHandleCommands());
    CHECK(XcpIsConnected());
    close_client();
    client = master;
    check_status();
    teardown();
}

//-----------------------------------------------------------------------------------------------------
// Valid command sizes

static void test_valid_boundaries(void) {
    for (unsigned tcp = 0; (tcp < 2); tcp++) {
        setup(tcp != 0);
        connect_xcp();
        check_status(); // Smallest legal command.
        send_command(CC_GET_STATUS, XCPTL_MAX_CTO_SIZE);
        CHECK(XcpEthTlHandleCommands());
        CHECK(expect_response(PID_RES) == CRM_GET_STATUS_LEN);
        teardown();
    }
}

//-----------------------------------------------------------------------------------------------------
// Server thread regression tests

static void reconnect_after_server_close(void) {
    uint8_t byte;
    ssize_t n;
    do {
        n = recv(client, (char *)&byte, 1, 0);
    } while ((n < 0) && (socketGetLastError() == SOCKET_ERROR_INTR));
    CHECK((n == 0) || ((n < 0) && (socketGetLastError() == SOCKET_ERROR_RESET)));
    close_client();
    open_client();
    connect_xcp();
    check_status();
}

static void test_tcp_server_recovery(void) {
    setup(true);
    const uint16_t lengths[] = {0, XCPTL_MAX_CTO_SIZE + 1, UINT16_MAX};
    for (size_t i = 0; (i < (sizeof(lengths) / sizeof(lengths[0]))); i++) {
        // The first malformed header arrives before XCP CONNECT.
        const uint8_t header[] = {(uint8_t)lengths[i], (uint8_t)(lengths[i] >> 8), 0, 0};
        send_bytes(header, sizeof(header));
        reconnect_after_server_close();
    }
    const uint8_t frame[] = {2, 0, 0, 0, CC_CONNECT};
    const size_t sizes[] = {0, 1, 3, 4, 5};
    for (size_t i = 0; (i < (sizeof(sizes) / sizeof(sizes[0]))); i++) {
        if (sizes[i] != 0) {
            send_bytes(frame, sizes[i]);
        }
        shutdown_client_send();
        reconnect_after_server_close();
    }
    // An incomplete payload times out without stopping the receive thread.
    send_bytes(frame, 4);
    reconnect_after_server_close();
    teardown();
}

static void test_tcp_streaming(void) {
    setup(true);
    const uint8_t connect_frame[] = {2, 0, 0, 0, CC_CONNECT, 0};
    // Split application writes at every header and payload boundary.
    for (size_t split = 1; (split < sizeof(connect_frame)); split++) {
        send_bytes(connect_frame, split);
        send_bytes(connect_frame + split, sizeof(connect_frame) - split);
        CHECK(expect_response(PID_RES) == CRM_CONNECT_LEN);
        check_status();
    }
    const uint8_t commands[] = {2, 0, 0, 0, CC_CONNECT, 0, 1, 0, 1, 0, CC_GET_STATUS};
    send_bytes(commands, sizeof(commands));
    CHECK(expect_response(PID_RES) == CRM_CONNECT_LEN);
    CHECK(expect_response(PID_RES) == CRM_GET_STATUS_LEN);
    check_status();
    teardown();
}

static void test_udp_server_recovery(void) {
    setup(false);
    // Verify CONNECT still works after malformed traffic before a session exists.
    send_bytes(malformed_datagrams[1].data, malformed_datagrams[1].size);
    connect_xcp();
    for (size_t i = 0; (i < (sizeof(malformed_datagrams) / sizeof(malformed_datagrams[0]))); i++) {
        send_bytes(malformed_datagrams[i].data, malformed_datagrams[i].size);
        check_status();
    }
    uint8_t oversized[4 + XCPTL_MAX_CTO_SIZE + 64] = {0};
    oversized[0] = (uint8_t)XCPTL_MAX_CTO_SIZE;
    oversized[1] = (uint8_t)(XCPTL_MAX_CTO_SIZE >> 8);
    oversized[4] = CC_DISCONNECT;
    send_bytes(oversized, sizeof(oversized));
    check_status();
    expect_no_response();
    teardown();
}

//-----------------------------------------------------------------------------------------------------
// Test runner

static const struct {
    const char *name;
    void (*run)(void);
    bool threaded;
} cases[] = {
    {"tcp_oversized", test_tcp_oversized, false},
    {"tcp_invalid_lengths", test_tcp_invalid_lengths, false},
    {"tcp_payload_timeout", test_tcp_payload_timeout, false},
    {"tcp_idle_timeout", test_tcp_idle_timeout, false},
    {"tcp_partial_frames", test_tcp_partial_frames, false},
    {"tcp_eof_stale_error", test_tcp_eof_stale_error, false},
    {"socket_eof_status", test_socket_eof_status, false},
    {"udp_malformed", test_udp_malformed, false},
    {"udp_oversized", test_udp_oversized, false},
    {"udp_other_peer", test_udp_other_peer, false},
    {"udp_receive_errors", test_udp_receive_errors, false},
    {"valid_boundaries", test_valid_boundaries, false},
    {"tcp_server_recovery", test_tcp_server_recovery, true},
    {"tcp_streaming", test_tcp_streaming, true},
    {"udp_server_recovery", test_udp_server_recovery, true},
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
        threaded = cases[i].threaded;
        CHECK(XcpInit("eth_transport_test", "1.0", XCP_MODE_LOCAL));
        if (threaded == false) {
            CHECK(socketStartup());
            test_queue = queueInit(16 * 1024);
            CHECK(test_queue != NULL);
            XcpStart(test_queue, false);
        }
        cases[i].run();
        if (threaded == false) {
            XcpDeinit();
            queueDeinit(test_queue);
            socketCleanup();
        }
        puts("  PASSED");
        executed++;
    }
    CHECK(executed != 0);
    return EXIT_SUCCESS;
}
