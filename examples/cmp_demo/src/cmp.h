#pragma once

/*----------------------------------------------------------------------------
| File:
|   cmp.h
|
| Description:
|   ASAM CMP (Capture Module Protocol) envelope codec for the cmp_demo backend.
|
|   This file and cmp.c are the ONLY place in the demo that know about CMP, and they
|   are pure: no sockets, no I/O, no global state. socket_raw_hal_cmp.c owns the
|   transport and calls into here. xcplib itself knows nothing about CMP - it builds
|   and parses plain Ethernet/IPv4/UDP frames and hands them to the HAL.
|   See docs/SOCKET_RAW.md and the README.
|
|   The demo emulates a Capture Module which tunnels one XCP ECU:
|     ECU -> tool   the frame xcplib hands to eth_hal_send becomes a Captured Data
|                   Message (CAP_DATA_MSG, 0x01)
|     tool -> ECU   a Transmit Data Message (TX_DATA_MSG, 0x04, new in CMP 1.1) is
|                   unwrapped and its inner frame handed to eth_hal_recv
|
|   Reference: ASAM CMP Protocol Layer Specification V1.1.0. Section numbers below
|   refer to it. All CMP header fields are BIG endian (6.2); the payload keeps the
|   original byte order of the captured data.
|
| Code released into public domain, no attribution required
 ----------------------------------------------------------------------------*/

#include <stdbool.h>
#include <stdint.h>

// EtherType assigned to ASAM CMP (6.4.1).
// Shared with TECMP/PLP, which are distinguished by a first byte of 0x00 (5.2).
#define CMP_ETHERTYPE 0x99FE

// CMP major version, first byte of every message. Must be >= 1 (5.2).
#define CMP_VERSION 0x01

// CMP_MESSAGE_TYPE (6.2.1, Table 6)
#define CMP_MSG_CAP_DATA 0x01 // captured data, capture module -> data sink
#define CMP_MSG_CTRL 0x02     // control
#define CMP_MSG_STATUS 0x03   // status, capture module -> data sink
#define CMP_MSG_TX_DATA 0x04  // transmit data, data sink -> capture module (CMP 1.1)
#define CMP_MSG_VENDOR 0xFF   // vendor defined

// DATA_MESSAGE_PAYLOAD_TYPE (7.2, Table 9), the subset this demo uses
#define CMP_PAYLOAD_INVALID 0x00      // padding: discard this and the rest of the message
#define CMP_PAYLOAD_ETHERNET 0x08     // Ethernet frame, dst MAC .. FCS
#define CMP_PAYLOAD_RAW_ETHERNET 0x0D // as 0x08 but with preamble and SFD, not used here

// Header lengths, fixed by the specification
#define CMP_HDR_LEN 8             // CMP header (6.2.1)
#define CMP_CAP_DATA_HDR_LEN 16   // Captured Data Message header (7.2.1)
#define CMP_TX_DATA_HDR_LEN 24    // Transmit Data Message header (7.2.2)
#define CMP_ETH_PAYLOAD_HDR_LEN 6 // Ethernet payload flags/reserved/data_length (7.3.8)
#define CMP_FCS_LEN 4             // FCS, part of the Ethernet payload DATA

// Bytes the envelope adds to one inner Ethernet frame, per direction.
// The HAL needs these to size its buffers and to compute the MTU budget: the CMP message
// must fit into one un-fragmented outer packet (6.4.2 forbids IP fragmentation).
#define CMP_CAP_OVERHEAD (CMP_HDR_LEN + CMP_CAP_DATA_HDR_LEN + CMP_ETH_PAYLOAD_HDR_LEN + CMP_FCS_LEN) // 34
#define CMP_TX_OVERHEAD (CMP_HDR_LEN + CMP_TX_DATA_HDR_LEN + CMP_ETH_PAYLOAD_HDR_LEN + CMP_FCS_LEN)   // 42

// Captured Data Message header common flags (7.2.1, Table 11)
#define CMP_CAP_FLAG_RECALC 0x01    // timestamp was recalculated before transmission
#define CMP_CAP_FLAG_INSYNC 0x02    // synchronized to the time provider
#define CMP_CAP_FLAG_SEG_MASK 0x0C  // 00 unsegmented, 01 first, 10 intermediary, 11 last
#define CMP_CAP_FLAG_DIR_ON_IF 0x10 // 0 received on interface, 1 sent on interface
#define CMP_CAP_FLAG_OVERFLOW 0x20  // one or more messages were lost while capturing
#define CMP_CAP_FLAG_ERROR 0x40     // error detected in the captured message

// Transmit Data Message header common flags (7.2.2, Table 13)
#define CMP_TX_FLAG_RELATIVE 0x01 // timestamp is a minimum distance to the previous frame
#define CMP_TX_FLAG_SEG_MASK 0x0C

// Ethernet Data Message payload flags (7.3.8, Table 35)
#define CMP_ETH_FLAG_FCS_ERR 0x0001
#define CMP_ETH_FLAG_FRAME_TOO_SHORT_ERR 0x0002
#define CMP_ETH_FLAG_TX_PORT_DOWN 0x0004
#define CMP_ETH_FLAG_COLLISION 0x0008
#define CMP_ETH_FLAG_FRAME_TOO_LONG_ERR 0x0010
#define CMP_ETH_FLAG_PHY_ERR 0x0020
#define CMP_ETH_FLAG_FRAME_TRUNCATED 0x0040
#define CMP_ETH_FLAG_FCS_SUPPORT 0x0080 // 1: the CM is able to fill in the FCS value
#define CMP_ETH_FLAG_FCS_SENDING 0x0100 // TX: 1: DATA carries a real FCS, 0: dummy, ignore

// Configuration of the emulated capture module
typedef struct {
    uint16_t device_id;    // identifies this capture module, unique in the network (6.2.1)
    uint8_t stream_id;     // our outgoing stream (6.3.1)
    uint32_t interface_id; // the emulated capture interface (7.2.1)
} tCmpConfig;

// Why a received message was not delivered. Diagnostics only - a drop is never fatal.
typedef enum {
    CMP_OK = 0,
    CMP_DROP_TOO_SHORT,    // shorter than the mandatory headers
    CMP_DROP_VERSION,      // version 0 (that is TECMP/PLP) or unknown
    CMP_DROP_MESSAGE_TYPE, // not a TX_DATA_MSG
    CMP_DROP_PAYLOAD_TYPE, // not an Ethernet payload
    CMP_DROP_INTERFACE_ID, // addressed to a different interface
    CMP_DROP_SEGMENTED,    // segmentation not supported, see 6.3.3
    CMP_DROP_MALFORMED,    // length fields inconsistent
    CMP_DROP_NO_FCS,       // Ethernet payload too short to hold the 4 byte FCS
    CMP_DROP_TOO_LARGE,    // inner frame does not fit the caller's buffer
} tCmpResult;

// Codec state. One instance per link; not thread safe by itself, but the HAL contract
// serializes send (transmit mutex held) and receive (XCP receive thread only) separately,
// and the two directions touch disjoint fields.
typedef struct {
    tCmpConfig config;

    uint16_t tx_seq; // our StreamSequenceCounter, wraps 0xFFFF -> 0 (6.2.1)

    // Sequence counter monitoring of the peer, purely a diagnostic (6.3.1)
    bool peer_seq_valid;
    uint16_t peer_device_id;
    uint16_t peer_seq;

    // Counters, for the REST interface and the shutdown summary
    uint64_t n_wrapped;
    uint64_t n_unwrapped;
    uint64_t n_dropped;
    uint64_t n_seq_jumps;
    uint64_t n_aggregated_ignored; // extra data messages in an aggregated TX message
} tCmpCodec;

// Initialize the codec. config must not be NULL.
void cmpCodecInit(tCmpCodec *codec, const tCmpConfig *config);

// Wrap one captured inner Ethernet frame as a Captured Data Message (CAP_DATA_MSG).
//
// frame/frame_len: complete Ethernet frame WITHOUT FCS, as delivered by xcplib's HAL
//                  contract (socket_raw_hal.h). A dummy zero FCS is appended here,
//                  because the Ethernet payload DATA runs from dst MAC through FCS (7.3.8).
// timestamp_ns:    capture timestamp, nanoseconds
// in_sync:         sets INSYNC; false when not synchronized to a time provider
// out/out_max:     caller owned buffer for the complete CMP message
//
// Returns the CMP message length, or 0 if it does not fit into out_max.
//
// The envelope is applied into the CALLER's buffer, never into the transmit queue
// headroom: CMP must not participate in the zero copy path and must not influence
// XCPTL_TX_HEADROOM. The extra copy is accepted, this is a test bench path.
uint16_t cmpWrapCaptured(tCmpCodec *codec, const uint8_t *frame, uint16_t frame_len, uint64_t timestamp_ns, bool in_sync, uint8_t *out, uint16_t out_max);

// Unwrap a received CMP message and extract the inner Ethernet frame of a Transmit Data
// Message (TX_DATA_MSG) addressed to our interface. The trailing FCS is stripped, so the
// result satisfies xcplib's "frames WITHOUT FCS" contract.
//
// Returns the inner frame length, or 0 if nothing was delivered; *result then says why.
// An aggregated message (6.3.2) delivers its first Ethernet payload and counts the rest
// in n_aggregated_ignored - the demo advertises AggregationCount = 1 over REST, so a
// conformant data sink does not aggregate.
uint16_t cmpUnwrapTransmit(tCmpCodec *codec, const uint8_t *in, uint16_t in_len, uint8_t *out, uint16_t out_max, tCmpResult *result);

// Human readable form of a tCmpResult, for logging
const char *cmpResultName(tCmpResult result);
