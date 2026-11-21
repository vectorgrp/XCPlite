// File upload lifecycle regression tests. No network peer is required.
// Run: ctest --test-dir build -R '^file_upload_test$' -V
// Run one case: ./build/file_upload_test restart (or empty).
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "xcplib.h"
#include "xcplite.h"
#include "xcptl.h"
#include "queue.h"
#include "xcp.h"
#include "../../support/check.h"

static unsigned open_upload_files;

static FILE *tracked_fopen(const char *name, const char *mode) {
    FILE *file = fopen(name, mode);
    if (file != NULL) {
        open_upload_files++;
    }
    return file;
}

static int tracked_fclose(FILE *file) {
    CHECK(open_upload_files > 0, "closing an untracked upload file");
    int result = fclose(file);
    CHECK(result == 0, "could not close an upload file");
    open_upload_files--;
    return result;
}

// Instrument the production file operations so leaks are detected on every platform,
// including Release builds where the original restart assertion is disabled.
#define fopen tracked_fopen
#define fclose tracked_fclose
#include "xcpappl.c"
#undef fopen
#undef fclose

#define DATA_NAME "file_upload_test_data"
#define EMPTY_NAME "file_upload_test_empty"
static const uint8_t file_data[] = "0123456789ABCDEF";
#define DATA_SIZE (sizeof(file_data) - 1)

static tXcpCto response;
static uint8_t response_length;
static unsigned response_count;

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

static void command(uint8_t code, uint8_t argument, uint8_t expected) {
    const tXcpCto request = {.b = {code, argument}};
    response_count = 0;
    uint8_t result = XcpCommand(request.dw, 2);
    CHECK(result == expected, "command 0x%02X: expected result 0x%02X, got 0x%02X", (unsigned)code, (unsigned)expected, (unsigned)result);
    CHECK(response_count == 1, "expected one command response, got %u", response_count);
    CHECK(response_length > 0, "empty command response");
    if (expected == CRC_CMD_OK) {
        CHECK(response.b[0] == PID_RES, "expected a positive response to command 0x%02X", (unsigned)code);
    } else {
        CHECK((response_length == 2) && (response.b[0] == PID_ERR) && (response.b[1] == expected), "expected error response 0x%02X", (unsigned)expected);
    }
}

static void write_fixture(const char *filename, size_t size) {
    FILE *file = fopen(filename, "wb");
    CHECK(file != NULL, "could not create fixture '%s'", filename);
    CHECK(fwrite(file_data, 1, size, file) == size, "could not write fixture '%s'", filename);
    CHECK(fclose(file) == 0, "could not close fixture '%s'", filename);
}

static void start_upload(const char *name, uint32_t expected_size) {
    XcpSetA2lName(name);
    command(CC_GET_ID, IDT_ASAM_UPLOAD, CRC_CMD_OK);
    CHECK((response_length == CRM_GET_ID_LEN) && (response.b[1] == 0), "expected GET_ID to select upload transfer mode");
    uint32_t actual_size = 0;
    memcpy(&actual_size, &response.b[4], sizeof(actual_size));
    CHECK(actual_size == expected_size, "GET_ID '%s': expected %u bytes, got %u", name, (unsigned)expected_size, (unsigned)actual_size);
    CHECK(open_upload_files == (expected_size > 0 ? 1U : 0U), "GET_ID '%s': unexpected number of open upload files: %u", name, open_upload_files);
}

static void expect_upload(const uint8_t *expected, uint8_t size) {
    command(CC_UPLOAD, size, CRC_CMD_OK);
    CHECK(response_length == CRM_UPLOAD_LEN + size, "UPLOAD returned an unexpected payload length");
    CHECK(memcmp(&response.b[CRM_UPLOAD_LEN], expected, size) == 0, "UPLOAD returned incorrect file data");
}

static void test_restart(void) {
    // Restart after a partial read. Every GET_ID must replace the old handle and reset the position.
    for (unsigned attempt = 0; attempt < 32; attempt++) {
        start_upload(DATA_NAME, DATA_SIZE);
        expect_upload(file_data, 4);
    }
    expect_upload(file_data + 4, DATA_SIZE - 4);
    CHECK(open_upload_files == 0, "completing the restarted upload leaked %u file handles", open_upload_files);
}

static void test_empty(void) {
    // An empty file is unavailable, not an assertion failure or an open upload.
    for (unsigned attempt = 0; attempt < 3; attempt++) {
        start_upload(EMPTY_NAME, 0);
        command(CC_UPLOAD, 1, CRC_ACCESS_DENIED);
    }

    // Switching from an active upload to an empty file must close both file handles.
    start_upload(DATA_NAME, DATA_SIZE);
    expect_upload(file_data, 4);
    start_upload(EMPTY_NAME, 0);
    command(CC_UPLOAD, 1, CRC_ACCESS_DENIED);

    // A valid upload must still work after the unavailable file.
    start_upload(DATA_NAME, DATA_SIZE);
    expect_upload(file_data, DATA_SIZE);
    CHECK(open_upload_files == 0, "empty-file recovery leaked %u upload handles", open_upload_files);
}

static const struct {
    const char *name;
    void (*run)(void);
} cases[] = {{"restart", test_restart}, {"empty", test_empty}};

int main(int argc, char **argv) {
    CHECK(argc <= 2, "expected at most one test case name");
    XcpSetLogLevel(0);
    CHECK(XcpInit("file_upload_test", "1.0", XCP_MODE_LOCAL), "XCP initialization failed");
    tQueueHandle queue = queueInit(16 * 1024);
    CHECK(queue != NULL, "could not allocate the test transmit queue");
    XcpStart(queue, false);
    command(CC_CONNECT, 0, CRC_CMD_OK);
    write_fixture(DATA_NAME ".a2l", DATA_SIZE);
    write_fixture(EMPTY_NAME ".a2l", 0);

    unsigned executed = 0;
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        if ((argc == 2) && (strcmp(argv[1], cases[i].name) != 0)) {
            continue;
        }
        printf("RUN file upload %s\n", cases[i].name);
        fflush(stdout);
        cases[i].run();
        puts("  PASSED");
        executed++;
    }
    CHECK(executed > 0, "unknown test case");
    CHECK(remove(DATA_NAME ".a2l") == 0, "could not remove the data fixture");
    CHECK(remove(EMPTY_NAME ".a2l") == 0, "could not remove the empty fixture");
    XcpDisconnect();
    XcpDeinit();
    queueDeinit(queue);
    return 0;
}
