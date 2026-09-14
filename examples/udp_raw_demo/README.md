# udp_raw_demo — Raw Ethernet Transport (no TCP/IP stack)

XCP on UDP/IPv4 with the transport implemented **inside xcplib**, on top of a thin raw
Ethernet HAL. For targets that have no TCP/IP stack at all — a bare-metal EMAC driver, or an
RTOS Ethernet abstraction without lwIP. This demo is the Linux development and test vehicle
for that transport, and the template for embedded ports.

Design and internals: [docs/SOCKET_RAW.md](../../docs/SOCKET_RAW.md).

> **Linux only.** The HAL backend uses `AF_PACKET` and needs `CAP_NET_RAW`.
> A build of the `raw` configuration on macOS or Windows stops with a clear `#error`.

---

## What it demonstrates

| Feature | How it is demonstrated |
|---|---|
| Raw Ethernet transport | `OPTION_ENABLE_UDP_RAW` — Ethernet, IPv4 and UDP headers are built by xcplib, no OS socket API involved |
| Interface selection | `socketRawSetInterface(ifname)` before `XcpEthServerInit`, from the `--if` command line option |
| Explicit local address | `XcpEthServerInit(addr, ...)` with a **concrete** IPv4 address — there is no IP stack and no DHCP, so `0.0.0.0` (ANY) is rejected |
| ARP and ICMP | answered by xcplib itself: ARP Requests for our IP get a Reply, and `ping` is answered |
| Calibration segment | `XcpCreateCalSeg` + `A2lSetSegmentAddrMode` / `A2lCreateParameter` for `counter_max`, `delay_us`, `amplitude` |
| Measurement | event `mainloop` with `global_counter` and `demo_signal` (absolute addressing) and `counter` (stack frame relative) |

The demo is intentionally close to [hello_xcp](../hello_xcp/README.md); the difference is the
transport and the mandatory address configuration. Everything above the transport layer — A2L
generation, calibration segments, DAQ — is unchanged.

### Files

| File | Purpose |
|---|---|
| `src/main.c` | Demo application — command line, server setup, calibration segment, event |
| `test.sh` | Sync, build, run and test against a remote Linux target over SSH |
| `CANape/` | CANape project and the A2L file uploaded by `test.sh` |

---

## Building

```bash
./build.sh raw examples
```

Or with CMake directly:

```bash
cmake -B build-raw -S . -DXCPLITE_CONFIGURATION=raw -DXCPLITE_BUILD_EXAMPLES=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build-raw --target udp_raw_demo
```

The `raw` configuration is selected by `XCPLITE_CONFIGURATION=raw`
([src/xcplib_raw_cfg.h](../../src/xcplib_raw_cfg.h)) and builds into `build-raw/`.

---

## Running

```
Usage: udp_raw_demo [--if <interface>] [--ip <a.b.c.d>] [--port <port>]

  --if    Ethernet interface for the raw transport (default: eth0)
  --ip    local IPv4 address of this target (default: 192.168.0.220)
  --port  XCP UDP port (default: 5555)
```

The process needs `CAP_NET_RAW`, either by granting the capability once:

```bash
sudo setcap cap_net_raw+ep ./build-raw/udp_raw_demo
./build-raw/udp_raw_demo --if eth0 --ip 192.168.1.240
```

or by running it as root.

**The address must not be one the kernel owns.** xcplib answers ARP and ICMP for it itself; if the
kernel also had that address it would answer first and send ICMP port unreachable for the XCP port.
Pick a spare address outside the DHCP pool, or use the isolated setup below.

---

## Isolated test setup (recommended)

[test/test_socket_raw.sh](../../test/test_socket_raw.sh) creates a `veth` pair with the target in
its own network namespace and **no kernel IP on the target side**, so xcplib alone owns
`192.168.90.2`. It then runs ARP, `ping` and XCP CONNECT checks:

```bash
./build.sh raw examples
sudo ./test/test_socket_raw.sh           # run the checks and clean up
sudo ./test/test_socket_raw.sh --keep    # leave it running for manual tests
```

With `--keep`, connect from the same machine:

```bash
ping 192.168.90.2                        # proves the HAL, MAC filter, ARP and the IPv4 header
tcpdump -i veth0 -nn -e -vv              # watch the frames
xcpclient --dest-addr 192.168.90.2 --port 5555 --udp
```

`ping` is the highest value first check: if it answers, the Ethernet HAL, the MAC filter, the ARP
responder, the IPv4 header build and the header checksum are all working — before any XCP tooling
is involved.

---

## Remote target: build and test on a Raspberry Pi

[test.sh](test.sh) drives the whole loop against a remote Linux target over SSH: sync the sources,
build there, grant `CAP_NET_RAW`, start the demo, upload the A2L and run a test measurement.
Edit the parameters at the top of the script:

| Variable | Meaning |
|---|---|
| `TARGET_USER` / `TARGET_HOST` | SSH login of the build machine |
| `TARGET_PATH` | where the sources are synced to on the target |
| `TARGET_IP` / `TARGET_PORT` | address the **demo** serves, must match its `--ip` / default |
| `BUILD_TYPE` | `Debug`, `RelWithDebInfo` (default) or `Release` |

```bash
./examples/udp_raw_demo/test.sh
```

Prerequisites: SSH access with keys, `rsync` on both sides, and `xcpclient` on the local machine
(`./build.sh rust_tools`, or `cargo install --path tools/xcpclient`).

---

## Using xcpclient

**xcpclient does not upload the A2L automatically.** Without any A2L option it queries the A2L file
name from the target (`GET_ID` `ASAM_NAME`) and then expects a file of that name **in the current
directory**:

```bash
xcpclient --dest-addr 192.168.0.220:5555 --udp
# [INFO ] Using A2L file name from XCP server GET_ID ASAM_NAME: udp_raw_demo_V2.2.0
# [INFO ] A2L path: udp_raw_demo_V2.2.0.a2l
# [ERROR] Could not load A2L file ... No such file or directory
```

To fetch it from the target, ask for the upload explicitly and name the local file:

```bash
xcpclient --dest-addr 192.168.0.220:5555 --udp --upload-a2l --a2l ./udp_raw_demo.a2l
```

> The uploaded A2L starts with `/include "XCP_104.aml"`, so that file has to sit next to it or the
> parse fails. Copy it from the repository root: `cp XCP_104.aml .`

List what the target offers, and run a short measurement:

```bash
# list all measurement and calibration variables (the argument is a regex, "." matches everything)
xcpclient --dest-addr 192.168.0.220:5555 --udp --a2l ./udp_raw_demo.a2l --list-mea . --list-cal .

# measure for 2 seconds  (the argument is a regex, "counter" matches counter and global_counter)
xcpclient --dest-addr 192.168.0.220:5555 --udp --a2l ./udp_raw_demo.a2l --mea counter --time 2
```

Typical output of the list command for this demo:

```
Calibration variables:
 params.counter_max 0:80010004  = 1024
 params.delay_us 0:80010000  = 1000
 params.amplitude 0:80010008 = 100

Measurement variables:
 global_counter 1:0x00030258 event 0 4 byte unsigned
 demo_signal 1:0x00030260 event 0 8 byte float
 counter 2:0x00010096 event 0 2 byte unsigned
```

Note the address extensions: `1` is absolute addressing, `2` is stack frame relative — `counter` is
a local variable of the mainloop.

---

## Configuration

Options in [src/xcplib_raw_cfg.h](../../src/xcplib_raw_cfg.h):

| Option | Default | Purpose |
|---|---|---|
| `OPTION_UDP_RAW_IFNAME` | `"eth0"` | default interface, overridden by `--if` |
| `OPTION_UDP_RAW_ENABLE_ICMP_ECHO` | on | answer `ping` |
| `OPTION_UDP_RAW_UDP_CHECKSUM_ZERO` | on | transmit UDP checksum 0, legal for IPv4 (RFC 768) |
| `OPTION_UDP_RAW_VERIFY_RX_CHECKSUM` | on | verify received IPv4 header checksums |
| `OPTION_UDP_RAW_GRATUITOUS_ARP` | off | announce our IP/MAC on bind |
| `OPTION_UDP_RAW_ZERO_COPY` | on | write the Ethernet/IPv4/UDP header into queue headroom instead of copying the payload |

Two notes:

- With the UDP checksum zeroed, `tcpdump` and Wireshark cannot validate the UDP framing. Switch to
  `OPTION_UDP_RAW_UDP_CHECKSUM_COMPUTE` while bringing up a new target if you want that check.
- `OPTION_UDP_RAW_ZERO_COPY` mainly pays off on embedded targets. The copy it removes is ~12 MB/s of
  memory bandwidth at a saturated 100 Mbit/s — negligible on a PC or a Raspberry Pi, a meaningful
  fraction of a core on a microcontroller. Turn it off to fall back to the copying transmit path,
  which is useful when bringing up a new HAL backend.

---

## Porting to a target without an IP stack

Implement the functions of [src/socket_raw_hal.h](../../src/socket_raw_hal.h) for your EMAC —
open/close, get MAC, send and receive one complete Ethernet frame, plus an optional wakeup.
[src/socket_raw_hal_linux.c](../../src/socket_raw_hal_linux.c) is the reference backend; everything
above it (UDP/IPv4, ARP, ICMP, the receive filter) is shared and needs no porting.

A backend can also live outside this repository: define `OPTION_UDP_RAW_HAL_EXTERNAL` and xcplib
selects none, leaving the `eth_hal_*` symbols for your application to link. See
[docs/SOCKET_RAW.md](../../docs/SOCKET_RAW.md).

---

## Tests

```bash
./build.sh raw tests
./build-raw/socket_raw_test
```

Unit tests for the parts that need no network: checksums against the RFC 1071 reference vector,
wire struct packing, the frame build, the receive filter, and the ARP and ICMP responders.
