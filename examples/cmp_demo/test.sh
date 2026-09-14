#!/bin/bash

# On-target test for the cmp_demo example project
#
# Syncs the sources to the target, builds xcplite and the demo there, starts the emulated
# capture module and checks it from this machine:
#   1. the CMP envelope codec unit test, on the target
#   2. the REST interface (12.3), including that transmission is advertised
#   3. the status of the CMP endpoint
#   4. a hardcoded XCP CONNECT tunnelled through CMP, with the response decoded
#
# Unlike examples/udp_raw_demo/test.sh this needs NO setcap and NO free IP address on the
# network: the CMP transport is an ordinary UDP socket, and the emulated ECU address only
# ever appears inside the CMP payload.
#
# For a purely local run on this machine, without a target, use test/test_local.sh.
#
# Prerequisites:
# - The target must be reachable via SSH, with rsync, cmake and a C compiler installed
# - This machine must have rsync, curl and python3

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

#======================================================================================================================
# Parameters
#======================================================================================================================

# Target connection details.
# All parameters in this block can be overridden from the environment, so a target on a
# different address needs no edit here:
#   TARGET_HOST=192.168.0.205 ./test.sh
TARGET_USER="${TARGET_USER:-rainer}"
TARGET_HOST="${TARGET_HOST:-192.168.0.206}"
TARGET_PATH="${TARGET_PATH:-~/XCPlite-CMP}"
TARGET_BINARY="cmp_demo"

# Where the xcplite library is installed on the target, and where the demo is built
TARGET_INSTALL_DIR="xcplite-install"
TARGET_DEMO_DIR="examples/cmp_demo"

# Build type for the target executables: Release, RelWithDebInfo or Debug
BUILD_TYPE="${BUILD_TYPE:-RelWithDebInfo}"

# Ports the capture module serves on the target
CMP_PORT="${CMP_PORT:-55555}" # UDP, CMP messages (12.2.2.2 uses this port in its examples)
REST_PORT="${REST_PORT:-8080}" # TCP, REST interface. 12.3 says "should" be 80, which would need root

# Identity of the emulated capture module. The XCP CONNECT below is built for exactly
# these values, so change them here and nowhere else.
DEVICE_ID="1"
INTERFACE_ID="1"

# The emulated ECU behind the capture module. This address lives ONLY inside the CMP
# payload, so it does not have to be free on the network and must not be pinged.
ECU_IP="192.168.0.220"
ECU_PORT="5555"
ECU_MAC="02:00:00:00:00:01" # derived by the demo from DEVICE_ID

# Us, as the Data Sink, inside the tunnelled frame
SINK_IP="192.168.0.10"
SINK_MAC="02:00:00:00:FF:01"
SINK_PORT="50000"
SINK_DEVICE_ID="8738" # 0x2222, our own CMP DeviceId

FAILURES=0

#======================================================================================================================
# Helpers
#======================================================================================================================

step_failed() {
    echo "❌ FAILED: $*"
    FAILURES=$((FAILURES + 1))
}

# Stop the demo on the target, however this script ends
SSH_PID=""
cleanup() {
    ssh "$TARGET_USER@$TARGET_HOST" "pkill -x $TARGET_BINARY" 2> /dev/null
    if [ -n "$SSH_PID" ]; then
        wait "$SSH_PID" 2> /dev/null
        SSH_PID=""
    fi
}
trap cleanup EXIT

for tool in rsync ssh curl python3; do
    if ! command -v "$tool" > /dev/null 2>&1; then
        echo "❌ FAILED: '$tool' is required on this machine"
        exit 1
    fi
done

echo "========================================================================================================"
echo "cmp_demo on-target test"
echo "  target        $TARGET_USER@$TARGET_HOST:$TARGET_PATH"
echo "  capture module  CMP on UDP $TARGET_HOST:$CMP_PORT, REST on $TARGET_HOST:$REST_PORT"
echo "  emulated ECU    $ECU_IP:$ECU_PORT, MAC $ECU_MAC (inside the CMP payload only)"
echo "========================================================================================================"

#======================================================================================================================
# Sync target
#======================================================================================================================

echo ""
echo "Sync target ..."
# Exclusions come FIRST: rsync applies the first matching rule, so these have to precede the
# --include patterns below. Build artifacts must not be synced - a CMakeCache.txt carried over
# from this machine records the local source and build paths, and cmake on the target then
# refuses to configure. This is the same list as examples/cmp_demo/.gitignore.
rsync -avz --delete \
    --exclude='build/' \
    --exclude='build-*/' \
    --exclude='*.a2l' \
    --exclude='*.bin' \
    --exclude='*.log' \
    --include='/build.sh' \
    --include='/CMakeLists.txt' \
    --include='/cmake/***' \
    --include='/inc/***' \
    --include='/src/***' \
    --include='/examples/' \
    --include='/examples/cmp_demo/***' \
    --exclude='*' \
    "$REPO_ROOT/" "$TARGET_USER@$TARGET_HOST:$TARGET_PATH/" 1> /dev/null
if [ $? -ne 0 ]; then
    echo "❌ FAILED: Rsync with target"
    exit 1
fi

#======================================================================================================================
# Build on target
#======================================================================================================================

# Resolve TARGET_PATH to an absolute path on the target, once.
# A leading ~ is expanded by the remote shell only at the start of a word. After an '=' it is
# not: bash expands install=~/... because that looks like an assignment, but leaves
# -Dxcplite_DIR=~/... alone because -Dxcplite_DIR is not a valid identifier, and dash expands
# neither. Every remote path below therefore uses TARGET_ABS, never the ~ form.
TARGET_ABS=$(ssh "$TARGET_USER@$TARGET_HOST" "cd $TARGET_PATH && pwd")
if [ $? -ne 0 ] || [ -z "$TARGET_ABS" ]; then
    echo "❌ FAILED: cannot resolve $TARGET_PATH on the target"
    exit 1
fi

# cmp_demo is a STANDALONE cmake project: it consumes an INSTALLED xcplite rather than being
# built from the root CMakeLists. So the library is built and installed first, then the demo
# is pointed at that install - exactly as the README describes for a manual build.

echo "Build and install the xcplite library (raw configuration) on the target ..."
ssh "$TARGET_USER@$TARGET_HOST" \
    "cd $TARGET_ABS && ./build.sh $BUILD_TYPE raw lib install=$TARGET_ABS/$TARGET_INSTALL_DIR" 1> /dev/null
if [ $? -ne 0 ]; then
    echo "❌ FAILED: Build/install of libxcplite on the target"
    exit 1
fi

echo "Build cmp_demo on the target ..."
ssh "$TARGET_USER@$TARGET_HOST" "cd $TARGET_ABS/$TARGET_DEMO_DIR \
    && cmake -B build -S . -DCMAKE_BUILD_TYPE=$BUILD_TYPE \
             -Dxcplite_DIR=$TARGET_ABS/$TARGET_INSTALL_DIR/lib/cmake/xcplite \
    && cmake --build build --parallel" 1> /dev/null
if [ $? -ne 0 ]; then
    echo "❌ FAILED: Build of cmp_demo on the target"
    exit 1
fi

# The override works because libxcplite is a STATIC library: the linker only pulls an archive
# member in to resolve an UNDEFINED symbol, and this project already defines all six eth_hal_*
# functions. On Linux socket_raw_hal_linux.o is present in the archive, so looking at the
# archive proves nothing - the LINKED BINARY is what has to be checked. The built in backend
# is the only place that mentions cap_net_raw, so its absence means it was not pulled in.
echo "Check that the built in AF_PACKET backend is not linked in ..."
ssh "$TARGET_USER@$TARGET_HOST" \
    "grep -a -q 'cap_net_raw+ep' $TARGET_ABS/$TARGET_DEMO_DIR/build/$TARGET_BINARY"
if [ $? -eq 0 ]; then
    step_failed "the built in AF_PACKET backend is linked into $TARGET_BINARY - check that libxcplite is a STATIC library"
else
    echo "✅ the built in AF_PACKET backend is not linked in, this project supplies the HAL"
fi

#======================================================================================================================
# 1. Envelope codec unit test, on the target
#======================================================================================================================

echo ""
echo "========================================================================================================"
echo "1. CMP envelope codec on the target, against the ASAM CMP 1.1.0 sample files"
echo "========================================================================================================"
ssh "$TARGET_USER@$TARGET_HOST" "$TARGET_ABS/$TARGET_DEMO_DIR/build/cmp_codec_test"
if [ $? -ne 0 ]; then
    step_failed "the codec unit test did not pass on the target"
fi

#======================================================================================================================
# Start the capture module on the target
#======================================================================================================================

echo ""
echo "Start $TARGET_BINARY on the target ..."

# Clear any leftover from an aborted run first. Both sockets use SO_REUSEADDR, so a stale
# process does not reliably make the bind fail - it can instead answer in place of the one
# started here, and every check below would then be testing the wrong process.
ssh "$TARGET_USER@$TARGET_HOST" "pkill -x $TARGET_BINARY" 2> /dev/null

# No --sink: the Data Sink address is learned from the first CMP message we send, which
# avoids having to know this machine's address on the target's network.
ssh "$TARGET_USER@$TARGET_HOST" "cd $TARGET_ABS/$TARGET_DEMO_DIR \
    && ./build/$TARGET_BINARY --listen $CMP_PORT --rest-port $REST_PORT \
           --ip $ECU_IP --port $ECU_PORT \
           --device-id $DEVICE_ID --interface-id $INTERFACE_ID > cmp_demo.log 2>&1" &
SSH_PID=$!

# Note: pgrep/pkill -x matches the process NAME. Do NOT use -f here: it matches the full
# command line, and the ssh command line on the target contains "$TARGET_BINARY" itself, so
# -f would match the ssh session as well and terminate it.
ssh "$TARGET_USER@$TARGET_HOST" "for i in \$(seq 1 20); do pgrep -x $TARGET_BINARY > /dev/null && exit 0; sleep 0.5; done; exit 1"
if [ $? -ne 0 ]; then
    echo "❌ FAILED: $TARGET_BINARY is not running on the target"
    ssh "$TARGET_USER@$TARGET_HOST" "cat $TARGET_ABS/$TARGET_DEMO_DIR/cmp_demo.log" 2> /dev/null
    exit 1
fi

# Being in the process table is not the same as serving: wait for the REST port to answer.
echo "Waiting for the REST interface on $TARGET_HOST:$REST_PORT ..."
for i in $(seq 1 20); do
    curl -s -m 2 -o /dev/null "http://$TARGET_HOST:$REST_PORT/asam-cmp/version-info" && break
    sleep 0.5
done
curl -s -m 2 -o /dev/null "http://$TARGET_HOST:$REST_PORT/asam-cmp/version-info"
if [ $? -ne 0 ]; then
    echo "❌ FAILED: the REST interface did not come up on $TARGET_HOST:$REST_PORT"
    ssh "$TARGET_USER@$TARGET_HOST" "cat $TARGET_ABS/$TARGET_DEMO_DIR/cmp_demo.log" 2> /dev/null
    exit 1
fi

#======================================================================================================================
# 2. REST interface
#======================================================================================================================

echo ""
echo "========================================================================================================"
echo "2. REST interface (12.3)"
echo "========================================================================================================"

for path in "/asam-cmp/version-info" \
            "/asam-cmp/v1/identification" \
            "/asam-cmp/v1/interfaces" \
            "/asam-cmp/v1/measurement"; do
    echo ""
    echo "GET $path"
    body=$(curl -s -m 5 -w '\n%{http_code}' "http://$TARGET_HOST:$REST_PORT$path")
    code=$(echo "$body" | tail -1)
    json=$(echo "$body" | sed '$d')
    if [ "$code" = "000" ]; then
        step_failed "GET $path: could not connect to $TARGET_HOST:$REST_PORT at all"
        continue
    elif [ "$code" != "200" ]; then
        step_failed "GET $path returned HTTP $code"
        continue
    fi
    echo "$json" | python3 -m json.tool 2> /dev/null || echo "$json"
done

# 7.2.2: "Support for transmission is optional in the Capture Module, the data sink can use
# the REST API to detect if transmission is supported or not". The Transmitter object of
# /interfaces is that signal - without it a tool may never inject and XCP cannot connect.
echo ""
echo "Checking that transmission is advertised ..."
curl -s -m 5 "http://$TARGET_HOST:$REST_PORT/asam-cmp/v1/interfaces" | python3 -c '
import json, sys
try:
    interfaces = json.load(sys.stdin).get("Interfaces", [])
except ValueError as exc:
    print("  cannot parse the response: %s" % exc); sys.exit(1)
if not interfaces:
    print("  no interfaces reported"); sys.exit(1)
transmitter = interfaces[0].get("Transmitter")
if transmitter is None:
    print("  no Transmitter object: a Data Sink would conclude that this capture module")
    print("  cannot transmit, and would never inject (7.2.2)"); sys.exit(1)
bitmask = transmitter.get("TransmissionSupportBitmask", 0)
if not bitmask & 1:
    print("  TransmissionSupportBitmask=0x%02X has TIMESTAMP_IMMEDIATE clear" % bitmask); sys.exit(1)
mtu = transmitter.get("AggregationMtu", 0)
print("  transmission supported, TransmissionSupportBitmask=0x%02X (TIMESTAMP_IMMEDIATE)" % bitmask)
print("  AggregationMtu=%u, so the largest inner Ethernet frame is %u bytes" % (mtu, mtu - 34))
'
if [ $? -ne 0 ]; then
    step_failed "the REST interface does not advertise transmission support"
else
    echo "✅ transmission is advertised"
fi

#======================================================================================================================
# 3. Status of the CMP endpoint
#======================================================================================================================

echo ""
echo "========================================================================================================"
echo "3. Status of the CMP endpoint"
echo "========================================================================================================"
curl -s -m 5 "http://$TARGET_HOST:$REST_PORT/asam-cmp/v1/measurement" | python3 -c '
import json, sys
try:
    status = json.load(sys.stdin)
except ValueError as exc:
    print("  cannot parse the response: %s" % exc); sys.exit(1)
print("  CaptureModuleState : %s" % status.get("CaptureModuleState"))
print("  Counters           : %s" % status.get("Message"))
for stream in status.get("StateOfStreams", []):
    print("  Stream %-3s         : %s" % (stream.get("StreamId"), stream.get("State")))
sys.exit(0 if status.get("CaptureModuleState") == "active" else 1)
'
if [ $? -ne 0 ]; then
    step_failed "the capture module does not report itself as active"
fi

# The endpoint itself: a CMP message sent to a closed UDP port would be answered with an
# ICMP port unreachable, which the CONNECT below would surface as a confusing timeout.
echo ""
echo "CMP endpoint: UDP $TARGET_HOST:$CMP_PORT"
LISTENING=$(ssh "$TARGET_USER@$TARGET_HOST" \
    "if command -v ss > /dev/null; then ss -lun; elif command -v netstat > /dev/null; then netstat -lun; else echo NO_TOOL; fi" 2> /dev/null)
if echo "$LISTENING" | grep -q "NO_TOOL"; then
    echo "ℹ️  neither ss nor netstat on the target, skipping the port check"
elif echo "$LISTENING" | grep -q ":$CMP_PORT"; then
    echo "✅ UDP port $CMP_PORT is open"
else
    step_failed "UDP port $CMP_PORT is not open on the target"
fi

#======================================================================================================================
# 4. Hardcoded XCP CONNECT, tunnelled through CMP
#======================================================================================================================

echo ""
echo "========================================================================================================"
echo "4. Multicast discovery (12.1.1)"
echo "========================================================================================================"

# Sends CMP_CM_DISCOVERY to 239.255.0.0:5556 from THIS machine and decodes the answer, so it
# also proves multicast crosses the link to the target. Unlike the local run, the module
# reports a real LAN address, prefix length and MAC here.
"$SCRIPT_DIR/test/discovery_probe.py" --expect-http "$REST_PORT"
if [ $? -ne 0 ]; then
    step_failed "the capture module did not answer CMP_CM_DISCOVERY"
    echo "   If every other check passes, multicast is most likely not forwarded between this"
    echo "   machine and the target, rather than the responder being broken."
fi

echo ""
echo "========================================================================================================"
echo "5. XCP CONNECT through the capture module"
echo "========================================================================================================"

# The XCP command is the hardcoded constant FF 00 (CONNECT, normal mode), wrapped in the XCP
# on Ethernet transport header and then in a complete Ethernet/IPv4/UDP frame, which is what
# a capture module transmits on behalf of the tool. The frame is assembled here from the
# parameters at the top of this script rather than pasted in as a fixed hex blob, so that
# changing e.g. ECU_IP cannot silently leave a stale IPv4 header checksum behind. The exact
# bytes that go on the wire are printed below.
#
# For the fuller exchange - CONNECT, GET_STATUS, DISCONNECT, sequence counter checks and a
# Wireshark capture file - use test/fake_sink.py, which this check is a cut down version of.

CMP_TARGET="$TARGET_HOST" \
CMP_PORT="$CMP_PORT" \
ECU_IP="$ECU_IP" ECU_PORT="$ECU_PORT" ECU_MAC="$ECU_MAC" \
SINK_IP="$SINK_IP" SINK_PORT="$SINK_PORT" SINK_MAC="$SINK_MAC" \
SINK_DEVICE_ID="$SINK_DEVICE_ID" INTERFACE_ID="$INTERFACE_ID" \
python3 - <<'PY'
import os, socket, struct, sys

env = os.environ
target = (env["CMP_TARGET"], int(env["CMP_PORT"]))
ecu_ip, ecu_port, ecu_mac = env["ECU_IP"], int(env["ECU_PORT"]), env["ECU_MAC"]
sink_ip, sink_port, sink_mac = env["SINK_IP"], int(env["SINK_PORT"]), env["SINK_MAC"]
device_id, interface_id = int(env["SINK_DEVICE_ID"]), int(env["INTERFACE_ID"])


def checksum16(data):
    total = 0
    for i in range(0, len(data), 2):
        total += (data[i] << 8) | data[i + 1]
    while total >> 16:
        total = (total & 0xFFFF) + (total >> 16)
    return (~total) & 0xFFFF


# --- the XCP command, hardcoded ---------------------------------------------------
xcp_packet = bytes([0xFF, 0x00])                       # CONNECT, mode 0
xcp = struct.pack("<HH", len(xcp_packet), 0) + xcp_packet   # len + ctr, little endian

# --- the inner Ethernet/IPv4/UDP frame the capture module has to transmit ----------
udp = struct.pack(">HHHH", sink_port, ecu_port, 8 + len(xcp), 0) + xcp
ip = struct.pack(">BBHHHBBH4s4s", 0x45, 0x00, 20 + len(udp), 1, 0x4000, 64, 17, 0,
                 socket.inet_aton(sink_ip), socket.inet_aton(ecu_ip))
ip = ip[:10] + struct.pack(">H", checksum16(ip)) + ip[12:]
frame = bytes.fromhex(ecu_mac.replace(":", "")) + bytes.fromhex(sink_mac.replace(":", "")) \
    + struct.pack(">H", 0x0800) + ip + udp

# --- the CMP Transmit Data Message (7.2.2) with an Ethernet payload (7.3.8) --------
data = frame + b"\x00\x00\x00\x00"                     # dummy FCS, FCS_SENDING = 0
payload = struct.pack(">HHH", 0, 0, len(data)) + data
message = (struct.pack(">BBHBBH", 0x01, 0, device_id, 0x04, 0, 0)      # CMP header
           + struct.pack(">QIIIBBH", 0, 0, interface_id, 0, 0, 0x08, len(payload))
           + payload)

print("  TX_DATA_MSG, %u bytes, carrying a %u byte Ethernet frame with XCP CONNECT:"
      % (len(message), len(frame)))
for off in range(0, len(message), 24):
    print("    %04x  %s" % (off, message[off:off + 24].hex(" ")))

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.settimeout(5.0)
try:
    sock.sendto(message, target)
except OSError as exc:
    # EHOSTUNREACH/ENETUNREACH here mean the datagram never left this machine: the route
    # lookup or the ARP for the target failed. That is a plain connectivity problem, not a
    # CMP one, so say so rather than letting it surface as a traceback.
    print("  -> could not send to %s:%u: %s" % (target[0], target[1], exc))
    print("     The datagram never left this machine. Check that the target is reachable")
    print("     (ping %s) and that nothing filters UDP to port %u." % (target[0], target[1]))
    sys.exit(1)
print("  -> sent to %s:%u from UDP port %u" % (target[0], target[1], sock.getsockname()[1]))

# --- the response, a Captured Data Message (7.2.1) --------------------------------
try:
    response, sender = sock.recvfrom(65535)
except socket.timeout:
    print("  <- TIMEOUT: no CMP message came back within 5s")
    sys.exit(1)
except OSError as exc:
    # A cached ICMP port unreachable arrives here: the target answered, but nothing is
    # listening on the CMP port.
    print("  <- no response: %s" % exc)
    print("     The target is reachable but did not accept the message on UDP port %u." % target[1])
    sys.exit(1)

if len(response) < 8 + 16:
    print("  <- %u bytes, too short to be a CMP data message" % len(response))
    sys.exit(1)
version, _res, dev, msg_type, stream_id, seq = struct.unpack_from(">BBHBBH", response, 0)
if version < 1 or msg_type != 0x01:
    print("  <- version %u message type 0x%02X, expected a Captured Data Message"
          % (version, msg_type))
    sys.exit(1)
timestamp, iface, flags, ptype, plen = struct.unpack_from(">QIBBH", response, 8)
print("  <- CAP_DATA_MSG from %s:%u, %u bytes" % (sender[0], sender[1], len(response)))
print("     DeviceId 0x%04X, StreamId %u, StreamSequenceCounter %u" % (dev, stream_id, seq))
print("     InterfaceId %u, PayloadType 0x%02X, capture timestamp %u ns, INSYNC=%u"
      % (iface, ptype, timestamp, (flags >> 1) & 1))
if ptype != 0x08:
    print("     payload is not an Ethernet Data Message")
    sys.exit(1)

body = response[24:24 + plen]
_pflags, _pres, dlen = struct.unpack_from(">HHH", body, 0)
inner = body[6:6 + dlen][:-4]                          # strip the FCS
ihl = (inner[14] & 0x0F) * 4
udp_off = 14 + ihl
udp_len = struct.unpack_from(">H", inner, udp_off + 4)[0]
xcp_payload = inner[udp_off + 8: udp_off + udp_len]
xlen, xctr = struct.unpack_from("<HH", xcp_payload, 0)
packet = xcp_payload[4:4 + xlen]

print("     inner frame %u bytes, UDP %u->%u"
      % (len(inner), *struct.unpack_from(">HH", inner, udp_off)))
if packet[0] != 0xFF:
    print("  <- XCP error, PID 0x%02X" % packet[0])
    sys.exit(1)
resource, comm_mode, max_cto, max_dto, proto, transport = struct.unpack_from("<BBBHBB", packet, 1)
print("  <- CONNECT positive response (PID 0xFF), XCP counter %u" % xctr)
print("     MAX_CTO=%u  MAX_DTO=%u  resource=0x%02X  comm_mode_basic=0x%02X"
      % (max_cto, max_dto, resource, comm_mode))
print("     protocol layer version %u, transport layer version %u" % (proto, transport))
sys.exit(0)
PY
if [ $? -ne 0 ]; then
    step_failed "the hardcoded XCP CONNECT was not answered"
else
    echo "✅ XCP CONNECT answered through the CMP tunnel"
fi

#======================================================================================================================
# Stop and report
#======================================================================================================================

echo ""
echo "========================================================================================================"
echo "Capture module log (tail)"
echo "========================================================================================================"
cleanup
ssh "$TARGET_USER@$TARGET_HOST" "grep -E 'CMP |WARNING|ERROR' $TARGET_ABS/$TARGET_DEMO_DIR/cmp_demo.log" 2> /dev/null

echo ""
echo "========================================================================================================"
if [ "$FAILURES" -eq 0 ]; then
    echo "✅ SUCCESS: all checks passed"
    echo "========================================================================================================"
    exit 0
fi
echo "❌ FAILED: $FAILURES check(s) did not pass"
echo "========================================================================================================"
exit 1
