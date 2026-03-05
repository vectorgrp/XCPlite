/*----------------------------------------------------------------------------
| File:
|   tools/shmtool/src/main.cpp
|
| Description:
|   XCPlite shared memory diagnostic tool.
|   Inspect, validate and clean the POSIX shared memory regions used by XCPlite
|   multi-process (OPTION_SHM_MODE) sessions.
|
|   Pulls in xcpLite.h so the struct layout is always in sync with the library.
|
| Usage:
|   shmtool [command] [options]
|
| Commands:
|   status   (default)  Print the contents of /xcpdata and /xcpqueue
|   clean               Remove /xcpdata, /xcpqueue and associated lock files
|   help                Print this help text
|
| Options:
|   -v, --verbose       Show additional low-level details (offsets, pad fields)
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
#include <string>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "xcp_cfg.h"
#include "xcplib_cfg.h"

#ifdef OPTION_SHM_MODE

#include "shm.h"
#include "xcpLite.h"

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Portable relaxed-atomic read from a C11 _Atomic field via volatile cast.
// Sufficient for a read-only diagnostic tool; no store-load barriers needed.
static inline uint32_t read_u32(const atomic_uint_least32_t *p) { return *reinterpret_cast<const volatile uint32_t *>(p); }

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
    // --- /xcpdata ---
    int fd = shm_open("/xcpdata", O_RDONLY, 0);
    if (fd < 0) {
        if (errno == ENOENT)
            printf("/xcpdata  : not found (no XCPlite SHM session active)\n");
        else
            fprintf(stderr, "/xcpdata  : shm_open failed: %s\n", strerror(errno));
        return (errno == ENOENT) ? 0 : 1;
    }

    struct stat st{};
    if (fstat(fd, &st) < 0) {
        fprintf(stderr, "/xcpdata  : fstat failed: %s\n", strerror(errno));
        close(fd);
        return 1;
    }
    size_t shm_size = (size_t)st.st_size;

    void *ptr = mmap(nullptr, shm_size, PROT_READ, MAP_SHARED, fd, 0);
    close(fd);
    if (ptr == MAP_FAILED) {
        fprintf(stderr, "/xcpdata  : mmap failed: %s\n", strerror(errno));
        return 1;
    }

    const auto *hdr = reinterpret_cast<const tShmHeader *>(reinterpret_cast<const uint8_t *>(ptr) + offsetof(tXcpData, shm_header));

    print_separator('=');
    printf("/xcpdata  mmap size  : %zu bytes\n", shm_size);

    if (shm_size < sizeof(tXcpData)) {
        fprintf(stderr, "  WARNING: SHM mmap size %zu < sizeof(tXcpData) %zu — stale or incompatible binary!\n", shm_size, sizeof(tXcpData));
    }
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

    if (verbose) {
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
    printf("  version            : %u.%u.%u\n", (ver >> 16) & 0xFF, (ver >> 8) & 0xFF, ver & 0xFF);
    printf("  declared size      : %u bytes  (this build: %zu)%s\n", hdr_size, sizeof(tXcpData), size_ok ? "" : "  *** MISMATCH ***");
    printf("  leader PID         : %u%s\n", lpid, (getpid() == (pid_t)lpid) ? "  (this process)" : "");
    printf("  app count          : %u / %d\n", napp, SHM_MAX_APP_COUNT);
    printf("  A2L finalize req'd : %s\n", bool_str(a2lrq));
    print_separator();

    if (!magic_ok) {
        munmap(ptr, shm_size);
        return 1;
    }

    // Per-application slots
    for (uint32_t i = 0; i < napp && i < SHM_MAX_APP_COUNT; ++i) {
        const tApp *app = &hdr->app_list[i];
        uint32_t ac = read_u32(&app->alive_counter);
        uint32_t fin = read_u32(&app->a2l_finalized);
        printf("  App %u:  %s %s epk=%s  pid=%u  %s\n", i, app->project_name[0] ? app->project_name : "(vacant)", app->is_server ? "[server]" : "", app->epk, app->pid,
               app->is_leader ? "[leader]" : "[follower]");
        printf("          a2l_name=%s  finalized=%s  alive=%u\n", fin ? app->a2l_name : "(pending)", bool_str(fin), ac);

        print_separator();
    }

    munmap(ptr, shm_size);

    // --- /xcpqueue ---
    fd = shm_open("/xcpqueue", O_RDONLY, 0);
    if (fd < 0) {
        printf("/xcpqueue : not found\n");
    } else {
        fstat(fd, &st);
        close(fd);
        printf("/xcpqueue : exists, size=%lld bytes\n", (long long)st.st_size);
    }

    return 0;
}

#endif

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
           "  clean                Remove /xcpdata, /xcpqueue and lock files\n"
           "  help                 Print this help text\n\n"
           "Options:\n"
           "  -v, --verbose        Show additional details (offsets, verbose layout)\n",
           argv0);
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(int argc, char *argv[]) {

    enum class Cmd { Status, Clean, Help } cmd = Cmd::Status;
    bool verbose = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg(argv[i]);

        if (arg == "clean")
            cmd = Cmd::Clean;
#ifdef OPTION_SHM_MODE
        else if (arg == "status")
            cmd = Cmd::Status;
#endif
        else if (arg == "help" || arg == "--help" || arg == "-h")
            cmd = Cmd::Help;
        else if (arg == "-v" || arg == "--verbose")
            verbose = true;
        else {
            fprintf(stderr, "Unknown argument: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }

    switch (cmd) {
    case Cmd::Status:
#ifdef OPTION_SHM_MODE
        return cmd_status(verbose);
#endif
    case Cmd::Clean:
        return cmd_clean();

    case Cmd::Help:
        print_usage(argv[0]);
        return 0;
    }
    return 0;
}
