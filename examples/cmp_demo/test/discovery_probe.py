#!/usr/bin/env python3
"""
discovery_probe.py - a Data Sink looking for capture modules (ASAM CMP 1.1.0, 12.1.1)

Multicasts a CMP_CM_DISCOVERY request to 239.255.0.0:5556 and decodes every response.
That request is an ordinary XCP packet: transport header, command 0xF2
(CC_TRANSPORT_LAYER_CMD), sub command 0x10.

  ./test/discovery_probe.py                 discover on every interface
  ./test/discovery_probe.py --expect-http 8080

Exit code 0 when at least one capture module answered and every response parsed.
"""

import argparse
import re
import socket
import subprocess
import struct
import sys

GROUP = "239.255.0.0"
PORT = 5556

XCP_CMD_TL = 0xF2
XCP_SUB_DISCOVERY = 0x10
XCP_PID_RESPONSE = 0xFF


def local_interfaces():
    """IPv4 address of every interface, loopback included, best effort and stdlib only."""
    ips = []
    try:
        out = subprocess.run(["ifconfig"], capture_output=True, text=True, timeout=5).stdout
    except (OSError, subprocess.SubprocessError):
        try:
            out = subprocess.run(["ip", "-4", "addr"], capture_output=True, text=True, timeout=5).stdout
        except (OSError, subprocess.SubprocessError):
            return ["127.0.0.1"]
    for match in re.finditer(r"inet (?:addr:)?(\d+\.\d+\.\d+\.\d+)", out):
        ip = match.group(1)
        if ip not in ips:
            ips.append(ip)
    return ips or ["127.0.0.1"]


def build_request(reply_addr, reply_port):
    """Table 78. Little endian scalars, address as a 16 byte array in network order."""
    body = struct.pack("<BBH", XCP_CMD_TL, XCP_SUB_DISCOVERY, reply_port)
    body += socket.inet_aton(reply_addr) + b"\x00" * 12   # IPv4 uses the first 4 of 16
    body += b"\x00"                                        # bit 0 = 0: IPv4
    assert len(body) == 0x15, len(body)
    return struct.pack("<HH", len(body), 0) + body


def take_string(payload, off):
    """A_UINT16 padded length, then a zero terminated A_UTF8 string.

    The length counts the padded bytes, not the characters: 12.1.1 encodes "Dev1" as
    six bytes and the empty string as two. See docs/XCP_DISCOVERY.md.
    """
    (n,) = struct.unpack_from("<H", payload, off)
    off += 2
    raw = payload[off:off + n]
    return raw.split(b"\x00", 1)[0].decode("utf-8", "replace"), off + n


def decode(datagram):
    if len(datagram) < 4:
        raise ValueError("shorter than an XCP transport header")
    length, reserved = struct.unpack_from("<HH", datagram, 0)
    payload = datagram[4:]
    if reserved != 0:
        raise ValueError("reserved word is 0x%04X, expected 0" % reserved)
    if length != len(payload):
        raise ValueError("header says %u bytes, got %u" % (length, len(payload)))
    if len(payload) < 47:
        raise ValueError("shorter than the fixed part of Table 79")
    if payload[0] != XCP_PID_RESPONSE:
        raise ValueError("PID 0x%02X, expected 0xFF" % payload[0])
    if payload[1] != XCP_SUB_DISCOVERY:
        raise ValueError("sub command 0x%02X, expected 0x10" % payload[1])

    out = {
        "ip": socket.inet_ntoa(payload[2:6]),
        "prefix_len": payload[18],
        "gateway": socket.inet_ntoa(payload[19:23]),
        "mac": ":".join("%02X" % b for b in payload[35:41]),
        "ipv6": bool(payload[41] & 1),
        "http_port": struct.unpack_from("<H", payload, 43)[0],
    }
    out["description"], off = take_string(payload, 45)
    out["serial"], off = take_string(payload, off)
    if off != len(payload):
        raise ValueError("%u trailing bytes after SerialNumber" % (len(payload) - off))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--reply-port", type=int, default=50100)
    ap.add_argument("--timeout", type=float, default=2.0)
    ap.add_argument("--expect-http", type=int, default=None,
                    help="fail unless a module advertises this HTTP port")
    args = ap.parse_args()

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind(("", args.reply_port))
    # We may ask to be answered on the group, per 12.1.1, so we have to be a member of it.
    # Join on every interface: imr_interface INADDR_ANY does not mean "all", it lets the
    # stack pick one, and on macOS it receives nothing at all.
    interfaces = local_interfaces()
    for ip in interfaces:
        try:
            sock.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP,
                            socket.inet_aton(GROUP) + socket.inet_aton(ip))
        except OSError:
            pass  # already a member, or the interface does not route multicast
    sock.settimeout(args.timeout)

    modules, problems, sent = {}, 0, []

    def sweep(reply_to_group):
        """Send one request per interface and collect answers until the timeout.

        Two passes are made. 12.1.1 says the module answers "to the multicast address and
        port given in the command request", which is the canonical path; but the return
        multicast is routinely filtered - a Wi-Fi AP typically will not forward group
        traffic to a wireless client, so the request arrives and the answer never comes
        back. 12.1 also says plainly that "the IP destination address and UDP destination
        port of the response are given by the request", so asking for a unicast answer is
        within the standard and is what actually works across such a link.
        """
        nonlocal problems
        for ip in interfaces:
            request = build_request(GROUP if reply_to_group else ip, args.reply_port)
            sent.append(request)
            try:
                sock.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_IF, socket.inet_aton(ip))
                sock.sendto(request, (GROUP, PORT))
            except OSError as exc:
                print("    %s: %s" % (ip, exc))
        answers = 0
        while True:
            try:
                data, sender = sock.recvfrom(65535)
            except socket.timeout:
                break
            if data in sent:
                continue  # our own multicast, looped back
            try:
                info = decode(data)
            except ValueError as exc:
                print("    %s sent %u bytes that do not decode: %s" % (sender[0], len(data), exc))
                problems += 1
                continue
            answers += 1
            key = (info["serial"], info["description"])
            entry = modules.setdefault(key, dict(info, addresses=[], via=set()))
            entry["via"].add("multicast" if reply_to_group else "unicast")
            if info["ip"] not in entry["addresses"]:
                entry["addresses"].append(info["ip"])
            if info["mac"] != "00:00:00:00:00:00":
                entry["mac"] = info["mac"]  # prefer a real MAC over a loopback reply
        return answers

    print("CMP_CM_DISCOVERY -> %s:%u, via %u interface(s): %s"
          % (GROUP, PORT, len(interfaces), ", ".join(interfaces)))
    print("  " + build_request(GROUP, args.reply_port).hex(" "))

    n_mcast = sweep(reply_to_group=True)
    print("  reply to the group (12.1.1)     : %u answer(s)" % n_mcast)
    n_ucast = sweep(reply_to_group=False)
    print("  reply to us directly            : %u answer(s)" % n_ucast)

    for info in modules.values():
        print("\n  capture module %s" % info["serial"])
        print("    description  %s" % info["description"])
        print("    MAC          %s" % info["mac"])
        for ip in info["addresses"]:
            print("    reachable at %s   -> http://%s:%u/asam-cmp/version-info"
                  % (ip, ip, info["http_port"]))
        print("    prefix /%u   gateway %s" % (info["prefix_len"], info["gateway"]))
        print("    answered via %s" % ", ".join(sorted(info["via"])))

    print()
    if not modules:
        print("FAILED: no capture module answered within %.1fs" % args.timeout)
        return 1
    if args.expect_http is not None and not any(m["http_port"] == args.expect_http for m in modules.values()):
        print("FAILED: no module advertised HTTP port %u" % args.expect_http)
        return 1
    if problems:
        print("FAILED (%u malformed response(s))" % problems)
        return 1
    if not any("multicast" in m["via"] for m in modules.values()):
        print("Note: no module answered on the multicast group, only directly.")
        print("      The requests clearly arrived, so the return multicast is being filtered")
        print("      somewhere in between - a Wi-Fi AP will normally not forward group traffic")
        print("      to a wireless client. Not a fault of the capture module.")
        print()
    print("PASSED (%u capture module(s))" % len(modules))
    return 0


if __name__ == "__main__":
    sys.exit(main())
