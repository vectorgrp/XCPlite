#pragma once

/*----------------------------------------------------------------------------
| File:
|   cmp_transport.h
|
| Description:
|   Outer transport for CMP messages, i.e. how a complete CMP message reaches the
|   Data Sink and back. Kept behind this seam so the envelope codec in cmp.c stays
|   independent of it.
|
|   The specification defines two transport options (6.4):
|     6.4.1  IEEE 802.3 Ethernet frames, EtherType 0x99FE - MANDATORY for a Capture Module
|     6.4.2  UDP, destination IP and port configurable       - OPTIONAL
|
|   Only the UDP option is implemented (cmp_transport_udp.c). It needs no AF_PACKET, no
|   CAP_NET_RAW and no root, and it is portable. The Ethernet option is the next step;
|   the AF_PACKET plumbing it needs is preserved in git commit 01e7f40
|   (examples/cmp_demo/src/socket_raw_hal_cmp.c), which used it for the pass through
|   version of this backend. It additionally needs an outer Ethernet header
|   (dst = sink MAC, src = our MAC, EtherType 0x99FE) and padding to the 60 byte
|   Ethernet minimum (6.4.1).
|
|   Note 6.4.2: "CMP messages shall not be sent over IP fragmented packets." The largest
|   CMP message the outer path can carry is therefore a hard limit, not a soft one - see
|   cmpTransportMaxMessage().
|
| Code released into public domain, no attribution required
 ----------------------------------------------------------------------------*/

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Opaque transport instance
typedef struct cmp_transport tCmpTransport;

typedef struct {
    uint16_t local_port; // UDP port to listen on for Transmit Data and Control Messages
    const char *sink_ip; // Data Sink IPv4 address, dotted quad. NULL or empty: learn it
                         // from the source of the first CMP message we receive.
    uint16_t sink_port;  // Data Sink UDP port
    uint16_t outer_mtu;  // MTU of the path to the Data Sink, for the size budget
} tCmpTransportConfig;

// Open the transport. Returns true on success, *transport is then valid until close.
bool cmpTransportOpen(const tCmpTransportConfig *config, tCmpTransport **transport);
void cmpTransportClose(tCmpTransport *transport);

// Send one complete CMP message to the Data Sink.
// Returns len on success, 0 if the sink address is not known yet (not an error: the demo
// simply has nowhere to send until the sink announces itself), or -1 on error.
int32_t cmpTransportSend(tCmpTransport *transport, const uint8_t *msg, uint16_t len);

// Receive one complete CMP message, blocking with timeout.
// Returns > 0 bytes received, 0 on timeout or wakeup, -1 on a fatal error.
int32_t cmpTransportRecv(tCmpTransport *transport, uint8_t *msg, uint16_t max_len, uint32_t timeout_ms);

// Abort a blocked cmpTransportRecv(). May be called from any thread.
void cmpTransportWakeup(tCmpTransport *transport);

// Largest CMP message this path can carry without IP fragmentation (6.4.2).
uint16_t cmpTransportMaxMessage(const tCmpTransport *transport);

// Endpoint information for the REST identification response (12.3.2) and logging.
// ip must be at least 16 bytes. Returns false if the endpoint is not known yet.
bool cmpTransportGetLocal(const tCmpTransport *transport, char *ip, size_t ip_size, uint16_t *port);
bool cmpTransportGetSink(const tCmpTransport *transport, char *ip, size_t ip_size, uint16_t *port);
