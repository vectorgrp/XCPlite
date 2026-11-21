// BUILD_CHECKSUM command regression tests. No network peer is required.
// Run: ctest --test-dir build -R '^checksum_' --output-on-failure
// Run one case: ./build/checksum_test zero (or valid, dynamic_zero).
// Before the fix, zero-size requests abort Debug builds and succeed in Release;
// dynamic-address requests are deferred instead of being rejected immediately.
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "xcplib.h"
#include "xcplite.h"
#include "xcptl.h"
#include "queue.h"
#include "xcp.h"
#include "../../support/check.h"

static tXcpCto response;
static uint8_t response_length;
static unsigned response_count;
// The first nine bytes form a known CRC vector; 'X' marks the expected next MTA byte.
static const uint8_t test_data[] = "123456789X";
static uint32_t test_address;

// Capture the protocol's immediate command responses without opening a socket.
void XcpTlSendCrm(const uint8_t *data, uint8_t size) {
    CHECK(size <= sizeof(response.b), "response exceeds test buffer");
    memcpy(response.b, data, size);
    response_length = size;
    response_count++;
}

bool XcpTlWaitForTransmitQueueEmpty(uint16_t timeout_ms) {
    // There is no transport thread or network traffic to drain in this test.
    (void)timeout_ms;
    return true;
}

static void execute(const tXcpCto *request, uint8_t length, uint8_t expected) {
    response_count = 0;
    const uint8_t result = XcpCommand(request->dw, length);
    // Check both the dispatch result and the response a client would receive.
    // CHECK remains active with NDEBUG, so Release must obey the same contract.
    CHECK(result == expected, "command 0x%02X: expected 0x%02X, got 0x%02X", request->b[0], expected, result);
    CHECK(response_count == 1, "expected one immediate response, got %u", response_count);
    if (expected == CRC_CMD_OK) {
        CHECK(response_length > 0 && response.b[0] == PID_RES, "expected positive response");
    } else {
        CHECK(response_length == 2 && response.b[0] == PID_ERR && response.b[1] == expected, "incorrect error response");
    }
}

static void set_mta(uint8_t extension, uint32_t address) {
    tXcpCto request = {.b = {CC_SET_MTA, 0, 0, extension}};
    memcpy(&request.b[4], &address, sizeof(address));
    execute(&request, CRO_SET_MTA_LEN, CRC_CMD_OK);
}

static void checksum(uint32_t size, uint8_t expected) {
    tXcpCto request = {.b = {CC_BUILD_CHECKSUM}};
    // BUILD_CHECKSUM carries its 32-bit block size at byte offset 4.
    memcpy(&request.b[4], &size, sizeof(size));
    execute(&request, CRO_BUILD_CHECKSUM_LEN, expected);
}

static void expect_next_byte(uint8_t expected) {
    // Observe MTA advancement through a normal UPLOAD instead of internal state.
    const tXcpCto request = {.b = {CC_UPLOAD, 1}};
    execute(&request, CRO_UPLOAD_LEN, CRC_CMD_OK);
    CHECK(response_length == CRM_UPLOAD_LEN + 1 && response.b[CRM_UPLOAD_LEN] == expected, "unexpected MTA position");
}

static void test_zero(void) {
    // Start at a nonzero offset: rejecting repeated requests must preserve the
    // current MTA and leave the session usable for the following UPLOAD.
    set_mta(XCP_ADDR_EXT_SEG, test_address + 3);
    for (unsigned attempt = 0; attempt < 3; attempt++) {
        checksum(0, CRC_OUT_OF_RANGE);
    }
    expect_next_byte('4');
}

static void test_valid(void) {
    // A normal checksum must still succeed and advance the MTA by the block size.
    set_mta(XCP_ADDR_EXT_SEG, test_address);
    checksum(9, CRC_CMD_OK);
    CHECK(response_length == CRM_BUILD_CHECKSUM_LEN, "incorrect checksum response length");
    CHECK(response.b[1] == XCP_CHECKSUM_TYPE_CRC16CCITT, "expected CRC16-CCITT checksum");
    uint32_t actual = 0;
    memcpy(&actual, &response.b[4], sizeof(actual));
    // Standard CRC16-CCITT vector: initial value 0xFFFF, input "123456789".
    CHECK(actual == 0x29B1, "expected checksum 0x29B1, got 0x%08X", (unsigned)actual);
    expect_next_byte('X'); // Successful checksum consumes exactly nine bytes.
}

static void test_dynamic_zero(void) {
    // Valid event-relative accesses normally wait for the associated event to
    // provide a base pointer. Invalid sizes must be rejected before that dispatch.
    const tXcpEventId event = XcpCreateEvent("checksum", 0, 0);
    CHECK(event != XCP_UNDEFINED_EVENT_ID, "could not create checksum event");
    set_mta(XCP_ADDR_EXT_DYN, XcpAddrEncodeDyn(0, event));
    // Reject immediately, even if this event never fires, rather than queuing the request.
    checksum(0, CRC_OUT_OF_RANGE);
    // Also fire the event: rejection must leave no invalid checksum to execute later.
    XcpEventExt(event, test_data);
    CHECK(response_count == 1, "invalid checksum generated a deferred response");
    set_mta(XCP_ADDR_EXT_SEG, test_address);
    expect_next_byte('1');
}

int main(int argc, char **argv) {
    CHECK(argc == 2, "expected zero, valid, or dynamic_zero case");
    XcpSetLogLevel(0);
    CHECK(XcpInit("checksum_test", "1.0", XCP_MODE_LOCAL), "could not initialize XCP");
    // Command dispatch requires a started, connected session. Its transmit queue
    // is kept in memory; the transport callbacks above replace network operations.
    const tQueueHandle queue = queueInit(16 * 1024);
    CHECK(queue != NULL, "could not create transmit queue");
    XcpStart(queue, false);
    const tXcpCto connect = {.b = {CC_CONNECT, 0}};
    execute(&connect, CRO_CONNECT_LEN, CRC_CMD_OK);
    // Segment-relative addressing gives the commands a valid, bounded data source
    // without depending on the executable's absolute address or ASLR layout.
    const tXcpCalSegIndex segment = XcpCreateCalSeg("checksum_data", test_data, sizeof(test_data));
    CHECK(segment != XCP_UNDEFINED_CALSEG, "could not create test segment");
    test_address = XcpAddrEncodeSegIndex(segment, 0);

    if (strcmp(argv[1], "zero") == 0) {
        test_zero();
    } else if (strcmp(argv[1], "valid") == 0) {
        test_valid();
    } else {
        CHECK(strcmp(argv[1], "dynamic_zero") == 0, "unknown checksum case: %s", argv[1]);
        test_dynamic_zero();
    }

    XcpDisconnect();
    XcpDeinit();
    queueDeinit(queue);
    printf("PASSED: checksum %s\n", argv[1]);
    return 0;
}
