#!/usr/bin/env bash
# Remove stale XCPlite POSIX shared memory objects and optionally kill demo processes.
# Usage: ./tools/shm_cleanup.sh [--kill]
#   --kill   also kill any running XCPlite demo processes (hello_xcp, c_demo, cpp_demo, ...)
#
# Delegates to tools/shmtool (build/shmtool) when available — no Python or cc required.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SHMTOOL="${SCRIPT_DIR}/../build/shmtool"
DEMO_PROCS="hello_xcp|hello_xcp_cpp|c_demo|cpp_demo|multi_thread_demo|struct_demo|point_cloud_demo|no_a2l_demo|ptp4l_demo"

# Optionally kill running demo processes first so they can't re-create SHM after unlink
if [[ "$1" == "--kill" ]]; then
    echo "Killing XCPlite demo processes..."
    pkill -f "$DEMO_PROCS" 2>/dev/null && echo "  sent SIGTERM" || echo "  no matching processes"
    sleep 0.3
fi

# Use shmtool if it has been built, otherwise fall back to a minimal C snippet
if [[ -x "$SHMTOOL" ]]; then
    "$SHMTOOL" clean
else
    # Fallback: compile and run a tiny C helper (no Python dependency)
    HELPER=$(mktemp /tmp/shm_cleanup_XXXXXX)
    cat > "${HELPER}.c" << 'CSRC'
#include <stdio.h>
#include <fcntl.h>
#include <sys/mman.h>
static void unlink_shm(const char *name) {
    if (shm_unlink(name) == 0) printf("  removed SHM  %s\n", name);
    else                       printf("  not found    %s\n", name);
}
int main(void) {
    unlink_shm("/xcpdata");
    unlink_shm("/xcpqueue");
    // Remove lock files
    if (remove("/tmp/xcpdata.lock")  == 0) printf("  removed lock /tmp/xcpdata.lock\n");
    if (remove("/tmp/xcpqueue.lock") == 0) printf("  removed lock /tmp/xcpqueue.lock\n");
    printf("Done.\n");
    return 0;
}
CSRC
    if cc -o "$HELPER" "${HELPER}.c" 2>/dev/null; then
        "$HELPER"
    else
        echo "ERROR: shmtool not built and cc not available. Build the project first: cmake --build build --target shmtool" >&2
    fi
    rm -f "$HELPER" "${HELPER}.c"
fi
