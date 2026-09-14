// daq_config_test
// Tests dynamic DAQ configuration bounds and event-list linking through the XCP command processor.
// No network server or XCP client is required.
//
// Build:
//   cmake -B build -S . -DXCPLITE_BUILD_TESTS=ON
//   cmake --build build --target daq_config_test
// Run:
//   ctest --test-dir build -R daq_config --output-on-failure
//   ./build/daq_config_test <case>   # run one case for step-by-step debugging
//
// References: ASAM XCP Protocol Layer 1.4.0, printed page numbers.
// These fixtures use the default DAQ memory/DTO limits (3072/1024 bytes).
// CTest isolates each case and times it out, including cycles inside XcpEventExt().

#include <stdint.h> // for uintxx_t
#include <stdio.h>  // for printf
#include <stdlib.h> // for exit (checks must execute even with NDEBUG)
#include <string.h> // for memcpy

// Public XCPlite API
#include "xcplib.h" // for XcpEventExt, XcpSetLogLevel

// Internal interfaces used to exercise the protocol command path without a network server.
#include "queue.h"   // for the transmit queue
#include "xcp.h"     // for XCP commands and error codes
#include "xcp_cfg.h" // for dynamic address encoding
#include "xcplite.h" // for the protocol-layer interface

//-----------------------------------------------------------------------------------------------------
// Test configuration

#define TEST_QUEUE_SIZE ((size_t)16 * 1024)

typedef union {
    uint32_t words[XCPTL_MAX_CTO_SIZE / sizeof(uint32_t)];
    uint8_t bytes[XCPTL_MAX_CTO_SIZE];
} tTestCommand;

static tQueueHandle test_queue;
static tXcpEventId test_event;
static tXcpEventId other_event;

// Unlike assert(), CHECK always evaluates commands and initialization in Release builds.
#define CHECK(condition)                                                                                                                                                           \
    do {                                                                                                                                                                           \
        if (!(condition)) {                                                                                                                                                        \
            fprintf(stderr, "%s:%d: %s failed\n", __FILE__, __LINE__, #condition);                                                                                                 \
            exit(EXIT_FAILURE);                                                                                                                                                    \
        }                                                                                                                                                                          \
    } while (0)

//-----------------------------------------------------------------------------------------------------
// XCP command helpers

// Encode integers in the little-endian byte order used by this test target.
static void set_u16(uint8_t *p, uint16_t value) {
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}

static void set_u32(uint8_t *p, uint32_t value) {
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
}

static uint8_t run_command(const uint8_t *data, uint8_t size) {
    tTestCommand command = {0};
    memcpy(command.bytes, data, size);
    return XcpCommand(command.words, size);
}

// The following helpers construct complete XCP commands and execute them through XcpCommand().
static uint8_t free_daq(void) {
    const uint8_t command[] = {CC_FREE_DAQ};
    return run_command(command, sizeof(command));
}

static uint8_t alloc_daq(uint16_t count) {
    uint8_t command[CRO_ALLOC_DAQ_LEN] = {CC_ALLOC_DAQ};
    set_u16(&command[2], count);
    return run_command(command, sizeof(command));
}

static uint8_t alloc_odt(uint16_t daq, uint8_t count) {
    uint8_t command[CRO_ALLOC_ODT_LEN] = {CC_ALLOC_ODT};
    set_u16(&command[2], daq);
    command[4] = count;
    return run_command(command, sizeof(command));
}

static uint8_t alloc_odt_entry(uint16_t daq, uint8_t odt, uint8_t count) {
    uint8_t command[CRO_ALLOC_ODT_ENTRY_LEN] = {CC_ALLOC_ODT_ENTRY};
    set_u16(&command[2], daq);
    command[4] = odt;
    command[5] = count;
    return run_command(command, sizeof(command));
}

static uint8_t set_daq_ptr(uint16_t daq, uint8_t odt, uint8_t entry) {
    uint8_t command[CRO_SET_DAQ_PTR_LEN] = {CC_SET_DAQ_PTR};
    set_u16(&command[2], daq);
    command[4] = odt;
    command[5] = entry;
    return run_command(command, sizeof(command));
}

static uint8_t write_daq(uint8_t size, uint8_t ext, uint32_t addr) {
    uint8_t command[CRO_WRITE_DAQ_LEN] = {CC_WRITE_DAQ};
    command[1] = 0xFF; // Normal data element, not a bit element (7.5.4.2, p. 160).
    command[2] = size;
    command[3] = ext;
    set_u32(&command[4], addr);
    return run_command(command, sizeof(command));
}

static uint8_t set_daq_list_mode_priority(uint16_t daq, uint16_t event, uint8_t priority) {
    uint8_t command[CRO_SET_DAQ_LIST_MODE_LEN] = {CC_SET_DAQ_LIST_MODE};
    command[1] = DAQ_MODE_TIMESTAMP;
    set_u16(&command[2], daq);
    set_u16(&command[4], event);
    command[6] = 1;
    command[7] = priority;
    return run_command(command, sizeof(command));
}

static uint8_t set_daq_list_mode(uint16_t daq, uint16_t event) { return set_daq_list_mode_priority(daq, event, 0); }

static uint8_t start_stop_daq_list(uint16_t daq, uint8_t mode) {
    uint8_t command[CRO_START_STOP_DAQ_LIST_LEN] = {CC_START_STOP_DAQ_LIST};
    command[1] = mode;
    set_u16(&command[2], daq);
    return run_command(command, sizeof(command));
}

static uint8_t start_stop_synch(uint8_t mode) {
    const uint8_t command[CRO_START_STOP_SYNCH_LEN] = {CC_START_STOP_SYNCH, mode};
    return run_command(command, sizeof(command));
}

//-----------------------------------------------------------------------------------------------------
// Regression tests

// Retained for review, deliberately disabled: these are NOT our acceptance criteria.
// The first three cases require in-place rollback after CRC_MEMORY_OVERFLOW.
// XCP specification 1.4 section 4.1.6 (p. 19) invalidates the entire configuration; Table 232 (pp. 253-254)
// requires the master to reinitialize DAQ before retrying with smaller parameters.
// The fourth case requires immediate recovery from WRITE_DAQ CRC_DAQ_CONFIG.
// Table 232 (p. 249) specifies "display error", not a smaller retry at the same cursor.
// We still require safe rejection and recovery after FREE_DAQ, tested below.
#if 0
// An oversized ALLOC_DAQ must be rejected before writing the DAQ table.
// A valid allocation immediately afterwards verifies that the rejected command did not alter the state.
static void test_daq_allocation_rollback(void) {
    assert(alloc_daq(UINT16_MAX) == CRC_MEMORY_OVERFLOW);
    assert(alloc_daq(1) == CRC_CMD_OK);
    assert(free_daq() == CRC_CMD_OK);
}

// The first ODT allocation fits in the configured DAQ memory; the second does not.
// Retrying with a smaller count verifies that the rejected allocation did not change the total ODT count.
static void test_odt_allocation_rollback(void) {
    assert(alloc_daq(2) == CRC_CMD_OK);
    assert(alloc_odt(0, 200) == CRC_CMD_OK);
    assert(alloc_odt(1, 200) == CRC_MEMORY_OVERFLOW);
    assert(alloc_odt(1, 1) == CRC_CMD_OK);
    assert(free_daq() == CRC_CMD_OK);
}

// Fill most of the DAQ memory with ODT entries, provoke an overflow, then retry with one entry.
// The retry succeeds only if the rejected allocation preserved the previous ODT entry count.
static void test_odt_entry_allocation_rollback(void) {
    assert(alloc_daq(1) == CRC_CMD_OK);
    assert(alloc_odt(0, 3) == CRC_CMD_OK);
    assert(alloc_odt_entry(0, 0, 200) == CRC_CMD_OK);
    assert(alloc_odt_entry(0, 1, 200) == CRC_CMD_OK);
    assert(alloc_odt_entry(0, 2, 200) == CRC_MEMORY_OVERFLOW);
    assert(alloc_odt_entry(0, 2, 1) == CRC_CMD_OK);
    assert(free_daq() == CRC_CMD_OK);
}

// Four maximum-size entries fit into the first ODT. The fifth exceeds the DTO limit.
// A smaller entry must still fit afterwards, proving that the failed write did not increase the ODT size.
static void test_odt_size_rollback(tXcpEventId event) {
    assert(alloc_daq(1) == CRC_CMD_OK);
    assert(alloc_odt(0, 1) == CRC_CMD_OK);
    assert(alloc_odt_entry(0, 0, 5) == CRC_CMD_OK);
    assert(set_daq_ptr(0, 0, 0) == CRC_CMD_OK);

    uint32_t addr = XcpAddrEncodeDyn(0, event);
    for (uint8_t i = 0; i < 4; i++) {
        assert(write_daq(XCPTL_MAX_CTO_SIZE, XCP_ADDR_EXT_DYN, addr) == CRC_CMD_OK);
    }
    assert(write_daq(XCPTL_MAX_CTO_SIZE, XCP_ADDR_EXT_DYN, addr) == CRC_DAQ_CONFIG);
    assert(write_daq(24, XCP_ADDR_EXT_DYN, addr) == CRC_CMD_OK);
    assert(free_daq() == CRC_CMD_OK);
}

#endif

//-----------------------------------------------------------------------------------------------------
// Accepted tests and helpers

static uint8_t write_daq_multiple(uint8_t first_size, uint8_t second_size) {
    uint8_t command[CRO_WRITE_DAQ_MULTIPLE_LEN(2)] = {CC_WRITE_DAQ_MULTIPLE, 2};
    command[2] = command[10] = 0xFF;
    command[3] = first_size;
    command[11] = second_size;
    set_u32(&command[4], XcpAddrEncodeDyn(0, test_event));
    set_u32(&command[12], XcpAddrEncodeDyn(first_size, test_event));
    command[8] = command[16] = XCP_ADDR_EXT_DYN;
    return run_command(command, sizeof(command));
}

static void allocate_single_odt(uint8_t entries) {
    CHECK(alloc_daq(1) == CRC_CMD_OK);
    CHECK(alloc_odt(0, 1) == CRC_CMD_OK);
    CHECK(alloc_odt_entry(0, 0, entries) == CRC_CMD_OK);
    CHECK(set_daq_ptr(0, 0, 0) == CRC_CMD_OK);
}

static void start_list(uint16_t daq) {
    CHECK(set_daq_list_mode(daq, test_event) == CRC_CMD_OK);
    CHECK(start_stop_daq_list(daq, 2) == CRC_CMD_OK);
    CHECK(start_stop_synch(1) == CRC_CMD_OK);
    CHECK(XcpIsDaqRunning());
}

static void expect_empty_queue(void) { CHECK(queuePeek(test_queue, 0, NULL, NULL).buffer == NULL); }

// Verify identity, length and content, not merely the number of queued buffers.
// queuePeek includes the transport header space; the transport stub never fills it.
// All payloads used here are multiples of 4, so default queue padding adds no bytes.
static void expect_dto(uint16_t daq, const void *payload, uint16_t size) {
    tQueueBuffer dto = queuePeek(test_queue, 0, NULL, NULL);
    CHECK(dto.buffer != NULL);
    CHECK(dto.size == XCPTL_TRANSPORT_LAYER_HEADER_SIZE + 8 + size);
    const uint8_t *packet = dto.buffer + XCPTL_TRANSPORT_LAYER_HEADER_SIZE;
    CHECK(packet[0] == 0); // Relative ODT number; each fixture has one ODT per DAQ.
    uint16_t received_daq;
    memcpy(&received_daq, &packet[2], sizeof(received_daq));
    CHECK(received_daq == daq);
    CHECK(memcmp(&packet[8], payload, size) == 0); // Skip DAQ header and timestamp.
    queueRelease(test_queue, &dto);
}

static void configure_and_measure(void) {
    const uint32_t measurement = 0x12345678;
    allocate_single_odt(1);
    CHECK(write_daq(sizeof(measurement), XCP_ADDR_EXT_DYN, XcpAddrEncodeDyn(0, test_event)) == CRC_CMD_OK);
    start_list(0);
    XcpEventExt(test_event, (const uint8_t *)&measurement);
    expect_dto(0, &measurement, sizeof(measurement));
    expect_empty_queue();
    CHECK(start_stop_synch(0) == CRC_CMD_OK);
    CHECK(!XcpIsDaqRunning());
}

// Implementation policy, beyond the master's recovery obligation: clear all DAQs on
// allocation overflow or a WRITE_DAQ_MULTIPLE entry error. The spec calls
// the configuration invalid; clearing is our proposed small-diff way to make it unusable.
// Do not confuse this with disconnecting: these are S2 errors (5.3 p. 76, Table 228).
static void expect_configuration_cleared(void) {
    CHECK(XcpIsConnected());
    CHECK(!XcpIsDaqRunning());
    CHECK(start_stop_synch(1) == CRC_DAQ_CONFIG);
    CHECK(start_stop_daq_list(0, 2) == CRC_OUT_OF_RANGE);
    CHECK(set_daq_ptr(0, 0, 0) == CRC_OUT_OF_RANGE);
}

static void reinitialize_and_measure(void) {
    CHECK(free_daq() == CRC_CMD_OK);
    configure_and_measure(); // Prove recovery with actual data, not only positive CRMs.
}

// Baseline: a complete legal configuration must deliver the expected value once.
static void test_single_entry(void) { configure_and_measure(); }

// FREE_DAQ must remove old event links as well as allocation metadata. Reusing the
// same event and DAQ number must not retain a stale link or produce duplicate DTOs.
static void test_free_and_reconfigure(void) {
    configure_and_measure();
    reinitialize_and_measure();
}

// Clearing/reallocating DAQ must invalidate the old write cursor, even when the new
// allocation reuses the same indices. Reject a write until SET_DAQ_PTR is repeated;
// CRC_SEQUENCE uses SET_DAQ_PTR as its recovery pre-action (Table 232, p. 249).
static void test_daq_ptr_after_free(void) {
    const uint32_t measurement = 0x12345678;
    allocate_single_odt(1); // Establish a cursor that would fit the new allocation too.
    CHECK(free_daq() == CRC_CMD_OK);
    CHECK(alloc_daq(1) == CRC_CMD_OK);
    CHECK(alloc_odt(0, 1) == CRC_CMD_OK);
    CHECK(alloc_odt_entry(0, 0, 1) == CRC_CMD_OK);
    CHECK(write_daq(4, XCP_ADDR_EXT_DYN, XcpAddrEncodeDyn(0, test_event)) == CRC_SEQUENCE);
    CHECK(set_daq_ptr(0, 0, 0) == CRC_CMD_OK);
    CHECK(write_daq(4, XCP_ADDR_EXT_DYN, XcpAddrEncodeDyn(0, test_event)) == CRC_CMD_OK);
    start_list(0);
    XcpEventExt(test_event, (const uint8_t *)&measurement);
    expect_dto(0, &measurement, sizeof(measurement));
    expect_empty_queue();
}

// Legal allocation phases are FREE -> DAQ -> ODT -> ENTRY (4.1.6, pp. 19-20).
// After CRC_SEQUENCE the master reinitializes, rather than attempting in-place repair.
static void test_allocation_sequence(void) {
    CHECK(alloc_odt(0, 1) == CRC_SEQUENCE);
    CHECK(free_daq() == CRC_CMD_OK);
    CHECK(alloc_odt_entry(0, 0, 1) == CRC_SEQUENCE);
    CHECK(free_daq() == CRC_CMD_OK);
    CHECK(alloc_daq(1) == CRC_CMD_OK);
    CHECK(alloc_odt_entry(0, 0, 1) == CRC_SEQUENCE);
    CHECK(free_daq() == CRC_CMD_OK);
    allocate_single_odt(1);
    CHECK(alloc_odt(0, 1) == CRC_SEQUENCE);
    CHECK(free_daq() == CRC_CMD_OK);
    CHECK(alloc_daq(1) == CRC_CMD_OK);
    CHECK(alloc_odt(0, 1) == CRC_CMD_OK);
    CHECK(alloc_daq(1) == CRC_SEQUENCE);
    reinitialize_and_measure();
}

// Oversized allocation must not write outside the DAQ pool. A sanitizer can catch
// the access before a crash. Then verify our clear-on-error policy and spec recovery.
static void test_daq_allocation_overflow(void) {
    CHECK(alloc_daq(UINT16_MAX) == CRC_MEMORY_OVERFLOW);
    expect_configuration_cleared();
    reinitialize_and_measure();
}

// At the default 3072-byte pool, 24 + 200*8 fits; 24 + 400*8 does not.
// We do NOT retry ALLOC_ODT in the invalid old configuration.
static void test_odt_allocation_overflow(void) {
    CHECK(XCP_DAQ_MEM_SIZE == 3072);
    CHECK(alloc_daq(2) == CRC_CMD_OK);
    CHECK(alloc_odt(0, 200) == CRC_CMD_OK);
    CHECK(alloc_odt(1, 200) == CRC_MEMORY_OVERFLOW);
    expect_configuration_cleared();
    reinitialize_and_measure();
}

// 440 entries fit, 660 do not (5 or 6 bytes per entry). The failed allocation must
// leave the configuration unusable; recovery restarts all allocation phases.
static void test_odt_entry_allocation_overflow(void) {
    CHECK(XCP_DAQ_MEM_SIZE == 3072);
    CHECK(alloc_daq(1) == CRC_CMD_OK);
    CHECK(alloc_odt(0, 3) == CRC_CMD_OK);
    CHECK(alloc_odt_entry(0, 0, 220) == CRC_CMD_OK);
    CHECK(alloc_odt_entry(0, 1, 220) == CRC_CMD_OK);
    CHECK(alloc_odt_entry(0, 2, 220) == CRC_MEMORY_OVERFLOW);
    expect_configuration_cleared();
    reinitialize_and_measure();
}

// Four 248-byte entries fit; a fifth exceeds the 1016-byte first-ODT payload limit.
// Require rejection and recovery after FREE_DAQ. Do not require rollback or depend on
// the pointer's value following CRC_DAQ_CONFIG (Table 232, p. 249).
static void test_odt_size_overflow(void) {
    CHECK(XCPTL_MAX_DTO_SIZE == 1024);
    allocate_single_odt(5);
    for (unsigned i = 0; i < 4; i++)
        CHECK(write_daq(248, XCP_ADDR_EXT_DYN, XcpAddrEncodeDyn(0, test_event)) == CRC_CMD_OK);
    CHECK(write_daq(248, XCP_ADDR_EXT_DYN, XcpAddrEncodeDyn(0, test_event)) == CRC_DAQ_CONFIG);
    CHECK(XcpIsConnected());
    reinitialize_and_measure();
}

// Positive boundary: 4*248 + 24 bytes plus header/timestamp exactly fills MAX_DTO.
// It must still acquire a queue buffer and copy all configured bytes correctly.
static void test_dto_size_limit(void) {
    CHECK(XCPTL_MAX_DTO_SIZE == 1024);
    uint8_t measurement[1016];
    for (unsigned i = 0; i < sizeof(measurement); i++)
        measurement[i] = (uint8_t)i;
    allocate_single_odt(5);
    for (unsigned i = 0; i < 4; i++)
        CHECK(write_daq(248, XCP_ADDR_EXT_DYN, XcpAddrEncodeDyn(i * 248, test_event)) == CRC_CMD_OK);
    CHECK(write_daq(24, XCP_ADDR_EXT_DYN, XcpAddrEncodeDyn(992, test_event)) == CRC_CMD_OK);
    start_list(0);
    XcpEventExt(test_event, measurement);
    expect_dto(0, measurement, sizeof(measurement));
    expect_empty_queue();
}

// SET_DAQ_PTR permits writing a chosen entry again. Replacing its size must not
// add the old size to the DTO length; check both final data and packet length.
static void test_rewrite_entry_size(void) {
    const uint32_t measurement[2] = {0x12345678, 0xABCDEF01};
    allocate_single_odt(1);
    CHECK(write_daq(4, XCP_ADDR_EXT_DYN, XcpAddrEncodeDyn(0, test_event)) == CRC_CMD_OK);
    CHECK(set_daq_ptr(0, 0, 0) == CRC_CMD_OK);
    CHECK(write_daq(8, XCP_ADDR_EXT_DYN, XcpAddrEncodeDyn(0, test_event)) == CRC_CMD_OK);
    start_list(0);
    XcpEventExt(test_event, (const uint8_t *)measurement);
    expect_dto(0, measurement, sizeof(measurement));
    expect_empty_queue();
}

// A lost WRITE_DAQ response is recovered with SYNCH + SET_DAQ_PTR + retry
// (Table 232, p. 248). Model the repeated write and verify it does not enlarge the DTO.
static void test_rewrite_retry(void) {
    const uint32_t measurement = 0x12345678;
    allocate_single_odt(1);
    CHECK(write_daq(4, XCP_ADDR_EXT_DYN, XcpAddrEncodeDyn(0, test_event)) == CRC_CMD_OK);
    CHECK(set_daq_ptr(0, 0, 0) == CRC_CMD_OK);
    CHECK(write_daq(4, XCP_ADDR_EXT_DYN, XcpAddrEncodeDyn(0, test_event)) == CRC_CMD_OK);
    start_list(0);
    XcpEventExt(test_event, (const uint8_t *)&measurement);
    expect_dto(0, &measurement, sizeof(measurement));
    expect_empty_queue();
}

// Replacement can shrink an entry and change its address. Verify the negative size
// adjustment and that the copied value comes from the replacement address.
static void test_rewrite_shrink(void) {
    const uint32_t measurement[2] = {0x12345678, 0xABCDEF01};
    allocate_single_odt(1);
    CHECK(write_daq(8, XCP_ADDR_EXT_DYN, XcpAddrEncodeDyn(0, test_event)) == CRC_CMD_OK);
    CHECK(set_daq_ptr(0, 0, 0) == CRC_CMD_OK);
    CHECK(write_daq(4, XCP_ADDR_EXT_DYN, XcpAddrEncodeDyn(4, test_event)) == CRC_CMD_OK);
    start_list(0);
    XcpEventExt(test_event, (const uint8_t *)measurement);
    expect_dto(0, &measurement[1], sizeof(measurement[1]));
    expect_empty_queue();
}

// Implementation policy: a failed single-entry replacement leaves the previously
// valid configuration intact. This tests validate-before-commit, beyond the XCP
// recovery requirement; it does not reinstate the rejected smaller-retry requirement.
static void test_rewrite_overflow(void) {
    CHECK(XCPTL_MAX_DTO_SIZE == 1024);
    uint8_t measurement[1016];
    for (unsigned i = 0; i < sizeof(measurement); i++)
        measurement[i] = (uint8_t)i;
    allocate_single_odt(5);
    for (unsigned i = 0; i < 4; i++)
        CHECK(write_daq(248, XCP_ADDR_EXT_DYN, XcpAddrEncodeDyn(i * 248, test_event)) == CRC_CMD_OK);
    CHECK(write_daq(24, XCP_ADDR_EXT_DYN, XcpAddrEncodeDyn(992, test_event)) == CRC_CMD_OK);
    CHECK(set_daq_ptr(0, 0, 4) == CRC_CMD_OK);
    CHECK(write_daq(28, XCP_ADDR_EXT_DYN, XcpAddrEncodeDyn(0, test_event)) == CRC_DAQ_CONFIG);
    start_list(0);
    XcpEventExt(test_event, measurement);
    expect_dto(0, measurement, sizeof(measurement));
    expect_empty_queue();
}

// SET_DAQ_PTR may visit entries out of order. A temporary gap during configuration
// is harmless if filled before acquisition. DTO data must follow entry order, not
// the order of WRITE_DAQ commands; each entry here is written exactly once.
static void test_fill_gap(void) {
    const uint32_t measurement[3] = {0x12345678, 0xABCDEF01, 0x76543210};
    allocate_single_odt(3);
    CHECK(set_daq_ptr(0, 0, 2) == CRC_CMD_OK);
    CHECK(write_daq(4, XCP_ADDR_EXT_DYN, XcpAddrEncodeDyn(8, test_event)) == CRC_CMD_OK);
    CHECK(set_daq_ptr(0, 0, 0) == CRC_CMD_OK);
    CHECK(write_daq(4, XCP_ADDR_EXT_DYN, XcpAddrEncodeDyn(0, test_event)) == CRC_CMD_OK);
    CHECK(write_daq(4, XCP_ADDR_EXT_DYN, XcpAddrEncodeDyn(4, test_event)) == CRC_CMD_OK);
    start_list(0);
    XcpEventExt(test_event, (const uint8_t *)measurement);
    expect_dto(0, measurement, sizeof(measurement));
    expect_empty_queue();
}

// Current XCPlite TEST_CHECKS policy: all allocated entries must be populated.
// Prepare detects a middle gap; filling it allows preparation and acquisition.
// This is stricter than XCP's zero-size termination option (4.1.12, p. 30).
// Direct-start validation is covered separately by start_incomplete.
static void test_gap_prepare(void) {
#ifdef XCP_ENABLE_TEST_CHECKS
    const uint32_t measurement[3] = {0x12345678, 0xABCDEF01, 0x76543210};
    allocate_single_odt(3);
    CHECK(write_daq(4, XCP_ADDR_EXT_DYN, XcpAddrEncodeDyn(0, test_event)) == CRC_CMD_OK);
    CHECK(set_daq_ptr(0, 0, 2) == CRC_CMD_OK);
    CHECK(write_daq(4, XCP_ADDR_EXT_DYN, XcpAddrEncodeDyn(8, test_event)) == CRC_CMD_OK);
    CHECK(set_daq_list_mode(0, test_event) == CRC_CMD_OK);
    CHECK(start_stop_daq_list(0, 2) == CRC_CMD_OK);
    CHECK(start_stop_synch(3) == CRC_DAQ_CONFIG);
    CHECK(!XcpIsDaqRunning());
    expect_empty_queue();
    CHECK(set_daq_ptr(0, 0, 1) == CRC_CMD_OK);
    CHECK(write_daq(4, XCP_ADDR_EXT_DYN, XcpAddrEncodeDyn(4, test_event)) == CRC_CMD_OK);
    CHECK(start_stop_synch(3) == CRC_CMD_OK);
    CHECK(start_stop_synch(1) == CRC_CMD_OK);
    XcpEventExt(test_event, (const uint8_t *)measurement);
    expect_dto(0, measurement, sizeof(measurement));
    expect_empty_queue();
#else
    puts("SKIP: gap_prepare requires XCP_ENABLE_TEST_CHECKS");
#endif
}

// Positive batch case: two entries within one ODT, followed by a measured DTO.
static void test_write_multiple_success(void) {
    const uint32_t measurement[2] = {0x12345678, 0xABCDEF01};
    allocate_single_odt(2);
    CHECK(write_daq_multiple(4, 4) == CRC_CMD_OK);
    start_list(0);
    XcpEventExt(test_event, (const uint8_t *)measurement);
    expect_dto(0, measurement, sizeof(measurement));
    expect_empty_queue();
}

// The first entry is valid; the second exceeds the advertised 248-byte entry limit.
// Section 7.5.4.6 (p. 168) invalidates the whole configuration after a batch entry
// error. Test clear-on-error, not restoration of the first entry or cursor rollback.
static void test_write_multiple_error(void) {
    allocate_single_odt(2);
    CHECK(write_daq_multiple(4, 249) == CRC_OUT_OF_RANGE);
    expect_configuration_cleared();
    reinitialize_and_measure();
}

// The response does not identify which batch entry failed (7.5.4.6, p. 168).
// Apply clear-on-entry-error even when the first element fails, so recovery does
// not depend on how far the slave got through the batch.
static void test_write_multiple_first_error(void) {
    allocate_single_odt(2);
    CHECK(write_daq_multiple(249, 4) == CRC_OUT_OF_RANGE);
    expect_configuration_cleared();
    reinitialize_and_measure();
}

// A batch must stay within one ODT (7.5.4.6, p. 168), even when another allocated
// ODT follows it. The first write succeeds; the second must fail instead of spilling
// into the next ODT. Discard the partially written configuration and reinitialize.
static void test_write_multiple_odt_boundary(void) {
    CHECK(alloc_daq(1) == CRC_CMD_OK);
    CHECK(alloc_odt(0, 2) == CRC_CMD_OK);
    CHECK(alloc_odt_entry(0, 0, 1) == CRC_CMD_OK);
    CHECK(alloc_odt_entry(0, 1, 1) == CRC_CMD_OK);
    CHECK(set_daq_ptr(0, 0, 0) == CRC_CMD_OK);
    CHECK(write_daq_multiple(4, 4) == CRC_OUT_OF_RANGE);
    expect_configuration_cleared();
    reinitialize_and_measure();
}

// Individual sizes and entry indices are valid, but the second batch element takes
// the ODT payload from 992 to 1020 bytes, exceeding its 1016-byte limit. Preserve the
// CRC_DAQ_CONFIG response while clearing all DAQs, not only the offending ODT.
static void test_write_multiple_dto_overflow(void) {
    CHECK(XCPTL_MAX_DTO_SIZE == 1024);
    allocate_single_odt(5);
    for (unsigned i = 0; i < 3; i++)
        CHECK(write_daq(248, XCP_ADDR_EXT_DYN, XcpAddrEncodeDyn(0, test_event)) == CRC_CMD_OK);
    CHECK(write_daq_multiple(248, 28) == CRC_DAQ_CONFIG);
    expect_configuration_cleared();
    reinitialize_and_measure();
}

// Explicit DAQ, ODT and entry indices outside allocated ranges must be rejected.
// This does not make assumptions about the cursor after a failed SET_DAQ_PTR.
static void test_daq_ptr_bounds(void) {
    allocate_single_odt(1);
    CHECK(set_daq_ptr(1, 0, 0) == CRC_OUT_OF_RANGE);
    CHECK(set_daq_ptr(0, 1, 0) == CRC_OUT_OF_RANGE);
    CHECK(set_daq_ptr(0, 0, 1) == CRC_OUT_OF_RANGE);
    CHECK(set_daq_ptr(0, 0, 0) == CRC_CMD_OK);
}

// Unlike CRC_DAQ_CONFIG, CRC_OUT_OF_RANGE has "retry other parameter" recovery
// (Table 232, p. 249). Reject an oversized element, then write a valid size and
// verify that the rejection did not advance the cursor or damage the configuration.
static void test_invalid_entry_size(void) {
    const uint32_t measurement = 0x12345678;
    allocate_single_odt(1);
    CHECK(write_daq(249, XCP_ADDR_EXT_DYN, XcpAddrEncodeDyn(0, test_event)) == CRC_OUT_OF_RANGE);
    CHECK(write_daq(4, XCP_ADDR_EXT_DYN, XcpAddrEncodeDyn(0, test_event)) == CRC_CMD_OK);
    start_list(0);
    XcpEventExt(test_event, (const uint8_t *)&measurement);
    expect_dto(0, &measurement, sizeof(measurement));
    expect_empty_queue();
}

// A rejected command while acquiring must not clear the active configuration.
// In particular, do not implement clear-on-error in the generic error-response path.
static void test_write_while_running(void) {
    const uint32_t measurement = 0x12345678;
    allocate_single_odt(1);
    CHECK(write_daq(4, XCP_ADDR_EXT_DYN, XcpAddrEncodeDyn(0, test_event)) == CRC_CMD_OK);
    CHECK(set_daq_ptr(0, 0, 0) == CRC_CMD_OK);
    start_list(0);
    CHECK(write_daq(4, XCP_ADDR_EXT_DYN, XcpAddrEncodeDyn(0, test_event)) == CRC_DAQ_ACTIVE);
    CHECK(write_daq_multiple(4, 4) == CRC_DAQ_ACTIVE);
    // Even an invalid first entry must not turn a running-command rejection into
    // configuration invalidation: the DAQ-active check precedes entry validation.
    CHECK(write_daq_multiple(249, 4) == CRC_DAQ_ACTIVE);
    CHECK(alloc_daq(1) == CRC_DAQ_ACTIVE);
    CHECK(XcpIsDaqRunning());
    XcpEventExt(test_event, (const uint8_t *)&measurement);
    expect_dto(0, &measurement, sizeof(measurement));
    expect_empty_queue();
}

// One event may serve two DAQs. Check each DAQ ID and its distinct value, so two
// copies of the same DAQ cannot pass. Optionally repeat head and tail associations to
// exercise the original test's multi-node cycle scenario as a separate case.
static void measure_shared_event(bool repeat_association) {
    const uint32_t measurement[2] = {0x12345678, 0xABCDEF01};
    CHECK(alloc_daq(2) == CRC_CMD_OK);
    CHECK(alloc_odt(0, 1) == CRC_CMD_OK);
    CHECK(alloc_odt(1, 1) == CRC_CMD_OK);
    CHECK(alloc_odt_entry(0, 0, 1) == CRC_CMD_OK);
    CHECK(alloc_odt_entry(1, 0, 1) == CRC_CMD_OK);
    for (uint16_t daq = 0; daq < 2; daq++) {
        CHECK(set_daq_ptr(daq, 0, 0) == CRC_CMD_OK);
        CHECK(write_daq(4, XCP_ADDR_EXT_DYN, XcpAddrEncodeDyn(daq * 4, test_event)) == CRC_CMD_OK);
        CHECK(set_daq_list_mode(daq, test_event) == CRC_CMD_OK);
        CHECK(start_stop_daq_list(daq, 2) == CRC_CMD_OK);
    }
    if (repeat_association) {
        for (unsigned i = 0; i < 3; i++) {
            CHECK(set_daq_list_mode(0, test_event) == CRC_CMD_OK);
            CHECK(set_daq_list_mode(1, test_event) == CRC_CMD_OK);
        }
    }
    CHECK(start_stop_synch(1) == CRC_CMD_OK);
    XcpEventExt(test_event, (const uint8_t *)measurement);
    // Do not prescribe ordering between DAQs of equal priority.
    bool seen[2] = {false, false};
    for (unsigned i = 0; i < 2; i++) {
        tQueueBuffer dto = queuePeek(test_queue, 0, NULL, NULL);
        CHECK(dto.buffer != NULL && dto.size >= XCPTL_TRANSPORT_LAYER_HEADER_SIZE + 4);
        uint16_t daq;
        memcpy(&daq, dto.buffer + XCPTL_TRANSPORT_LAYER_HEADER_SIZE + 2, sizeof(daq));
        CHECK(daq < 2 && !seen[daq]);
        seen[daq] = true;
        expect_dto(daq, &measurement[daq], sizeof(measurement[daq]));
    }
    expect_empty_queue();
}

// Basic multi-DAQ append, without any repeated SET_DAQ_LIST_MODE.
static void test_shared_event(void) { measure_shared_event(false); }

// Re-appending the head can create a multi-node cycle; re-appending the tail can
// create a self-cycle. Both DAQs must still appear exactly once in the event.
static void test_repeated_shared_association(void) { measure_shared_event(true); }

// SET_DAQ_LIST_MODE can be repeated (including master retry after a timeout,
// Table 232, p. 249). Repetition must neither append a duplicate nor create a cycle.
// A repeated command must also apply a changed priority: verify its queue flush request.
static void test_repeated_association(void) {
    const uint32_t measurement = 0x12345678;
    allocate_single_odt(1);
    CHECK(write_daq(4, XCP_ADDR_EXT_DYN, XcpAddrEncodeDyn(0, test_event)) == CRC_CMD_OK);
    CHECK(set_daq_list_mode(0, test_event) == CRC_CMD_OK);
    CHECK(set_daq_list_mode_priority(0, test_event, 1) == CRC_CMD_OK);
    CHECK(start_stop_daq_list(0, 2) == CRC_CMD_OK);
    CHECK(start_stop_synch(1) == CRC_CMD_OK);
    XcpEventExt(test_event, (const uint8_t *)&measurement);
    bool flush_requested = false;
    CHECK(queuePeek(test_queue, 0, NULL, &flush_requested).buffer != NULL);
    CHECK(flush_requested);
    expect_dto(0, &measurement, sizeof(measurement));
    expect_empty_queue();
}

// XCPlite-specific DYN addressing binds an entry to its encoded event. Reject a
// conflicting association, then accept the original event and acquire correctly.
static void test_event_mismatch(void) {
    const uint32_t measurement = 0x12345678;
    allocate_single_odt(1);
    CHECK(write_daq(4, XCP_ADDR_EXT_DYN, XcpAddrEncodeDyn(0, test_event)) == CRC_CMD_OK);
    CHECK(set_daq_list_mode(0, other_event) == CRC_DAQ_CONFIG);
    start_list(0);
    XcpEventExt(other_event, (const uint8_t *)&measurement);
    expect_empty_queue();
    XcpEventExt(test_event, (const uint8_t *)&measurement);
    expect_dto(0, &measurement, sizeof(measurement));
    expect_empty_queue();
}

// XCPlite TEST_CHECKS policy requires all allocated entries to be populated.
// Prepare is optional (7.5.4.5, p. 166): direct start must detect a leading, middle
// or trailing gap. Rejection must preserve the configuration and selection, so
// filling the gap and retrying start works without FREE_DAQ or another select.
static void test_start_incomplete(void) {
#ifdef XCP_ENABLE_TEST_CHECKS
    const uint32_t measurement[3] = {0x12345678, 0xABCDEF01, 0x76543210};
    for (uint8_t gap = 0; gap < 3; gap++) {
        CHECK(free_daq() == CRC_CMD_OK);
        allocate_single_odt(3);
        for (uint8_t entry = 0; entry < 3; entry++) {
            if (entry == gap)
                continue;
            CHECK(set_daq_ptr(0, 0, entry) == CRC_CMD_OK);
            CHECK(write_daq(4, XCP_ADDR_EXT_DYN, XcpAddrEncodeDyn(entry * 4, test_event)) == CRC_CMD_OK);
        }
        CHECK(set_daq_list_mode(0, test_event) == CRC_CMD_OK);
        CHECK(start_stop_daq_list(0, 2) == CRC_CMD_OK);
        CHECK(start_stop_synch(1) == CRC_DAQ_CONFIG);
        CHECK(!XcpIsDaqRunning());
        CHECK(XcpIsConnected());
        expect_empty_queue();
        CHECK(set_daq_ptr(0, 0, gap) == CRC_CMD_OK);
        CHECK(write_daq(4, XCP_ADDR_EXT_DYN, XcpAddrEncodeDyn(gap * 4, test_event)) == CRC_CMD_OK);
        CHECK(start_stop_synch(1) == CRC_CMD_OK);
        CHECK(XcpIsDaqRunning());
        XcpEventExt(test_event, (const uint8_t *)measurement);
        expect_dto(0, measurement, sizeof(measurement));
        expect_empty_queue();
        CHECK(start_stop_synch(0) == CRC_CMD_OK);
    }
#else
    puts("SKIP: start_incomplete requires XCP_ENABLE_TEST_CHECKS");
#endif
}

// Check only selected DAQs: an incomplete, unselected DAQ must not block a valid
// selection. Selecting both must reject the entire start before either DAQ runs.
static void test_start_selected_only(void) {
#ifdef XCP_ENABLE_TEST_CHECKS
    const uint32_t measurement = 0x12345678;
    CHECK(alloc_daq(2) == CRC_CMD_OK);
    CHECK(alloc_odt(0, 1) == CRC_CMD_OK);
    CHECK(alloc_odt(1, 1) == CRC_CMD_OK);
    CHECK(alloc_odt_entry(0, 0, 1) == CRC_CMD_OK);
    CHECK(alloc_odt_entry(1, 0, 1) == CRC_CMD_OK);
    CHECK(set_daq_ptr(0, 0, 0) == CRC_CMD_OK);
    CHECK(write_daq(4, XCP_ADDR_EXT_DYN, XcpAddrEncodeDyn(0, test_event)) == CRC_CMD_OK);
    CHECK(set_daq_list_mode(0, test_event) == CRC_CMD_OK);
    CHECK(set_daq_list_mode(1, test_event) == CRC_CMD_OK);
    CHECK(start_stop_daq_list(0, 2) == CRC_CMD_OK);
    CHECK(start_stop_synch(1) == CRC_CMD_OK);
    XcpEventExt(test_event, (const uint8_t *)&measurement);
    expect_dto(0, &measurement, sizeof(measurement));
    expect_empty_queue();
    CHECK(start_stop_synch(0) == CRC_CMD_OK);
    CHECK(start_stop_daq_list(0, 2) == CRC_CMD_OK);
    CHECK(start_stop_daq_list(1, 2) == CRC_CMD_OK);
    CHECK(start_stop_synch(1) == CRC_DAQ_CONFIG);
    CHECK(!XcpIsDaqRunning());
    XcpEventExt(test_event, (const uint8_t *)&measurement);
    expect_empty_queue();
#else
    puts("SKIP: start_selected_only requires XCP_ENABLE_TEST_CHECKS");
#endif
}

//-----------------------------------------------------------------------------------------------------
// Named cases let CTest run each in a fresh process with a timeout. Direct execution
// without arguments also runs all cases, but stops at the first failure.

typedef struct {
    const char *name;
    void (*run)(void);
} tTestCase;

static const tTestCase cases[] = {
    {"single_entry", test_single_entry},
    {"free_and_reconfigure", test_free_and_reconfigure},
    {"daq_ptr_after_free", test_daq_ptr_after_free},
    {"allocation_sequence", test_allocation_sequence},
    {"daq_allocation_overflow", test_daq_allocation_overflow},
    {"odt_allocation_overflow", test_odt_allocation_overflow},
    {"odt_entry_allocation_overflow", test_odt_entry_allocation_overflow},
    {"odt_size_overflow", test_odt_size_overflow},
    {"dto_size_limit", test_dto_size_limit},
    {"rewrite_entry_size", test_rewrite_entry_size},
    {"rewrite_retry", test_rewrite_retry},
    {"rewrite_shrink", test_rewrite_shrink},
    {"rewrite_overflow", test_rewrite_overflow},
    {"fill_gap", test_fill_gap},
    {"gap_prepare", test_gap_prepare},
    {"write_multiple_success", test_write_multiple_success},
    {"write_multiple_error", test_write_multiple_error},
    {"write_multiple_first_error", test_write_multiple_first_error},
    {"write_multiple_odt_boundary", test_write_multiple_odt_boundary},
    {"write_multiple_dto_overflow", test_write_multiple_dto_overflow},
    {"daq_ptr_bounds", test_daq_ptr_bounds},
    {"invalid_entry_size", test_invalid_entry_size},
    {"write_while_running", test_write_while_running},
    {"shared_event", test_shared_event},
    {"repeated_association", test_repeated_association},
    {"repeated_shared_association", test_repeated_shared_association},
    {"event_mismatch", test_event_mismatch},
    {"start_incomplete", test_start_incomplete},
    {"start_selected_only", test_start_selected_only},
};

int main(int argc, char **argv) {
    CHECK(argc <= 2);
    if (argc == 2 && strcmp(argv[1], "--list") == 0) {
        for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
            puts(cases[i].name);
        return EXIT_SUCCESS;
    }
    XcpSetLogLevel(0);
    CHECK(XcpInit("daq_config_test", "1.0", XCP_MODE_LOCAL));
    test_event = XcpCreateEvent("test_event", 1000000, 0);
    other_event = XcpCreateEvent("other_event", 1000000, 0);
    CHECK(test_event != XCP_UNDEFINED_EVENT_ID && other_event != XCP_UNDEFINED_EVENT_ID);
    test_queue = queueInit(TEST_QUEUE_SIZE);
    CHECK(test_queue != NULL);
    XcpStart(test_queue, false);
    const uint8_t connect[] = {CC_CONNECT, 0};
    CHECK(run_command(connect, sizeof(connect)) == CRC_CMD_OK);

    unsigned executed = 0;
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        if (argc == 2 && strcmp(argv[1], cases[i].name) != 0)
            continue;
        CHECK(free_daq() == CRC_CMD_OK); // Required start of every dynamic configuration.
        expect_empty_queue();
        printf("RUN %s\n", cases[i].name);
        fflush(stdout);
        cases[i].run();
        // All successful DTO cases consumed their queue before stopping.
        CHECK(free_daq() == CRC_CMD_OK);
        printf("  PASSED\n");
        executed++;
    }
    CHECK(executed != 0); // Unknown case name must not silently pass.
    XcpDeinit();
    queueDeinit(test_queue);
    return EXIT_SUCCESS;
}
