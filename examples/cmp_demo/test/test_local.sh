#!/usr/bin/env bash
#
# test_local.sh - end to end test of the cmp_demo capture module, on this machine
#
# Runs the envelope codec unit test, then starts cmp_demo and drives it with fake_sink.py,
# which plays the part of the XCP tool: it queries the REST interface and tunnels XCP
# CONNECT / GET_STATUS / DISCONNECT through CMP.
#
# No veth pair and no network namespace are needed, unlike the plain raw Ethernet
# transport: with CMP over UDP (6.4.2) the outer transport is an ordinary UDP socket, the
# emulated ECU address only ever appears inside the CMP payload, and loopback is enough.
# Nothing here needs root.
#
# For the on-target (Raspberry Pi) variant see ../test.sh in the example root.
#
# Usage:  ./test/test_local.sh [build_dir]        (default: build)

set -u

DEMO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${1:-$DEMO_DIR/build}"
DEMO="$BUILD_DIR/cmp_demo"
CODEC_TEST="$BUILD_DIR/cmp_codec_test"

CMP_PORT=55555
REST_PORT=8080
WORK_DIR="$(mktemp -d)"
# The pcap goes to the folder the script was started from, not into WORK_DIR: it is the
# one artifact meant to be opened afterwards. Everything else the demo writes - demo.log,
# the .a2l and the .bin - stays in WORK_DIR and is thrown away with it.
OUT_DIR="$(pwd)"
PCAP="$OUT_DIR/cmp.pcap"
DEMO_PID=""

cleanup() {
    if [ -n "$DEMO_PID" ] && kill -0 "$DEMO_PID" 2>/dev/null; then
        kill -TERM "$DEMO_PID" 2>/dev/null
        wait "$DEMO_PID" 2>/dev/null
    fi
}
trap cleanup EXIT

fail() { echo "FAILED: $*"; exit 1; }

for binary in "$DEMO" "$CODEC_TEST"; do
    [ -x "$binary" ] || fail "$binary not found. Build first:
  cmake -B build -S . -Dxcplite_DIR=<install>/lib/cmake/xcplite
  cmake --build build"
done

echo "=============================================================="
echo "1. CMP envelope codec, against the ASAM CMP 1.1.0 sample files"
echo "=============================================================="
"$CODEC_TEST" || fail "the codec unit test did not pass"

echo
echo "=============================================================="
echo "2. cmp_demo end to end, driven by fake_sink.py"
echo "=============================================================="

# Refuse to start when something already holds the ports. Both sockets are opened with
# SO_REUSEADDR, so a leftover cmp_demo from an aborted run does not necessarily make the
# bind fail - it can instead leave the exchange talking to the STALE process while the one
# started here has already exited. That produces a confusing failure much further down.
for port in "$CMP_PORT" "$REST_PORT"; do
    holder=$(lsof -nP -iTCP:"$port" -iUDP:"$port" 2>/dev/null | awk 'NR>1 {print $2" ("$1")"}' | sort -u | tr '\n' ' ')
    [ -z "$holder" ] || fail "port $port is already in use by: $holder
  A cmp_demo from an earlier run is probably still alive. Stop it with:
    pkill -x cmp_demo"
done

cd "$WORK_DIR" || fail "cannot enter $WORK_DIR"

"$DEMO" --listen "$CMP_PORT" --rest-port "$REST_PORT" > demo.log 2>&1 &
DEMO_PID=$!

# Wait for the REST port to accept connections rather than grepping the log: it is the
# only readiness signal that does not depend on how stdout happens to be buffered.
wait_for_port() {
    python3 - "$1" <<'PY'
import socket, sys, time
port = int(sys.argv[1])
deadline = time.time() + 10.0
while time.time() < deadline:
    try:
        with socket.create_connection(("127.0.0.1", port), timeout=0.2):
            sys.exit(0)
    except OSError:
        time.sleep(0.1)
sys.exit(1)
PY
}
wait_for_port "$REST_PORT" || {
    echo "--- cmp_demo log ---"; cat demo.log
    fail "cmp_demo did not start listening on port $REST_PORT within 10s"
}

# The port answering does not prove OUR process is the one answering it.
kill -0 "$DEMO_PID" 2>/dev/null || {
    echo "--- cmp_demo log ---"; cat demo.log
    fail "the cmp_demo started here has already exited, yet port $REST_PORT answers.
  Something else is serving these ports - see the log above."
}

grep -E "CMP capture module:|CMP transport:|CMP frame budget:" demo.log
echo

python3 "$DEMO_DIR/test/fake_sink.py" \
    --target "127.0.0.1:$CMP_PORT" \
    --rest "127.0.0.1:$REST_PORT" \
    --pcap "$PCAP" || fail "fake_sink.py reported a problem"

echo
echo "=============================================================="
echo "3. Multicast discovery (12.1.1)"
echo "=============================================================="
"$DEMO_DIR/test/discovery_probe.py" --expect-http "$REST_PORT" \
    || fail "the capture module did not answer CMP_CM_DISCOVERY"

cleanup
DEMO_PID=""

echo
echo "=============================================================="
echo "4. Capture module counters"
echo "=============================================================="
grep -E "^  CMP: " demo.log || fail "no summary line in the cmp_demo log"

# Anything dropped or refused means the two sides disagree about the wire format
if grep -qE "dropped [1-9]|refused" demo.log; then
    echo "--- cmp_demo log ---"; cat demo.log
    fail "the capture module dropped or refused messages"
fi

echo
echo "Capture file for Wireshark: $PCAP"
echo "  (its ASAM CMP dissector keys on EtherType 0x99FE)"
echo
echo "PASSED"
