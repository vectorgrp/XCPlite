// File upload lifecycle regression tests. No network peer is required.
// Run: ctest --test-dir build -R '^file_upload_test$' -V
// Run one case: ./build/file_upload_test offsets (or restart, empty, reconnect, disconnect, deinit, io_failures).
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
static unsigned fail_seek_after;
static bool fail_next_tell;
static bool force_short_read;
static tQueueHandle test_queue;

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

static int tracked_fseek(FILE *file, long offset, int origin) {
    if ((fail_seek_after > 0) && (--fail_seek_after == 0)) {
        return -1;
    }
    return fseek(file, offset, origin);
}

static long tracked_ftell(FILE *file) {
    if (fail_next_tell) {
        fail_next_tell = false;
        return -1L;
    }
    return ftell(file);
}

static size_t tracked_fread(void *data, size_t size, size_t count, FILE *file) {
    if (force_short_read && (count > 0)) {
        force_short_read = false;
        count--;
    }
    return fread(data, size, count, file);
}

// Instrument the production file operations so leaks are detected on every platform,
// including Release builds where the original restart assertion is disabled.
#define fopen tracked_fopen
#define fclose tracked_fclose
#define fseek tracked_fseek
#define ftell tracked_ftell
#define fread tracked_fread
#include "xcpappl.c"
#undef fopen
#undef fclose
#undef fseek
#undef ftell
#undef fread

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

static void execute(const tXcpCto *request, uint8_t length, uint8_t expected) {
    response_count = 0;
    uint8_t result = XcpCommand(request->dw, length);
    CHECK(result == expected, "command 0x%02X: expected result 0x%02X, got 0x%02X", (unsigned)request->b[0], (unsigned)expected, (unsigned)result);
    CHECK(response_count == 1, "expected one command response, got %u", response_count);
    CHECK(response_length > 0, "empty command response");
    if (expected == CRC_CMD_OK) {
        CHECK(response.b[0] == PID_RES, "expected a positive response to command 0x%02X", (unsigned)request->b[0]);
    } else {
        CHECK((response_length == 2) && (response.b[0] == PID_ERR) && (response.b[1] == expected), "expected error response 0x%02X", (unsigned)expected);
    }
}

static void command(uint8_t code, uint8_t argument, uint8_t expected) {
    const tXcpCto request = {.b = {code, argument}};
    execute(&request, 2, expected);
}

static void set_file_offset(uint32_t offset) {
    tXcpCto request = {.b = {CC_SET_MTA, 0, 0, XCP_ADDR_EXT_FILE}};
    memcpy(&request.b[4], &offset, sizeof(offset));
    execute(&request, CRO_SET_MTA_LEN, CRC_CMD_OK);
}

static void end_session(void) {
    command(CC_DISCONNECT, 0, CRC_CMD_OK);
    CHECK(open_upload_files == 0, "disconnect leaked %u upload handles", open_upload_files);
    command(CC_CONNECT, 0, CRC_CMD_OK);
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
    CHECK(open_upload_files == 1, "a completed upload must remain available for retries");
    end_session();
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
    end_session();
}

static void test_offsets(void) {
    start_upload(DATA_NAME, DATA_SIZE);
    expect_upload(file_data, 4);
    set_file_offset(0);
    expect_upload(file_data, 4); // Retry the first block.
    set_file_offset(DATA_SIZE - 4);
    expect_upload(file_data + DATA_SIZE - 4, 4);
    set_file_offset(DATA_SIZE - 4);
    expect_upload(file_data + DATA_SIZE - 4, 4); // Retry the last block after EOF.
    set_file_offset(4);
    expect_upload(file_data + 4, 4); // Read out of order.

    set_file_offset(UINT32_MAX - 1);
    command(CC_UPLOAD, 4, CRC_ACCESS_DENIED); // The bounds check must not overflow.
    set_file_offset(DATA_SIZE - 1);
    command(CC_UPLOAD, 2, CRC_ACCESS_DENIED);
    CHECK(open_upload_files == 1, "an out-of-range request must not invalidate a valid upload");
    set_file_offset(0);
    expect_upload(file_data, DATA_SIZE);
    end_session();
}

static void test_disconnect(void) {
    start_upload(DATA_NAME, DATA_SIZE);
    expect_upload(file_data, 4);
    end_session();
    command(CC_UPLOAD, 1, CRC_ACCESS_DENIED); // A new session cannot read the previous upload.
    start_upload(DATA_NAME, DATA_SIZE);
    expect_upload(file_data, DATA_SIZE);
    end_session();
}

static bool reject_connect(uint8_t mode) {
    (void)mode;
    return false;
}

static void test_reconnect(void) {
    start_upload(DATA_NAME, DATA_SIZE);
    expect_upload(file_data, 4);

    // A refused connection must not discard the current session's upload.
    ApplXcpRegisterConnectCallback(reject_connect);
    const tXcpCto request = {.b = {CC_CONNECT, 0}};
    response_count = 0;
    CHECK(XcpCommand(request.dw, CRO_CONNECT_LEN) == CRC_CMD_OK, "refused CONNECT returned an unexpected result");
    CHECK(response_count == 0, "a refused CONNECT should be ignored");
    ApplXcpRegisterConnectCallback(NULL);
    CHECK(open_upload_files == 1, "a refused CONNECT closed the active upload");
    expect_upload(file_data + 4, 4);

    command(CC_CONNECT, 0, CRC_CMD_OK);
    CHECK(open_upload_files == 0, "accepted CONNECT retained the previous session's upload");
    command(CC_UPLOAD, 1, CRC_ACCESS_DENIED);
    start_upload(DATA_NAME, DATA_SIZE);
    expect_upload(file_data, DATA_SIZE);
    end_session();
}

static void test_deinit(void) {
    start_upload(DATA_NAME, DATA_SIZE);
    expect_upload(file_data, 4);
    XcpDeinit(); // Direct deinitialization must clean up without a preceding disconnect.
    CHECK(open_upload_files == 0, "deinitialization leaked %u upload handles", open_upload_files);
    CHECK(XcpInit("file_upload_test", "1.0", XCP_MODE_LOCAL), "XCP reinitialization failed");
    XcpStart(test_queue, false);
    command(CC_CONNECT, 0, CRC_CMD_OK);
    start_upload(DATA_NAME, DATA_SIZE);
    expect_upload(file_data, DATA_SIZE);
    end_session();
}

static void test_io_failures(void) {
    fail_seek_after = 1;
    start_upload(DATA_NAME, 0); // Failed seek to determine the file length.
    fail_seek_after = 2;
    start_upload(DATA_NAME, 0); // Failed rewind after determining the length.
    fail_next_tell = true;
    start_upload(DATA_NAME, 0);

    start_upload(DATA_NAME, DATA_SIZE);
    fail_seek_after = 1;
    command(CC_UPLOAD, 4, CRC_ACCESS_DENIED);
    CHECK(open_upload_files == 0, "a failed read seek leaked an upload handle");

    start_upload(DATA_NAME, DATA_SIZE);
    force_short_read = true;
    command(CC_UPLOAD, 4, CRC_ACCESS_DENIED);
    CHECK(open_upload_files == 0, "a short read leaked an upload handle");
    start_upload(DATA_NAME, DATA_SIZE);
    expect_upload(file_data, DATA_SIZE);
    end_session();
}

static const struct {
    const char *name;
    void (*run)(void);
} cases[] = {
    {"restart", test_restart}, {"empty", test_empty}, {"offsets", test_offsets},
    {"reconnect", test_reconnect}, {"disconnect", test_disconnect}, {"deinit", test_deinit}, {"io_failures", test_io_failures},
};

int main(int argc, char **argv) {
    CHECK(argc <= 2, "expected at most one test case name");
    XcpSetLogLevel(0);
    CHECK(XcpInit("file_upload_test", "1.0", XCP_MODE_LOCAL), "XCP initialization failed");
    test_queue = queueInit(16 * 1024);
    CHECK(test_queue != NULL, "could not allocate the test transmit queue");
    XcpStart(test_queue, false);
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
    queueDeinit(test_queue);
    return 0;
}
