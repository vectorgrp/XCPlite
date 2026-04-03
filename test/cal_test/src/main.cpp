// cal_test - Multi-threaded calibration segment access test

#include <atomic>   // for std::atomic
#include <cassert>  // for assert
#include <cstdint>  // for uintxx_t
#include <cstring>  // for memset
#include <iostream> // for std::cout
#include <memory>   // for std::unique_ptr
#include <thread>   // for std::thread
#include <vector>

// Public XCPlite/libxcplite API
#include "a2l.hpp"    // for A2l generation application programming interface
#include "xcplib.hpp" // for application programming interface

// Internal libxcplite includes
// Note: Take care for include order, when using internal libxcplite headers !!
// xcp_cfg.h would includes xcplib_cfg.h and platform.h, which enables atomic emulation under Windows, we use <atomic> in this file
#include "xcplib_cfg.h"
#undef OPTION_ATOMIC_EMULATION
#include "dbg_print.h"
#include "platform.h"
#include "xcp_cfg.h" // For XcpAddrEncodeSegIndex

//-----------------------------------------------------------------------------------------------------
// XCP parameters

#define OPTION_PROJECT_NAME "cal_test"  // A2L project name
#define OPTION_PROJECT_VERSION "V2.0.0" // EPK version string
#define OPTION_USE_TCP false            // TCP or UDP
#define OPTION_SERVER_PORT 5555         // Port
#define OPTION_SERVER_ADDR {0, 0, 0, 0} // Bind addr, 0.0.0.0 = ANY
#define OPTION_QUEUE_SIZE (1024 * 256)  // Size of the measurement queue in bytes, must be a multiple of 8
#define OPTION_LOG_LEVEL 3              // Log level, 0 = no log, 1 = error, 2 = warning, 3 = info, 4 = debug

// #define TEST_CALBLK                 // Use CalBlk instead of CalSeg
#define TEST_THREAD_COUNT 4         // Number of threads
#define TEST_WRITE_COUNT 10000      // Test writes
#define TEST_ATOMIC_CAL 10          // Test with atomic begin/end calibration segment access, every N writes
#define TEST_TASK_LOOP_DELAY_US 50  // Task loop delay in us
#define TEST_TASK_LOCK_DELAY_US 0   // Task lock delay in us
#define TEST_MAIN_LOOP_DELAY_US 100 // Write loop delay in us
#define TEST_DATA_SIZE 8            // Default test data size
#define TEST_LOCK_TIMING            // Create a histogram for the duration of XcpLockCalSeg

bool verbose = false;

//-----------------------------------------------------------------------------------------------------

// Internally used XCP functions for testing
extern "C" {
uint8_t XcpWriteMta(uint8_t size, const uint8_t *data);
uint8_t XcpSetMta(uint8_t ext, uint32_t addr);
void XcpCalSegBeginAtomicTransaction(void);
uint8_t XcpCalSegEndAtomicTransaction(void);
uint8_t XcpCalSegSetCalPage(uint8_t segment, uint8_t page, uint8_t mode);

#ifdef TEST_ENABLE_DBG_METRICS
extern uint32_t gXcpWritePendingCount;
extern uint32_t gXcpCalSegPublishAllCount;
extern uint32_t gXcpDaqEventCount;
extern uint32_t gXcpTxPacketCount;
extern uint32_t gXcpTxMessageCount;
extern uint32_t gXcpRxPacketCount;
#endif
}

//-----------------------------------------------------------------------------------------------------
// Demo calibration parameters

typedef struct {
    bool run;
    uint32_t check;
    uint8_t data[TEST_DATA_SIZE];

} ParametersT;

// Default parameters - make this global/static so the address is stable
static ParametersT kParameters = {.run = true, .check = 0, .data = {0}};

// Global calibration segment handle
#ifdef TEST_CALBLK
static xcplib::CalBlk<ParametersT> *calseg = nullptr; // Pointer to the calibration segment wrapper
#else
static xcplib::CalSeg<ParametersT> *calseg = nullptr; // Pointer to the calibration segment wrapper
#endif

//-----------------------------------------------------------------------------------------------------
// Test statistics

#ifdef TEST_LOCK_TIMING

static MUTEX lock_mutex = MUTEX_INTIALIZER;
static uint64_t lock_time_max = 0;
static uint64_t lock_time_sum = 0;
static uint64_t lock_count = 0;
static uint64_t lock_calibration = 0; // Calibration value for the overhead of the timing measurement itself, to get more accurate results for short lock times

// Variable-width lock timing histogram
// Fine granularity for short latencies, coarser for long-tail latencies
// Bin[i] counts events where EDGES[i-1] <= t < EDGES[i]; bin[SIZE-1] is the overflow (>EDGES[SIZE-2])
#define LOCK_TIME_HISTOGRAM_SIZE 26
static const uint64_t LOCK_TIME_HISTOGRAM_EDGES[LOCK_TIME_HISTOGRAM_SIZE - 1] = {
    10, 20, 40, 80, 120, 160, 200, 300, 400, 500, 600, 800, 1000, 1500, 2000, 3000, 4000, 6000, 8000, 10000, 20000, 40000, 80000, 160000, 320000,
};
static uint64_t lock_time_histogram[LOCK_TIME_HISTOGRAM_SIZE] = {0};

static void lock_test_init(void) {
    memset(lock_time_histogram, 0, sizeof(lock_time_histogram));
    lock_time_max = 0;
    lock_time_sum = 0;
    lock_count = 0;

    // Calibrate
    uint64_t sum = 0;
    for (int i = 0; i < 1000; i++) {
        uint64_t time = clockGetMonotonicNs();
        sum += clockGetMonotonicNs() - time;
    }
    lock_calibration = sum / 1000;
}

static void lock_test_add_sample(uint64_t d) {
    if (d >= lock_calibration) // Subtract calibration value to get more accurate results for short lock times
        d -= lock_calibration;
    else
        d = 0;
    mutexLock(&lock_mutex);
    ; // Subtract calibration value to get more accurate results for short lock times
    if (d > lock_time_max)
        lock_time_max = d;
    int i = 0;
    while (i < LOCK_TIME_HISTOGRAM_SIZE - 1 && d >= LOCK_TIME_HISTOGRAM_EDGES[i])
        i++;
    lock_time_histogram[i]++;
    lock_time_sum += d;
    lock_count++;
    mutexUnlock(&lock_mutex);
}

static void lock_test_print_results(void) {
    printf("\nProducer acquire lock time statistics:\n");
    printf("  count=%" PRIu64 "  max=%" PRIu64 "ns  avg=%" PRIu64 "ns (cal=%" PRIu64 "ns)\n", lock_count, lock_time_max, lock_time_sum / lock_count, lock_calibration);

    uint64_t histogram_sum = 0;
    for (int i = 0; i < LOCK_TIME_HISTOGRAM_SIZE; i++)
        histogram_sum += lock_time_histogram[i];
    uint64_t histogram_max = 0;
    for (int i = 0; i < LOCK_TIME_HISTOGRAM_SIZE; i++)
        if (lock_time_histogram[i] > histogram_max)
            histogram_max = lock_time_histogram[i];

    printf("\nLock time histogram (%" PRIu64 " events):\n", histogram_sum);
    printf("  %-20s  %10s  %7s  %s\n", "Range", "Count", "%", "Bar");
    printf("  %-20s  %10s  %7s  %s\n", "--------------------", "----------", "-------", "------------------------------");

    for (int i = 0; i < LOCK_TIME_HISTOGRAM_SIZE; i++) {
        if (!lock_time_histogram[i])
            continue;
        double pct = (double)lock_time_histogram[i] * 100.0 / (double)histogram_sum;

        char range_str[32];
        uint64_t lo = (i == 0) ? 0 : LOCK_TIME_HISTOGRAM_EDGES[i - 1];
        if (i == LOCK_TIME_HISTOGRAM_SIZE - 1) {
            snprintf(range_str, sizeof(range_str), ">%" PRIu64 "ns", lo);
        } else {
            snprintf(range_str, sizeof(range_str), "%" PRIu64 "-%" PRIu64 "ns", lo, LOCK_TIME_HISTOGRAM_EDGES[i]);
        }

        char bar[31];
        int bar_len = (histogram_max > 0) ? (int)((double)lock_time_histogram[i] * 30.0 / (double)histogram_max) : 0;
        if (bar_len > 30)
            bar_len = 30;
        for (int j = 0; j < bar_len; j++)
            bar[j] = '#';
        bar[bar_len] = '\0';

        printf("  %-20s  %10" PRIu64 "  %6.2f%%  %s\n", range_str, lock_time_histogram[i], pct, bar);
    }
    printf("\n");
}

#endif

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
static uint32_t write_single_count = 0;
static uint32_t write_atomic_count = 0;
std::atomic<uint64_t> error_count{0};

bool check_test_data(const ParametersT *params, uint8_t expected_first_byte) {
    uint16_t first_byte = (uint16_t)params->data[0];
    if (first_byte != expected_first_byte) {
        return false;
    }
    for (size_t i = 0; i < sizeof(params->data); i++) {
        if (params->data[i] != (uint8_t)(first_byte + i)) {
            return false;
        }
    }
    return true;
}

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

    // printf("Thread %u started with event ID %u\n", thread_id, event_id);

    while (test_running.load(std::memory_order_relaxed)) {

        uint64_t start_time = clockGetMonotonicNs();

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

        uint64_t read_time_ns = clockGetMonotonicNs() - start_time;
#ifdef TEST_LOCK_TIMING
        lock_test_add_sample(read_time_ns);
#endif
        if (read_time_ns > stats.max_read_time_ns.load(std::memory_order_relaxed)) { // @@@@ Not threads safe, but good enough for max measurement
            stats.max_read_time_ns.store(read_time_ns, std::memory_order_relaxed);
        }
        stats.read_time_ns.fetch_add(read_time_ns, std::memory_order_relaxed);
        stats.read_count.fetch_add(1, std::memory_order_relaxed);

        counter++;
        if (verbose) {
            if (counter % 0x10000 == 0) {
                printf("Thread %u: read_count=%llu, change_count=%llu, errors=%llu\n", thread_id, (unsigned long long)stats.read_count, (unsigned long long)stats.change_count,
                       (unsigned long long)error_count.load());
            }
        }

        // Trigger XCP measurement event
        DaqTriggerEvent_i(event_id);

        // Record timing
        sleepUs(TEST_TASK_LOOP_DELAY_US);
    }

    if (verbose)
        printf("Thread %u finished: reads=%llu\n", thread_id, (unsigned long long)stats.read_count.load());
}

//-----------------------------------------------------------------------------------------------------
// Main function

extern "C" {
void XcpBackgroundTasks(void);
}

int main(int argc, char *argv[]) {
    printf("\nXCP Calibration Segment Multi-Threading Test\n");
    printf("============================================\n");

    // Initialize test statistics
    uint64_t total_errors = 0;
    thread_stats.clear();
    thread_stats.reserve(TEST_THREAD_COUNT);
    for (uint32_t i = 0; i < TEST_THREAD_COUNT; i++) {
        thread_stats.emplace_back(std::make_unique<ThreadStats>());
    }

    // Set log level
    XcpSetLogLevel(OPTION_LOG_LEVEL);

    // Initialize XCP
    XcpInit(OPTION_PROJECT_NAME, OPTION_PROJECT_VERSION, XCP_MODE_LOCAL);

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
#ifdef TEST_CALBLK
    auto calseg1 = xcplib::CalBlk("kParameters", &kParameters);
#else
    auto calseg1 = xcplib::CalSeg("kParameters", &kParameters);
#endif

    // Add the calibration segment description as a typedef instance to the A2L file
    A2lTypedefBegin(ParametersT, &kParameters, "A2L Typedef for ParametersT");
    A2lTypedefParameterComponent(run, "Run or stop test", "", 0, 1);
    A2lTypedefParameterComponent(check, "Check value for test", "", 0, 0xFFFFFFFF);
    A2lTypedefCurveComponent(data, TEST_DATA_SIZE, "Test data array", "", 0, 255);
    A2lTypedefEnd();
    calseg1.CreateA2lTypedefInstance("test_params_t", "Test parameters");

    // Store the pointer to the calibration segment wrapper
    calseg = &calseg1;

    printf("\n\nStart calibration segment access test ...\n");

    // Check initial values
    // Could be 0 or 1,2,3,4,.. from binary persistence file
    {
        auto parameters = calseg->lock();
        if (check_test_data(parameters.get(), 1)) {
            printf("Calibration segment has binary persistence file values\n");
        } else if (memcmp(parameters.get(), &kParameters, sizeof(ParametersT)) == 0) {
            printf("Calibration segment has default initial values\n");
        } else {
            printf("ERROR: Checking calibration segment read initial values failed\n");
            total_errors += 1;
        }
    }

#ifndef TEST_CALBLK
    // RAM page (0) of segment 1
    XcpCalSegSetCalPage(1, 0, 0x83);
    {
        auto parameters = calseg->lock();
        if (memcmp(parameters.get(), &kParameters, sizeof(ParametersT)) != 0 && !check_test_data(parameters.get(), 1)) {
            printf("ERROR: Checking calibration segment read initial RAM page values failed\n");
            total_errors += 1;
        }
    }
    // FLASH page (0) of segment 1
    XcpCalSegSetCalPage(1, 0, 0x83);
    {
        auto parameters = calseg->lock();
        if (memcmp(parameters.get(), &kParameters, sizeof(ParametersT)) != 0 && !check_test_data(parameters.get(), 1)) {
            printf("ERROR: Checking calibration segment read initial FLASH page values failed\n");
            total_errors += 1;
        }
    }

    // Note:
    // The RCU implementation make writes visible after the second read after a write !!!!!!!!!!!!
    // This is a compromise of the lock-less implementation
    // This is the reason for the for loops below

    // Do single calibration changes
    uint32_t check;

    // RAM page (0) of segment 1
    XcpCalSegSetCalPage(1, 0, 0x83);

    //  1 write and multiple reads
    check = 1;
    XcpSetMta(XCP_ADDR_EXT_SEG, XcpAddrEncodeSegIndex(1, offsetof(ParametersT, check)));
    XcpWriteMta((uint8_t)sizeof(check), (const uint8_t *)&check);
    for (int i = 0; i < 3; i++) {
        {
            auto parameters = calseg->lock();
            if (parameters->check != check) {
                printf("ERROR: Checking calibration segment read %u after write failed, expected check=%u, got %u\n", i, check, parameters->check);
                total_errors += 1;
            }
        }
    }

    // 2 consecutive write and multiple reads
    check = 2;
    XcpSetMta(XCP_ADDR_EXT_SEG, XcpAddrEncodeSegIndex(1, offsetof(ParametersT, check)));
    XcpWriteMta((uint8_t)sizeof(check), (const uint8_t *)&check);
    for (int i = 0; i < 1; i++) { // A single read after the write is not enough to make the change visible
        auto parameters = calseg->lock();
        printf("lock %u: check = %u\n", i, parameters->check);
    }
    check = 3;
    XcpSetMta(XCP_ADDR_EXT_SEG, XcpAddrEncodeSegIndex(1, offsetof(ParametersT, check)));
    XcpWriteMta((uint8_t)sizeof(check), (const uint8_t *)&check);
    for (int i = 0; i < 3; i++) {
        {
            auto parameters = calseg->lock();
            if (parameters->check != check) {
                // Consecutive read do not fix the problem, because this would be too expensive in the lock-less implementation
                printf("Checking calibration segment read %u after dual write failed as expected,  check=%u, got %u\n", i, check, parameters->check);
            }
        }
    }

    // Handle background tasks, e.g. pending calibration updates
    // This is done on a regular basis in the main loop of the application, but we need to call it
    // manually here to make pending updates visible
    XcpBackgroundTasks();

    {
        auto parameters = calseg->lock();
        if (parameters->check != check) {
            printf("ERROR: Checking calibration segment read after dual write with background tasks failed, expected check=%u, got %u\n", check, parameters->check);
            total_errors += 1;
        }
    }

    // FLASH page (1) of segment 1
    XcpCalSegSetCalPage(1, 1, 0x83);
    {
        auto parameters = calseg->lock();
        if (parameters->check != 0 && parameters->check != 3) {
            printf("ERROR: Checking calibration segment FLASH page check=%u, expected 0 or 3 \n", parameters->check);
            total_errors += 1;
        }
    }

    // RAM page (0) of segment 1
    XcpCalSegSetCalPage(1, 0, 0x83);

#endif // TEST_CALBLK

    // Initialize thread test data
    uint8_t test_data[TEST_DATA_SIZE];
    uint8_t test_data_zero[TEST_DATA_SIZE];
    for (size_t i = 0; i < sizeof(test_data); i++) {
        test_data[i] = (uint8_t)(i);
        test_data_zero[i] = 0;
    }

    XcpSetMta(XCP_ADDR_EXT_SEG, XcpAddrEncodeSegIndex(1, offsetof(ParametersT, data)));
    XcpWriteMta(TEST_DATA_SIZE, &test_data[0]);
    for (int i = 0; i < 2; i++) {
        auto parameters = calseg->lock();
        if (!check_test_data(parameters.get(), 0)) {
            total_errors += 1;
            printf("ERROR: Calibration segment read %u after write failed, data[0]=%u, expected 0\n", i, parameters->data[0]);
        }
    }
    for (size_t i = 0; i < sizeof(test_data); i++) {
        test_data[i] = (uint8_t)(i + 1);
    }
    XcpSetMta(XCP_ADDR_EXT_SEG, XcpAddrEncodeSegIndex(1, offsetof(ParametersT, data)));
    XcpWriteMta(TEST_DATA_SIZE, &test_data[0]);
    for (int i = 0; i < 2; i++) {
        auto parameters = calseg->lock();
        if (!check_test_data(parameters.get(), 1)) {
            total_errors += 1;
            printf("ERROR: Calibration segment read %u nafter write failed, data[0]=%u, expected 1\n", i, parameters->data[0]);
        }
    }

    if (total_errors == 0)
        printf("Calibration segment access test OK\n");

    // Create and start test threads
    printf("\nStarting %u worker threads...\n", TEST_THREAD_COUNT);
    std::vector<std::thread> threads;
    for (uint32_t i = 0; i < TEST_THREAD_COUNT; i++) {
        threads.emplace_back(worker_thread, i);
    }

    // Finalize A2L and write binary persistence file, to test loading of default values from the persistence file
    sleepUs(100000);
    A2lFinalize();

// Initialize lock timing test
#ifdef TEST_LOCK_TIMING
    lock_test_init();
#endif

    // Let the test run for the specified duration
    printf("Test running for %u writes ...\n", TEST_WRITE_COUNT);

    uint64_t start_time = clockGetMonotonicNs();
    uint64_t last_print_time = start_time;
    for (;;) {

        // Sleep for the specified duration
        sleepUs(TEST_MAIN_LOOP_DELAY_US);

        // Simulate modification of calibration data
        uint8_t d0 = (uint8_t)(write_count << 1);
        for (size_t i = 0; i < sizeof(test_data); i++) {
            test_data[i] = (uint8_t)(d0 + i);
        }
#if defined(TEST_ATOMIC_CAL) && TEST_ATOMIC_CAL > 0
        if ((write_count % TEST_ATOMIC_CAL) == 0) {
            XcpCalSegBeginAtomicTransaction(); // Begin atomic calibration operation
            XcpSetMta(XCP_ADDR_EXT_SEG, XcpAddrEncodeSegIndex(1, offsetof(ParametersT, data)));
            XcpWriteMta(TEST_DATA_SIZE / 2, &test_data_zero[0]);
            XcpSetMta(XCP_ADDR_EXT_SEG, XcpAddrEncodeSegIndex(1, offsetof(ParametersT, data)));
            XcpWriteMta(TEST_DATA_SIZE / 2, &test_data[0]);
            sleepUs(100);
            XcpSetMta(XCP_ADDR_EXT_SEG, XcpAddrEncodeSegIndex(1, offsetof(ParametersT, data) + TEST_DATA_SIZE / 2));
            XcpWriteMta(TEST_DATA_SIZE / 2, &test_data[TEST_DATA_SIZE / 2]);
            if (0 != XcpCalSegEndAtomicTransaction()) {
                total_errors += 1;
                printf("ERROR: Atomic calibration transaction failed at write_count=%u\n", write_count);
            }; // End atomic calibration operation
            write_atomic_count++;
        } else
#endif
        {
            XcpSetMta(XCP_ADDR_EXT_SEG, XcpAddrEncodeSegIndex(1, offsetof(ParametersT, data)));
            XcpWriteMta(TEST_DATA_SIZE, &test_data[0]);
            write_single_count++;
        }
        write_count++;

        if (last_print_time + 1000000000 < clockGetMonotonicNs()) { // Print every second
            last_print_time = clockGetMonotonicNs();
            printf("single writes = %u, atomic_writes = %u, errors=%llu\n", write_single_count, write_atomic_count, (unsigned long long)error_count.load());
        }

        // Check if the test should continue
        if (!test_running.load(std::memory_order_relaxed) || write_count >= TEST_WRITE_COUNT) {
            break;
        }
    } // for

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
    printf("===========================================================\n");

    printf("\nTest parameters:\n");
    printf("TEST_WRITE_COUNT = %u\n", TEST_WRITE_COUNT);
    printf("TEST_THREAD_COUNT = %u\n", TEST_THREAD_COUNT);
#ifdef TEST_CALBLK
    printf("TEST_CALBLK = ON\n");
#else
    printf("TEST_CALBLK = OFF\n");
#endif
#ifdef TEST_ATOMIC_CAL
    printf("TEST_ATOMIC_CAL = ON\n");
#else
    printf("TEST_ATOMIC_CAL = OFF\n");
#endif
    printf("TEST_TASK_LOOP_DELAY_US = %u\n", TEST_TASK_LOOP_DELAY_US);
    printf("TEST_TASK_LOCK_DELAY_US = %u\n", TEST_TASK_LOCK_DELAY_US);
    printf("TEST_MAIN_LOOP_DELAY_US = %u\n", TEST_MAIN_LOOP_DELAY_US);
    printf("TEST_DATA_SIZE = %u\n", TEST_DATA_SIZE);
    printf("\n");

    uint64_t total_read_count = 0;
    uint64_t total_change_count = 0;
    uint64_t total_read_time_ns = 0;
    uint64_t total_max_read_time_ns = 0;
    total_errors += error_count.load();
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
    printf("  Total writes: %u\n", write_count);
    printf("  Total atomic writes: %u\n", write_atomic_count);
    printf("  Total reads: %llu\n", (unsigned long long)total_read_count);
    printf("  Total changes observed: %llu (%.1f%%)\n", (unsigned long long)total_change_count,
           total_read_count > 0 ? (double)total_change_count * 100.0 / (double)total_read_count : 0.0);
#ifdef TEST_ENABLE_DBG_METRICS
    printf("  Total writes pending: %u\n", gXcpWritePendingCount);
    printf("  Total publish all count: %u\n", gXcpCalSegPublishAllCount);
#endif
    printf("  Total errors: %llu\n", (unsigned long long)error_count.load());
    printf("  Average lock time: %.2f us\n", total_read_count > 0 ? (double)total_read_time_ns / total_read_count / 1000.0 : 0.0);
    printf("  Maximum lock time: %.2f us\n", (double)total_max_read_time_ns / 1000.0);

#ifdef TEST_LOCK_TIMING
    lock_test_print_results();
#endif

    if (total_errors > 0) {
        printf("ERROR: %llu errors occurred during the test!\n", (unsigned long long)total_errors);
    } else {
        printf("SUCCESS: No errors occurred during the test\n");
    }

    XcpDisconnect();        // Force disconnect the XCP client
    A2lFinalize();          // Finalize A2L generation, if not done yet
    XcpEthServerShutdown(); // Stop the XCP server

    if (total_errors == 0) {
        printf("\nTest completed successfully!\n");
    } else {
        printf("\nTest completed with errors!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
    }
    return total_errors > 0 ? 1 : 0;
}
