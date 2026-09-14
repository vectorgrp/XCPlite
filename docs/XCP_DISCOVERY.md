# XCP discovery — findings and options

Analysis by Claude

**Status: nothing decided, nothing changed.** This records what is in the tree today, what
was learned while implementing the ASAM CMP equivalent in `examples/cmp_demo`, and what the
options are. XCPlite does not do XCP discovery today and this document does not change that.

---

## 1. What "XCP discovery" is

A tool that wants to talk to an XCP server has to know its IP address and port. Discovery is
how it finds them without being told. XCP does this with a **transport layer command sent to
a multicast group**, answered by every server that hears it:

| | |
|---|---|
| Command | `0xF2` — `CC_TRANSPORT_LAYER_CMD` (`src/xcp.h:43`) |
| Sub-command | `0xFF` `GET_SERVER_ID`, or `0xFD` `GET_SERVER_ID_EXTENDED` |
| Carried in | the ordinary XCP-on-Ethernet transport header (`len`, `ctr`) |
| Answered to | the address and port **carried in the request**, not the sender's |

The last row is the important design property: the responder needs to know nothing about the
network it sits on. The asking tool says where to reply.

This is not an XCPlite invention and not a CANape quirk — it is the same mechanism ASAM CMP
adopted wholesale, see §3.

## 2. What XCPlite has today

| Piece | Where | State |
|---|---|---|
| `CC_TL_GET_SERVER_ID_EXTENDED` (`0xFD`) | `src/xcplite.c:2804` | **Implemented.** Reads the reply address/port from the request, fills in server IP, port, status, resource, ASCII id and MAC, answers with `XcpSendMulticastResponse()` |
| `CC_TL_GET_SERVER_ID` (`0xFF`) | `src/xcplite.c:2801` | **Stubbed.** `goto no_response; // Not supported, no response, response has atypical layout` |
| Multicast socket + thread | `src/xcpethtl.c:559`, `:682` | Present. Binds `XCPTL_MULTICAST_PORT` (**5557**), joins `239.255.<cluster_id>`, one dedicated thread, hands each datagram to `XcpCommand()` |
| `XcpEthTlSetClusterId()` | `src/xcpethtl.c:550` | Empty — `// Not implemented` |
| Enablement | `src/xcptl_cfg.h:118` | `XCPTL_ENABLE_MULTICAST` is commented out and **no shipped configuration defines it** |

Three consequences worth being explicit about:

- **It is dead code as shipped.** Nothing in `src/xcplib_*_cfg.h` turns it on, so none of the
  above is compiled into any build the project produces.
- **It cannot be used on the raw transport at all.** `docs/SOCKET_RAW.md:30` lists
  `XCPTL_ENABLE_MULTICAST` as incompatible with `OPTION_ENABLE_UDP_RAW`, because `socketJoin`
  is not implemented there — joining a group means IGMP and the raw stack has none.
- **The A2L never advertises it.** `src/a2l_writer.c:200` emits `OPTIONAL_TL_SUBCMD
  GET_DAQ_CLOCK_MULTICAST` when multicast is on, but there is no corresponding line for
  `GET_SERVER_ID`. A tool reading the A2L is not told the server can be discovered.

### The multicast option is entangled with a feature that is not wanted

`XCPTL_ENABLE_MULTICAST` currently exists to serve `GET_DAQ_CLOCK_MULTICAST`, not discovery.
`src/xcptl_cfg.h:117` calls the whole option *"Not recommended setting"* and explains that it
*"needs to create an additional thread and socket"* with *"no benefit if PTP time synchronized
is used or if there is only one XCP device"*.

That verdict is about the **clock** feature. It is not a verdict on discovery, which happens
to sit behind the same switch. Any decision here should separate the two.

## 3. ASAM CMP uses exactly this mechanism

ASAM CMP 1.1.0 §12.1 is titled *"XCP-based approach"* and means it literally: *"The following
commands are based on the Ethernet Transport Layer of ASAM MCD-1 XCP."* `CMP_CM_DISCOVERY` is
`0xF2` with sub-command `0x10`, multicast to `239.255.0.0:5556`, answered to the address and
port from the request.

So the two are the same shape, differing in payload, port and one byte:

| | XCP | ASAM CMP |
|---|---|---|
| Command / sub | `0xF2` / `0xFD` | `0xF2` / `0x10` |
| Group | `239.255.<cluster_id>` | `239.255.0.0` |
| Port | 5557 *(in XCPlite)* | 5556 |
| Answer says | server IP, XCP port, resource, id | module IP, prefix, gateway, MAC, **HTTP port**, description, serial |
| Next step for the tool | XCP `CONNECT` | the REST interface |

`examples/cmp_demo/src/cmp_discovery.c` implements the CMP side. It is a working, tested
reference for the mechanism — roughly 200 lines including the interface enumeration — and
deliberately lives outside the library, because its answer advertises an HTTP port for a REST
interface, a concept `libxcplite` has no business knowing.

### Port 5556 vs 5557 — an open question

The CMP spec says XCP uses **5556**: *"While the port number was not registered with IANA,
XCP uses the already registered UDP port number 5556 because a private and closed network is
assumed."* XCPlite's `XCPTL_MULTICAST_PORT` is **5557**.

This has not been checked against the XCP standard itself, and it matters: if 5556 is what
the XCP specification actually mandates, XCPlite's multicast socket is on the wrong port and
no standard tool would ever find it — which would explain why the feature has never been
exercised. **Verify against ASAM MCD-1 XCP before building anything on top of this.**

Note also the group differs by default: `XCP_MULTICAST_CLUSTER_ID` is 1 (`src/xcp_cfg.h:448`),
giving `239.255.0.1`, while CMP uses `239.255.0.0` — i.e. cluster id 0.

## 4. What the CMP implementation taught us

These are portability findings from getting the CMP responder actually working, and they
apply unchanged to any XCP discovery responder:

- **`imr_interface = INADDR_ANY` does not mean "all interfaces".** It lets the stack pick one
  from the routing table. Measured on macOS 15: a join with `INADDR_ANY` receives **nothing at
  all**. The fix is to enumerate interfaces with `getifaddrs()` and join the group on every
  `IFF_UP | IFF_MULTICAST` IPv4 interface. A capture module — or an XCP server — cannot know
  which interface a tool will appear on.
- **Loopback is a separate interface and must be joined too.** Also measured on macOS: a
  process does not receive its own multicast sent via a LAN interface; only a loopback join
  and a loopback send see each other. Including `lo0` is what makes a same-host test possible
  at all, which matters because it is how CI would exercise this without two machines.
- **A multicast reply needs `IP_MULTICAST_IF` set per response.** Otherwise the reply leaves
  through the default route and a tool on another interface never sees it. The correct value
  is the local address facing the requester, obtainable by `connect()`ing a scratch UDP socket
  to the requester and reading back `getsockname()` — no packet is sent.
- **The multicast RETURN path is filtered far more often than the request path.** Measured
  between a Wi-Fi laptop and a wired Raspberry Pi on one subnet: the multicast request
  reached the target and was answered, and the multicast answer never came back — an access
  point normally does not forward group traffic to a wireless client. Discovery that only
  ever answers to a multicast address will appear broken on exactly the setup an engineer
  is most likely to use. The escape is already in the protocol: both `CMP_CM_DISCOVERY` and
  `GET_SERVER_ID_EXTENDED` take the reply address **from the request**, so a tool can ask to
  be answered directly, which ASAM CMP §12.1 permits in as many words — *"The IP destination
  address and UDP destination port of the response are given by the request."* A responder
  should therefore honour whatever address it is given rather than forcing the group, and a
  tool should be prepared to ask for both.
- **`XcpTlMulticastThread` does none of this.** It calls `socketJoin(sock, maddr, addr, NULL)`
  once with the server address. On a single-homed Linux box that is fine; anywhere else it is
  the `INADDR_ANY` trap in a different shape. This is the most likely reason the feature would
  fail if someone enabled it today.

## 5. Options

### A. Leave it alone
Discovery stays unimplemented, multicast stays off, the code stays as dead weight.
**Cost:** none. **Consequence:** the `GET_SERVER_ID` stub and the "not recommended" comment
keep implying the feature half-exists, and the next person re-derives all of §4.

### B. Delete the clock feature, keep and fix discovery *(recommended)*
Remove `XCP_ENABLE_DAQ_CLOCK_MULTICAST` and its A2L line. What remains is a discovery
transport rather than a clock transport, which is a much easier thing to justify keeping.
Then fix the join per §4 and rename the option to say what it is.

**Cost:** a deletion, plus ~40 lines in the join path. **Consequence:** `XCPTL_ENABLE_MULTICAST`
becomes an honest, defensible option; the raw-transport exclusion still applies.

### C. Also implement plain `GET_SERVER_ID` (`0xFF`)
The stub says the response *"has atypical layout"* — it is not a normal CRM, which is why it
was skipped. Only worth doing if a real tool is found that sends `0xFF` and not `0xFD`.

**Cost:** small, but needs the XCP spec open. **Consequence:** none until such a tool exists.
**Do not do this speculatively.**

### D. Move discovery out of the library, as CMP did
Discovery is stateless, answered before any session exists, and touches neither the queue nor
DAQ. An application could own the socket and call a small library helper to format the
response, exactly as `cmp_demo` owns its CMP responder.

**Cost:** a new public API. **Consequence:** keeps multicast, interface enumeration and IGMP
out of the library — attractive for FreeRTOS targets, where a multicast join may not exist.
**Downside:** every application that wants discovery re-implements the socket handling.

### Summary

| | Effort | Removes dead code | Works on `raw` | Needs the XCP spec |
|---|---|---|---|---|
| A. Leave alone | none | no | n/a | no |
| B. Delete clock, fix join | small | yes | no | to settle the port |
| C. Plain `GET_SERVER_ID` | small | no | no | yes |
| D. Move out of the library | medium | yes | yes | to settle the port |

## 6. What to settle first

Two questions gate everything above, and neither needs code:

1. **Is the XCP multicast port 5556 or 5557?** Read ASAM MCD-1 XCP. If XCPlite is on the wrong
   port, option A is not "leave it alone", it is "leave a bug in place".
2. **Does any tool you care about actually use XCP discovery?** CANape is normally pointed at
   an address, and the A2L carries it. If nothing in the toolchain sends `GET_SERVER_ID`, then
   B is a cleanup exercise, not a feature — still worth doing, but with no deadline.

Until both are answered, `examples/cmp_demo/src/cmp_discovery.c` stands as the working
reference for how this mechanism behaves in practice.

---

## Appendix: a spec ambiguity resolved in the CMP responder

ASAM CMP §12.1.1 Table 79 encodes `DeviceDescription` and `SerialNumber` as an `A_UINT16`
length followed by a zero-terminated, `0x00`-padded-to-16-bit `A_UTF8` string. Its offset
column is self-inconsistent by one about where the next field begins (`47–47+N = L1`, then
the next field at `L1+1`, which makes the string `N+1` bytes while calling it `A_UTF8[N]`).

`cmp_discovery.c` takes **N as the padded byte count** — `"Dev1"` → 6, `""` → 2. The
justification is the identical construction in the status message payload (§8.2.1), whose
wording is *"N is length before"* against a field typed `A_UTF8[N]`, plus the spec's own
worked example showing the padded form as what goes on the wire. A tool that interprets N as
the character count will mis-parse everything after `DeviceDescription`; if one is ever found
that does, this is the first place to look.
