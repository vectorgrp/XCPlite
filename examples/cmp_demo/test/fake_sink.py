#!/usr/bin/env python3
"""
fake_sink.py - a minimal ASAM CMP Data Sink, i.e. the half that CANape will play.

It talks to cmp_demo over CMP on UDP (ASAM CMP 1.1.0, section 6.4.2):

  sink -> module   Transmit Data Messages (TX_DATA_MSG, 0x04) carrying an Ethernet frame
                   with an XCP command inside, so the emulated ECU can be reached at all
  module -> sink   Captured Data Messages (CAP_DATA_MSG, 0x01) carrying the ECU's frames

Nothing but the Python standard library is needed: the Ethernet, IPv4 and UDP headers of
the inner frame are built by hand, which is also the point - it proves the capture module
really does tunnel an ordinary frame rather than something bespoke.

With --pcap it also writes every CMP message it sends and receives to a capture file.
Wireshark has a built in ASAM CMP dissector keyed on EtherType 0x99FE, so the messages are
framed for the Ethernet transport option (6.4.1) in that file and dissect automatically.
The CMP message bytes are identical under both transport options - only the outer framing
differs - so this validates the envelope, it is not a literal recording of the wire.

Usage:
  ./fake_sink.py --target 127.0.0.1:55555 [--ecu-ip 192.168.0.220] [--rest 127.0.0.1:8080]
  ./fake_sink.py --target 127.0.0.1:55555 --pcap cmp.pcap
"""

import argparse
import json
import socket
import struct
import sys
import time
import urllib.error
import urllib.request

# ---------------------------------------------------------------------------- CMP

CMP_VERSION = 0x01
CMP_MSG_CAP_DATA = 0x01
CMP_MSG_TX_DATA = 0x04
CMP_PAYLOAD_ETHERNET = 0x08

MSG_TYPE_NAMES = {0x01: "CAP_DATA_MSG", 0x02: "CTRL_MSG", 0x03: "STATUS_MSG",
                  0x04: "TX_DATA_MSG", 0xFF: "VENDOR_MSG"}


def cmp_header(device_id, message_type, stream_id, seq):
    """CMP header, 8 bytes, big endian (6.2.1)."""
    return struct.pack(">BBHBBH", CMP_VERSION, 0, device_id, message_type, stream_id, seq)


def cmp_wrap_transmit(device_id, stream_id, seq, interface_id, frame):
    """Transmit Data Message (7.2.2) with an Ethernet payload (7.3.8)."""
    data = frame + b"\x00\x00\x00\x00"          # dummy FCS, FCS_SENDING = 0
    payload = struct.pack(">HHH", 0, 0, len(data)) + data
    tx_header = struct.pack(
        ">QIIIBBH",
        0,                    # Timestamp 0: send immediately
        0,                    # Deadline 0: none
        interface_id,
        0,                    # Transmission Options: 0 for Ethernet payloads
        0,                    # Common Flags: SEG = 00, absolute mode
        CMP_PAYLOAD_ETHERNET,
        len(payload),
    )
    return cmp_header(device_id, CMP_MSG_TX_DATA, stream_id, seq) + tx_header + payload


def cmp_parse(msg):
    """Decode one CMP message. Returns a dict, or None if it is not one we understand."""
    if len(msg) < 8:
        return None
    version, _res, device_id, msg_type, stream_id, seq = struct.unpack_from(">BBHBBH", msg, 0)
    if version < CMP_VERSION:
        return None
    out = {"version": version, "device_id": device_id, "message_type": msg_type,
           "stream_id": stream_id, "seq": seq, "frame": None}
    if msg_type == CMP_MSG_CAP_DATA and len(msg) >= 8 + 16:
        ts, iface, flags, ptype, plen = struct.unpack_from(">QIBBH", msg, 8)
        out.update(timestamp=ts, interface_id=iface, flags=flags, payload_type=ptype)
        body = msg[24:24 + plen]
        if ptype == CMP_PAYLOAD_ETHERNET and len(body) >= 6:
            _pflags, _pres, dlen = struct.unpack_from(">HHH", body, 0)
            data = body[6:6 + dlen]
            if len(data) >= 4:
                out["frame"] = data[:-4]        # strip the FCS
    return out


# -------------------------------------------------------------------------- pcap

CMP_ETHERTYPE = 0x99FE
PCAP_LINKTYPE_ETHERNET = 1


class PcapWriter:
    """Classic libpcap writer, microsecond resolution."""

    def __init__(self, path, module_mac, sink_mac):
        self.file = open(path, "wb")
        # magic, version 2.4, no timezone/sigfigs, snaplen, linktype
        self.file.write(struct.pack("<IHHiIII", 0xA1B2C3D4, 2, 4, 0, 65535, 0,
                                    PCAP_LINKTYPE_ETHERNET))
        self.module = bytes.fromhex(module_mac.replace(":", ""))
        self.sink = bytes.fromhex(sink_mac.replace(":", ""))
        self.count = 0

    def write(self, cmp_message, from_module):
        """Frame one CMP message for the Ethernet transport option and record it."""
        src, dst = (self.module, self.sink) if from_module else (self.sink, self.module)
        frame = dst + src + struct.pack(">H", CMP_ETHERTYPE) + cmp_message
        if len(frame) < 60:                     # Ethernet minimum, zero padded (6.4.1)
            frame += b"\x00" * (60 - len(frame))
        now = time.time()
        self.file.write(struct.pack("<IIII", int(now), int((now % 1) * 1e6),
                                    len(frame), len(frame)))
        self.file.write(frame)
        self.count += 1

    def close(self):
        self.file.close()


# ------------------------------------------------------------------- inner frame

def checksum16(data):
    if len(data) % 2:
        data += b"\x00"
    total = 0
    for i in range(0, len(data), 2):
        total += (data[i] << 8) | data[i + 1]
    while total >> 16:
        total = (total & 0xFFFF) + (total >> 16)
    return (~total) & 0xFFFF


def build_frame(src_mac, dst_mac, src_ip, dst_ip, src_port, dst_port, payload, ident):
    """One complete Ethernet/IPv4/UDP frame, without FCS."""
    udp = struct.pack(">HHHH", src_port, dst_port, 8 + len(payload), 0) + payload

    ip_no_csum = struct.pack(
        ">BBHHHBBH4s4s",
        0x45, 0x00, 20 + len(udp), ident,
        0x4000,                     # Don't Fragment, as socket_raw.c also sets
        64, 17, 0,
        socket.inet_aton(src_ip), socket.inet_aton(dst_ip),
    )
    csum = checksum16(ip_no_csum)
    ip = ip_no_csum[:10] + struct.pack(">H", csum) + ip_no_csum[12:]

    eth = bytes.fromhex(dst_mac.replace(":", "")) + bytes.fromhex(src_mac.replace(":", "")) \
        + struct.pack(">H", 0x0800)
    return eth + ip + udp


def parse_frame(frame):
    """Pull the UDP payload out of an Ethernet/IPv4/UDP frame. None if it is not one."""
    if len(frame) < 14 + 20 + 8 or struct.unpack_from(">H", frame, 12)[0] != 0x0800:
        return None
    ihl = (frame[14] & 0x0F) * 4
    if frame[14 + 9] != 17:
        return None
    udp_off = 14 + ihl
    udp_len = struct.unpack_from(">H", frame, udp_off + 4)[0]
    return frame[udp_off + 8: udp_off + udp_len]


# --------------------------------------------------------------------------- XCP

XCP_CONNECT = 0xFF
XCP_DISCONNECT = 0xFE
XCP_GET_STATUS = 0xFD
PID_RES, PID_ERR = 0xFF, 0xFE


def xcp_message(counter, packet):
    """XCP on Ethernet transport layer: WORD len + WORD ctr + packet, little endian."""
    return struct.pack("<HH", len(packet), counter) + packet


def xcp_packets(payload):
    """Split a UDP payload into its XCP packets."""
    out, off = [], 0
    while off + 4 <= len(payload):
        length, counter = struct.unpack_from("<HH", payload, off)
        if off + 4 + length > len(payload):
            break
        out.append((counter, payload[off + 4: off + 4 + length]))
        off += 4 + length
    return out


# -------------------------------------------------------------------------- REST

def query_rest(endpoint):
    base = "http://%s" % endpoint
    paths = ["/asam-cmp/version-info", "/asam-cmp/v1/identification",
             "/asam-cmp/v1/interfaces", "/asam-cmp/v1/measurement"]
    print("REST interface at %s" % base)
    ok = True
    for path in paths:
        try:
            with urllib.request.urlopen(base + path, timeout=3) as response:
                body = json.loads(response.read().decode())
            print("  GET %-32s %s" % (path, json.dumps(body)))
        except (urllib.error.URLError, OSError, ValueError) as exc:
            print("  GET %-32s FAILED: %s" % (path, exc))
            ok = False
            continue
        if path.endswith("/interfaces"):
            interfaces = body.get("Interfaces", [])
            transmitter = interfaces[0].get("Transmitter") if interfaces else None
            if transmitter is None:
                print("  -> no Transmitter object: a Data Sink would conclude that this")
                print("     capture module cannot transmit, and would never inject (7.2.2)")
                ok = False
            else:
                bitmask = transmitter.get("TransmissionSupportBitmask", 0)
                print("  -> transmission supported, TransmissionSupportBitmask=0x%02X%s"
                      % (bitmask, " (TIMESTAMP_IMMEDIATE)" if bitmask & 1 else ""))
                print("  -> AggregationMtu=%s, so the largest inner frame is %s bytes"
                      % (transmitter.get("AggregationMtu"),
                         (transmitter.get("AggregationMtu") or 34) - 34))
    return ok


# -------------------------------------------------------------------------- main

def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--target", default="127.0.0.1:55555",
                        help="CMP port of the capture module (default: %(default)s)")
    parser.add_argument("--rest", default="127.0.0.1:8080",
                        help="REST interface of the capture module, '' to skip")
    parser.add_argument("--ecu-ip", default="192.168.0.220")
    parser.add_argument("--ecu-port", type=int, default=5555)
    parser.add_argument("--ecu-mac", default="02:00:00:00:00:01",
                        help="default matches DeviceId 1 of cmp_demo")
    parser.add_argument("--sink-ip", default="192.168.0.10",
                        help="source address inside the tunnelled frame")
    parser.add_argument("--sink-mac", default="02:00:00:00:FF:01")
    parser.add_argument("--device-id", type=int, default=0x2222, help="our own CMP DeviceId")
    parser.add_argument("--interface-id", type=int, default=1)
    parser.add_argument("--timeout", type=float, default=3.0)
    parser.add_argument("--pcap", default=None,
                        help="write the CMP messages to this capture file for Wireshark")
    args = parser.parse_args()

    host, _, port = args.target.rpartition(":")
    target = (host, int(port))

    failures = 0
    if args.rest:
        if not query_rest(args.rest):
            failures += 1
        print()

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(("0.0.0.0", 0))
    sock.settimeout(args.timeout)
    print("Data Sink on UDP port %u, capture module at %s:%u"
          % (sock.getsockname()[1], target[0], target[1]))

    pcap = PcapWriter(args.pcap, args.ecu_mac, args.sink_mac) if args.pcap else None

    seq = 0
    expected_cap_seq = None

    def send_xcp(packet, name):
        nonlocal seq
        frame = build_frame(args.sink_mac, args.ecu_mac, args.sink_ip, args.ecu_ip,
                            50000, args.ecu_port, xcp_message(seq, packet), ident=seq + 1)
        msg = cmp_wrap_transmit(args.device_id, 0, seq, args.interface_id, frame)
        sock.sendto(msg, target)
        if pcap:
            pcap.write(msg, from_module=False)
        print("  -> %-12s TX_DATA_MSG seq=%u, %u byte inner frame, %u byte CMP message"
              % (name, seq, len(frame), len(msg)))
        seq += 1

    def recv_xcp(name):
        nonlocal expected_cap_seq
        try:
            while True:
                data, _ = sock.recvfrom(65535)
                if pcap:
                    pcap.write(data, from_module=True)
                parsed = cmp_parse(data)
                if parsed is None:
                    print("  <- %u bytes that are not a CMP message" % len(data))
                    continue
                kind = MSG_TYPE_NAMES.get(parsed["message_type"], "0x%02X" % parsed["message_type"])
                if parsed["message_type"] != CMP_MSG_CAP_DATA:
                    print("  <- %s, ignored" % kind)
                    continue
                if expected_cap_seq is not None and parsed["seq"] != expected_cap_seq:
                    print("  !! StreamSequenceCounter gap: expected %u, got %u"
                          % (expected_cap_seq, parsed["seq"]))
                expected_cap_seq = (parsed["seq"] + 1) & 0xFFFF
                frame = parsed["frame"]
                if frame is None:
                    print("  <- %s without an Ethernet payload" % kind)
                    continue
                payload = parse_frame(frame)
                if payload is None:
                    print("  <- %s: inner frame is not Ethernet/IPv4/UDP" % kind)
                    continue
                return parsed, xcp_packets(payload)
        except socket.timeout:
            print("  <- %s: TIMEOUT after %.1fs" % (name, args.timeout))
            return None, []

    print("\nXCP through the capture module:")
    send_xcp(bytes([XCP_CONNECT, 0x00]), "CONNECT")
    parsed, packets = recv_xcp("CONNECT")
    if not packets:
        failures += 1
    for counter, packet in packets:
        if packet[0] == PID_RES and len(packet) >= 8:
            resource, comm_mode, max_cto, max_dto, proto, transport = struct.unpack_from(
                "<BBBHBB", packet, 1)
            print("  <- CONNECT ok: CAP_DATA_MSG seq=%u from DeviceId 0x%04X, InterfaceId %u"
                  % (parsed["seq"], parsed["device_id"], parsed["interface_id"]))
            print("     MAX_CTO=%u MAX_DTO=%u resource=0x%02X protocol=%u transport=%u"
                  % (max_cto, max_dto, resource, proto, transport))
            print("     capture timestamp %u ns, INSYNC=%u"
                  % (parsed["timestamp"], (parsed["flags"] >> 1) & 1))
        elif packet[0] == PID_ERR:
            print("  <- XCP error 0x%02X" % packet[1])
            failures += 1
        else:
            print("  <- unexpected XCP packet 0x%02X (ctr %u)" % (packet[0], counter))
            failures += 1

    if packets:
        send_xcp(bytes([XCP_GET_STATUS]), "GET_STATUS")
        _, packets = recv_xcp("GET_STATUS")
        for _counter, packet in packets:
            if packet[0] == PID_RES:
                print("  <- GET_STATUS ok: session status 0x%02X" % packet[1])
            else:
                print("  <- GET_STATUS unexpected packet 0x%02X" % packet[0])
                failures += 1
        if not packets:
            failures += 1

        send_xcp(bytes([XCP_DISCONNECT]), "DISCONNECT")
        _, packets = recv_xcp("DISCONNECT")
        for _counter, packet in packets:
            print("  <- DISCONNECT %s" % ("ok" if packet[0] == PID_RES else "error"))

    if pcap:
        pcap.close()
        print("\nWrote %u CMP messages to %s" % (pcap.count, args.pcap))
        print("Open it in Wireshark: the ASAM CMP dissector keys on EtherType 0x99FE.")

    print("\n%s" % ("FAILED (%u problems)" % failures if failures else "PASSED"))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
