// cal_test xcplib example - Multi-threaded calibration segment access test

#include <assert.h> // for assert
#include <atomic>
#include <stdbool.h> // for bool
#include <stdint.h>  // for uintxx_t
#include <stdio.h>   // for printf
#include <string.h>  // for sprintf
#include <thread>
#include <vector>

#include "platform.h"

#include "a2l.hpp"    // for xcplib A2l generation application programming interface
#include "xcplib.hpp" // for xcplib application programming interface

// Internally used XCP functions for testing
extern "C" {
uint8_t XcpWriteMta(uint8_t size, const uint8_t *data);
uint8_t XcpSetMta(uint8_t ext, uint32_t addr);
uint8_t XcpCalSegCommand(uint8_t cmd);
}

//-----------------------------------------------------------------------------------------------------
// XCP parameters

#define OPTION_PROJECT_NAME "cal_test"  // A2L project name
#define OPTION_USE_TCP false            // TCP or UDP
#define OPTION_SERVER_PORT 5555         // Port
#define OPTION_SERVER_ADDR {0, 0, 0, 0} // Bind addr, 0.0.0.0 = ANY
#define OPTION_QUEUE_SIZE 1024 * 32     // Size of the measurement queue in bytes, must be a multiple of 8
#define OPTION_LOG_LEVEL 3

#define DEFAULT_THREAD_COUNT 10         // Default number of threads
#define DEFAULT_TEST_WRITE_COUNT 100000 // Default test writes
#define DEFAULT_TASK_LOOP_DELAY_US 50   // Task loop delay in us
#define DEFAULT_MAIN_LOOP_DELAY_US 50   // Write loop delay in us
#define DEFAULT_TEST_DATA_SIZE 16       // Default test data size

//-----------------------------------------------------------------------------------------------------
// Demo calibration parameters

typedef struct {
    uint32_t checksum;
    bool run;
    // Number of threads to create
    uint8_t data[DEFAULT_TEST_DATA_SIZE];

} ParametersT;

// Default parameters - make this global/static so the address is stable
static ParametersT kParameters = {.checksum = 0, .run = true, .data = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15}};

// Global calibration segment handle
static xcplib::CalSeg<ParametersT> *calseg = nullptr; // Pointer to the calibration segment wrapper

//-----------------------------------------------------------------------------------------------------
// Test statistics

// Thread statistics
struct ThreadStats {
    std::atomic<uint64_t> read_count{0};

    std::atomic<uint64_t> total_time_ns{0};
    uint32_t thread_id{0};

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

    auto start_time = std::chrono::high_resolution_clock::now();

    while (test_running.load()) {
        auto access_start = std::chrono::high_resolution_clock::now();

        // Read from calibration segment
        {
            auto parameters = calseg->lock();

            // Check the parameter data for consistency
            for (size_t i = 0; i < sizeof(parameters->data); i++) {
                if (parameters->data[i] != (uint8_t)(parameters->data[0] + i)) {
                    uint32_t errors = error_count.fetch_add(1);
                    printf("Thread %u: Data mismatch\n", thread_id);
                    printf("At index %zu: expected %u, got: %u, errors=%u\n", i, (uint8_t)(parameters->data[0] + i), parameters->data[i], errors);
                    break;
                }
            }

            // Check if test should continue
            if (!parameters->run) {
                test_running.store(false);
                break;
            }
        }

        stats.read_count++;

        counter++;
        if (counter % 1000 == 0) {
            printf("Thread %u: read_count=%llu, write_count=%llu, errors=%llu\n", thread_id, (unsigned long long)stats.read_count, (unsigned long long)write_count,
                   (unsigned long long)error_count.load());
        }

        // Trigger XCP measurement event
        DaqEvent_i(event_id);

        // Record timing
        auto access_end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(access_end - access_start);
        stats.total_time_ns.fetch_add(duration.count());
        sleepUs(DEFAULT_TASK_LOOP_DELAY_US);
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    auto total_duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

    printf("Thread %u finished after %lld ms: reads=%llu\n", thread_id, (long long)total_duration.count(), (unsigned long long)stats.read_count.load());
}

//-----------------------------------------------------------------------------------------------------
// Main function

int main(int argc, char *argv[]) {
    printf("\nXCP Calibration Segment Multi-Threading Test\n");
    printf("============================================\n");

    // Set log level
    XcpSetLogLevel(OPTION_LOG_LEVEL);

    // Initialize XCP
    XcpInit(true);

    // Initialize XCP Server
    uint8_t addr[4] = OPTION_SERVER_ADDR;
    if (!XcpEthServerInit(addr, OPTION_SERVER_PORT, OPTION_USE_TCP, OPTION_QUEUE_SIZE)) {
        printf("Failed to initialize XCP server\n");
        return 1;
    }

    // Initialize A2L generation
    if (!A2lInit(OPTION_PROJECT_NAME, nullptr, addr, OPTION_SERVER_PORT, OPTION_USE_TCP, A2L_MODE_WRITE_ALWAYS | A2L_MODE_FINALIZE_ON_CONNECT | A2L_MODE_AUTO_GROUPS)) {
        printf("Failed to initialize A2L generation\n");
        return 1;
    }

    // Create the test calibration segment
    auto calseg1 = xcplib::CreateCalSeg("TestParameters", kParameters);

    // Add the calibration segment description as a typedef instance to the A2L file
    A2lTypedefBegin(ParametersT, "A2L Typedef for ParametersT");
    A2lTypedefParameterComponent(run, ParametersT, "Run or stop test", "", 0, 1);
    A2lTypedefMeasurementArrayComponent(data, ParametersT);
    A2lTypedefEnd();
    calseg1.CreateA2lTypedefInstance("test_params_t", "Test parameters");

    // Store the pointer to the calibration segment wrapper
    calseg = &calseg1;

    // Initialize thread statistics
    thread_stats.clear();
    thread_stats.reserve(DEFAULT_THREAD_COUNT);
    for (uint32_t i = 0; i < DEFAULT_THREAD_COUNT; i++) {
        thread_stats.emplace_back(std::make_unique<ThreadStats>());
    }

    printf("Starting %u worker threads...\n", DEFAULT_THREAD_COUNT);

    // Create and start worker threads
    std::vector<std::thread> threads;
    for (uint32_t i = 0; i < DEFAULT_THREAD_COUNT; i++) {
        threads.emplace_back(worker_thread, i);
    }

    // Finalize A2L
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    A2lFinalize();

    // Let the test run for the specified duration
    printf("Test running for %u ms...\n", DEFAULT_TEST_WRITE_COUNT);

    for (;;) {

        // Sleep for the specified duration
        std::this_thread::sleep_for(std::chrono::microseconds(DEFAULT_MAIN_LOOP_DELAY_US));

        // Simulate modification of calibration data
        write_count++;
        uint8_t test_data[DEFAULT_TEST_DATA_SIZE];
        uint8_t d0 = (uint8_t)(write_count << 1);
        for (size_t i = 0; i < sizeof(test_data); i++) {
            test_data[i] = (uint8_t)(d0 + i);
        }
        if (write_count & 1) {
            XcpSetMta(0, 0x80010000 + offsetof(ParametersT, data));
            XcpWriteMta(DEFAULT_TEST_DATA_SIZE, &test_data[0]);

        } else {
            XcpCalSegCommand(0x01); // Begin atomic calibration operation
            XcpSetMta(0, 0x80010000 + offsetof(ParametersT, data));
            XcpWriteMta(DEFAULT_TEST_DATA_SIZE / 2, &test_data[0]);
            sleepNs(100);
            XcpSetMta(0, 0x80010000 + offsetof(ParametersT, data) + DEFAULT_TEST_DATA_SIZE / 2);
            XcpWriteMta(DEFAULT_TEST_DATA_SIZE / 2, &test_data[DEFAULT_TEST_DATA_SIZE / 2]);
            XcpCalSegCommand(0x02); // End atomic calibration operation
        }
        // Check if the test should continue
        if (!test_running.load() || write_count >= DEFAULT_TEST_WRITE_COUNT) {
            break;
        }
    }

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
    uint64_t total_reads = 0;
    uint64_t total_errors = error_count.load();
    uint64_t total_time_ns = 0;
    for (uint32_t i = 0; i < DEFAULT_THREAD_COUNT; i++) {
        const auto &stats = *thread_stats[i];
        total_reads += stats.read_count.load();

        total_time_ns += stats.total_time_ns.load();
        printf("Thread %u: reads=%llu, avg_time=%.2f us\n", i, (unsigned long long)stats.read_count.load(),
               stats.read_count.load() > 0 ? (double)stats.total_time_ns.load() / stats.read_count.load() / 1000.0 : 0.0);
    }
    printf("\nTotals:\n");
    printf("  Total reads: %llu\n", (unsigned long long)total_reads);
    printf("  Total writes: %llu\n", (unsigned long long)write_count);
    printf("  Total errors: %llu\n", (unsigned long long)error_count.load());
    printf("  Average access time: %.2f us\n", total_reads > 0 ? (double)total_time_ns / total_reads / 1000.0 : 0.0);
    if (total_errors > 0) {
        printf("  ERROR: %llu errors occurred during the test!\n", (unsigned long long)total_errors);
    } else {
        printf("  SUCCESS: No errors occurred during the test\n");
    }

    // Shutdown XCP
    XcpDisconnect();
    XcpEthServerShutdown();

    printf("\nTest completed successfully!\n");
    return total_errors > 0 ? 1 : 0;
}
