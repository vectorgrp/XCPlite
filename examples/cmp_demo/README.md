# cmp_demo — XCP tunnelled through an emulated ASAM CMP capture module

Demonstrates supplying **your own backend** for the xcplib raw Ethernet transport
(`OPTION_ENABLE_UDP_RAW`) from outside the library. xcplite is used as installed and
unmodified: it builds plain Ethernet/IPv4/UDP frames and hands them to the six `eth_hal_*`
functions, which this project implements.

ASAM CMP (Capture Module Protocol) serves **testing of XCP tools that communicate through
capture modules**. It is not an ECU developer feature, so nothing about it lives in
libxcplite — that separation is the point of this example.

Implements **ASAM CMP 1.1.0**: Ethernet Data Message payloads (`0x08`) over UDP (§6.4.2), in
both directions, plus the read-only part of the REST interface.

---

## What it does

The demo emulates a capture module with one interface, behind which sits one XCP ECU. The
tool never addresses the ECU over IP — everything is tunnelled:

```
   XCP tool  ============ CMP over UDP ============  cmp_demo
  (Data Sink)                                    (Capture Module)
                                                        |
      <--- CAP_DATA_MSG (0x01) -- captured frames -------+--- emulated ECU
      ---- TX_DATA_MSG  (0x04) -- injected frames ------>+    (xcplib)
```

Transmission — `TX_DATA_MSG`, message type `0x04` — is what CMP **1.1** added, and it is what
makes XCP communication with an ECU possible.  

| Direction | What happens |
|---|---|
| ECU → tool | The frame xcplib hands to `eth_hal_send` is what the capture module just captured, so it is wrapped as a Captured Data Message and sent to the Data Sink |
| tool → ECU | A Transmit Data Message is unwrapped and its inner frame handed to `eth_hal_recv`; xcplib parses it as ordinary Ethernet/IPv4/UDP and answers the XCP command inside |

### Wire format

All CMP fields are big endian (§6.2). Sizes are fixed by the specification.

```
CMP header (8 B, §6.2.1)          version=1, reserved, DeviceId, MessageType,
                                  StreamId, StreamSequenceCounter
Captured Data Message hdr (16 B)  Timestamp, InterfaceId, CommonFlags,
                                  PayloadType=0x08, PayloadLength           §7.2.1
Transmit Data Message hdr (24 B)  Timestamp, Deadline, InterfaceId,
                                  TransmissionOptions, CommonFlags,
                                  PayloadType=0x08, PayloadLength           §7.2.2
Ethernet payload (6 B + data)     Flags, Reserved, DataLength, DATA         §7.3.8
```

`DATA` runs from the destination MAC **through the FCS**, but xcplib's HAL contract passes
frames *without* FCS — so the codec appends four zero bytes on wrap and strips four on
unwrap. `FCS_SUPPORT` is reported as 0, which is what §7.3.8 prescribes for a module that
cannot compute a real FCS. Payload type `0x0D RAW_ETHERNET` is deliberately not used: it
additionally carries the preamble and SFD, which a synthetic ECU has nothing useful to put in.

Not used, and advertised as unsupported over REST: aggregation (§6.3.2), segmentation
(§6.3.3), status messages (§8) and control messages (§9).

---

## How the override works

`libxcplite`'s `socket_raw.c` calls six `eth_hal_*` functions. This project defines all six in
`src/socket_raw_hal_cmp.c`. Because **libxcplite is a static library**, the linker only pulls an
archive member in to resolve an *undefined* symbol — and these are already defined here, so a
built-in backend is never pulled in and there is no clash.


### Files

| File | Purpose |
|---|---|
| `src/cmp.h` / `src/cmp.c` | The CMP envelope codec — **the only place that knows about CMP**. Pure: no sockets, no I/O, no global state |
| `src/cmp_transport.h` | The outer transport seam |
| `src/cmp_transport_udp.c` | CMP over UDP (§6.4.2) — one ordinary datagram socket |
| `src/socket_raw_hal_cmp.c` | The six `eth_hal_*` functions, joining codec and transport |
| `src/cmp_backend.h` | Backend configuration and status |
| `src/cmp_rest.h` / `src/cmp_rest.c` | The read-only REST interface (§12.3) |
| `src/main.c` | Demo application — command line, server setup, calibration segment, event |
| `test/cmp_codec_test.c` | Codec unit test against the specification's own sample files |
| `src/cmp_discovery.c` | CMP_CM_DISCOVERY responder (12.1.1), multicast |
| `test/fake_sink.py` | A minimal Data Sink — plays the tool's half |
| `test/discovery_probe.py` | A Data Sink looking for capture modules |
| `test.sh` | On-target test: syncs to the target, builds, runs and checks it |
| `test/test_local.sh` | The same end-to-end check, on this machine over loopback |

---

## Building

xcplite has to be installed from the `raw` configuration first:

```bash
cd <xcplite>
cmake -B build-raw -S . -DXCPLITE_CONFIGURATION=raw -DCMAKE_BUILD_TYPE=Debug -DCMAKE_INSTALL_PREFIX=$HOME/xcplite-install
cmake --build build-raw --target install
```

On macOS or Windows add `-DCMAKE_C_FLAGS="-DOPTION_UDP_RAW_HAL_EXTERNAL"` to that first
command.  

```bash
cd <xcplite>
cmake -B build-raw -S . -DXCPLITE_CONFIGURATION=raw -DCMAKE_C_FLAGS="-DOPTION_UDP_RAW_HAL_EXTERNAL" -DCMAKE_BUILD_TYPE=Debug -DCMAKE_INSTALL_PREFIX=$HOME/xcplite-install
cmake --build build-raw --target install
```

Then build this project against the install:

```bash
cd examples/cmp_demo
cmake -B build -S . -Dxcplite_DIR=$HOME/xcplite-install/lib/cmake/xcplite
cmake --build build
```

This project only needs an **installed** xcplite, not a source checkout. If you move it into a
repository of its own, that install is the whole dependency.

---

## Running

```bash
./build/cmp_demo --sink 192.168.0.10:55555
```

`--sink` may be omitted, in which case the Data Sink address is learned from the first CMP
message received, the same way `socket_raw.c` learns the peer MAC.

| Option | Meaning |
|---|---|
| `--sink <a.b.c.d:port>` | Data Sink address for captured data. Default: learned |
| `--listen <port>` | UDP port for incoming CMP messages (default 55555) |
| `--mtu <bytes>` | MTU of the path to the Data Sink (default 1500, max 9000) |
| `--rest-port <port>` | REST interface port (default 8080, 0 disables) |
| `--device-id`, `--stream-id`, `--interface-id` | CMP identity |
| `--ecu-mac <xx:…:xx>` | MAC of the emulated ECU. Default: derived from `--device-id` |
| `--ip`, `--port` | Address of the emulated ECU, seen only inside the CMP payload |

Because the ECU address only ever appears inside the payload, it does **not** have to be free
on any real network — unlike the plain raw Ethernet transport. `ping` to it does not work
either: there is no IP route to the ECU, it lives behind the tunnel. Send an ICMP echo as a
`TX_DATA_MSG` instead if you want to exercise that path.

---

## The MTU constraint

§6.4.2: *"CMP messages shall not be sent over IP fragmented packets."* The envelope adds 34
bytes to every captured frame, and the outer IPv4/UDP headers add 28 more, so on a 1500-byte
path the largest inner frame is **1438 bytes**.

This is why xcplite's `raw` configuration sets **`OPTION_MTU 1420`** rather than the 1500 a standard Ethernet link allows:

| | |
|---|---|
| `OPTION_MTU` | 1420 |
| `XCPTL_MAX_SEGMENT_SIZE` | 1392 (`OPTION_MTU - 32`, `%8`) |
| Largest inner Ethernet frame | 1434 (`42 + segment`) |
| As a CMP message | 1468 (`+ 34` envelope) |
| As an IP packet | 1496 (`+ 28`) — fits 1500 with 4 bytes to spare |

At the full link MTU a segment is 1472 bytes and the frame 1514, filling a 1500-byte path on its own, leaving
the envelope nothing: small transfers still work, but a saturated DAQ stream hits the limit
and reports `SOCKET_ERROR_MSGSIZE`. The demo checks this at startup and warns, naming the
budget and the remedy, so a mismatched configuration is visible before it bites:

```
  CMP frame budget: 1438 bytes per inner frame (1472 byte CMP message - 34 byte envelope)
```

If you raise `OPTION_MTU` again, either lower the envelope's share with a jumbo-capable path
(`--mtu 9000`, allowed explicitly by §6.4) or expect that warning back.

Either way an oversized frame is refused with `ETH_HAL_ERROR_SIZE` rather than fragmented.
That is exactly what the HAL contract designed that error for: whether a frame fits is a
runtime property only the backend knows.

```bash
# Test MTU 1501 — just above standard Ethernet MTU
ping -D -c 3 -s 1473 192.168.0.206
```



---

## Discovery

§12 requires a capture module to support **at least one** of three approaches to address
configuration and discovery — and one of them is "static configuration without Capture Module
Discovery", so implementing none of it conforms. This demo implements the second:

| Approach | §  | Here |
|---|---|---|
| Static, no discovery | 12 | still works, `--no-discovery` |
| XCP-based, IP multicast | 12.1 | **implemented**, `src/cmp_discovery.c` |
| mDNS / DNS-SD | 12.2 | not implemented — needs a full mDNS responder or a dependency on Avahi |

§12.1 is titled "XCP-based approach" and means it literally: the request is an ordinary XCP
packet in the ordinary XCP-on-Ethernet transport header, with command code `0xF2`
(`CC_TRANSPORT_LAYER_CMD`) and sub-command `0x10`, multicast to `239.255.0.0:5556`. XCP uses
the same mechanism for its own discovery — see the repository
[docs/XCP_DISCOVERY.md](../../docs/XCP_DISCOVERY.md) for what XCPlite has there today and the
options for it. Nothing about XCP discovery is decided by this demo.

```bash
./test/discovery_probe.py
```

```
  reply to the group (12.1.1)     : 0 answer(s)
  reply to us directly            : 1 answer(s)

  capture module cmp_demo-0001
    description  XCPlite cmp_demo, emulated ASAM CMP capture module
    MAC          2C:CF:67:EF:F6:78
    reachable at 192.168.0.206   -> http://192.168.0.206:8080/asam-cmp/version-info
    prefix /24   gateway 0.0.0.0
    answered via unicast
```

The point of the exchange is the **HTTP port**: discovery hands the tool the REST interface it
then configures the module through. The reply address and port come from the request, so the
responder never needs to know anything about the network it is on.

It runs on the REST thread, not a thread of its own — that thread is already in a `poll()`
loop and already knows the HTTP port the response has to advertise.

**The multicast return path is filtered more often than the request path.** In the run above
the request reached the Pi over Wi-Fi and was answered, and the multicast answer never came
back: an access point does not normally forward group traffic to a wireless client. §12.1 is
explicit that "the IP destination address and UDP destination port of the response are given
by the request", so `discovery_probe.py` makes two passes — one asking to be answered on the
group as §12.1.1 describes, one asking to be answered directly — and reports which worked.
The responder simply honours whatever address the request carries. On loopback both work; on
Wi-Fi typically only the direct one does.

**Two things worth knowing if you touch the socket code.** Joining with
`imr_interface = INADDR_ANY` does *not* mean "all interfaces": it lets the stack pick one, and
on macOS it receives nothing at all. `cmpDiscoveryStart()` therefore enumerates interfaces and
joins the group on each — loopback included, which is what makes the same-host test work,
since a process does not see its own multicast sent via a LAN interface. And a multicast reply
needs `IP_MULTICAST_IF` set per response, or it leaves through the default route and the tool
that asked never sees it.

---

## The REST interface

§12.3 calls the REST interface mandatory for a capture module, and §7.2.2 says a Data Sink
uses it **to detect whether transmission is supported**. If a tool gates injection on that,
no REST means no XCP — which is why the read-only part is implemented here.

| Endpoint | § |
|---|---|
| `GET /asam-cmp/version-info` | 12.3.1 |
| `GET /asam-cmp/v1/identification` | 12.3.2 |
| `GET /asam-cmp/v1/interfaces` | 12.3.4 — **advertises transmission support** |
| `GET /asam-cmp/v1/measurement` | 12.3.6 |

The decisive part is the `Transmitter` object of `/interfaces` (§12.3.4, Table 88):

```json
"Transmitter": { "TransmissionSupportBitmask": 1, "FeatureSupportBitmask": 0,
                 "AggregationMtu": 1472, "AggregationCount": 1 }
```

`TransmissionSupportBitmask` bit 0 is `TIMESTAMP_IMMEDIATE`: we send every request straight
away and support neither absolute nor relative scheduling, no deadline and no segmentation.
§7.2.2 allows that — *"If the CM does not support Timestamp, it shall always send
immediately"*. `AggregationMtu` is where the MTU budget above is advertised to the tool.

§12.3 says the interface *should* run on port 80; the demo defaults to 8080 so it needs no
privileges. Everything that would change configuration (the `PUT` methods), mDNS/DNS-SD
discovery (§12.2.2) and XCP-based discovery (§12.1) are **not** implemented — §12 permits
*"Static configuration without Capture Module Discovery"*, which is what this demo uses.

---

## Testing

Two scripts, same checks, different place to run them:

```bash
./test.sh              # on the target: sync, build, run and check over the network
./test/test_local.sh   # on this machine, over loopback
```

Neither needs `veth`, a network namespace or root, unlike the plain raw Ethernet transport:
the outer transport is an ordinary UDP socket, and the emulated ECU address only ever appears
inside the CMP payload.

**`test.sh`** is the on-target one, modelled on
[udp_raw_demo/test.sh]. Set `TARGET_USER`
and `TARGET_HOST` at the top of it, then it:

1. rsyncs the library sources and this example to the target;
2. builds and installs the `raw` configuration of xcplite there, then builds `cmp_demo`
   against that install — the two-stage build a standalone project needs;
3. checks that the built-in AF_PACKET backend was **not** linked into the binary, by looking
   for a string only it contains. Checking the archive would prove nothing: on Linux
   `socket_raw_hal_linux.o` is in `libxcplite.a` either way, and it is the static-library link
   rule that keeps it out of the executable;
4. runs the codec unit test on the target, which is also the check that the big-endian
   packing is right on aarch64;
5. starts the capture module and queries all four REST endpoints, asserting that the
   `Transmitter` object advertises transmission;
6. reports the CMP endpoint status and confirms the UDP port is open;
7. sends a hardcoded XCP CONNECT tunnelled through CMP and decodes the response, printing
   the exact bytes of the `TX_DATA_MSG` it puts on the wire.

The CONNECT frame is assembled from the parameters at the top of the script rather than
pasted in as a fixed hex blob, so that changing e.g. `ECU_IP` cannot leave a stale IPv4
header checksum behind. The XCP command itself — `FF 00` — is the hardcoded part.

**`test/test_local.sh`** runs the codec test and then drives `cmp_demo` with `fake_sink.py`
over loopback, for a full CONNECT / GET_STATUS / DISCONNECT exchange. It leaves `cmp.pcap` in
the folder it was started from — open it in Wireshark, whose ASAM CMP dissector keys on
EtherType 0x99FE. Everything else the demo writes (`demo.log`, the `.a2l` and the `.bin`)
stays in a temporary directory and is discarded.

**`test/cmp_codec_test.c`** is the strongest check. Its golden vectors are lifted byte for byte
from the sample PCAPNG files shipped with the specification, so it pins the wire format against
the standard itself rather than against one reading of it:

- `CMP_1.0/asam_cmp_cap_0x08_Ethernet.pcapng` — a Captured Data Message with an Ethernet
  payload, exactly the shape this backend emits. `cmpWrapCaptured()` must reproduce it byte for
  byte (with the sample's FCS zeroed, since we report `FCS_SUPPORT = 0`).
- `CMP_1.1/asam_cmp_tx_0x01_can_29bit_0x12345678.pcapng` — a real Transmit Data Message. Its
  payload is CAN, so it must be *rejected* — but only after the 24-byte transmit header has
  been parsed correctly. A wrong header length surfaces as `MALFORMED` instead of
  `PAYLOAD_TYPE`, so this vector pins the transmit header layout too.

There is no sample of a transmit message carrying an Ethernet payload — the 1.1 samples cover
CAN, CAN FD and LIN only — so the happy path uses a message built from those two pinned
layouts.

**`test/fake_sink.py`** plays the tool's half with nothing but the Python standard library: it
queries the REST endpoints, checks that transmission is advertised, and tunnels XCP CONNECT /
GET_STATUS / DISCONNECT, verifying the responses and the `StreamSequenceCounter` continuity.
`--pcap <file>` writes every CMP message to a capture file for **Wireshark**, whose built-in
ASAM CMP dissector keys on EtherType `0x99FE` — so the messages are framed for the Ethernet
transport option there and dissect automatically. The CMP bytes are identical under both
transport options; only the outer framing differs.

The [openDAQ ASAM-CMP-Library](https://github.com/openDAQ/ASAM-CMP-Library) can serve as an
independent decoder for the capture direction, but not for transmission: its `MessageType` enum
stops at `0x03`, so it predates CMP 1.1 and does not know `TX_DATA_MSG`.

---

## Platform dependencies

Runs on Linux and macOS. Threads go through the library's own abstraction; sockets do not:

| | |
|---|---|
| Threads | `platform.h` — `THREAD_HANDLE`, `create_thread`, `join_thread`, `THREAD_FUNC_RETURN` |
| Mutexes | none needed |
| Sockets | **direct POSIX**, in `cmp_transport_udp.c`, `cmp_rest.c` and `cmp_discovery.c` |

The sockets are deliberately not routed through `sockets.h`, and that is not an oversight —
it is not possible. In the `raw` configuration `sockets.c` is compiled out entirely
(`#if (TCP || UDP) && !defined(OPTION_ENABLE_UDP_RAW)`), and `sockets.h` does not even
*declare* `socketJoin`, `socketListen`, `socketAccept` or `socketRecv` when
`OPTION_ENABLE_UDP_RAW` is set, so discovery has no multicast call and the REST interface has
no TCP listener to use. `socket_raw.c` implements a UDP-datagram subset and has no TCP at all.

There is a deeper reason. In the `raw` configuration the socket API **is the emulated
ECU-side stack** — UDP/IPv4 over the Ethernet HAL. This project's sockets are real host
sockets on the *other* side of that HAL. Different layer, different network: calling
`socketOpen()` for the REST listener would open it on the emulated ECU's network.

**On Windows** the port is about 54 lines of socket code across those three files, plus
`getifaddrs` → `GetAdaptersAddresses` and `WSAStartup` (the raw transport's `socketStartup()`
does not call it). All of it additive `#ifdef`, no effect on Linux. It is not currently done:
a Windows XCP tool can already drive this on a Linux target over the network, which is what
`test.sh` does. Before attempting it, check that Windows' `IP_MULTICAST_LOOP` — a
receive-side option there, send-side on BSD and Linux — still allows a same-machine discovery
test, since "everything on one box" is the only thing the port would buy.

---

## Not implemented

- **CMP over Ethernet, EtherType 0x99FE (§6.4.1).** This is the transport option the
  specification makes **mandatory** for a capture module, so it should not be deferred
  indefinitely. It needs the AF_PACKET plumbing — preserved in git commit `01e7f40`, which used
  it for the pass-through version of this backend — plus an outer Ethernet header
  (dst = sink MAC, src = our MAC, EtherType `0x99FE`) and padding to the 60-byte minimum.
  The envelope codec itself is unchanged: only `cmp_transport.h` gains an implementation.
- Status messages (§8), control messages (§9), aggregation, segmentation, time
  synchronisation and the configuration-changing REST methods.

---

## Constraints that should not be worked around

- Nothing CMP-specific belongs in libxcplite. If a change there appears necessary, that is the
  signal that the encapsulation has leaked — fix it in the backend. **No such change was
  needed:** `socket_raw_hal.h` is untouched, including for the capture timestamp, which the
  backend takes itself in `eth_hal_send` because that is the moment the emulated module sees
  the frame.
- The envelope is applied **into the backend's own buffer**, never into the transmit queue
  headroom. CMP must not participate in the zero-copy path or influence `XCPTL_TX_HEADROOM`.
- The extra copy that costs is accepted — this is a test-bench path, not a performance one.

See [docs/SOCKET_RAW.md] for the transport design and the HAL contract.
