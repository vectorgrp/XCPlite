// COPY_CAL_PAGE command-length regression test. No network peer is required.
// Run: ctest --test-dir build -R '^cal_page_test$' -V
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "xcplib.h"
#include "cal.h"
#include "queue.h"
#include "xcp.h"
#include "xcplite.h"
#include "xcptl.h"
#include "../../support/check.h"

static tXcpCto response;
static uint8_t response_length;
static unsigned response_count;

// Capture command responses without opening an Ethernet server.
void XcpTlSendCrm(const uint8_t *data, uint8_t size) {
    CHECK(size <= sizeof(response.b), "command response exceeds the test buffer");
    memcpy(response.b, data, size);
    response_length = size;
    response_count++;
}

bool XcpTlWaitForTransmitQueueEmpty(uint16_t timeout_ms) {
    (void)timeout_ms;
    return true;
}

static void expect_command(const tXcpCto *command, uint8_t length, uint8_t expected) {
    response_count = 0;
    uint8_t result = XcpCommand(command->dw, length);
    CHECK(result == expected, "command 0x%02X, length %u: expected XCP result 0x%02X, got 0x%02X", (unsigned)command->b[0],
          (unsigned)length, (unsigned)expected, (unsigned)result);
    CHECK(response_count == 1, "expected one response, got %u", response_count);
    if (expected == CRC_CMD_OK) {
        CHECK((response_length == 1) && (response.b[0] == PID_RES), "expected a positive command response");
    } else {
        CHECK((response_length == 2) && (response.b[0] == PID_ERR) && (response.b[1] == expected), "expected an error response with code 0x%02X",
              (unsigned)expected);
    }
}

static void expect_calibration_value(tXcpCalSegIndex segment, uint32_t address, uint32_t expected) {
    uint32_t actual = 0;
    CHECK(XcpCalSegReadMemory(address, sizeof(actual), (uint8_t *)&actual) == CRC_CMD_OK, "could not read the calibration working page");
    CHECK(actual == expected, "working page changed: expected 0x%08X, got 0x%08X", (unsigned)expected, (unsigned)actual);

    // Model application reader cycles: take the published page over, then confirm the old page is free.
    for (unsigned read = 0; read < 2; read++) {
        const void *page = XcpLockCalSeg(segment);
        memcpy(&actual, page, sizeof(actual));
#ifdef OPTION_CAL_RCU_REFCOUNT
        XcpUnlockCalSeg(segment, page);
#else
        XcpUnlockCalSeg(segment);
#endif
        CHECK(actual == expected, "application reader: expected 0x%08X, got 0x%08X", (unsigned)expected, (unsigned)actual);
    }
}

int main(void) {
    puts("COPY_CAL_PAGE command-length regression test");
    XcpSetLogLevel(0);
    CHECK(XcpInit("cal_page_test", "1.0", XCP_MODE_LOCAL), "XCP initialization failed");

    const uint32_t default_value = 0x12345678;
    const uint32_t working_value = 0xABCDEF01;
    tXcpCalSegIndex segment = XcpCreateCalSeg("parameters", &default_value, sizeof(default_value));
    CHECK(segment != XCP_UNDEFINED_CALSEG, "could not create the test calibration segment");
    uint8_t number = XcpGetCalSegNumber(segment);
    uint32_t address = XcpGetCalSegBaseAddress(segment);
    CHECK(XcpCalSegSetCalPage(number, XCP_CALPAGE_WORKING_PAGE, CAL_PAGE_MODE_XCP | CAL_PAGE_MODE_ECU) == CRC_CMD_OK,
          "could not select the working page");

    tQueueHandle queue = queueInit(16 * 1024);
    CHECK(queue != NULL, "could not allocate the test transmit queue");
    XcpStart(queue, false);
    const tXcpCto connect = {.b = {CC_CONNECT, 0}};
    // CONNECT has a longer positive response than the other commands tested here.
    CHECK(XcpCommand(connect.dw, CRO_CONNECT_LEN) == CRC_CMD_OK, "XCP CONNECT failed");

    const tXcpCto copy = {.b = {CC_COPY_CAL_PAGE, number, XCP_CALPAGE_DEFAULT_PAGE, number, XCP_CALPAGE_WORKING_PAGE}};
    expect_command(&copy, CRO_COPY_CAL_PAGE_LEN, CRC_CMD_OK);
    expect_calibration_value(segment, address, default_value);
    CHECK(XcpCalSegWriteMemory(address, sizeof(working_value), (const uint8_t *)&working_value) == CRC_CMD_OK, "could not modify the working page");
    expect_calibration_value(segment, address, working_value);

    // Model a reused receive buffer: bytes beyond the received length retain a valid prior command.
    // Every incomplete prefix must be rejected before those stale fields can reset the working page.
    for (uint8_t length = 1; length < CRO_COPY_CAL_PAGE_LEN; length++) {
        printf("  %u/%u-byte COPY_CAL_PAGE ... ", (unsigned)length, (unsigned)CRO_COPY_CAL_PAGE_LEN);
        fflush(stdout);
        expect_command(&copy, length, CRC_CMD_SYNTAX);
        expect_calibration_value(segment, address, working_value);
        puts("PASS (rejected, calibration unchanged)");
    }

    // A complete command must still work after the rejected prefixes.
    expect_command(&copy, CRO_COPY_CAL_PAGE_LEN, CRC_CMD_OK);
    expect_calibration_value(segment, address, default_value);
    puts("  Complete COPY_CAL_PAGE ... PASS (default value restored)");

    XcpDisconnect();
    XcpDeinit();
    queueDeinit(queue);
    return 0;
}
