# XCPlite Configuration Guide

**Version**: 2.3.x

## 0 · XCPlite Configuration

The default configuration of XCPlite should work on any platform out of the box, with most features enabled and a typical parameter configuration.

However, there are several reasons, why to change the default configuration:

1. Optimize memory usage
2. Enable/disable features
3. Modify communication parameters

Most important parameters are:

1. Maximum number of calibration segments and events
2. Maximum size of the DAQ table, a measurement signal (ODT entry) needs 6 bytes in DAQ table memory, more if it is fragmented into several ODT entries
3. MTU for UDP (Jumbo frames)
4. Clock source, epoch and resolution

The size of the transmit queue is a runtime parameter of `XcpEthServerInit`, except for the FreeRTOS queue variant (`rtos` configuration), where it is fixed at compile time with `OPTION_QUEUE_32_SIZE`.

Changes are made in an override header which `xcplib_cfg.h` includes at its end (`#undef`/`#define` of the `OPTION_*` defaults). The shipped build configurations are such headers, `src/xcplib_<name>_cfg.h`, selected with the CMake variable `XCPLITE_CONFIGURATION`. An application specific header is selected with the CMake variable `XCPLITE_CFG_OVERRIDE` (path to the file, configuration `default` only), see [BUILDING.md](BUILDING.md#application-specific-configuration-override) and `examples/fetchcontent_example/config/xcplib_app_cfg.h`. The library and the application must be compiled with the same override, which CMake guarantees by applying it as a PUBLIC usage requirement of the `xcplite` target.

### Shipped configurations

| Configuration | Override header | Key differences to the defaults |
|---|---|---|
| `default` | *(none)* | 64-bit, on-target A2L generation, persistence, UDP and TCP |
| `no_a2l` | `src/xcplib_no_a2l_cfg.h` | `OPTION_SECTION_REGISTRATION`, `OPTION_CAL_SEGMENTS_ABS`, `OPTION_ENABLE_ELF_UPLOAD`; no `OPTION_DAQ_EVENT_LIST`, no persistence, no A2L generator/upload |
| `ptp` | `src/xcplib_ptp_cfg.h` | `OPTION_SOCKET_HW_TIMESTAMPS` |
| `shm` | `src/xcplib_shm_cfg.h` | `OPTION_SHM_MODE`, no `OPTION_SECTION_REGISTRATION`, see [SHM.md](SHM.md) |
| `rtos` | `src/xcplib_rtos_cfg.h` | FreeRTOS/lwIP, UDP only, 1 µs clock, `OPTION_QUEUE_32` with critical sections, `OPTION_SECTION_REGISTRATION`, `OPTION_CAL_SEGMENTS_ABS`; no persistence, no A2L generator/upload |
| `raw` | `src/xcplib_raw_cfg.h` | `OPTION_ENABLE_UDP_RAW` instead of UDP/TCP, `OPTION_MTU 1420`, `OPTION_QUEUE_32`, see [SOCKET_RAW.md](SOCKET_RAW.md) |

## 1 · xcplib_cfg.h

This section describes the configuration parameters in xcplib_cfg.h.

### Version and Compatibility

| Parameter | Description |
|-----------|-------------|
| `OPTION_VERSION_MAJOR`, `OPTION_VERSION_MINOR`, `OPTION_VERSION_PATCH` | XCPlite version, keep in sync with `project(xcplite VERSION ...)` in `CMakeLists.txt`. Major and minor version are reported as `XCP_DRIVER_VERSION` in GET_COMM_MODE_INFO |
| `OPTION_CANAPE_24` | Disables workarounds for CANape versions < 24SP2 (COPY_CAL_PAGE workaround `XCP_ENABLE_COPY_CAL_PAGE_WORKAROUND`, `.this` references to shared calibration axis) (default: defined) |

### Logging Options

| Parameter | Description |
|-----------|-------------|
| `OPTION_ENABLE_DBG_PRINTS` | Enables debug print statements throughout the library |
| `OPTION_ENABLE_DBG_STDERR` | Errors and warnings are printed to stderr instead of stdout (default: defined, undefined in `rtos`) |
| `OPTION_DEFAULT_DBG_LEVEL` | Default log level, changed at runtime with `XcpSetLogLevel()`: 0=Off, 1=Error, 2=Warning, 3=Info, 4=Trace (all XCP commands and responses), 5=Debug, 6=Very verbose (default: 3) |
| `OPTION_MAX_DBG_LEVEL` | Maximum runtime adjustable log level, the prints of higher levels are optimized out (default: 4) |
| `OPTION_FIXED_DBG_LEVEL` | Fixed log level, not runtime adjustable, `XcpSetLogLevel()` has no effect. Recommended for production builds, together with a low level to save code size and runtime (default: undefined) |

### Clock Configuration Options

| Parameter | Description |
|-----------|-------------|
| `OPTION_CLOCK_EPOCH_ARB` | Uses arbitrary epoch (CLOCK_MONOTONIC_RAW on Linux, CLOCK_MONOTONIC on QNX) for DAQ timestamps (default) |
| `OPTION_CLOCK_EPOCH_PTP` | Uses PTP epoch (CLOCK_REALTIME which may be disciplined by NTP or PTP4L on Linux) for DAQ timestamps |
| `OPTION_CLOCK_TICKS_1NS` | Sets clock resolution to 1 nanosecond (default) |
| `OPTION_CLOCK_TICKS_1US` | Sets clock resolution to 1 microsecond (`rtos` configuration) |

Exactly one epoch and one resolution option must be defined.

### Platform Options

| Parameter | Description |
|-----------|-------------|
| `OPTION_ATOMIC_EMULATION` | Emulates atomics with a mutex, forces `OPTION_QUEUE_32`. Defined automatically on Windows |

### XCP Server Options

| Parameter | Description |
|-----------|-------------|
| `OPTION_ENABLE_TCP` | Enables TCP transport layer support for XCP communication |
| `OPTION_ENABLE_UDP` | Enables UDP transport layer support for XCP communication |
| `OPTION_MTU` | IP (link) MTU in bytes, the Ethernet header is not part of it. The UDP segment size is derived as `(OPTION_MTU - 28) & ~7`, so the IP packet never exceeds `OPTION_MTU`. Must be <= the link MTU, which is not checked at compile time (see the note in `xcptl_cfg.h`). Jumbo frames are supported (default: 1500) |
| `OPTION_SERVER_FORCEFULL_TERMINATION` | Terminates server threads forcefully instead of waiting for graceful shutdown |
| `OPTION_ENABLE_GET_LOCAL_ADDR` | Enables automatic determination of local IP address for A2L files when bound to ANY (0.0.0.0) (default: undefined) |

### Calibration Segment Options

| Parameter | Description |
|-----------|-------------|
| `OPTION_CAL_SEGMENTS` | Enables calibration segment management and the API for thread-safe calibration (`XCP_ENABLE_CALSEG_LIST`). Without it, the addressing scheme is `XCPLITE__AXSDD` (default: defined) |
| `OPTION_CAL_SEGMENT_COUNT` | Maximum number of calibration segments, including the EPK segment (default: 8) |
| `OPTION_CAL_MEM_SIZE` | Total memory pool size for all calibration segments (header and pages), must be large enough for all segments combined (default: 5 KB, 4 KB in `rtos`) |
| `OPTION_CAL_RCU_REFCOUNT` | RCU algorithm for calibration segments. Undefined - lock count and page hand-over (wait-free lock, publishing depends on the readers). Defined - per page reference counting (lock-free lock, publishing independent of reader progress, `XcpUnlockCalSeg`/`CalSegUnlock`/`CalBlkUnlock` take the page pointer returned by the lock), only for special needs such as slow readers of configuration parameters. See [CAL_RCU.md](CAL_RCU.md) (default: undefined) |
| `OPTION_CAL_SEGMENTS_SINGLE_PAGE` | Single page mode, disables the calibration page commands GET/SET_CAL_PAGE and COPY_CAL_PAGE, not compatible with `OPTION_CAL_SEGMENTS_START_ON_REFERENCE_PAGE` (default: undefined) |
| `OPTION_ENABLE_PERSISTENCE` | Enables the binary persistence file (.bin), which stores events and calibration segments, keeps their numbering stable across application restarts and persists working page data (enables `XCP_ENABLE_CAL_PERSISTENCE`, `XCP_ENABLE_FREEZE_CAL_PAGE`). Mandatory in `shm` (default: defined, undefined in `no_a2l` and `rtos`) |
| `OPTION_CAL_SEGMENT_EPK` | Enables the EPK calibration segment (index 0) to check HEX/BIN file compatibility via the EPK address in the A2L file (default: defined) |
| `OPTION_CAL_SEGMENTS_ABS` | Switches from segment relative addressing (`XCPLITE__CASDD`, address extension 0 is segment relative) to absolute addressing on address extension 0 (`XCPLITE__ACSDD`), as expected by most A2L creation and update tools. Requires the default/reference pages to be at stable addresses in the range 0x0000_0000 to 0xFFFF_FFFF (default: undefined, defined in `no_a2l` and `rtos`) |
| `OPTION_CAL_SEGMENTS_START_ON_REFERENCE_PAGE` | Start on the reference/default page instead of the working page (default: undefined) |

### DAQ Options

| Parameter | Description |
|-----------|-------------|
| `OPTION_DAQ_EVENT_LIST` | Enables DAQ event management: dynamic event creation at runtime, per thread event instances, `XcpCreateEvent`, `XcpFindEvent`, ... Mandatory for most XCPlite examples; when off, `OPTION_SECTION_REGISTRATION` is mandatory (default: defined, undefined in `no_a2l`) |
| `OPTION_DAQ_EVENT_COUNT` | Maximum number of DAQ events (must be even, max 1000), needs `OPTION_DAQ_EVENT_COUNT * 2` bytes of memory. Without `OPTION_DAQ_EVENT_LIST`, it bounds the event number and is used for optimization (default: 16) |
| `OPTION_DAQ_MEM_SIZE` | Memory bytes used for XCP DAQ tables. Each ODT entry (measurement signal) needs 6 bytes, each ODT 8 bytes, each DAQ list 12 bytes (default: 512 × 6) |
| `OPTION_DAQ_ASYNC_EVENT` | Creates a cyclic default event `async` with event id 0 and 1 ms cycle time for asynchronous data acquisition, requires `OPTION_DAQ_EVENT_LIST` (default: undefined) |

### Transmit Queue Options

Exactly one queue variant is selected, see `src/queue.h`.

| Parameter | Description |
|-----------|-------------|
| `OPTION_QUEUE_64_VAR_SIZE` | Lock-free queue with variable entry size, vectored IO (`queue64v.c`). Default on 64-bit platforms |
| `OPTION_QUEUE_64_FIX_SIZE` | Lock-free queue with fixed entry size, vectored IO (`queue64f.c`). Maximum performance, less memory efficient, sets `XCPTL_MAX_DTO_SIZE` to 248 so that an entry fits the cache line granularity |
| `OPTION_QUEUE_32` | Mutex or critical section based queue with variable entry size and message accumulation into segments (`queue32.c`, or `queue32m.c` on FreeRTOS). Forced on Windows (`OPTION_ATOMIC_EMULATION`) and on 32-bit platforms, mandatory with `OPTION_ENABLE_UDP_RAW` |

### Runtime A2L Generation Options

| Parameter | Description |
|-----------|-------------|
| `OPTION_ENABLE_A2L_GENERATOR` | Enables the on-target A2L generator (default: defined, undefined in `no_a2l` and `rtos`) |
| `OPTION_ENABLE_A2L_UPLOAD` | Enables A2L file upload to the XCP client (GET_ID and XCP protocol UPLOAD commands) |

### Build-time A2L Generation Options

| Parameter | Description |
|-----------|-------------|
| `OPTION_SECTION_REGISTRATION` | Registers events and calibration segments at link time in the sections `xcp_evts` and `xcp_cals` (ELF or Mach-O required), the event id and the segment number are the positions in the sections, stable for a build, and the offline A2L generator (`xcpclient --elf`) reads the sections. Not supported with MSVC and in `shm`. Set in the `no_a2l` and `rtos` configurations, mandatory when `OPTION_DAQ_EVENT_LIST` is off. Off by default: events are created at runtime in creation order and the trigger macros look them up by name once. See [OFFLINE_A2L.md](OFFLINE_A2L.md) |
| `OPTION_ENABLE_ELF_UPLOAD` | Enables ELF file upload to the XCP client with a user defined command, used by `xcpclient` (default: undefined, defined in `no_a2l`) |

### Test Options

Only effective in debug builds (`NDEBUG` not defined), all undefined by default: `TEST_CLOCK_GET_STATISTIC`, `TEST_ACQUIRE_SPIN_COUNT`, `TEST_ACQUIRE_LOCK_TIMING`, `TEST_ENABLE_TL_METRICS`, `TEST_ENABLE_CAL_METRICS`, `TEST_ENABLE_BUFFERCOUNT_HISTOGRAM`, `TEST_MUTABLE_ACCESS_OWNERSHIP`, `TEST_ENABLE_DBG_CHECKS`, `TEST_STACK_SIZE`. See the comments in `xcplib_cfg.h`.

## 2 · Configuration specific options

These options are not defined in `xcplib_cfg.h`, they are set in the override headers of the shipped configurations or in an application specific override.

### SHM (`xcplib_shm_cfg.h`)

| Parameter | Description |
|-----------|-------------|
| `OPTION_SHM_MODE` | Shared memory multi-application mode, selects the addressing scheme `XCPLITE__CXSDD`. See [SHM.md](SHM.md) |

### PTP (`xcplib_ptp_cfg.h`)

| Parameter | Description |
|-----------|-------------|
| `OPTION_SOCKET_HW_TIMESTAMPS` | Enables hardware timestamps on UDP sockets if available (Linux, needed for `ptptool`) |

### Raw Ethernet (`xcplib_raw_cfg.h`)

| Parameter | Description |
|-----------|-------------|
| `OPTION_ENABLE_UDP_RAW` | XCP on UDP/IPv4 implemented inside xcplib on a raw Ethernet HAL. Mutually exclusive with `OPTION_ENABLE_UDP`/`OPTION_ENABLE_TCP`, requires `OPTION_QUEUE_32`, not supported with `OPTION_SHM_MODE` |
| `OPTION_UDP_RAW_HAL_EXTERNAL` | The application supplies its own Ethernet HAL backend instead of the Linux AF_PACKET backend |
| `OPTION_UDP_RAW_ZERO_COPY` | Reserves `XCPTL_TX_HEADROOM` bytes in front of every transmit segment, so the Ethernet/IPv4/UDP header is written in place |
| `OPTION_UDP_RAW_IFNAME` | Default network interface (default: `"eth0"`) |
| `OPTION_UDP_RAW_ENABLE_ICMP_ECHO` | Answers ICMP echo requests (ping) |
| `OPTION_UDP_RAW_UDP_CHECKSUM_ZERO`, `_COMPUTE`, `_HW` | Transmit UDP checksum: 0 (not used), computed in software, or inserted by the EMAC hardware |
| `OPTION_UDP_RAW_VERIFY_RX_CHECKSUM` | Verifies the checksums of received packets |
| `OPTION_UDP_RAW_GRATUITOUS_ARP` | Sends a gratuitous ARP announcement on bind |

See [SOCKET_RAW.md](SOCKET_RAW.md) for details.

### FreeRTOS (`xcplib_rtos_cfg.h`)

| Parameter | Description |
|-----------|-------------|
| `OPTION_FREERTOS_STACK_BYTES` | Stack size of the XCP server tasks (default: 4 KB, 16 KB in the POSIX simulator) |
| `OPTION_FREERTOS_PRIORITY` | Priority of the XCP server tasks (default: `tskIDLE_PRIORITY + 2`) |
| `OPTION_FREERTOS_LWIP` | Use lwIP as IP stack (raw API by default) |
| `OPTION_FREERTOS_LWIP_SOCKET_API` | Use the lwIP socket API instead of the raw API |
| `OPTION_QUEUE_32_SIZE` | Fixed transmit queue buffer size, must be a multiple of `sizeof(tXcpSegmentBuffer)`, the queue size parameter of `XcpEthServerInit` must be 0 (default: 16 segments) |
| `OPTION_QUEUE32_MUTEX` | Use a mutex in `queue32m.c`; otherwise use FreeRTOS critical sections |
| `OPTION_QUEUE_32_ATTRIBUTE`, `OPTION_QUEUE_32_BUFFER_ATTRIBUTE` | Optional attributes for the memory placement of the queue state and the segment buffers, e.g. a section attribute for DTCM or non-cacheable memory, see `queue32m.c` |

## 3 · xcptl_cfg.h

This section describes the transport layer configuration parameters in xcptl_cfg.h.

### Transport Layer Version

| Parameter | Description |
|-----------|-------------|
| `XCP_TRANSPORT_LAYER_VERSION` | Defines the XCP transport layer version (0x0104) |

### Packet Size Configuration

| Parameter | Description |
|-----------|-------------|
| `XCPTL_MAX_CTO_SIZE` | Maximum size of XCP command packets (CRO/CRM) in bytes. Must be divisible by 8 and <= `XCPTL_MAX_DTO_SIZE` (default: 248) |
| `XCPTL_MAX_DTO_SIZE` | Maximum size of XCP data packets (DAQ/STIM) in bytes. Must be divisible by 8 and <= `XCPTL_MAX_SEGMENT_SIZE` (default: 1024, 248 with `OPTION_QUEUE_64_FIX_SIZE`) |
| `XCPTL_MAX_SEGMENT_SIZE` | Maximum data buffer size for socket send operations, for UDP the maximum UDP payload size. Calculated as `(OPTION_MTU - 20 - 8) & ~7` (IPv4 and UDP headers, aligned down to 8) |
| `XCPTL_PACKET_ALIGNMENT` | Size granularity of the protocol layer packet inside a transport layer message; the packet is padded to this alignment so that concatenated messages in a segment stay aligned. Only 4 is supported, enforced by an `#error` in `queue.h`. Note the fill is included in the message LEN — see "Transport Layer Message Padding" in [TECHNICAL.md](TECHNICAL.md) (default: 4) |
| `XCPTL_TRANSPORT_LAYER_HEADER_SIZE` | Transport layer message header size in bytes (fixed: 4) |
| `XCPTL_TX_HEADROOM` | Space reserved in front of each transmit segment for link headers, 48 with `OPTION_ENABLE_UDP_RAW` and `OPTION_UDP_RAW_ZERO_COPY`, otherwise 0. Requires `OPTION_QUEUE_32` |

### Transport Layer Behavior

| Parameter | Description |
|-----------|-------------|
| `XCPTL_RECV_TIMEOUT_MS` | Receive timeout in milliseconds, the rate of periodic checks for shutdown and background tasks in the receive thread (default: 100) |
| `XCPTL_CRM_VIA_TRANSMIT_QUEUE` | Sends command responses via the transmit queue instead of directly from the command processing thread. May improve overall performance, but adds latency to command responses (problem with GET_DAQ_CLOCK, not in PTP mode) (default: undefined) |
| `XCPTL_EXCLUDE_CRM_FROM_CTR` | Uses separate transport layer counters for DAQ and command response messages. Not supported by all XCP tools, in CANape it must match the COUNTER_HANDLING setting (default 'include command response') (default: undefined) |

### Multicast Configuration

| Parameter | Description |
|-----------|-------------|
| `XCPTL_ENABLE_MULTICAST` | Enables multicast time synchronization (GET_DAQ_CLOCK_MULTICAST) for improved synchronization of multiple XCP servers (default: undefined, not recommended) |
| `XCLTL_RESTRICT_MULTICAST` | Restricts multicast functionality (optional) |
| `XCPTL_MULTICAST_PORT` | Port number for multicast communication (default: 5557) |

**Note on Multicast**: Multicast time synchronization is available since XCP V1.3 but requires an additional thread and socket. There's no benefit if PTP time synchronization is used or with only one XCP device. Older CANape versions may expect this option to be enabled by default, turn it off in device/protocol/event/TIME_CORRELATION_GETDAQCLOCK by changing from "multicast" to "extendedresponse".

## 4 · xcp_cfg.h

This section describes the XCP protocol layer configuration parameters in xcp_cfg.h. Most of them are derived from the `OPTION_*` settings in `xcplib_cfg.h` and are not meant to be changed directly.

### Version Configuration

| Parameter | Description |
|-----------|-------------|
| `XCP_DRIVER_VERSION` | XCP driver version for GET_COMM_MODE_INFO, derived from `OPTION_VERSION_MAJOR` (high nibble) and `OPTION_VERSION_MINOR` (low nibble) |
| `XCP_ENABLE_PROTOCOL_LAYER_ETH` | Enables Ethernet-specific protocol layer commands |
| `XCP_PROTOCOL_LAYER_VERSION` | XCP protocol layer version (0x0104 - supports PACKED_MODE, CC_START_STOP_SYNCH) |
| `XCP_EPK_MAX_LENGTH` | Maximum length of the EPK string without null terminator, must be odd (default: 31) |
| `XCP_PROJECT_NAME_MAX_LENGTH` | Maximum length of the project name without null terminator, must be odd (default: 31) |
| `XCP_A2L_FILENAME_MAX_LENGTH` | Maximum length of the A2L file name with extension, without null terminator (default: 255) |

### Addressing Schemes

The addressing scheme is selected by the options in `xcplib_cfg.h` and written to the A2L file as PROJECT_NO (`XCP_ADDRESS_MODE`). See [TECHNICAL.md](TECHNICAL.md) for the addressing modes.

| `XCP_ADDRESS_MODE` | Selected by | `XCP_ADDR_EXT_SEG` | `XCP_ADDR_EXT_ABS` | `XCP_ADDR_EXT_APP` |
|---|---|---|---|---|
| `XCPLITE__CASDD` | default (`OPTION_CAL_SEGMENTS`) | 0x00 | 0x01 | 0x80 |
| `XCPLITE__ACSDD` | `OPTION_CAL_SEGMENTS_ABS` | 0x01 | 0x00 | – |
| `XCPLITE__CXSDD` | `OPTION_SHM_MODE` | 0x00 | 0x80 + application id | 0x01 |
| `XCPLITE__AXSDD` | `OPTION_CAL_SEGMENTS` undefined | – | 0x00 | 0x01 |

In all schemes the address extensions 0x02 to 0x0F are used for dynamic (event based relative) addressing, 0xFD for file upload, 0xFE for the MTA pointer and 0xFF is undefined.

### Address and Address Extension Configuration

| Parameter | Description |
|-----------|-------------|
| `XCP_ENABLE_DAQ_ADDREXT` | Enables individual address extensions for each ODT entry, otherwise the address extension must be unique for each DAQ list |
| `XCP_ENABLE_SEG_ADDRESSING` | Enables calibration segment relative addressing, requires `XCP_ENABLE_CALSEG_LIST` |
| `XCP_ADDR_EXT_SEG` | Address extension for segment relative addressing, see the table above |
| `XCP_ENABLE_ABS_ADDRESSING` | Enables asynchronous absolute addressing mode (not thread safe) |
| `XCP_ADDR_EXT_ABS` | Address extension for absolute addressing, see the table above |
| `XCP_ENABLE_APP_ADDRESSING` | Enables application specific addressing, memory access via `ApplXcpReadMemory`/`ApplXcpWriteMemory` |
| `XCP_ADDR_EXT_APP` | Address extension for application specific addressing, see the table above |
| `XCP_ENABLE_DYN_ADDRESSING` | Enables event based relative addressing modes with asynchronous access (always enabled) |
| `XCP_ADDR_EXT_DYN` | First address extension for dynamic addressing, the range is `XCP_ADDR_EXT_DYN` to `XCP_ADDR_EXT_DYN_MAX` (default: 0x02 to 0x0F) |
| `XCP_ADDR_EXT_DYN_COUNT` | Number of address extensions (base pointers) for dynamic addressing (default: 14) |
| `XCP_ENABLE_REL_ADDRESSING` | Enables event based relative addressing without asynchronous access, uses `XCP_ADDR_EXT_REL`. Not enabled in any configuration, `XCP_ADDR_EXT_REL` must be defined together with it (default: undefined) |
| `XCP_ENABLE_EPK_CALSEG` | EPK calibration segment with index 0 (`XCP_EPK_CALSEG_INDEX`), enabled by `OPTION_CAL_SEGMENT_EPK` |
| `XCP_ADDR_EPK` | EPK address in the A2L file: 0x80000000 (segment relative) with the EPK segment and `XCP_ADDR_EXT_SEG` 0, otherwise 0xFFFFFF00 |

### Protocol Features

| Parameter | Description |
|-----------|-------------|
| `XCP_ENABLE_CAL_PAGE` | Enables calibration page switching commands GET/SET_CAL_PAGE, disabled by `OPTION_CAL_SEGMENTS_SINGLE_PAGE` |
| `XCP_ENABLE_COPY_CAL_PAGE` | Enables calibration page initialization COPY_CAL_PAGE (FLASH→RAM copy only supported), disabled by `OPTION_CAL_SEGMENTS_SINGLE_PAGE` |
| `XCP_ENABLE_COPY_CAL_PAGE_WORKAROUND` | Workaround for CANape < 24SP2, where COPY_CAL_PAGE copies all segments. Enabled when `OPTION_CANAPE_24` is undefined |
| `XCP_START_ON_REFERENCE_PAGE` | Start on the reference page, enabled by `OPTION_CAL_SEGMENTS_START_ON_REFERENCE_PAGE` |
| `XCP_ENABLE_CAL_PERSISTENCE` | Enables persistence of calibration segment working page data, enabled by `OPTION_ENABLE_PERSISTENCE` |
| `XCP_ENABLE_FREEZE_CAL_PAGE` | Enables calibration page freeze functionality (GET/SET_SEGMENT_MODE), required for calibration segment persistence, enabled by `OPTION_ENABLE_PERSISTENCE` |
| `XCP_ENABLE_FREEZE_ON_DISCONNECT` | Freezes (persists) the working page data on XCP disconnect, requires `OPTION_ENABLE_PERSISTENCE` (default: undefined) |
| `XCP_ENABLE_CHECKSUM` | Enables checksum calculation command |
| `XCP_CHECKSUM_TYPE` | Checksum algorithm type (XCP_CHECKSUM_TYPE_CRC16CCITT or XCP_CHECKSUM_TYPE_ADD44) |
| `XCP_ENABLE_SEED_KEY` | Enables seed/key security mechanism (commented out by default) |
| `XCP_ENABLE_SERV_TEXT` | Enables SERV_TEXT events |
| `XCP_ENABLE_IDT_A2L_UPLOAD` | Enables A2L file upload via XCP (depends on OPTION_ENABLE_A2L_UPLOAD) |
| `XCP_ENABLE_IDT_ELF_UPLOAD` | Enables ELF file upload via XCP (depends on OPTION_ENABLE_ELF_UPLOAD) |
| `XCP_ENABLE_USER_COMMAND` | Enables user-defined commands for atomic calibration operations |

### DAQ Features and Parameters

| Parameter | Description |
|-----------|-------------|
| `XCP_ENABLE_DAQ_EVENT_LIST` | Enables event list management, enabled by `OPTION_DAQ_EVENT_LIST` |
| `XCP_MAX_EVENT_COUNT` | Maximum number of DAQ events, equals `OPTION_DAQ_EVENT_COUNT`, must be even and <= 1024 (`XCP_DYN_ADDR_EVENT_BITS`). Optimizes the DAQ list to event association lookup (default: 16) |
| `XCP_MAX_EVENT_NAME` | Maximum length for event names in characters (default: 15) |
| `XCP_ENABLE_DAQ_EVENT_INFO` | Enables XCP_GET_EVENT_INFO command |
| `XCP_DAQ_MEM_SIZE` | Memory for DAQ tables, equals `OPTION_DAQ_MEM_SIZE`. Each ODT entry needs 6 bytes (5 without `XCP_ENABLE_DAQ_ADDREXT`), each ODT 8 bytes, each DAQ list 12 bytes |
| `XCP_ENABLE_DAQ_RESUME` | Enables DAQ resume mode functionality, requires `XCP_ENABLE_DAQ_EVENT_LIST` (default: undefined) |
| `XCP_ENABLE_DAQ_PRESCALER` | Enables DAQ prescaler (downsampling), requires `XCP_ENABLE_DAQ_EVENT_LIST` (default: undefined) |
| `XCP_ENABLE_DAQ_CONTROL` | Enables event control (enable/disable of DAQ events), requires `XCP_ENABLE_DAQ_EVENT_LIST` |
| `XCP_ENABLE_OVERRUN_INDICATION_PID` | Overrun indication via PID. Not needed for Ethernet, the client detects data loss by transport layer counter gaps (default: undefined) |
| `XCP_TRANSMIT_QUEUE_FLUSH_TIMEOUT_MS` | Timeout waiting for the transmit queue to be empty after stopping DAQ (default: 250) |

### Calibration Segment Configuration

| Parameter | Description |
|-----------|-------------|
| `XCP_ENABLE_CALSEG_LIST` | Enables calibration segment list management, enabled by `OPTION_CAL_SEGMENTS` |
| `XCP_MAX_CALSEG_COUNT` | Maximum number of calibration segments, equals `OPTION_CAL_SEGMENT_COUNT` (default: 8) |
| `XCP_CAL_MEM_SIZE` | Memory pool for calibration segments, equals `OPTION_CAL_MEM_SIZE` (default: 5 KB) |
| `XCP_ENABLE_CALSEG_LAZY_WRITE` | Enables lazy write mode for calibration segments with background RCU updates. Calibration write speed is then independent of the lock rate, but single calibration latencies are higher |
| `XCP_CALSEG_AQUIRE_FREE_PAGE_TIMEOUT` | Timeout for acquiring free calibration segment pages in milliseconds (default: 500) |
| `XCP_CALSEG_RCU_PAGES` | Number of RCU pages per calibration segment with `OPTION_CAL_RCU_REFCOUNT`, 3 to 6. 3 is the minimum and needs the same memory as the lock count algorithm, each further page tolerates one more reader which never unlocks and improves the publish rate under a bursting writer (default: 3) |

### Clock and Timestamp Configuration

| Parameter | Description |
|-----------|-------------|
| `XCP_DAQ_CLOCK_64BIT` | Uses XCP V1.3 64-bit timestamps (default) |
| `XCP_TIMESTAMP_UNIT` | Timestamp unit, always DAQ_TIMESTAMP_UNIT_1NS |
| `XCP_TIMESTAMP_TICKS` | Timestamp tick duration in timestamp units, `1000000000 / CLOCK_TICKS_PER_S` (1 with `OPTION_CLOCK_TICKS_1NS`, 1000 with `OPTION_CLOCK_TICKS_1US`) |
| `XCP_ENABLE_PTP` | Enables PTP support: the application may register callbacks to provide clock, synchronization state and clock info. If not registered, the configured DAQ clock is used and considered unsynchronized |
| `XCP_DAQ_CLOCK_UUID` | Default clock UUID for unsynchronized clocks |

### Multicast Clock Synchronization

| Parameter | Description |
|-----------|-------------|
| `XCP_ENABLE_DAQ_CLOCK_MULTICAST` | Enables GET_DAQ_CLOCK_MULTICAST functionality, requires `XCPTL_ENABLE_MULTICAST` (not recommended) |
| `XCP_MULTICAST_CLUSTER_ID` | XCP multicast cluster ID for time synchronization (default: 1) |

### Debug

| Parameter | Description |
|-----------|-------------|
| `XCP_ENABLE_TEST_CHECKS` | Enables extended error checks in `xcplite.c` and `cal.c`, small DAQ performance penalty in the inner DAQ loop (default: defined) |

## 5 · Transmit Queue Runtime Configuration

The transmit queue size is passed as `measurement_queue_size` to `XcpEthServerInit(address, port, use_tcp, measurement_queue_size)`. It should hold at least 10 ms of the expected measurement data rate. The size determines:

- Total memory allocated for the queue
- Number of queue entries that can be stored
- Maximum throughput capacity

**Memory Calculation** (`queue64v.c`): the queue header is added to the given size, which is then rounded up to the cache line size. Space for one maximum size entry (`QUEUE_MAX_ENTRY_SIZE` plus entry header) is reserved for the wrap around, so the effective queue size is roughly `measurement_queue_size - QUEUE_MAX_ENTRY_SIZE`.

With `queue32m.c` (`rtos` configuration) the queue buffer is statically allocated with the size `OPTION_QUEUE_32_SIZE`, and `measurement_queue_size` must be 0.
