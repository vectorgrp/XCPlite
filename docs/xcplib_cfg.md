# XCPlite Configuration Guide

**Version**: 0.9.2

## 0 · XCPlite Configuration

The default configuration of XCPlite should work on any platform out of the box, with most featutures enabled and a typical parameter configuration.

However, there are several reasons, why to change the default configuration:

1. Optimize memory usage
2. Enable/disable features
3. Modify communication parameters

Most important parameters are:

1. Maximum number of calibration segments and events
2. Maximum size of the DAQ table, on maximum fragmentation, a measurement signal may need up to 6 bytes in DAQ table memory
3. MTU for UDP (Jumbo frames)
4. Clock source, epoch and resolution

The size of the transmission queue is a runtime parameter.

## 1 · main_cfg.h

This section describes the configuration parameters in main_cfg.h.

### XCP Protocol Options

| Parameter | Description |
|-----------|-------------|
| `OPTION_ENABLE_TCP` | Enables TCP transport layer support for XCP communication |
| `OPTION_ENABLE_UDP` | Enables UDP transport layer support for XCP communication |
| `OPTION_MTU` | Ethernet packet size (MTU) in bytes. Must be divisible by 8. Jumbo frames are supported (default: 8000) |
| `OPTION_DAQ_MEM_SIZE` | Memory bytes used for XCP DAQ tables. Each signal needs approximately 5 bytes (default: 32 × 1024 × 5) |
| `OPTION_ENABLE_A2L_UPLOAD` | Enables A2L file upload through XCP protocol |
| `OPTION_SERVER_FORCEFULL_TERMINATION` | Terminates server threads forcefully instead of waiting for graceful shutdown |

### Clock Configuration Options

| Parameter | Description |
|-----------|-------------|
| `OPTION_CLOCK_EPOCH_ARB` | Uses arbitrary epoch (CLOCK_MONOTONIC_RAW) for time synchronization |
| `OPTION_CLOCK_EPOCH_PTP` | Uses PTP epoch (CLOCK_REALTIME) for time synchronization |
| `OPTION_CLOCK_TICKS_1NS` | Sets clock resolution to 1 nanosecond |
| `OPTION_CLOCK_TICKS_1US` | Sets clock resolution to 1 microsecond |

### Network Options

| Parameter | Description |
|-----------|-------------|
| `OPTION_ENABLE_GET_LOCAL_ADDR` | Enables automatic determination of local IP address for A2L files when bound to ANY (0.0.0.0). Note: May crash on Windows |

### Logging Options

| Parameter | Description |
|-----------|-------------|
| `OPTION_ENABLE_DBG_PRINTS` | Enables debug print statements throughout the library |
| `OPTION_DEFAULT_DBG_LEVEL` | Sets default logging level: 1=Error, 2=Warning, 3=Info, 4=Trace, 5=Debug (default: 2) |

## 2 · xcptl_cfg.h

This section describes the transport layer configuration parameters in xcptl_cfg.h.

### Transport Layer Version

| Parameter | Description |
|-----------|-------------|
| `XCP_TRANSPORT_LAYER_VERSION` | Defines the XCP transport layer version (0x0104) |

### Packet Size Configuration

| Parameter | Description |
|-----------|-------------|
| `XCPTL_MAX_CTO_SIZE` | Maximum size of XCP command packets (CRO/CRM) in bytes. Must be divisible by 8 (default: 248) |
| `XCPTL_MAX_DTO_SIZE` | Maximum size of XCP data packets (DAQ/STIM) in bytes. Must be divisible by 8 (default: 1024) |
| `XCPTL_MAX_SEGMENT_SIZE` | Maximum data buffer size for socket send operations. For UDP, this is the UDP MTU. Calculated as OPTION_MTU - 32 (IP header) |
| `XCPTL_PACKET_ALIGNMENT` | Packet alignment for multiple XCP transport layer packets in a message (default: 4) |
| `XCPTL_TRANSPORT_LAYER_HEADER_SIZE` | Transport layer message header size in bytes (fixed: 4) |

### Multicast Configuration

| Parameter | Description |
|-----------|-------------|
| `XCPTL_ENABLE_MULTICAST` | Enables multicast time synchronization for improved synchronization of multiple XCP slaves |
| `XCLTL_RESTRICT_MULTICAST` | Restricts multicast functionality (optional) |
| `XCPTL_MULTICAST_PORT` | Port number for multicast communication (default: 5557) |

**Note on Multicast**: Multicast time synchronization is available since XCP V1.3 but requires an additional thread and socket. There's no benefit if PTP time synchronization is used or with only one XCP device. Older CANape versions may expect this option to be enabled by default.

## 3 · xcp_cfg.h

This section describes the XCP protocol layer configuration parameters in xcp_cfg.h.

### Version Configuration

| Parameter | Description |
|-----------|-------------|
| `XCP_DRIVER_VERSION` | XCP driver version for GET_COMM_MODE_INFO command (default: 0x01) |
| `XCP_ENABLE_PROTOCOL_LAYER_ETH` | Enables Ethernet-specific protocol layer commands |
| `XCP_PROTOCOL_LAYER_VERSION` | XCP protocol layer version (0x0104 - supports PACKED_MODE, CC_START_STOP_SYNCH) |

### Address and Address Extension Configuration

| Parameter | Description |
|-----------|-------------|
| `XCP_ENABLE_DAQ_ADDREXT` | Enables individual address extensions for each ODT entry |
| `XCP_ENABLE_REL_ADDRESSING` | Enables event-based relative addressing modes without asynchronous access |
| `XCP_ADDR_EXT_REL` | Address extension code for relative address format (default: 0x03) |
| `XCP_ENABLE_DYN_ADDRESSING` | Enables event-based addressing modes with asynchronous access |
| `XCP_ADDR_EXT_DYN` | Address extension code for dynamic addressing (default: 0x02) |
| `XCP_ENABLE_ABS_ADDRESSING` | Enables asynchronous absolute addressing mode (not thread safe) |
| `XCP_ADDR_EXT_ABS` | Address extension code for absolute addressing (default: 0x01) |
| `XCP_ENABLE_APP_ADDRESSING` | Enables segment or application-specific addressing mode |
| `XCP_ADDR_EXT_APP` | Address extension for application-specific memory access (default: 0x00) |
| `XCP_ADDR_EXT_SEG` | Address extension for segment relative addressing (must be 0x00) |

### Special Address Extensions

| Parameter | Description |
|-----------|-------------|
| `XCP_ADDR_EXT_EPK` | Address extension for EPK upload memory space (0x00) |
| `XCP_ADDR_EPK` | Base address for EPK memory space (0x80000000) |
| `XCP_ADDR_EXT_A2L` | Address extension for A2L upload memory space (0xFD) |
| `XCP_ADDR_A2l` | Base address for A2L memory space (0x00000000) |
| `XCP_ADDR_EXT_PTR` | Address extension indicating gXcp.MtaPtr is valid (0xFE) |
| `XCP_UNDEFINED_ADDR_EXT` | Undefined address extension marker (0xFF) |

### Protocol Features

| Parameter | Description |
|-----------|-------------|
| `XCP_ENABLE_CAL_PAGE` | Enables calibration page switching commands |
| `XCP_ENABLE_COPY_CAL_PAGE` | Enables calibration page initialization (FLASH→RAM copy) |
| `XCP_ENABLE_FREEZE_CAL_PAGE` | Enables calibration page freeze and preload functionality |
| `XCP_ENABLE_CHECKSUM` | Enables checksum calculation command |
| `XCP_CHECKSUM_TYPE` | Checksum algorithm type (XCP_CHECKSUM_TYPE_CRC16CCITT or XCP_CHECKSUM_TYPE_ADD44) |
| `XCP_ENABLE_SEED_KEY` | Enables seed/key security mechanism (commented out by default) |
| `XCP_ENABLE_SERV_TEXT` | Enables SERV_TEXT events |
| `XCP_ENABLE_IDT_A2L_UPLOAD` | Enables A2L file upload via XCP (depends on OPTION_ENABLE_A2L_UPLOAD) |
| `XCP_ENABLE_USER_COMMAND` | Enables user-defined commands for atomic calibration operations |

### DAQ Features and Parameters

| Parameter | Description |
|-----------|-------------|
| `XCP_MAX_EVENT_COUNT` | Maximum number of DAQ events. Must be even. Optimizes DAQ list to event association lookup (default: 256) |
| `XCP_MAX_DAQ_COUNT` | Maximum number of DAQ lists. Must be ≤ 0xFFFE (default: 1024) |
| `XCP_DAQ_MEM_SIZE` | Static memory allocation for DAQ tables. Each ODT entry needs 5 bytes, each DAQ list 12 bytes, each ODT 8 bytes |
| `XCP_ENABLE_DAQ_RESUME` | Enables DAQ resume mode functionality |
| `XCP_ENABLE_DAQ_EVENT_LIST` | Enables event list management (not needed for Rust xcp-lite) |
| `XCP_ENABLE_DAQ_EVENT_INFO` | Enables XCP_GET_EVENT_INFO command (overrides A2L event information) |
| `XCP_MAX_EVENT_NAME` | Maximum length for event names in characters (default: 15) |

### Calibration Segment Configuration

| Parameter | Description |
|-----------|-------------|
| `XCP_ENABLE_CALSEG_LIST` | Enables calibration segment list management (not needed for Rust xcp-lite) |
| `XCP_MAX_CALSEG_COUNT` | Maximum number of calibration segments (default: 4) |
| `XCP_MAX_CALSEG_NAME` | Maximum length for calibration segment names (default: 15) |
| `XCP_ENABLE_CALSEG_LAZY_WRITE` | Enables lazy write mode for calibration segments with background RCU updates |
| `XCP_CALSEG_AQUIRE_FREE_PAGE_TIMEOUT` | Timeout for acquiring free calibration segment pages in milliseconds (default: 500) |

### Clock and Timestamp Configuration

| Parameter | Description |
|-----------|-------------|
| `XCP_DAQ_CLOCK_32BIT` | Uses 32-bit timestamps (commented out) |
| `XCP_DAQ_CLOCK_64BIT` | Uses 64-bit timestamps (default) |
| `XCP_TIMESTAMP_UNIT` | Timestamp unit (DAQ_TIMESTAMP_UNIT_1US or DAQ_TIMESTAMP_UNIT_1NS) |
| `XCP_TIMESTAMP_TICKS` | Ticks per timestamp unit (default: 1) |
| `XCP_ENABLE_PTP` | Enables PTP (Precision Time Protocol) grandmaster clock support |
| `XCP_DAQ_CLOCK_UIID` | UUID for DAQ clock identification |

### Multicast Clock Synchronization

| Parameter | Description |
|-----------|-------------|
| `XCP_ENABLE_DAQ_CLOCK_MULTICAST` | Enables GET_DAQ_CLOCK_MULTICAST functionality (not recommended) |
| `XCP_MULTICAST_CLUSTER_ID` | XCP multicast cluster ID for time synchronization (default: 1) |

### Debug Configuration

| Parameter | Description |
|-----------|-------------|
| `XCP_ENABLE_TEST_CHECKS` | Enables extended error checks with performance penalty |
| `XCP_ENABLE_OVERRUN_INDICATION_PID` | Enables overrun indication via PID (not needed for Ethernet) |

## 4 · Transmit Queue Configuration

The 64-bit transmit queue has several parameters to further optimize DAQ measurement performance and data capture side effects.

### Queue Implementation Selection

The queue implementation can be configured using one of three mutually exclusive defines that control the synchronization mechanism between producers and consumers:

| Parameter | Description |
|-----------|-------------|
| `QUEUE_MUTEX` | Uses mutex-based producer locking. This is convenient and might be optimal for high throughput when worst-case producer latency is acceptable. No consumer lock, uses memory fences between producer and consumer |
| `QUEUE_SEQ_LOCK` | Uses sequence lock to protect against inconsistency during entry acquire. The queue is lock-free with minimal CAS spin wait in contention. The consumer may spin heavily to acquire a safe consistent head |
| `QUEUE_NO_LOCK` | No synchronization between producer and consumer. Producer uses CAS loop to increment head, consumer clears memory completely for consistent reservation state. Tradeoff between consumer spin activity and consumer cache activity - might be optimal for medium throughput |

**Default**: `QUEUE_NO_LOCK` is enabled by default as it provides the best balance for medium throughput scenarios.

### Queue Optimization Parameters

| Parameter | Description |
|-----------|-------------|
| `QUEUE_ACCUMULATE_PACKETS` | Enables accumulation of multiple XCP packets into XCP messages within a segment obtained with `QueuePeek()`. This improves efficiency by reducing the number of network operations |
| `QUEUE_PEEK_THRESHOLD` | Minimum number of bytes that must be in the queue before `QueuePeek()` returns a segment. Set to `XCPTL_MAX_SEGMENT_SIZE` by default to optimize transmission efficiency |
| `CACHE_LINE_SIZE` | Cache line size used to align queue entries and queue header. Set to 128 bytes to accommodate most modern CPU architectures |
| `MAX_ENTRY_SIZE` | Maximum size of a single queue entry, calculated as `XCPTL_MAX_DTO_SIZE + XCPTL_TRANSPORT_LAYER_HEADER_SIZE`. Must be aligned to `XCPTL_PACKET_ALIGNMENT` |

### Performance Testing and Profiling (Development Only)

**Warning**: These parameters have significant performance impact and should NOT be enabled in production builds.

| Parameter | Description |
|-----------|-------------|
| `TEST_ACQUIRE_LOCK_TIMING` | Enables timing measurement for queue acquire operations. Collects statistics on lock acquisition times including maximum, sum, and histogram data |
| `TEST_ACQUIRE_SPIN_COUNT` | Enables spin count statistics for producer acquire operations. Tracks how many spin loops are needed during contention |
| `TEST_CONSUMER_SEQ_LOCK_SPIN_COUNT` | Enables spin count statistics for consumer sequence lock operations. Tracks consumer spinning behavior with sequence locks |

### Profiling Configuration Constants

When profiling is enabled, the following constants control the statistics collection:

| Parameter | Description |
|-----------|-------------|
| `LOCK_TIME_HISTOGRAM_SIZE` | Size of lock time histogram array (default: 100 entries) |
| `LOCK_TIME_HISTOGRAM_STEP` | Step size for lock time measurements (10ns steps) |
| `SPIN_COUNT_HISTOGRAM_SIZE` | Size of spin count histogram for producer statistics (default: 100 entries) |
| `SEQ_LOCK_HISTOGRAM_SIZE` | Size of sequence lock histogram for consumer statistics (default: 200 entries) |

### Queue Runtime Configuration

The queue size is configured at runtime when calling `QueueInit(buffer_size)`. The buffer size determines:

- Total memory allocated for the queue
- Number of queue entries that can be stored
- Maximum throughput capacity

**Memory Calculation**: Each queue entry requires `MAX_ENTRY_SIZE` bytes plus alignment overhead. The effective queue size is `buffer_size - MAX_ENTRY_SIZE`.

### Platform Requirements

The 64-bit queue implementation requires:

- 64-bit POSIX platform (Linux or macOS)
- Support for atomic operations
- C11 or later for `stdatomic.h`

On 32-bit platforms or Windows, the system automatically falls back to the 32-bit queue implementation (`xcpQueue32.c`).

### Performance Recommendations

1. **For High Throughput**: Use `QUEUE_MUTEX` if worst-case producer latency is acceptable
2. **For Low Latency**: Use `QUEUE_SEQ_LOCK` but expect higher CPU usage due to spinning
3. **For Balanced Performance**: Use `QUEUE_NO_LOCK` (default) for most applications
4. **Buffer Sizing**: Allocate sufficient buffer size to handle peak data rates with some headroom
5. **Testing**: Always benchmark different configurations with your specific workload patterns
