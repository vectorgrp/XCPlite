/*----------------------------------------------------------------------------
| File:
|   tools/shmtool/src/main.cpp
|
| Description:
|   XCPlite shared memory diagnostic tool.
|   Inspect, validate and clean the POSIX shared memory regions used by XCPlite
|   multi-process (OPTION_SHM_MODE) sessions.
|
|   Pulls in xcplite.h so the struct layout is always in sync with the library.
|
| Usage:
|   shmtool [command] [options]
|
| Commands:
|   status   (default)  Print the contents of /xcpdata and /xcpqueue
|   finalize            Set a2l_finalize_requested, poll for acknowledgement, print status
|   clean               Remove /xcpdata, /xcpqueue and associated lock files
|   help                Print this help text
|
| Options:
|   -v, --verbose       Show additional low-level details (offsets, pad fields)
|   --timeout <ms>      Polling timeout for 'finalize' command (default: 5000 ms)
|
| Copyright (c) Vector Informatik GmbH. All rights reserved.
| See LICENSE file in the project root for details.
|
 ----------------------------------------------------------------------------*/

#include <cassert>
#include <cerrno>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "xcp_cfg.h"
#include "xcplib_cfg.h"

#ifdef OPTION_SHM_MODE

#include "shm.h"     // for shared memory management
#include "xcplite.h" // for tXcpData layout

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Portable relaxed-atomic read/write of a C11 _Atomic field via volatile cast.
// Sufficient for a diagnostic tool; no strict store-load barriers needed beyond
// what the volatile access provides on cache-coherent SMP systems.
template <typename T> static inline uint32_t read_u32(const T *p) { return *reinterpret_cast<const volatile uint32_t *>(p); }
static inline void write_u32(atomic_uint_least32_t *p, uint32_t v) { *reinterpret_cast<volatile uint32_t *>(p) = v; }

static const char *bool_str(uint32_t v) { return v ? "yes" : "no"; }

static void print_separator(char c = '-', int width = 80) {
    for (int i = 0; i < width; ++i)
        putchar(c);
    putchar('\n');
}

// ---------------------------------------------------------------------------
// status command
// ---------------------------------------------------------------------------

static int cmd_status(bool verbose) {

    int fd = shm_open("/xcpdata", O_RDONLY, 0);
    if (fd < 0) {
        if (errno == ENOENT)
            printf("No XCPlite SHM session active, /xcpdata not found\n");
        else
            fprintf(stderr, "shm_open '/xcpdata' failed: %s\n", strerror(errno));
        return (errno == ENOENT) ? 0 : 1;
    }
    struct stat st{};
    if (fstat(fd, &st) < 0) {
        fprintf(stderr, "fstat '/xcpdata' failed: %s\n", strerror(errno));
        close(fd);
        return 1;
    }
    size_t shm_size = (size_t)st.st_size;
    void *shm_ptr = mmap(nullptr, shm_size, PROT_READ, MAP_SHARED, fd, 0);
    close(fd);
    if (shm_ptr == MAP_FAILED) {
        fprintf(stderr, "mmap '/xcpdata' failed: %s\n", strerror(errno));
        return 1;
    }
    printf("/xcpdata mmap found, size = %zu bytes\n", shm_size);
    if (shm_size < sizeof(tXcpData)) {
        fprintf(stderr, "WARNING: SHM mmap size %zu < sizeof(tXcpData) %zu — stale or incompatible binary!\n", shm_size, sizeof(tXcpData));
        return 1;
    }

    print_separator('=');

    extern tXcpData *gXcpData;
    gXcpData = reinterpret_cast<tXcpData *>(shm_ptr); // Set global pointer for use in XcpShmDebugPrint and other functions
    const auto *hdr = &gXcpData->shm_header;          // Pointer to the header within the mapped region
    // const auto *hdr = reinterpret_cast<const tShmHeader *>(reinterpret_cast<const uint8_t *>(ptr) + offsetof(tXcpData, shm_header));

    // Note: shm_size may exceed sizeof(tXcpData) due to OS page-size rounding
    // (macOS ARM64 rounds ftruncate up to 16 KiB boundaries). Use hdr->size
    // (the declared size stored by the leader) for the real compatibility check.

    // Validate magic
    bool magic_ok = (hdr->magic == SHM_MAGIC);
    uint32_t ver = hdr->version;
    uint32_t hdr_size = hdr->size; // exact sizeof(tXcpData) as recorded by the leader
    uint32_t lpid = hdr->leader_pid;
    uint32_t napp = read_u32(&hdr->app_count);
    uint32_t a2lrq = read_u32(&hdr->a2l_finalize_requested);
    bool size_ok = (hdr_size == (uint32_t)sizeof(tXcpData));

    if (verbose || !magic_ok) {
        printf("  magic              : 0x%016llX  %s\n", (unsigned long long)hdr->magic, magic_ok ? "(valid XCPLITE_)" : "*** INVALID ***");
    } else if (!magic_ok) {
        printf("  magic              : *** INVALID *** (not an XCPlite SHM region)\n");
    }

    if (!size_ok) {
        fprintf(stderr,
                "  WARNING: declared size %u != sizeof(tXcpData) %zu — different binary!\n"
                "  Run: build/shmtool clean\n",
                hdr_size, sizeof(tXcpData));
    }

    // Header
    printf("  version            : %u.%u.%u\n", (ver >> 16) & 0xFF, (ver >> 8) & 0xFF, ver & 0xFF);
    printf("  declared size      : %u bytes  (this build: %zu)%s\n", hdr_size, sizeof(tXcpData), size_ok ? "" : "  *** MISMATCH ***");
    printf("  leader pid         : %u%s\n", lpid, (getpid() == (pid_t)lpid) ? "  (this process)" : "");
    printf("  app count          : %u / %d\n", napp, SHM_MAX_APP_COUNT);
    printf("  A2L finalize req'd : %s\n", bool_str(a2lrq));
    print_separator();

    if (!magic_ok) {
        munmap(shm_ptr, shm_size);
        return 1;
    }

    // Application slots
    for (uint32_t i = 0; i < napp && i < SHM_MAX_APP_COUNT; ++i) {
        const tApp *app = &hdr->app_list[i];
        uint32_t ac = read_u32(&app->alive_counter);
        uint32_t fin = read_u32(&app->a2l_finalized);
        printf("  App %u:  %s %s epk=%s  pid=%u  %s\n", i, app->project_name[0] ? app->project_name : "(vacant)", app->is_server ? "[server]" : "", app->epk, app->pid,
               app->is_leader ? "[leader]" : "");
        printf("          a2l_name=%s  finalized=%s  alive_counter=%u\n", fin ? app->a2l_name : "(pending)", bool_str(fin), ac);

        print_separator();
    }

    if (verbose) {
        XcpShmDebugPrint();
    }

    munmap(shm_ptr, shm_size);

    return 0;
}

// ---------------------------------------------------------------------------
// finalize command
// ---------------------------------------------------------------------------

static int cmd_finalize(uint32_t timeout_ms) {
    // Open /xcpdata read-write so we can set the flag
    int fd = shm_open("/xcpdata", O_RDWR, 0);
    if (fd < 0) {
        if (errno == ENOENT)
            printf("No XCPlite SHM session active, /xcpdata not found\n");
        else
            fprintf(stderr, "shm_open '/xcpdata' failed: %s\n", strerror(errno));
        return (errno == ENOENT) ? 0 : 1;
    }

    struct stat st{};
    if (fstat(fd, &st) < 0) {
        fprintf(stderr, "fstat '/xcpdata' failed: %s\n", strerror(errno));
        close(fd);
        return 1;
    }
    size_t shm_size = (size_t)st.st_size;

    void *ptr = mmap(nullptr, shm_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (ptr == MAP_FAILED) {
        fprintf(stderr, "mmap '/xcpdata' failed: %s\n", strerror(errno));
        return 1;
    }

    auto *hdr = reinterpret_cast<tShmHeader *>(reinterpret_cast<uint8_t *>(ptr) + offsetof(tXcpData, shm_header));

    if (hdr->magic != SHM_MAGIC) {
        fprintf(stderr, "Invalid SHM magic — not an XCPlite region or stale segment.\n");
        munmap(ptr, shm_size);
        return 1;
    }

    uint32_t napp = read_u32(&hdr->app_count);
    if (napp == 0) {
        printf("No applications registered in SHM — nothing to finalize.\n");
        munmap(ptr, shm_size);
        return 0;
    }

    // --- Step 1: set the flag ---
    write_u32(&hdr->a2l_finalize_requested, 1);
    printf("a2l_finalize_requested set.  Waiting for %u app(s) to acknowledge (timeout %u ms)...\n", napp, timeout_ms);
    fflush(stdout);

    // --- Step 2: poll for acknowledgement ---
    const uint32_t poll_interval_ms = 50;
    uint32_t elapsed_ms = 0;
    bool all_done = false;
    while (elapsed_ms <= timeout_ms) {
        all_done = true;
        for (uint32_t i = 0; i < napp && i < SHM_MAX_APP_COUNT; ++i) {
            const tApp *app = &hdr->app_list[i];
            if (app->pid == 0)
                continue; // vacant slot
            if (read_u32(&app->a2l_finalized) == 0) {
                all_done = false;
                break;
            }
        }
        if (all_done)
            break;

        usleep(poll_interval_ms * 1000);
        elapsed_ms += poll_interval_ms;

        // Print a progress dot every 500 ms
        if (elapsed_ms % 500 == 0) {
            putchar('.');
            fflush(stdout);
        }
    }
    putchar('\n');

    // --- Step 3: print result ---
    print_separator('=');
    if (all_done)
        printf("All applications acknowledged A2L finalization.\n");
    else
        printf("WARNING: Timeout after %u ms — not all applications acknowledged.\n", elapsed_ms);
    print_separator();

    for (uint32_t i = 0; i < napp && i < SHM_MAX_APP_COUNT; ++i) {
        const tApp *app = &hdr->app_list[i];
        uint32_t fin = read_u32(&app->a2l_finalized);
        printf("  App %u:  %-*s  pid=%-6u  a2l_finalized=%-3s  a2l_name=%s\n", i, XCP_PROJECT_NAME_MAX_LENGTH, app->project_name[0] ? app->project_name : "(vacant)", app->pid,
               bool_str(fin), fin ? app->a2l_name : "(pending)");
        print_separator();
    }

    munmap(ptr, shm_size);
    return all_done ? 0 : 2; // exit code 2 = partial timeout
}

#endif // SHM_MODE

// ---------------------------------------------------------------------------
// clean command
// ---------------------------------------------------------------------------

static int cmd_clean() {
    int rc = 0;

    auto unlink_shm = [&](const char *name) {
        if (shm_unlink(name) == 0)
            printf("  removed SHM  %s\n", name);
        else if (errno == ENOENT)
            printf("  not found    %s\n", name);
        else {
            fprintf(stderr, "  shm_unlink(%s) failed: %s\n", name, strerror(errno));
            rc = 1;
        }
    };

    auto remove_lock = [&](const char *path) {
        if (unlink(path) == 0)
            printf("  removed lock %s\n", path);
        else if (errno != ENOENT) {
            fprintf(stderr, "  unlink(%s) failed: %s\n", path, strerror(errno));
            rc = 1;
        }
    };

    unlink_shm("/xcpdata");
    unlink_shm("/xcpqueue");
    remove_lock("/tmp/xcpdata.lock");
    remove_lock("/tmp/xcpqueue.lock");

    printf("Done.\n");
    return rc;
}
// ---------------------------------------------------------------------------
// help
// ---------------------------------------------------------------------------

static void print_usage(const char *argv0) {
    printf("XCPlite shared memory diagnostic tool\n\n"
           "Usage:\n"
           "  %s [command] [options]\n\n"
           "Commands:\n"
           "  status    (default)  Print contents of /xcpdata and /xcpqueue\n"
           "  finalize             Set a2l_finalize_requested, poll for acknowledgement, print status\n"
           "  clean                Remove /xcpdata, /xcpqueue and lock files\n"
           "  help                 Print this help text\n\n"
           "Options:\n"
           "  -v, --verbose        Show additional details (offsets, verbose layout)\n"
           "  --timeout <ms>       Polling timeout for 'finalize' command (default: 5000 ms)\n",
           argv0);
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(int argc, char *argv[]) {

    enum class Cmd { Status, Finalize, Clean, Help } cmd = Cmd::Status;
    bool verbose = false;
    uint32_t timeout_ms = 5000;

    for (int i = 1; i < argc; ++i) {
        std::string arg(argv[i]);

        if (arg == "clean")
            cmd = Cmd::Clean;
#ifdef OPTION_SHM_MODE
        else if (arg == "status")
            cmd = Cmd::Status;
        else if (arg == "finalize")
            cmd = Cmd::Finalize;
#endif
        else if (arg == "help" || arg == "--help" || arg == "-h")
            cmd = Cmd::Help;
        else if (arg == "-v" || arg == "--verbose")
            verbose = true;
        else if ((arg == "--timeout") && i + 1 < argc) {
            timeout_ms = (uint32_t)std::stoul(argv[++i]);
        } else {
            fprintf(stderr, "Unknown argument: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }

    switch (cmd) {
    case Cmd::Status:
#ifdef OPTION_SHM_MODE
        return cmd_status(verbose);
#else // OPTION_SHM_MODE
        break;
#endif
    case Cmd::Finalize:
#ifdef OPTION_SHM_MODE
        return cmd_finalize(timeout_ms);
#else // OPTION_SHM_MODE
        break;
#endif
    case Cmd::Clean:
        return cmd_clean();

    case Cmd::Help:
        print_usage(argv[0]);
        return 0;
    }
    return 0;
}
