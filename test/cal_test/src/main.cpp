// cal_test - Multi-threaded calibration segment access test

#include <atomic>   // for std::atomic
#include <cassert>  // for assert
#include <cstdint>  // for uintxx_t
#include <cstring>  // for memset
#include <iostream> // for std::cout
#include <memory>   // for std::unique_ptr
#include <random>   // for std::minstd_rand (task delay jitter)
#include <thread>   // for std::thread
#include <vector>

// Public XCPlite/libxcplite API
#include "a2l.hpp"    // for A2l generation application programming interface
#include "xcplib.hpp" // for application programming interface
#include "xcplite.h"

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

#define OPTION_PROJECT_NAME "cal_test"   // A2L project name
#define OPTION_PROJECT_VERSION "V2.1.10" // EPK version string
#define OPTION_USE_TCP false             // TCP or UDP
#define OPTION_SERVER_PORT 5555          // Port
#define OPTION_SERVER_ADDR {0, 0, 0, 0}  // Bind addr, 0.0.0.0 = ANY
#define OPTION_QUEUE_SIZE (1024 * 256)   // Size of the measurement queue in bytes, must be a multiple of 8
#define OPTION_LOG_LEVEL 3               // Log level, 0 = no log, 1 = error, 2 = warning, 3 = info, 4 = debug

// #define TEST_CALBLK                 // Use CalBlk instead of CalSeg
#define TEST_THREAD_COUNT 8          // Number of threads
#define TEST_WRITE_COUNT 20000       // Test writes
#define TEST_ATOMIC_CAL 0            // Test with atomic begin/end calibration segment access, every N writes
#define TEST_TASK_LOOP_DELAY_US 50   // Task loop delay in us
#define TEST_TASK_LOOP_JITTER_PCT 25 // Random +/- jitter applied to the task loop delay, in percent (0 = none)
#define TEST_TASK_LOCK_DELAY_US 0    // Task lock delay in us
#define TEST_MAIN_LOOP_DELAY_US 250  // Write loop delay in us
#define TEST_DATA_SIZE 8             // Default test data size
#define TEST_LOCK_TIMING             // Create a histogram for the duration of XcpLockCalSeg

bool verbose = false;

//-----------------------------------------------------------------------------------------------------

// Internally used XCP functions for testing
extern "C" {
uint8_t XcpWriteMta(uint8_t size, const uint8_t *data);
uint8_t XcpReadMta(uint8_t size, uint8_t *data);
uint8_t XcpSetMta(uint8_t ext, uint32_t addr);
void XcpCalSegBeginAtomicTransaction(void);
uint8_t XcpCalSegEndAtomicTransaction(void);
uint8_t XcpCalSegSetCalPage(uint8_t segment, uint8_t page, uint8_t mode);
}

//-----------------------------------------------------------------------------------------------------
// Demo calibration parameters

typedef struct {
    bool run;
    uint32_t check;
    uint64_t write_time_ns; // Monotonic ns timestamp set by the writer; contiguous with data so both publish in one atomic write
    uint8_t data[TEST_DATA_SIZE];

} ParametersT;

// Default parameters - make this global/static so the address is stable
static ParametersT kParameters = {.run = true, .check = 0, .write_time_ns = 0, .data = {0}};

// Global calibration segment handle
#ifdef TEST_CALBLK
static xcp::CalBlk<ParametersT> *calseg = nullptr; // Pointer to the calibration segment wrapper
#else
static xcp::CalSeg<ParametersT> *calseg = nullptr; // Pointer to the calibration segment wrapper
#endif

//-----------------------------------------------------------------------------------------------------
// Histogram for lock timing

#ifdef TEST_LOCK_TIMING

// Variable-width timing histogram
// Fine granularity for short latencies, coarser for long-tail latencies
// Bin[i] counts events where EDGES[i-1] <= t < EDGES[i]; bin[SIZE-1] is the overflow (>EDGES[SIZE-2])
// scale multiplies the base edges, to stretch the same bin shape over a larger range (e.g. slow readers)
// unit ("ns"/"us"/"ms"/"s") only controls how ranges/stats are displayed; samples are always in ns
// linear_bin_width_ns > 0 switches to equal-width bins of that width (ignores scale/EDGES); better when the
// distribution depends on reader cycle time and count and the log-style bins are misleading
// Instantiate one per delay/latency measurement (thread-safe add_sample()).
class TimeHistogram {
  public:
    explicit TimeHistogram(const char *name, uint64_t scale = 1, const char *unit = "ns", uint64_t linear_bin_width_ns = 0)
        : name_(name), scale_(scale ? scale : 1), unit_(unit), unit_div_(ns_per_unit(unit)), linear_width_(linear_bin_width_ns) {
        init();
    }

    // (Re)initialize counters and measure the timing overhead calibration value
    void init() {
        mutexInit(&mutex_, false, 0);
        memset(histogram_, 0, sizeof(histogram_));
        time_max_ = 0;
        time_sum_ = 0;
        count_ = 0;

        // Calibrate
        uint64_t sum = 0;
        for (int i = 0; i < 10000; i++) {
            volatile uint64_t time = clockGetMonotonicNs();
            sum += clockGetMonotonicNs() - time;
        }
        calibration_ = sum / 10000;
    }

    void add_sample(uint64_t d) {
        if (d >= calibration_) // Subtract calibration value to get more accurate results for short lock times
            d -= calibration_;
        else
            d = 0;
        mutexLock(&mutex_);
        if (d > time_max_)
            time_max_ = d;
        int i;
        if (linear_width_) {
            i = (int)(d / linear_width_);
            if (i >= SIZE)
                i = SIZE - 1;
        } else {
            i = 0;
            while (i < SIZE - 1 && d >= EDGES[i] * scale_)
                i++;
        }
        histogram_[i]++;
        time_sum_ += d;
        count_++;
        mutexUnlock(&mutex_);
    }

    void print_results() const {
        printf("\n%s time statistics:\n", name_);
        printf("  count=%" PRIu64 "  max=%g%s  avg=%g%s (cal=%" PRIu64 "ns)\n", count_, (double)time_max_ / (double)unit_div_, unit_,
               count_ ? (double)(time_sum_ / count_) / (double)unit_div_ : 0.0, unit_, calibration_);

        uint64_t histogram_sum = 0;
        for (int i = 0; i < SIZE; i++)
            histogram_sum += histogram_[i];
        uint64_t histogram_max = 0;
        for (int i = 0; i < SIZE; i++)
            if (histogram_[i] > histogram_max)
                histogram_max = histogram_[i];

        printf("\n%s histogram (%" PRIu64 " events):\n", name_, histogram_sum);
        printf("  %-20s  %10s  %7s  %s\n", "Range", "Count", "%", "Bar");
        printf("  %-20s  %10s  %7s  %s\n", "--------------------", "----------", "-------", "------------------------------");

        for (int i = 0; i < SIZE; i++) {
            if (!histogram_[i])
                continue;
            double pct = (double)histogram_[i] * 100.0 / (double)histogram_sum;

            char range_str[32];
            double lo = (i == 0) ? 0.0 : (double)edge_ns(i - 1) / (double)unit_div_;
            if (i == SIZE - 1) {
                snprintf(range_str, sizeof(range_str), ">%g%s", lo, unit_);
            } else {
                snprintf(range_str, sizeof(range_str), "%g-%g%s", lo, (double)edge_ns(i) / (double)unit_div_, unit_);
            }

            char bar[31];
            int bar_len = (histogram_max > 0) ? (int)((double)histogram_[i] * 30.0 / (double)histogram_max) : 0;
            if (bar_len > 30)
                bar_len = 30;
            for (int j = 0; j < bar_len; j++)
                bar[j] = '#';
            bar[bar_len] = '\0';

            printf("  %-20s  %10" PRIu64 "  %6.2f%%  %s\n", range_str, histogram_[i], pct, bar);
        }
        printf("\n");
    }

    // Delete copy and move (holds a MUTEX)
    TimeHistogram(const TimeHistogram &) = delete;
    TimeHistogram &operator=(const TimeHistogram &) = delete;

  private:
    static constexpr int SIZE = 26;
    static const uint64_t EDGES[SIZE - 1];

    // Upper edge of bin i in ns (i in [0, SIZE-2]); linear or variable-width depending on mode
    uint64_t edge_ns(int i) const { return linear_width_ ? (uint64_t)(i + 1) * linear_width_ : EDGES[i] * scale_; }

    // Nanoseconds per display unit
    static uint64_t ns_per_unit(const char *u) {
        if (strcmp(u, "us") == 0)
            return 1000;
        if (strcmp(u, "ms") == 0)
            return 1000000;
        if (strcmp(u, "s") == 0)
            return 1000000000;
        return 1; // "ns"
    }

    const char *name_;
    uint64_t scale_; // Multiplier applied to the base edges
    const char *unit_;
    uint64_t unit_div_;     // Nanoseconds per display unit
    uint64_t linear_width_; // Equal bin width in ns, or 0 for variable-width bins
    MUTEX mutex_;
    uint64_t time_max_ = 0;
    uint64_t time_sum_ = 0;
    uint64_t count_ = 0;
    uint64_t calibration_ = 0; // Overhead of the timing measurement itself, subtracted from each sample
    uint64_t histogram_[SIZE] = {0};
};

const uint64_t TimeHistogram::EDGES[TimeHistogram::SIZE - 1] = {
    10, 20, 40, 80, 120, 160, 200, 300, 400, 500, 600, 800, 1000, 1500, 2000, 3000, 4000, 6000, 8000, 10000, 20000, 40000, 80000, 160000, 320000,
};

//-----------------------------------------------------------------------------------------------------
// Test statistics

// Histogram for the duration of the calibration segment lock acquisition
static TimeHistogram lock_time_histogram("Reader acquire lock");

// Histogram for the duration of the calibration write
static TimeHistogram write_time_histogram("Writer write");

// Histogram for the latency from a write until the first reader thread observes it
// Linear bins (100us each) since the distribution depends on reader cycle time/count; shown in us
static TimeHistogram visibility_time_histogram("Write to first-observe latency", 1, "us", 100000);

// Timestamp of the most recent write already claimed by a first-observer
static std::atomic<uint64_t> last_observed_write_time{0};

#endif

// Thread statistics
struct ThreadStats {

    uint32_t thread_id{0};

    std::atomic<uint64_t> read_count{0};
    std::atomic<uint64_t> change_count{0};
    std::atomic<uint64_t> tot_read_time_ns{0};
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

// Apply +/- TEST_TASK_LOOP_JITTER_PCT random jitter to a delay (per-thread RNG)
static uint32_t jittered_delay_us(uint32_t base_us) {
#if defined(TEST_TASK_LOOP_JITTER_PCT) && TEST_TASK_LOOP_JITTER_PCT > 0
    if (base_us == 0)
        return 0;
    thread_local std::minstd_rand rng([] {
        static std::atomic<uint32_t> seed{2654435761u};
        return seed.fetch_add(2654435761u, std::memory_order_relaxed);
    }());
    int64_t span = (int64_t)base_us * TEST_TASK_LOOP_JITTER_PCT / 100;
    std::uniform_int_distribution<int64_t> dist(-span, span);
    int64_t v = (int64_t)base_us + dist(rng);
    return v < 0 ? 0 : (uint32_t)v;
#else
    return base_us;
#endif
}

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

        // Lock and read from calibration segment
        {
            uint64_t start_time = clockGetMonotonicNs();

            auto parameters = calseg->lock();

            uint64_t read_time_ns = clockGetMonotonicNs() - start_time;
#ifdef TEST_LOCK_TIMING
            lock_time_histogram.add_sample(read_time_ns);
#endif
            if (read_time_ns > stats.max_read_time_ns.load(std::memory_order_relaxed)) { // @@@@ Not threads safe, but good enough for max measurement
                stats.max_read_time_ns.store(read_time_ns, std::memory_order_relaxed);
            }
            stats.tot_read_time_ns.fetch_add(read_time_ns, std::memory_order_relaxed);
            stats.read_count.fetch_add(1, std::memory_order_relaxed);

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

#ifdef TEST_LOCK_TIMING
            // Write->observe latency: the first reader to see a new write records how long it took to become visible
            uint64_t wt = parameters->write_time_ns;
            uint64_t prev = last_observed_write_time.load(std::memory_order_relaxed);
            while (wt > prev) {
                if (last_observed_write_time.compare_exchange_weak(prev, wt, std::memory_order_relaxed)) {
                    visibility_time_histogram.add_sample(clockGetMonotonicNs() - wt);
                    break;
                }
            }
#endif

            // Check if test should continue
            if (!parameters->run) {
                test_running.store(false, std::memory_order_relaxed);
                break;
            }

#if defined(TEST_TASK_LOCK_DELAY_US) && TEST_TASK_LOCK_DELAY_US > 0
            sleepUs(TEST_TASK_LOCK_DELAY_US); // Simulate some work
#endif
        } // unlock calibration segment

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
        sleepUs(jittered_delay_us(TEST_TASK_LOOP_DELAY_US));
    }

    if (verbose)
        printf("Thread %u finished: reads=%llu\n", thread_id, (unsigned long long)stats.read_count.load());
}

//-----------------------------------------------------------------------------------------------------
// Main function

extern "C" {
void XcpBackgroundTasks(void);
}

// Emulate memory access
#ifdef XCP_ENABLE_APP_ADDRESSING

#define CAL_MEM_SIZE 1024
uint8_t calmem[CAL_MEM_SIZE]; // Simulated calibration memory

uint8_t cb_read(uint32_t src, uint8_t size, uint8_t *dst) {
    if (src + size > CAL_MEM_SIZE) {
        printf("ERROR: Read out of bounds: src=%u, size=%u\n", src, size);
        return CRC_ACCESS_DENIED;
    }
    memcpy(dst, &calmem[src], size); // Return dummy data
    return CRC_CMD_OK;
}

uint8_t cb_write(uint32_t dst, uint8_t size, const uint8_t *src, uint8_t delay) {

    if (dst + size > CAL_MEM_SIZE) {
        printf("ERROR: Write out of bounds: dst=%u, size=%u\n", dst, size);
        return CRC_ACCESS_DENIED;
    }
    memcpy(&calmem[dst], src, size); // Store written data in simulated calibration memory
    return CRC_CMD_OK;
}

#endif

int main(int argc, char *argv[]) {
    printf("\nXCP Calibration Segment Multi-Threading Test\n");
    printf("============================================\n");

    // Run a hermetic self-check by default (no socket bound), so this test is CTest/CI friendly.
    // --server / --interactive additionally starts the XCP eth server so a tool (CANape/xcpclient) can attach.
    bool with_server = false;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--server") == 0 || strcmp(argv[i], "--interactive") == 0) {
            with_server = true;
        } else if (strcmp(argv[i], "--verbose") == 0 || strcmp(argv[i], "-v") == 0) {
            verbose = true;
        }
    }
    printf("Mode: %s\n", with_server ? "interactive (XCP eth server on)" : "hermetic self-check (pass --server for interactive use)");

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
#ifdef XCP_ENABLE_APP_ADDRESSING
    ApplXcpRegisterReadCallback(cb_read);
    ApplXcpRegisterWriteCallback(cb_write);
#endif

    // Initialize XCP Server (interactive part) - only when explicitly requested, so the self-check runs hermetically under CTest
    uint8_t addr[4] = OPTION_SERVER_ADDR;
    if (with_server) {
        if (!XcpEthServerInit(addr, OPTION_SERVER_PORT, OPTION_USE_TCP, OPTION_QUEUE_SIZE)) {
            printf("Failed to initialize XCP server\n");
            return 1;
        }
    }

    // Initialize A2L generation
    if (!A2lInit(addr, OPTION_SERVER_PORT, OPTION_USE_TCP, A2L_MODE_WRITE_ALWAYS | A2L_MODE_FINALIZE_ON_CONNECT | A2L_MODE_AUTO_GROUPS)) {
        printf("Failed to initialize A2L generation\n");
        return 1;
    }

    // Test application specific addressing if enabled
#ifdef XCP_ENABLE_APP_ADDRESSING
    printf("\n\nStart application specific memory segment access test ...\n");
    // Write some test data to the simulated calibration memory
    for (uint32_t i = 0; i < CAL_MEM_SIZE; i += 4) {
        XcpSetMta(XCP_ADDR_EXT_APP, i);
        XcpWriteMta(4, (uint8_t *)&i);
    }
    // Read back the data via the XCP read callback to verify it works
    for (uint32_t i = 0; i < CAL_MEM_SIZE; i += 4) {
        uint32_t value = 0;
        XcpSetMta(XCP_ADDR_EXT_APP, i);
        XcpReadMta(4, (uint8_t *)&value);
        if (value != i) {
            printf(ANSI_COLOR_RED "ERROR: App addressing read/write mismatch at addr %u: expected %u, got %u\n" ANSI_COLOR_RESET, i, i, value);
            total_errors++;
        }
    }
    if (total_errors == 0)
        printf("Application specific memory segment access test OK\n");

#endif

// Create the test calibration segment
#ifdef TEST_CALBLK
    auto calseg1 = xcp::CalBlk("kParameters", &kParameters);
#else
    auto calseg1 = xcp::CalSeg("kParameters", &kParameters);
#endif

    // Add the calibration segment description as a typedef instance to the A2L file
    A2lTypedefBegin(ParametersT, &kParameters, "A2L Typedef for ParametersT");
    A2lTypedefParameterComponent(run, "Run or stop test", "", 0, 1);
    A2lTypedefParameterComponent(check, "Check value for test", "", 0, 0xFFFFFFFF);
    A2lTypedefParameterComponent(write_time_ns, "Writer timestamp for visibility latency test", "ns", 0, 0);
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
            printf(ANSI_COLOR_RED "ERROR: Checking calibration segment read initial values failed\n" ANSI_COLOR_RESET);
            total_errors += 1;
        }
    }

#ifndef TEST_CALBLK
    // RAM page (0) of segment 1
    XcpCalSegSetCalPage(1, 0, 0x83);
    {
        auto parameters = calseg->lock();
        if (memcmp(parameters.get(), &kParameters, sizeof(ParametersT)) != 0 && !check_test_data(parameters.get(), 1)) {
            printf(ANSI_COLOR_RED "ERROR: Checking calibration segment read initial RAM page values failed\n" ANSI_COLOR_RESET);
            total_errors += 1;
        }
    }
    // FLASH page (0) of segment 1
    XcpCalSegSetCalPage(1, 0, 0x83);
    {
        auto parameters = calseg->lock();
        if (memcmp(parameters.get(), &kParameters, sizeof(ParametersT)) != 0 && !check_test_data(parameters.get(), 1)) {
            printf(ANSI_COLOR_RED "ERROR: Checking calibration segment read initial FLASH page values failed\n" ANSI_COLOR_RESET);
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
                printf(ANSI_COLOR_RED "ERROR: Checking calibration segment read %u after write failed, expected check=%u, got %u\n" ANSI_COLOR_RESET, i, check, parameters->check);
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
            printf(ANSI_COLOR_RED "ERROR: Checking calibration segment read after dual write with background tasks failed, expected check=%u, got %u\n" ANSI_COLOR_RESET, check,
                   parameters->check);
            total_errors += 1;
        }
    }

    // FLASH page (1) of segment 1
    XcpCalSegSetCalPage(1, 1, 0x83);
    {
        auto parameters = calseg->lock();
        if (parameters->check != 0 && parameters->check != 3) {
            printf(ANSI_COLOR_RED "ERROR: Checking calibration segment FLASH page check=%u, expected 0 or 3 \n" ANSI_COLOR_RESET, parameters->check);
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
            printf(ANSI_COLOR_RED "ERROR: Calibration segment read %u after write failed, data[0]=%u, expected 0\n" ANSI_COLOR_RESET, i, parameters->data[0]);
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
            printf(ANSI_COLOR_RED "ERROR: Calibration segment read %u after write failed, data[0]=%u, expected 1\n" ANSI_COLOR_RESET, i, parameters->data[0]);
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
    lock_time_histogram.init();
    write_time_histogram.init();
    visibility_time_histogram.init();
    last_observed_write_time.store(clockGetMonotonicNs(), std::memory_order_relaxed); // Baseline so stale/persisted timestamps are not measured
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
                printf(ANSI_COLOR_RED "ERROR: Atomic calibration transaction failed at write_count=%u\n" ANSI_COLOR_RESET, write_count);
            }; // End atomic calibration operation
            write_atomic_count++;
        } else
#endif

        {
            // Assemble one contiguous block {write_time_ns, data} so the timestamp and payload publish atomically
            uint8_t block[sizeof(uint64_t) + TEST_DATA_SIZE];
            uint64_t now_ns = clockGetMonotonicNs();
            memcpy(block, &now_ns, sizeof(now_ns));
            memcpy(block + sizeof(now_ns), test_data, TEST_DATA_SIZE);

            uint64_t start_time = clockGetMonotonicNs();
            XcpSetMta(XCP_ADDR_EXT_SEG, XcpAddrEncodeSegIndex(1, offsetof(ParametersT, write_time_ns)));
            XcpWriteMta((uint8_t)sizeof(block), block);
            uint64_t write_time_ns = clockGetMonotonicNs() - start_time;
#ifdef TEST_LOCK_TIMING
            write_time_histogram.add_sample(write_time_ns);
#endif

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
#ifdef OPTION_CAL_RCU_REFCOUNT
    printf("OPTION_CAL_RCU_REFCOUNT = ON\n");
#else
    printf("OPTION_CAL_RCU_REFCOUNT = OFF\n");
#endif
    printf("TEST_WRITE_COUNT = %u\n", TEST_WRITE_COUNT);
    printf("TEST_THREAD_COUNT = %u\n", TEST_THREAD_COUNT);
#ifdef TEST_CALBLK
    printf("TEST_CALBLK = ON\n");
#else
    printf("TEST_CALBLK = OFF\n");
#endif
#if defined(TEST_ATOMIC_CAL) && TEST_ATOMIC_CAL > 0
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
        total_read_time_ns += stats.tot_read_time_ns.load();
        if (stats.max_read_time_ns.load() > total_max_read_time_ns) {
            total_max_read_time_ns = stats.max_read_time_ns;
        }
        printf("Thread %u: reads=%llu, changes=%llu, avg_time=%.2fus, max_time=%.2fus\n", i, (unsigned long long)stats.read_count.load(),
               (unsigned long long)stats.change_count.load(), stats.read_count.load() > 0 ? (double)stats.tot_read_time_ns.load() / stats.read_count.load() / 1000.0 : 0.0,
               (double)stats.max_read_time_ns.load() / 1000.0);
    }
    printf("\nTotal Results:\n");
    printf("  Total writes: %u\n", write_count);
    printf("  Total atomic writes: %u\n", write_atomic_count);
    printf("  Total reads: %llu\n", (unsigned long long)total_read_count);
    printf("  Total changes observed: %llu (%.1f%%)\n", (unsigned long long)total_change_count,
           total_read_count > 0 ? (double)total_change_count * 100.0 / (double)total_read_count : 0.0);
#ifdef TEST_ENABLE_CAL_METRICS
    printf("  Total write pending: %u\n", gXcpWritePendingCount);
    printf("  Total publish all:   %u\n", gXcpCalSegPublishAllCount);
#endif
    printf("  Total errors: %llu\n", (unsigned long long)error_count.load());
    printf("  Average lock time: %.2f us\n", total_read_count > 0 ? (double)total_read_time_ns / total_read_count / 1000.0 : 0.0);
    printf("  Maximum lock time: %.2f us\n", (double)total_max_read_time_ns / 1000.0);

#ifdef TEST_LOCK_TIMING
    lock_time_histogram.print_results();
    write_time_histogram.print_results();
    visibility_time_histogram.print_results();
#endif

    if (total_errors > 0) {
        printf(ANSI_COLOR_RED "ERROR: %llu errors occurred during the test!\n" ANSI_COLOR_RESET, (unsigned long long)total_errors);
    } else {
        printf(ANSI_COLOR_GREEN "SUCCESS: No errors occurred during the test\n" ANSI_COLOR_RESET);
    }

    XcpDisconnect(); // Force disconnect the XCP client
    A2lFinalize();   // Finalize A2L generation, if not done yet
    if (with_server) {
        XcpEthServerShutdown(); // Stop the XCP server
    }

    if (total_errors == 0) {
        printf("\n" ANSI_COLOR_GREEN "Test completed successfully!\n" ANSI_COLOR_RESET);
    } else {
        printf("\n" ANSI_COLOR_RED "Test completed with errors!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n" ANSI_COLOR_RESET);
    }
    return total_errors > 0 ? 1 : 0;
}
