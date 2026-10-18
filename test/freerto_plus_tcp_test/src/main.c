// Exercise XCPlite against the real FreeRTOS-Plus-TCP loopback stack.
// The demo bootstrap calls demoApplication() from a FreeRTOS task.
// The server starts its workers once, after startup-failure checks.
#include "../../support/check.h"
#include "FreeRTOS_IP.h"
#include "NetworkBufferManagement.h"
#include "faults.h"
#include "sockets.h"
#include "xcp.h"
#include "xcpethtl.h"
#include "xcplib.h"
#include "xcplite.h"
#include <string.h>

#define TEST_SOCKET_PORT 65000U

static const uint8_t loopback_addr[4] = {127, 0, 0, 1};
static uint32_t measured_value = 0x12345678;
static const struct {
    uint32_t value;
} parameters = {123};
CalSegDecl(parameters);

// Inject count send results on one socket; FAULT_PASSTHROUGH restores normal sends.
static void injectSendResult(Socket_t socket, int32_t result, unsigned count) {
    atomic_store(&fault_socket, socket);
    atomic_store(&send_failures_remaining, count);
    atomic_store(&send_result, result);
}

// ---------------------------------------------------------------------------
// Socket API: error classification, send retries, and buffer ownership.

static void testSocketBackend(void) {
    SOCKET_HANDLE socket;
    CHECK(socketStartup());
    fail_open = true;
    CHECK(!socketOpen(&socket, 0));
    CHECK(socketOpen(&socket, 0));
    CHECK(socketBind(socket, loopback_addr, TEST_SOCKET_PORT));
    CHECK(socketSetTimeout(socket, 1));
    uint8_t data[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    uint8_t received[8];

    // An idle receive, timeout, or interruption returns zero; other errors propagate.
    CHECK(socketRecvFrom(socket, received, sizeof(received), NULL, NULL, NULL) == 0);
    const int32_t receive_timeouts[] = {SOCKET_ERROR_WBLOCK, SOCKET_ERROR_TIMEDOUT, SOCKET_ERROR_INTR};
    for (unsigned i = 0; i < sizeof(receive_timeouts) / sizeof(receive_timeouts[0]); i++) {
        recv_result = -receive_timeouts[i];
        CHECK(socketRecvFrom(socket, received, sizeof(received), NULL, NULL, NULL) == 0);
    }
    recv_result = -pdFREERTOS_ERRNO_EINVAL;
    CHECK(socketRecvFrom(socket, received, sizeof(received), NULL, NULL, NULL) == -pdFREERTOS_ERRNO_EINVAL);
    CHECK(socketSendTo(socket, data, UINT16_MAX, loopback_addr, TEST_SOCKET_PORT, NULL) == -SOCKET_ERROR_MSGSIZE);
    CHECK(socketSetTimeout(socket, 500));

    // Three resource failures must be retried before the real send succeeds.
    const int32_t retryable_results[] = {0, -pdFREERTOS_ERRNO_ENOBUFS, -pdFREERTOS_ERRNO_ENOMEM};
    for (unsigned i = 0; i < sizeof(retryable_results) / sizeof(retryable_results[0]); i++) {
        unsigned calls_before = atomic_load(&send_calls);
        injectSendResult(socket, retryable_results[i], 3);
        CHECK(socketSendTo(socket, data, sizeof(data), loopback_addr, TEST_SOCKET_PORT, NULL) == sizeof(data));
        CHECK(atomic_load(&send_calls) == calls_before + 4);
        CHECK(socketRecvFrom(socket, received, sizeof(received), NULL, NULL, NULL) == sizeof(received));
        CHECK(memcmp(data, received, sizeof(data)) == 0);
    }

    // These negative results propagate immediately, including EWOULDBLOCK.
    const int32_t nonretryable_errors[] = {-pdFREERTOS_ERRNO_EINVAL, -SOCKET_ERROR_BADF, -SOCKET_ERROR_NOTCONN, -SOCKET_ERROR_WBLOCK, -SOCKET_ERROR_TIMEDOUT, -SOCKET_ERROR_INTR};
    for (unsigned i = 0; i < sizeof(nonretryable_errors) / sizeof(nonretryable_errors[0]); i++) {
        unsigned calls_before = atomic_load(&send_calls);
        injectSendResult(socket, nonretryable_errors[i], FAULT_REPEAT_FOREVER);
        CHECK(socketSendTo(socket, data, sizeof(data), loopback_addr, TEST_SOCKET_PORT, NULL) == nonretryable_errors[i]);
        CHECK(atomic_load(&send_calls) == calls_before + 1);
    }

    // UDP sends are all-or-nothing; a short positive result is an I/O error.
    injectSendResult(socket, 1, 1);
    CHECK(socketSendTo(socket, data, sizeof(data), loopback_addr, TEST_SOCKET_PORT, NULL) == -pdFREERTOS_ERRNO_EIO);
    injectSendResult(socket, FAULT_PASSTHROUGH, 0);

    // Zero from an unsuccessful automatic bind must not become an endless retry.
    SOCKET_HANDLE unbound;
    CHECK(socketOpen(&unbound, 0));
    injectSendResult(unbound, 0, FAULT_REPEAT_FOREVER);
    CHECK(socketSendTo(unbound, data, sizeof(data), loopback_addr, TEST_SOCKET_PORT, NULL) == -SOCKET_ERROR_NOTCONN);
    injectSendResult(unbound, FAULT_PASSTHROUGH, 0);
    CHECK(socketSendTo(unbound, data, sizeof(data), loopback_addr, TEST_SOCKET_PORT, NULL) == sizeof(data));
    uint16_t source_port;
    CHECK(socketRecvFrom(socket, received, sizeof(received), NULL, &source_port, NULL) == sizeof(received));
    CHECK(source_port != 0 && memcmp(data, received, sizeof(data)) == 0);
    CHECK(socketClose(&unbound));

    // Outrank the IP task, send, then overwrite the caller's buffer before delivery.
    memset(data, 0xA5, sizeof(data));
    UBaseType_t original_priority = uxTaskPriorityGet(NULL);
    vTaskPrioritySet(NULL, configMAX_PRIORITIES - 1);
    CHECK(socketSendTo(socket, data, sizeof(data), loopback_addr, TEST_SOCKET_PORT, NULL) == sizeof(data));
    memset(data, 0, sizeof(data));
    vTaskPrioritySet(NULL, original_priority);
    CHECK(socketRecvFrom(socket, data, sizeof(data), NULL, NULL, NULL) == sizeof(data));
    for (unsigned i = 0; i < sizeof(data); i++) {
        CHECK(data[i] == 0xA5);
    }

    // A rejected close preserves the handle; a later accepted close invalidates it.
    close_failures_remaining = 1;
    CHECK(!socketClose(&socket));
    CHECK(xSocketValid(socket) == pdTRUE);
    CHECK(socketClose(&socket));
    CHECK(socket == INVALID_SOCKET_HANDLE);
    CHECK(socketClose(&socket));
    CHECK(!socketOpen(&socket, SOCKET_MODE_TCP));
    CHECK(atomic_load(&owned_sockets) == 0);
}

// ---------------------------------------------------------------------------
// Real resource exhaustion: hold every network buffer, then release them later.

static void *held_buffers[ipconfigNUM_NETWORK_BUFFER_DESCRIPTORS];
static unsigned held_buffer_count;
static atomic_bool buffers_released;

static void releaseNetworkBuffers(void *unused) {
    (void)unused;
    vTaskDelay(pdMS_TO_TICKS(100));
    while (held_buffer_count != 0) {
        FreeRTOS_ReleaseUDPPayloadBuffer(held_buffers[--held_buffer_count]);
    }
    atomic_store(&buffers_released, true);
    vTaskDelete(NULL);
}

static void testNetworkBufferExhaustion(void) {
    SOCKET_HANDLE socket;
    CHECK(socketOpen(&socket, 0));
    CHECK(socketBind(socket, loopback_addr, TEST_SOCKET_PORT));
    CHECK(socketSetTimeout(socket, 500));

    while (held_buffer_count < ipconfigNUM_NETWORK_BUFFER_DESCRIPTORS) {
        void *buffer = FreeRTOS_GetUDPPayloadBuffer_Multi(8, 0, ipTYPE_IPv4);
        if (buffer == NULL) {
            break;
        }
        held_buffers[held_buffer_count++] = buffer;
    }
    CHECK(uxGetNumberOfFreeNetworkBuffers() == 0);

    // Sending must wait until this helper task returns the held buffers.
    CHECK(xTaskCreate(releaseNetworkBuffers, "release", configMINIMAL_STACK_SIZE, NULL, 2, NULL) == pdPASS);
    uint8_t data[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    unsigned calls_before = atomic_load(&send_calls);
    TickType_t start_ticks = xTaskGetTickCount();
    CHECK(socketSendTo(socket, data, sizeof(data), loopback_addr, TEST_SOCKET_PORT, NULL) == sizeof(data));
    CHECK(atomic_load(&send_calls) > calls_before + 1);
    CHECK(xTaskGetTickCount() - start_ticks >= pdMS_TO_TICKS(100));
    CHECK(atomic_load(&buffers_released));
    uint8_t received[8];
    CHECK(socketRecvFrom(socket, received, sizeof(received), NULL, NULL, NULL) == sizeof(received));
    CHECK(memcmp(data, received, sizeof(data)) == 0);
    CHECK(socketClose(&socket));
}

// ---------------------------------------------------------------------------
// Server transport: startup cleanup, XCP commands, and queued DAQ transmission.

static void testStartupCleanup(void) {
    // Bind fails and its close request is rejected, leaving one retained socket.
    unsigned owned = atomic_load(&owned_sockets);
    fail_bind = true;
    close_failures_remaining = 1;
    CHECK(!XcpEthServerInit(loopback_addr, TEST_XCP_PORT, false, 0));
    CHECK(atomic_load(&owned_sockets) == owned + 1);

    // The next attempt closes that socket, then cleans up its own timeout failure.
    fail_timeout = true;
    CHECK(!XcpEthServerInit(loopback_addr, TEST_XCP_PORT, false, 0));
    CHECK(atomic_load(&owned_sockets) == owned);
}

static uint16_t last_packet_counter;

// Check transport framing, the expected PID, and the shared CRM/DAQ packet counter.
static int16_t receivePacket(Socket_t client, uint8_t *response, uint8_t pid) {
    int16_t size = socketRecvFrom(client, response, 256, NULL, NULL, NULL);
    CHECK(size >= 5 && response[4] == pid, "response size=%d PID=%02x expected=%02x", size, response[4], pid);
    CHECK((unsigned)(response[0] | response[1] << 8) == (unsigned)size - 4);
    uint16_t counter;
    memcpy(&counter, response + 2, sizeof(counter));
    CHECK(counter == (uint16_t)(last_packet_counter + 1), "CTR %u after %u", counter, last_packet_counter);
    last_packet_counter = counter;
    return size;
}

static void sendCommand(Socket_t client, const uint8_t *data, uint8_t size, uint8_t *response) {
    // Prepend the four-byte XCP/UDP header and require a positive command response.
    uint8_t packet[256] = {size, 0, 0, 0};
    memcpy(packet + 4, data, size);
    CHECK(socketSendTo(client, packet, size + 4, loopback_addr, TEST_XCP_PORT, NULL) == size + 4);
    (void)receivePacket(client, response, PID_RES);
}

static void testServerTransport(void) {
    CHECK(XcpInit("native_plus_tcp", "1.0", XCP_MODE_LOCAL));
    testStartupCleanup();
    CHECK(XcpEthServerInit(loopback_addr, TEST_XCP_PORT, false, 0));
    TickType_t start_ticks = xTaskGetTickCount();
    while (!XcpEthServerStatus()) {
        CHECK(xTaskGetTickCount() - start_ticks < pdMS_TO_TICKS(1000));
        vTaskDelay(1);
    }

    SOCKET_HANDLE client;
    CHECK(socketOpen(&client, 0));
    CHECK(socketBind(client, loopback_addr, 0));
    CHECK(socketSetTimeout(client, 1000));
    uint8_t response[256] = {0};
    DaqCreateEvent(measurement);
    last_packet_counter = UINT16_MAX;

    // Keep command payloads visible beside the checks that exercise them.
#define SEND_COMMAND(...)                                                                                                                                                          \
    do {                                                                                                                                                                           \
        const uint8_t data[] = {__VA_ARGS__};                                                                                                                                      \
        sendCommand(client, data, sizeof(data), response);                                                                                                                         \
    } while (0)

    SEND_COMMAND(CC_CONNECT, 0);
    CHECK(XcpIsConnected());
    SEND_COMMAND(CC_GET_STATUS);

    // Write the calibration segment's working page and read back the value.
    uint32_t addr = XcpAddrEncodeSegNumber(XcpGetCalSegNumber(calseg_id_parameters), 0);
    SEND_COMMAND(CC_SET_MTA, 0, 0, XCP_ADDR_EXT_SEG, addr, addr >> 8, addr >> 16, addr >> 24);
    SEND_COMMAND(CC_DOWNLOAD, 4, 0xEF, 0xBE, 0, 0);
    SEND_COMMAND(CC_SET_MTA, 0, 0, XCP_ADDR_EXT_SEG, addr, addr >> 8, addr >> 16, addr >> 24);
    SEND_COMMAND(CC_UPLOAD, 4);
    CHECK(response[5] == 0xEF && response[6] == 0xBE);

    // Configure one timestamped DAQ list with one 32-bit measurement.
    SEND_COMMAND(CC_FREE_DAQ);
    SEND_COMMAND(CC_ALLOC_DAQ, 0, 1, 0);
    SEND_COMMAND(CC_ALLOC_ODT, 0, 0, 0, 1);
    SEND_COMMAND(CC_ALLOC_ODT_ENTRY, 0, 0, 0, 0, 1);
    SEND_COMMAND(CC_SET_DAQ_PTR, 0, 0, 0, 0, 0);
    addr = ApplXcpGetAddr((const uint8_t *)&measured_value);
    SEND_COMMAND(CC_WRITE_DAQ, 0xFF, 4, XCP_ADDR_EXT_ABS, addr, addr >> 8, addr >> 16, addr >> 24);
    SEND_COMMAND(CC_SET_DAQ_LIST_MODE, DAQ_MODE_TIMESTAMP, 0, 0, evt_id_measurement, evt_id_measurement >> 8, 1, 1);
    SEND_COMMAND(CC_START_STOP_DAQ_LIST, 2, 0, 0);
    SEND_COMMAND(CC_START_STOP_SYNCH, 1);

    // Each trigger must deliver the known value with the next transport counter.
    for (unsigned i = 0; i < 64; i++) {
        measured_value = UINT32_C(0x12340000) + i;
        DaqTriggerEvent(measurement);
        CHECK(receivePacket(client, response, 0) == 16);
        uint32_t value;
        memcpy(&value, response + 12, sizeof(value));
        CHECK(value == measured_value);
    }

    // Stop DAQ and reconnect while keeping the same server workers.
    SEND_COMMAND(CC_START_STOP_SYNCH, 0);
    SEND_COMMAND(CC_DISCONNECT);
    CHECK(!XcpIsConnected() && XcpEthServerStatus());
    SEND_COMMAND(CC_CONNECT, 0);
    SEND_COMMAND(CC_GET_STATUS);

    // Keep TX from running until two messages are ready, then expect one datagram.
    vTaskSuspendAll();
    for (unsigned i = 0; i < 2; i++) {
        tQueueBuffer part = queueAcquire(NULL, 8);
        CHECK(part.buffer != NULL);
        memset(part.buffer, 0x40 + i, part.size);
        queuePush(NULL, &part, i == 1);
    }
    (void)xTaskResumeAll();
    CHECK(socketRecvFrom(client, response, sizeof(response), NULL, NULL, NULL) == 24);
    for (unsigned i = 0; i < 2; i++) {
        const uint8_t *packet = response + 12 * i;
        uint16_t counter;
        memcpy(&counter, packet + 2, sizeof(counter));
        CHECK(packet[0] == 8 && packet[1] == 0 && packet[4] == 0x40 + i);
        CHECK(counter == (uint16_t)(last_packet_counter + 1));
        last_packet_counter = counter;
    }

    // Resource retries must retain the queued segment until socketSendTo completes.
    atomic_store(&queue_releases, 0);
    atomic_store(&check_queue_ownership, true);
    injectSendResult(atomic_load(&server_socket), 0, 5);
    tQueueBuffer buffer = queueAcquire(NULL, 8);
    CHECK(buffer.buffer != NULL);
    memset(buffer.buffer, 0x42, buffer.size);
    queuePush(NULL, &buffer, true);
    CHECK(receivePacket(client, response, 0x42) == 12);
    start_ticks = xTaskGetTickCount();
    while (atomic_load(&queue_releases) == 0) {
        CHECK(xTaskGetTickCount() - start_ticks < pdMS_TO_TICKS(1000), "TX worker did not release the queue buffer within 1000 ms");
        vTaskDelay(1);
    }
    CHECK(atomic_load(&queue_releases) == 1);
    atomic_store(&check_queue_ownership, false);
    CHECK(XcpEthServerStatus());
    SEND_COMMAND(CC_GET_STATUS);

    // A permanent send error releases the segment and terminates the TX worker.
    atomic_store(&queue_releases, 0);
    injectSendResult(atomic_load(&server_socket), -pdFREERTOS_ERRNO_EINVAL, FAULT_REPEAT_FOREVER);
    buffer = queueAcquire(NULL, 8);
    CHECK(buffer.buffer != NULL);
    memset(buffer.buffer, 0x43, buffer.size);
    queuePush(NULL, &buffer, true);
    start_ticks = xTaskGetTickCount();
    while (XcpEthServerStatus()) {
        CHECK(xTaskGetTickCount() - start_ticks < pdMS_TO_TICKS(1000));
        vTaskDelay(1);
    }
    CHECK(atomic_load(&queue_releases) == 1);
#undef SEND_COMMAND
}

void demoApplication(void) {
    XcpSetLogLevel(1);
    testSocketBackend();
    testNetworkBufferExhaustion();
    testServerTransport();
    puts("FreeRTOS-Plus-TCP integration tests PASSED");
    exit(EXIT_SUCCESS);
}
