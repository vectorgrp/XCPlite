/*----------------------------------------------------------------------------
| File:
|   cmp.c
|
| Description:
|   ASAM CMP envelope codec. Pure: no sockets, no I/O, no global state, so it can be
|   unit tested against the sample frames shipped with the specification.
|   See cmp.h for the wire layout and the section references.
|
| Code released into public domain, no attribution required
 ----------------------------------------------------------------------------*/

#include "cmp.h"

#include <string.h>

//-------------------------------------------------------------------------------
// Big endian accessors
//
// All CMP header fields are big endian (6.2). Written out by hand rather than with
// htons/htonl: this file must stay free of platform headers so it can be linked into a
// standalone unit test, and on BSD/macOS the HTONS macros assign in place.

static uint8_t *put16(uint8_t *p, uint16_t v) {
    *p++ = (uint8_t)(v >> 8);
    *p++ = (uint8_t)v;
    return p;
}

static uint8_t *put32(uint8_t *p, uint32_t v) {
    *p++ = (uint8_t)(v >> 24);
    *p++ = (uint8_t)(v >> 16);
    *p++ = (uint8_t)(v >> 8);
    *p++ = (uint8_t)v;
    return p;
}

static uint8_t *put64(uint8_t *p, uint64_t v) {
    for (int i = 56; i >= 0; i -= 8) {
        *p++ = (uint8_t)(v >> i);
    }
    return p;
}

static uint16_t get16(const uint8_t *p) { return (uint16_t)(((uint16_t)p[0] << 8) | p[1]); }

static uint32_t get32(const uint8_t *p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }

//-------------------------------------------------------------------------------

void cmpCodecInit(tCmpCodec *codec, const tCmpConfig *config) {
    if (codec == NULL || config == NULL) {
        return;
    }
    memset(codec, 0, sizeof(*codec));
    codec->config = *config;
}

const char *cmpResultName(tCmpResult result) {
    switch (result) {
    case CMP_OK:
        return "ok";
    case CMP_DROP_TOO_SHORT:
        return "shorter than the CMP headers";
    case CMP_DROP_VERSION:
        return "unsupported CMP version (0x00 would be TECMP/PLP)";
    case CMP_DROP_MESSAGE_TYPE:
        return "not a Transmit Data Message";
    case CMP_DROP_PAYLOAD_TYPE:
        return "no Ethernet Data Message payload";
    case CMP_DROP_INTERFACE_ID:
        return "addressed to a different InterfaceId";
    case CMP_DROP_SEGMENTED:
        return "segmented, not supported";
    case CMP_DROP_MALFORMED:
        return "inconsistent length fields";
    case CMP_DROP_NO_FCS:
        return "Ethernet payload too short to hold the FCS";
    case CMP_DROP_TOO_LARGE:
        return "inner frame larger than the receive buffer";
    }
    return "unknown";
}

//-------------------------------------------------------------------------------
// Capture direction: inner Ethernet frame -> Captured Data Message

uint16_t cmpWrapCaptured(tCmpCodec *codec, const uint8_t *frame, uint16_t frame_len, uint64_t timestamp_ns, bool in_sync, uint8_t *out, uint16_t out_max) {

    if (codec == NULL || frame == NULL || out == NULL || frame_len == 0) {
        return 0;
    }

    // The Ethernet payload DATA runs from the destination MAC through the FCS (7.3.8), but
    // xcplib's HAL contract passes frames WITHOUT FCS, so four bytes are appended here.
    uint32_t data_length = (uint32_t)frame_len + CMP_FCS_LEN;
    uint32_t payload_length = CMP_ETH_PAYLOAD_HDR_LEN + data_length;
    uint32_t total = CMP_HDR_LEN + CMP_CAP_DATA_HDR_LEN + payload_length;
    if (payload_length > 0xFFFFu || total > (uint32_t)out_max) {
        return 0; // caller reports ETH_HAL_ERROR_SIZE
    }

    uint8_t *p = out;

    // CMP header (6.2.1)
    *p++ = CMP_VERSION;
    *p++ = 0; // reserved
    p = put16(p, codec->config.device_id);
    *p++ = CMP_MSG_CAP_DATA;
    *p++ = codec->config.stream_id;
    p = put16(p, codec->tx_seq);

    // Captured Data Message header (7.2.1)
    p = put64(p, timestamp_ns);
    p = put32(p, codec->config.interface_id);
    // DIR_ON_IF = 0: the emulated ECU sits behind the interface, so from the capture
    // module's point of view this frame was RECEIVED on the interface (Table 11 bit 4).
    // SEG = 00, unsegmented: one inner frame per CMP message, no aggregation (6.3.2/6.3.3).
    *p++ = (uint8_t)(in_sync ? CMP_CAP_FLAG_INSYNC : 0);
    *p++ = CMP_PAYLOAD_ETHERNET;
    p = put16(p, (uint16_t)payload_length);

    // Ethernet Data Message payload (7.3.8)
    // FCS_SUPPORT = 0: this capture module cannot compute a real FCS, so the four bytes
    // below are the zero value the specification prescribes for that case.
    p = put16(p, 0);
    p = put16(p, 0); // reserved
    p = put16(p, (uint16_t)data_length);
    memcpy(p, frame, frame_len);
    p += frame_len;
    memset(p, 0, CMP_FCS_LEN);
    p += CMP_FCS_LEN;

    codec->tx_seq++; // wraps 0xFFFF -> 0 naturally (6.2.1)
    codec->n_wrapped++;
    return (uint16_t)(p - out);
}

//-------------------------------------------------------------------------------
// Transmit direction: Transmit Data Message -> inner Ethernet frame

static uint16_t drop(tCmpCodec *codec, tCmpResult *result, tCmpResult reason) {
    codec->n_dropped++;
    if (result != NULL) {
        *result = reason;
    }
    return 0;
}

// StreamSequenceCounter monitoring (6.3.1): detects loss, duplication and reordering.
// A diagnostic only - a jump never causes a frame to be dropped.
static void checkPeerSeq(tCmpCodec *codec, uint16_t device_id, uint16_t seq) {
    if (codec->peer_seq_valid && codec->peer_device_id == device_id && (uint16_t)(codec->peer_seq + 1u) != seq) {
        codec->n_seq_jumps++;
    }
    codec->peer_device_id = device_id;
    codec->peer_seq = seq;
    codec->peer_seq_valid = true;
}

uint16_t cmpUnwrapTransmit(tCmpCodec *codec, const uint8_t *in, uint16_t in_len, uint8_t *out, uint16_t out_max, tCmpResult *result) {

    if (codec == NULL || in == NULL || out == NULL) {
        return 0;
    }
    if (in_len < CMP_HDR_LEN + CMP_TX_DATA_HDR_LEN + CMP_ETH_PAYLOAD_HDR_LEN) {
        return drop(codec, result, CMP_DROP_TOO_SHORT);
    }
    // Version 0x00 is TECMP/PLP, which is not compatible with CMP (5.2)
    if (in[0] < CMP_VERSION) {
        return drop(codec, result, CMP_DROP_VERSION);
    }
    if (in[4] != CMP_MSG_TX_DATA) {
        // Control and Status messages from the data sink land here too and are simply not
        // ours to deliver. Not counted as an error by the caller.
        return drop(codec, result, CMP_DROP_MESSAGE_TYPE);
    }

    checkPeerSeq(codec, get16(in + 2), get16(in + 6));

    // Walk the (Transmit Data Message header, payload) pairs. A conformant data sink sends
    // exactly one - we advertise AggregationCount = 1 over REST - but aggregation (6.3.2)
    // is legal, so extras are counted rather than silently ignored.
    uint16_t delivered = 0;
    tCmpResult reason = CMP_DROP_PAYLOAD_TYPE;
    uint32_t off = CMP_HDR_LEN;

    while (off + CMP_TX_DATA_HDR_LEN <= (uint32_t)in_len) {
        const uint8_t *h = in + off;
        uint32_t interface_id = get32(h + 12);
        uint8_t common_flags = h[20];
        uint8_t payload_type = h[21];
        uint16_t payload_length = get16(h + 22);
        uint32_t body = off + CMP_TX_DATA_HDR_LEN;

        // Payload type INVALID means padding: discard it and the rest of the message (7.2)
        if (payload_type == CMP_PAYLOAD_INVALID) {
            break;
        }
        if (body + payload_length > (uint32_t)in_len) {
            return drop(codec, result, CMP_DROP_MALFORMED);
        }

        if (delivered != 0) {
            codec->n_aggregated_ignored++;
        } else if ((common_flags & CMP_TX_FLAG_SEG_MASK) != 0) {
            reason = CMP_DROP_SEGMENTED;
        } else if (interface_id != codec->config.interface_id) {
            // Addressing first: a request for another interface is simply not ours, while an
            // unsupported payload type ON our interface means the data sink is misconfigured.
            reason = CMP_DROP_INTERFACE_ID;
        } else if (payload_type != CMP_PAYLOAD_ETHERNET) {
            reason = CMP_DROP_PAYLOAD_TYPE;
        } else if (payload_length < CMP_ETH_PAYLOAD_HDR_LEN) {
            reason = CMP_DROP_MALFORMED;
        } else {
            uint16_t data_length = get16(in + body + 4);
            if ((uint32_t)data_length + CMP_ETH_PAYLOAD_HDR_LEN > (uint32_t)payload_length) {
                reason = CMP_DROP_MALFORMED;
            } else if (data_length < CMP_FCS_LEN) {
                reason = CMP_DROP_NO_FCS;
            } else {
                // Strip the trailing FCS regardless of FCS_SENDING: with the flag clear the
                // four bytes are dummies to be ignored (7.3.8 Table 35 bit 8), and with it
                // set they are a real FCS - but xcplib's HAL contract wants neither.
                uint16_t frame_len = (uint16_t)(data_length - CMP_FCS_LEN);
                if (frame_len == 0) {
                    reason = CMP_DROP_MALFORMED;
                } else if (frame_len > out_max) {
                    reason = CMP_DROP_TOO_LARGE;
                } else {
                    memcpy(out, in + body + CMP_ETH_PAYLOAD_HDR_LEN, frame_len);
                    delivered = frame_len;
                }
            }
        }

        off = body + payload_length;
    }

    if (delivered == 0) {
        return drop(codec, result, reason);
    }
    codec->n_unwrapped++;
    if (result != NULL) {
        *result = CMP_OK;
    }
    return delivered;
}
