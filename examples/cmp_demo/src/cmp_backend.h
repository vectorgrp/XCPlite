#pragma once

/*----------------------------------------------------------------------------
| File:
|   cmp_backend.h
|
| Description:
|   Configuration and status of the CMP Ethernet HAL backend (socket_raw_hal_cmp.c).
|
|   main.c configures the backend before XcpEthServerInit(), because that is what opens
|   the transport. cmp_rest.c reads the status to answer the REST queries a Data Sink
|   uses to find us and to discover that we support transmission.
|
| Code released into public domain, no attribution required
 ----------------------------------------------------------------------------*/

#include <stdbool.h>
#include <stdint.h>

// Largest CMP message the backend buffers. 6.4 allows jumbo frames up to a 9000 byte
// Ethernet MTU for both transport options; this is that plus a little slack.
#define CMP_MAX_MESSAGE 9216
#define CMP_MAX_OUTER_MTU 9000

typedef struct {
    // Capture module identity, see cmp.h
    uint16_t device_id;
    uint8_t stream_id;
    uint32_t interface_id;

    // Outer transport, see cmp_transport.h
    uint16_t local_port; // UDP port we listen on
    const char *sink_ip; // Data Sink address, NULL or empty to learn it. Not copied.
    uint16_t sink_port;
    uint16_t outer_mtu; // MTU of the path to the Data Sink

    // MAC address of the emulated ECU, i.e. the source MAC of the frames xcplib builds.
    // All zero: derive a locally administered one from device_id.
    uint8_t ecu_mac[6];
} tCmpBackendConfig;

// Must be called before XcpEthServerInit(). config is copied except for sink_ip.
void cmpBackendConfigure(const tCmpBackendConfig *config);

typedef struct {
    bool open;       // the transport is open
    bool sink_known; // the Data Sink address is configured or has been learned

    char local_ip[16]; // empty while unknown
    uint16_t local_port;
    char sink_ip[16];
    uint16_t sink_port;

    uint16_t max_message;     // largest CMP message the path carries un-fragmented
    uint16_t max_inner_frame; // largest inner Ethernet frame that fits inside it
    bool mtu_warning;         // xcplib can produce frames larger than max_inner_frame

    uint8_t ecu_mac[6];
    uint16_t device_id;
    uint8_t stream_id;
    uint32_t interface_id;

    uint64_t n_wrapped;            // captured frames sent to the Data Sink
    uint64_t n_unwrapped;          // transmit requests delivered to xcplib
    uint64_t n_dropped;            // CMP messages received but not for us
    uint64_t n_seq_jumps;          // gaps in the Data Sink's StreamSequenceCounter
    uint64_t n_aggregated_ignored; // extra data messages in an aggregated request
    uint64_t n_oversize;           // frames refused because they exceed max_inner_frame
} tCmpBackendStatus;

// Snapshot the backend status. Returns false before the transport is open.
// Counters are read without synchronisation: this is a test bench, and a torn count in a
// status page is not worth a lock on the transmit path.
bool cmpBackendGetStatus(tCmpBackendStatus *status);
