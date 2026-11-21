// Regression test for unaligned DOWNLOAD payloads in XcpWriteMta.
// Run: ctest --test-dir build -R '^memory_transfer_test$' -V
// Build with -fsanitize=undefined to catch misaligned reads even on CPUs that tolerate them.
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "xcplib.h"
#include "queue.h"
#include "xcp.h"
#include "xcplite.h"
#include "../../support/check.h"

static _Alignas(8) uint8_t target[24];

static uint8_t command(const uint8_t *data, uint8_t size) {
    // Align the command itself; DOWNLOAD's two-byte header makes its payload unaligned for 4/8-byte reads.
    _Alignas(8) tXcpCto cmd = {0};
    memcpy(cmd.b, data, size);
    return XcpCommand(cmd.dw, size);
}

static void test_download(void) {
    const uint8_t sizes[] = {1, 2, 4, 8};
    for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
        uint8_t size = sizes[i];
        printf("  %u-byte DOWNLOAD ... ", (unsigned)size);
        fflush(stdout);

        // Select an aligned destination, so this test isolates source alignment.
        memset(target, 0, sizeof(target));
        uint8_t set_mta[CRO_SET_MTA_LEN] = {CC_SET_MTA};
        set_mta[3] = ApplXcpGetAddrExt(target);
        uint32_t addr = ApplXcpGetAddr(target);
        memcpy(&set_mta[4], &addr, sizeof(addr));
        uint8_t result = command(set_mta, sizeof(set_mta));
        CHECK(result == CRC_CMD_OK, "SET_MTA for %u-byte DOWNLOAD returned XCP error 0x%02X", (unsigned)size, (unsigned)result);

        // Send a known pattern and verify that exactly 'size' bytes were written.
        const uint8_t download[] = {CC_DOWNLOAD, size, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};
        result = command(download, 2 + size);
        CHECK(result == CRC_CMD_OK, "%u-byte DOWNLOAD returned XCP error 0x%02X", (unsigned)size, (unsigned)result);
        for (size_t byte = 0; byte < size; byte++) {
            CHECK(target[byte] == download[2 + byte], "%u-byte DOWNLOAD: byte %zu expected 0x%02X, got 0x%02X", (unsigned)size, byte,
                  (unsigned)download[2 + byte], (unsigned)target[byte]);
        }
        CHECK(target[size] == 0, "%u-byte DOWNLOAD wrote beyond its payload", (unsigned)size);

        // Verify MTA advanced by exactly the transferred size.
        const uint8_t next[] = {CC_DOWNLOAD, 1, 0xA5};
        result = command(next, sizeof(next));
        CHECK(result == CRC_CMD_OK, "follow-up DOWNLOAD after %u bytes returned XCP error 0x%02X", (unsigned)size, (unsigned)result);
        CHECK(target[size] == 0xA5, "MTA did not advance by %u bytes: expected 0xA5 at that offset, got 0x%02X", (unsigned)size, (unsigned)target[size]);
        puts("PASS (payload, bounds, MTA advancement)");
    }
}

int main(void) {
    puts("DOWNLOAD source alignment regression test");
    XcpSetLogLevel(0);
    CHECK(XcpInit("memory_transfer_test", "1.0", XCP_MODE_LOCAL), "XCP initialization failed");
    tQueueHandle queue = queueInit(16 * 1024);
    CHECK(queue != NULL, "could not allocate the test transmit queue");
    XcpStart(queue, false);
    const uint8_t connect[] = {CC_CONNECT, 0};
    CHECK(command(connect, sizeof(connect)) == CRC_CMD_OK, "XCP CONNECT failed");
    test_download();
    XcpDisconnect();
    XcpDeinit();
    queueDeinit(queue);
    puts("All 4 DOWNLOAD sizes passed.");
    return 0;
}
