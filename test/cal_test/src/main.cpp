// cal_test xcplib example - Multi-threaded calibration segment access test

#include <atomic>   // for std::atomic
#include <cassert>  // for assert
#include <cstdint>  // for uintxx_t
#include <cstring>  // for memset
#include <iostream> // for std::cout
#include <memory>   // for std::unique_ptr
#include <thread>   // for std::thread
#include <vector>

#include "a2l.hpp"    // for xcplib A2l generation application programming interface
#include "xcplib.hpp" // for xcplib application programming interface

#include "dbg_print.h"
#include "xcp_cfg.h"

// Internally used XCP functions for testing
extern "C" {
uint8_t XcpWriteMta(uint8_t size, const uint8_t *data);
uint8_t XcpSetMta(uint8_t ext, uint32_t addr);
uint8_t XcpCalSegCommand(uint8_t cmd);
uint8_t XcpCalSegSetCalPage(uint8_t segment, uint8_t page, uint8_t mode);
}

//-----------------------------------------------------------------------------------------------------
// XCP parameters

#define OPTION_PROJECT_NAME "cal_test"  // A2L project name
#define OPTION_PROJECT_EPK __TIME__     // EPK version string
#define OPTION_USE_TCP false            // TCP or UDP
#define OPTION_SERVER_PORT 5555         // Port
#define OPTION_SERVER_ADDR {0, 0, 0, 0} // Bind addr, 0.0.0.0 = ANY
#define OPTION_QUEUE_SIZE (1024 * 256)  // Size of the measurement queue in bytes, must be a multiple of 8
#define OPTION_LOG_LEVEL 3              // Log level, 0 = no log, 1 = error, 2 = warning, 3 = info, 4 = debug

#define TEST_THREAD_COUNT 32        // Number of threads
#define TEST_WRITE_COUNT 20000      // Test writes
#define TEST_ATOMIC_CAL             // Test with atomic begin/end calibration segment access
#define TEST_TASK_LOOP_DELAY_US 50  // Task loop delay in us
#define TEST_TASK_LOCK_DELAY_US 0   // Task lock delay in us
#define TEST_MAIN_LOOP_DELAY_US 200 // Write loop delay in us
#define TEST_DATA_SIZE 128          // Default test data size

//-----------------------------------------------------------------------------------------------------
// Demo calibration parameters

typedef struct {
    uint32_t checksum;
    bool run;
    // Number of threads to create
    uint8_t data[TEST_DATA_SIZE];

} ParametersT;

// Default parameters - make this global/static so the address is stable
static ParametersT kParameters = {.checksum = 0, .run = true, .data = {0}};

// Global calibration segment handle
static xcplib::CalSeg<ParametersT> *calseg = nullptr; // Pointer to the calibration segment wrapper

//-----------------------------------------------------------------------------------------------------
// Test statistics

// Thread statistics
struct ThreadStats {

    uint32_t thread_id{0};

    std::atomic<uint64_t> read_count{0};
    std::atomic<uint64_t> change_count{0};
    std::atomic<uint64_t> read_time_ns{0};
    std::atomic<uint64_t> max_read_time_ns{0};

    // Delete copy and move constructors
    ThreadStats() = default;
    ThreadStats(const ThreadStats &) = delete;
    ThreadStats &operator=(const ThreadStats &) = delete;
    ThreadStats(ThreadStats &&) = delete;
    ThreadStats &operator=(ThreadStats &&) = delete;
};

// Global statistics
static std::vector<std::unique_ptr<ThreadStats>> thread_stats;
static std::atomic<bool> test_running{true};
static uint32_t write_count = 0;
std::atomic<uint64_t> error_count{0};

//-----------------------------------------------------------------------------------------------------
// Thread worker function

void worker_thread(uint32_t thread_id) {

    ThreadStats &stats = *thread_stats[thread_id];
    stats.thread_id = thread_id;

    uint32_t counter = 0;
    uint16_t first_byte = 0x100;

    // Create thread-specific XCP event for measurements
    char event_name[32];
    snprintf(event_name, sizeof(event_name), "thread_%u", thread_id);
    tXcpEventId event_id = XcpCreateEvent(event_name, 0, 0);

    // Register thread-local measurements
    A2lLock();
    A2lSetStackAddrMode_i(event_id);
    A2lCreateMeasurementInstance(event_name, counter, "Thread local counter");
    A2lUnlock();

    printf("Thread %u started with event ID %u\n", thread_id, event_id);

    while (test_running.load(std::memory_order_relaxed)) {

        uint64_t start_time = clockGetNs();

        // Lock and read from calibration segment
        {
            auto parameters = calseg->lock();

            // Check the parameter data for consistency and change
            if (first_byte != (uint16_t)parameters->data[0]) {
                stats.change_count++;
            }
            first_byte = (uint16_t)parameters->data[0];
            for (size_t i = 0; i < sizeof(parameters->data); i++) {
                if (parameters->data[i] != (uint8_t)(first_byte + i)) {
                    uint64_t errors = error_count.fetch_add(1);
                    printf("Thread %u: Fatal error - Data mismatch\n", thread_id);
                    printf("At index %zu: expected %u, got: %u, errors=%llu\n", i, (uint8_t)(first_byte + i), parameters->data[i], errors);
                    break;
                }
            }

            // Check if test should continue
            if (!parameters->run) {
                test_running.store(false, std::memory_order_relaxed);
                break;
            }

#if defined(TEST_TASK_LOCK_DELAY_US) && TEST_TASK_LOCK_DELAY_US > 0
            sleepUs(TEST_TASK_LOCK_DELAY_US); // Simulate some work
#endif
        } // unlock calibration segment

        uint64_t read_time_ns = clockGetNs() - start_time;
        if (read_time_ns > stats.max_read_time_ns.load(std::memory_order_relaxed)) { // @@@@ Not threads safe, but good enough for max measurement
            stats.max_read_time_ns.store(read_time_ns, std::memory_order_relaxed);
        }
        stats.read_time_ns.fetch_add(read_time_ns, std::memory_order_relaxed);
        stats.read_count.fetch_add(1, std::memory_order_relaxed);

        counter++;
        if (counter % 0x10000 == 0) {
            printf("Thread %u: read_count=%llu, change_count=%llu, errors=%llu\n", thread_id, (unsigned long long)stats.read_count, (unsigned long long)stats.change_count,
                   (unsigned long long)error_count.load());
        }

        // Trigger XCP measurement event
        DaqTriggerEvent_i(event_id);

        // Record timing
        sleepUs(TEST_TASK_LOOP_DELAY_US);
    }

    printf("Thread %u finished: reads=%llu\n", thread_id, (unsigned long long)stats.read_count.load());
}

//-----------------------------------------------------------------------------------------------------
// Main function

int main(int argc, char *argv[]) {
    printf("\nXCP Calibration Segment Multi-Threading Test\n");
    printf("============================================\n");

    // Set log level
    XcpSetLogLevel(OPTION_LOG_LEVEL);

    // Initialize XCP
    XcpInit(OPTION_PROJECT_NAME, OPTION_PROJECT_EPK, true);

    // Initialize XCP Server
    uint8_t addr[4] = OPTION_SERVER_ADDR;
    if (!XcpEthServerInit(addr, OPTION_SERVER_PORT, OPTION_USE_TCP, OPTION_QUEUE_SIZE)) {
        printf("Failed to initialize XCP server\n");
        return 1;
    }

    // Initialize A2L generation
    if (!A2lInit(addr, OPTION_SERVER_PORT, OPTION_USE_TCP, A2L_MODE_WRITE_ALWAYS | A2L_MODE_FINALIZE_ON_CONNECT | A2L_MODE_AUTO_GROUPS)) {
        printf("Failed to initialize A2L generation\n");
        return 1;
    }

    // Create the test calibration segment
    auto calseg1 = xcplib::CreateCalSeg("kParameters", &kParameters);

    // Add the calibration segment description as a typedef instance to the A2L file
    {
        A2lTypedefBegin(ParametersT, &kParameters, "A2L Typedef for ParametersT");
        A2lTypedefParameterComponent(run, ParametersT, "Run or stop test", "", 0, 1);
        A2lTypedefParameterArrayComponent(data, ParametersT, "Test data array");
        A2lTypedefEnd();
    }
    calseg1.CreateA2lTypedefInstance("test_params_t", "Test parameters");

    // Store the pointer to the calibration segment wrapper
    calseg = &calseg1;

    // Initialize test data
    uint8_t test_data[TEST_DATA_SIZE];
    for (size_t i = 0; i < sizeof(test_data); i++) {
        test_data[i] = (uint8_t)(i);
    }
    XcpCalSegSetCalPage(1, 0, 0x83);
    XcpSetMta(XCP_ADDR_EXT_SEG, XcpAddrEncodeSegIndex(1, offsetof(ParametersT, data)));
    XcpWriteMta(TEST_DATA_SIZE, &test_data[0]);
    sleepMs(100);

    // Initialize test statistics
    thread_stats.clear();
    thread_stats.reserve(TEST_THREAD_COUNT);
    for (uint32_t i = 0; i < TEST_THREAD_COUNT; i++) {
        thread_stats.emplace_back(std::make_unique<ThreadStats>());
    }

    // Create and start test threads
    printf("Starting %u worker threads...\n", TEST_THREAD_COUNT);
    std::vector<std::thread> threads;
    for (uint32_t i = 0; i < TEST_THREAD_COUNT; i++) {
        threads.emplace_back(worker_thread, i);
    }

    // Finalize A2L
    sleepUs(100000);
    A2lFinalize();

    // Let the test run for the specified duration
    printf("Test running for %u ms...\n", TEST_WRITE_COUNT);
    for (;;) {

        // Sleep for the specified duration
        sleepUs(TEST_MAIN_LOOP_DELAY_US);

        // Simulate modification of calibration data
        uint8_t d0 = (uint8_t)(write_count << 1);
        for (size_t i = 0; i < sizeof(test_data); i++) {
            test_data[i] = (uint8_t)(d0 + i);
        }
#ifdef TEST_ATOMIC_CAL
        if ((write_count & 0xFF) == 0xFF) {
            XcpCalSegCommand(0x01); // Begin atomic calibration operation
            XcpSetMta(XCP_ADDR_EXT_SEG, XcpAddrEncodeSegIndex(1, offsetof(ParametersT, data)));
            XcpWriteMta(TEST_DATA_SIZE / 2, &test_data[0]);
            sleepUs(100);
            XcpSetMta(XCP_ADDR_EXT_SEG, XcpAddrEncodeSegIndex(1, offsetof(ParametersT, data) + TEST_DATA_SIZE / 2));
            XcpWriteMta(TEST_DATA_SIZE / 2, &test_data[TEST_DATA_SIZE / 2]);
            XcpCalSegCommand(0x02); // End atomic calibration operation
        } else
#endif
        {
            XcpSetMta(XCP_ADDR_EXT_SEG, XcpAddrEncodeSegIndex(1, offsetof(ParametersT, data)));
            XcpWriteMta(TEST_DATA_SIZE, &test_data[0]);
        }

        write_count++;
        if (write_count % 1000 == 0) {
            printf("write_count=%llu, errors=%llu\n", (unsigned long long)write_count, (unsigned long long)error_count.load());
        }

        // Check if the test should continue
        if (!test_running.load(std::memory_order_relaxed) || write_count >= TEST_WRITE_COUNT) {
            break;
        }
    }

    // Wait a moment before stopping, to let the threads observe the last changes
    sleepUs(200000);

    // Signal threads to stop
    printf("Stopping test...\n");
    test_running.store(false);

    // Wait for all threads to finish
    for (auto &thread : threads) {
        thread.join();
    }

    // Print final statistics
    printf("\nFinal Statistics:\n");
    printf("================\n");
    uint64_t total_read_count = 0;
    uint64_t total_change_count = 0;
    uint64_t total_read_time_ns = 0;
    uint64_t total_max_read_time_ns = 0;
    uint64_t total_errors = error_count.load();
    for (uint32_t i = 0; i < TEST_THREAD_COUNT; i++) {
        const auto &stats = *thread_stats[i];
        total_read_count += stats.read_count.load();
        total_change_count += stats.change_count.load();
        total_read_time_ns += stats.read_time_ns.load();
        if (stats.max_read_time_ns.load() > total_max_read_time_ns) {
            total_max_read_time_ns = stats.max_read_time_ns;
        }
        printf("Thread %u: reads=%llu, changes=%llu, avg_time=%.2fus, max_time=%.2fus\n", i, (unsigned long long)stats.read_count.load(),
               (unsigned long long)stats.change_count.load(), stats.read_count.load() > 0 ? (double)stats.read_time_ns.load() / stats.read_count.load() / 1000.0 : 0.0,
               (double)stats.max_read_time_ns.load() / 1000.0);
    }
    printf("\nTotal Results:\n");
    printf("  Total writes: %llu\n", (unsigned long long)write_count);
    printf("  Total reads: %llu\n", (unsigned long long)total_read_count);
    printf("  Total changes observed: %llu\n", (unsigned long long)total_change_count);
#ifdef OPTION_ENABLE_DBG_METRICS
    printf("  Total writes pending: %u\n", gXcpWritePendingCount);
    printf("  Total publish all count: %u (expected %llu)\n", gXcpCalSegPublishAllCount, (unsigned long long)(write_count / 256) + gXcpWritePendingCount);
#endif
    printf("  Total errors: %llu\n", (unsigned long long)error_count.load());
    printf("  Average lock time: %.2f us\n", total_read_count > 0 ? (double)total_read_time_ns / total_read_count / 1000.0 : 0.0);
    printf("  Maximum lock time: %.2f us\n", (double)total_max_read_time_ns / 1000.0);
    if (total_errors > 0) {
        printf("ERROR: %llu errors occurred during the test!\n", (unsigned long long)total_errors);
    } else {
        printf("SUCCESS: No errors occurred during the test\n");
    }

    // Shutdown XCP
    XcpDisconnect();
    XcpEthServerShutdown();

    printf("\nTest completed successfully!\n");
    return total_errors > 0 ? 1 : 0;
}
