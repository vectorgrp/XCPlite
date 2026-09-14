#!/bin/bash
# test_socket_raw.sh - network test setup for the raw Ethernet transport (Linux only)
#
# Phase A of the bring-up described in docs/SOCKET_RAW.md: creates an isolated veth pair
# with the target in its own network namespace, so the kernel IP stack does not compete
# with socket_raw.c - it would otherwise answer the ARP itself and send ICMP port
# unreachable for the XCP UDP port.
#
# Needs root (network namespaces and AF_PACKET). Not part of test/test.sh for that reason.
#
# Usage:
#   sudo ./test/test_socket_raw.sh [--keep]
#     --keep   leave the namespace and the demo running for manual tests
#
# Once running, from this host:
#   ping 192.168.90.2
#   arping -I veth0 192.168.90.2
#   tcpdump -i veth0 -nn -e -vv
#   xcpclient --addr 192.168.90.2 --port 5555

set -u

NS=xcpraw
HOST_IF=veth0
TARGET_IF=veth1
HOST_IP=192.168.90.1
TARGET_IP=192.168.90.2
PORT=5555

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DEMO="$SCRIPT_DIR/../build-raw/udp_raw_demo"
KEEP=false
[[ "${1:-}" == "--keep" ]] && KEEP=true

DEMO_PID=""

cleanup() {
    echo ""
    echo "Cleaning up..."
    [[ -n "$DEMO_PID" ]] && kill "$DEMO_PID" 2>/dev/null
    ip netns del "$NS" 2>/dev/null
    ip link del "$HOST_IF" 2>/dev/null
    echo "Done."
}

if [[ $EUID -ne 0 ]]; then
    echo "ERROR: needs root (network namespaces and AF_PACKET)."
    echo "  sudo $0"
    exit 1
fi

if [[ ! -x "$DEMO" ]]; then
    echo "ERROR: $DEMO not found or not executable."
    echo "  Build it first:  ./build.sh raw examples"
    exit 1
fi

trap cleanup EXIT INT TERM

# Remove leftovers of a previous run
ip netns del "$NS" 2>/dev/null
ip link del "$HOST_IF" 2>/dev/null

echo "Setting up namespace '$NS' with $HOST_IF <-> $TARGET_IF ..."
ip netns add "$NS" || exit 1
ip link add "$HOST_IF" type veth peer name "$TARGET_IF" || exit 1
ip link set "$TARGET_IF" netns "$NS" || exit 1
ip addr add "$HOST_IP/24" dev "$HOST_IF" || exit 1
ip link set "$HOST_IF" up || exit 1
# Deliberately NO IP address on the target side: socket_raw.c owns $TARGET_IP, not the kernel
ip netns exec "$NS" ip link set "$TARGET_IF" up || exit 1
ip netns exec "$NS" ip link set lo up

echo "  host   : $HOST_IF   $HOST_IP"
echo "  target : $TARGET_IF (in netns $NS, no kernel IP) -> xcplib owns $TARGET_IP:$PORT"
echo ""

echo "Starting udp_raw_demo in the namespace ..."
ip netns exec "$NS" "$DEMO" --if "$TARGET_IF" --ip "$TARGET_IP" --port "$PORT" &
DEMO_PID=$!
sleep 2

if ! kill -0 "$DEMO_PID" 2>/dev/null; then
    echo "ERROR: udp_raw_demo exited immediately - see its output above."
    exit 1
fi

FAILED=0

echo ""
echo "=== 1. ARP: does the target answer a request for $TARGET_IP? ==="
if command -v arping >/dev/null 2>&1; then
    if arping -I "$HOST_IF" -c 3 -w 3 "$TARGET_IP" >/dev/null 2>&1; then
        echo "  OK - ARP reply received"
    else
        echo "  FAILED - no ARP reply"
        FAILED=1
    fi
else
    echo "  SKIPPED - arping not installed"
fi

echo ""
echo "=== 2. ICMP: does the target answer a ping? ==="
echo "    (this also proves the Ethernet HAL, MAC filter, IPv4 header and its checksum)"
if ping -c 3 -W 2 "$TARGET_IP" >/dev/null 2>&1; then
    echo "  OK - ping replies received"
else
    echo "  FAILED - no ping reply"
    FAILED=1
fi

echo ""
echo "=== 3. XCP: CONNECT over the raw transport ==="
python3 - "$TARGET_IP" "$PORT" <<'PYEOF'
import socket, struct, sys
ip, port = sys.argv[1], int(sys.argv[2])
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); s.settimeout(3)
pkt = b'\xFF\x00'                                    # CONNECT, mode 0
s.sendto(struct.pack('<HH', len(pkt), 0) + pkt, (ip, port))
try:
    data, addr = s.recvfrom(2048)
    dlc, ctr = struct.unpack('<HH', data[:4])
    body = data[4:4+dlc]
    if body and body[0] == 0xFF:
        print("  OK - positive CONNECT response from %s: %s" % (addr[0], body.hex(' ')))
        sys.exit(0)
    print("  FAILED - unexpected response: %s" % body.hex(' '))
    sys.exit(1)
except socket.timeout:
    print("  FAILED - no response to CONNECT")
    sys.exit(1)
PYEOF
[[ $? -ne 0 ]] && FAILED=1

echo ""
if [[ $FAILED -eq 0 ]]; then
    echo "=================================================="
    echo " ALL CHECKS PASSED"
    echo "=================================================="
else
    echo "=================================================="
    echo " SOME CHECKS FAILED"
    echo "=================================================="
fi

if $KEEP; then
    echo ""
    echo "Leaving the setup running (--keep). Connect with:"
    echo "  xcpclient --addr $TARGET_IP --port $PORT"
    echo "  tcpdump -i $HOST_IF -nn -e -vv"
    echo "Press Ctrl-C to stop and clean up."
    wait "$DEMO_PID"
fi

exit $FAILED
