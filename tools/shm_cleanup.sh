#!/usr/bin/env bash
# Remove stale XCPlite POSIX shared memory objects and optionally kill demo processes.
# Usage: ./tools/shm_cleanup.sh [--kill]
#   --kill   also kill any running XCPlite demo processes (hello_xcp, c_demo, cpp_demo, ...)

LOCK_FILES=("/tmp/xcpdata.lock" "/tmp/xcpqueue.lock")
DEMO_PROCS="hello_xcp|hello_xcp_cpp|c_demo|cpp_demo|multi_thread_demo|struct_demo|point_cloud_demo|no_a2l_demo|ptp4l_demo"

# Optionally kill running demo processes first so they can't re-create SHM after unlink
if [[ "$1" == "--kill" ]]; then
    echo "Killing XCPlite demo processes..."
    pkill -f "$DEMO_PROCS" 2>/dev/null && echo "  sent SIGTERM" || echo "  no matching processes"
    sleep 0.3
fi

python3 - <<'EOF'
import ctypes, sys

lib = ctypes.CDLL(None)
names = ["/xcpdata", "/xcpqueue"]
for name in names:
    ret = lib.shm_unlink(name.encode())
    if ret == 0:
        print(f"  removed SHM  {name}")
    else:
        print(f"  not found    {name}")
EOF

# Remove lock files used by the flock-based leader election
for f in "${LOCK_FILES[@]}"; do
    if [ -f "$f" ]; then
        rm -f "$f" && echo "  removed lock $f"
    fi
done

echo "Done."
