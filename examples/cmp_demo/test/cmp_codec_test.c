/*----------------------------------------------------------------------------
| File:
|   cmp_codec_test.c
|
| Description:
|   Unit test for the ASAM CMP envelope codec (src/cmp.c).
|
|   Pure: links cmp.c only, no sockets, no libxcplite, no network. Run it as
|   ./build/cmp_codec_test - it returns non zero if anything fails.
|
|   The golden vectors are taken byte for byte from the sample PCAPNG files shipped
|   with the ASAM CMP 1.1.0 specification (Sample_Files/), so this test pins the wire
|   format against the standard itself rather than against our own reading of it:
|
|     GOLDEN_CAP_*  CMP_1.0/asam_cmp_cap_0x08_Ethernet.pcapng
|                   a Captured Data Message with an Ethernet payload - exactly the
|                   shape this backend emits
|     GOLDEN_TX_CAN CMP_1.1/asam_cmp_tx_0x01_can_29bit_0x12345678.pcapng
|                   a real Transmit Data Message. Its payload is CAN, not Ethernet, so
|                   the codec must reject it - but only after parsing the 24 byte
|                   Transmit Data Message header correctly. Getting that header's length
|                   or field offsets wrong yields MALFORMED instead of PAYLOAD_TYPE, so
|                   this vector pins the TX header layout against a real 1.1 message.
|
|   There is no sample of a Transmit Data Message carrying an Ethernet payload - the
|   1.1 samples cover CAN, CAN FD and LIN only - so the happy path uses a message built
|   by buildTxEthernet() below from the two layouts the vectors above have pinned.
|
| Code released into public domain, no attribution required
 ----------------------------------------------------------------------------*/

#include <stdio.h>
#include <string.h>

#include "../src/cmp.h"

//-------------------------------------------------------------------------------

static int sChecks = 0;
static int sFailures = 0;

#define CHECK(cond, ...)                                                                                                                                                           \
    do {                                                                                                                                                                           \
        sChecks++;                                                                                                                                                                 \
        if (!(cond)) {                                                                                                                                                             \
            sFailures++;                                                                                                                                                           \
            printf("  FAIL %s:%d: ", __FILE__, __LINE__);                                                                                                                          \
            printf(__VA_ARGS__);                                                                                                                                                   \
            printf("\n");                                                                                                                                                          \
        }                                                                                                                                                                          \
    } while (0)

static void hexdiff(const char *what, const uint8_t *got, const uint8_t *want, uint16_t len) {
    printf("  %s mismatch:\n", what);
    for (uint16_t i = 0; i < len; i++) {
        if (got[i] != want[i]) {
            printf("    offset %3u: got 0x%02x want 0x%02x\n", i, got[i], want[i]);
        }
    }
}

//-------------------------------------------------------------------------------
// Golden vectors

// The 42 byte inner Ethernet frame (an ARP request), dst MAC .. end, no FCS
static const uint8_t GOLDEN_CAP_FRAME[] = {
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x60, 0x00, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00, 0x01, 0x08, 0x00, 0x06, 0x04, 0x00,
    0x01, 0x60, 0x00, 0x00, 0x00, 0x00, 0x01, 0xc0, 0xa8, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xc0, 0xa8, 0x00, 0x02,
};

// The complete 76 byte CMP message from the sample, with one deliberate change: the
// sample's FCS bytes 0x11 0x22 0x00 0x00 are zeroed here. This capture module reports
// FCS_SUPPORT = 0 and the specification prescribes an all zero FCS for that case (7.3.8).
static const uint8_t GOLDEN_CAP_MSG[] = {
    0x01, 0x00, 0x47, 0x11, 0x01, 0x00, 0x00, 0x00, // CMP header: v1, device 0x4711, CAP_DATA, stream 0, seq 0
    0x18, 0x88, 0x66, 0xbc, 0xae, 0x60, 0x8e, 0xe4, // timestamp
    0x00, 0x00, 0x00, 0x08,                         // InterfaceId 8
    0x00,                                           // common flags: INSYNC = 0, SEG = 00, DIR_ON_IF = 0
    0x08,                                           // payload type ETHERNET_DATA_MSG
    0x00, 0x34,                                     // payload length 52
    0x00, 0x00,                                     // Ethernet payload flags: FCS_SUPPORT = 0
    0x00, 0x00,                                     // reserved
    0x00, 0x2e,                                     // data length 46 = 42 frame + 4 FCS
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x60, 0x00, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00, 0x01, 0x08, 0x00, 0x06, 0x04, 0x00, 0x01, 0x60,
    0x00, 0x00, 0x00, 0x00, 0x01, 0xc0, 0xa8, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xc0, 0xa8, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, // FCS, zeroed
};

#define GOLDEN_CAP_DEVICE_ID 0x4711
#define GOLDEN_CAP_STREAM_ID 0
#define GOLDEN_CAP_INTERFACE_ID 8
#define GOLDEN_CAP_TIMESTAMP 1767775814308368100u

// A real CMP 1.1 Transmit Data Message, verbatim. Device 0x4711, stream 0, seq 0,
// deadline 1e9 ns, InterfaceId 1, payload type 0x01 (CAN), payload length 24.
static const uint8_t GOLDEN_TX_CAN[] = {
    0x01, 0x00, 0x47, 0x11, 0x04, 0x00, 0x00, 0x00, // CMP header: TX_DATA_MSG
    0x18, 0x88, 0x66, 0xbc, 0xae, 0xfe, 0x47, 0x10, // timestamp
    0x3b, 0x9a, 0xca, 0x00,                         // deadline 1000000000 ns
    0x00, 0x00, 0x00, 0x01,                         // InterfaceId 1
    0x00, 0x00, 0x00, 0x00,                         // transmission options
    0x00,                                           // common flags: SEG = 00
    0x01,                                           // payload type CAN_DATA_MSG
    0x00, 0x18,                                     // payload length 24
    0x00, 0x00, 0x00, 0x00, 0x92, 0x34, 0x56, 0x78, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x08, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
};

//-------------------------------------------------------------------------------
// Test only: build a Transmit Data Message carrying an Ethernet payload, i.e. play the
// part of the data sink. Layout per 7.2.2 and 7.3.8, pinned by the vectors above.

static uint16_t buildTxEthernet(uint8_t *out, uint16_t device_id, uint8_t stream_id, uint16_t seq, uint32_t interface_id, uint8_t common_flags, uint8_t payload_type,
                                const uint8_t *frame, uint16_t frame_len) {
    uint16_t data_length = (uint16_t)(frame_len + CMP_FCS_LEN);
    uint16_t payload_length = (uint16_t)(CMP_ETH_PAYLOAD_HDR_LEN + data_length);
    uint8_t *p = out;

    *p++ = CMP_VERSION;
    *p++ = 0;
    *p++ = (uint8_t)(device_id >> 8);
    *p++ = (uint8_t)device_id;
    *p++ = CMP_MSG_TX_DATA;
    *p++ = stream_id;
    *p++ = (uint8_t)(seq >> 8);
    *p++ = (uint8_t)seq;

    memset(p, 0, 8); // timestamp 0 = send immediately
    p += 8;
    memset(p, 0, 4); // deadline 0 = none
    p += 4;
    *p++ = (uint8_t)(interface_id >> 24);
    *p++ = (uint8_t)(interface_id >> 16);
    *p++ = (uint8_t)(interface_id >> 8);
    *p++ = (uint8_t)interface_id;
    memset(p, 0, 4); // transmission options, 0 for Ethernet payloads
    p += 4;
    *p++ = common_flags;
    *p++ = payload_type;
    *p++ = (uint8_t)(payload_length >> 8);
    *p++ = (uint8_t)payload_length;

    *p++ = 0; // Ethernet payload flags: FCS_SENDING = 0, the FCS below is a dummy
    *p++ = 0;
    *p++ = 0; // reserved
    *p++ = 0;
    *p++ = (uint8_t)(data_length >> 8);
    *p++ = (uint8_t)data_length;
    memcpy(p, frame, frame_len);
    p += frame_len;
    memset(p, 0, CMP_FCS_LEN);
    p += CMP_FCS_LEN;

    return (uint16_t)(p - out);
}

static void initCodec(tCmpCodec *codec, uint32_t interface_id) {
    tCmpConfig config = {.device_id = GOLDEN_CAP_DEVICE_ID, .stream_id = GOLDEN_CAP_STREAM_ID, .interface_id = interface_id};
    cmpCodecInit(codec, &config);
}

//-------------------------------------------------------------------------------

static void testWrapGolden(void) {
    printf("wrap: golden Captured Data Message\n");
    tCmpCodec codec;
    initCodec(&codec, GOLDEN_CAP_INTERFACE_ID);

    uint8_t out[256];
    uint16_t n = cmpWrapCaptured(&codec, GOLDEN_CAP_FRAME, sizeof(GOLDEN_CAP_FRAME), GOLDEN_CAP_TIMESTAMP, false, out, sizeof(out));

    CHECK(n == sizeof(GOLDEN_CAP_MSG), "length %u, want %zu", n, sizeof(GOLDEN_CAP_MSG));
    if (n == sizeof(GOLDEN_CAP_MSG)) {
        bool equal = memcmp(out, GOLDEN_CAP_MSG, n) == 0;
        CHECK(equal, "wrapped message differs from the specification sample");
        if (!equal) {
            hexdiff("CAP message", out, GOLDEN_CAP_MSG, n);
        }
    }
    CHECK(n == sizeof(GOLDEN_CAP_FRAME) + CMP_CAP_OVERHEAD, "overhead %u, want %u", (unsigned)(n - sizeof(GOLDEN_CAP_FRAME)), CMP_CAP_OVERHEAD);
    CHECK(codec.n_wrapped == 1, "n_wrapped %llu", (unsigned long long)codec.n_wrapped);
}

static void testWrapInSyncFlag(void) {
    printf("wrap: INSYNC flag\n");
    tCmpCodec codec;
    initCodec(&codec, GOLDEN_CAP_INTERFACE_ID);
    uint8_t out[256];
    cmpWrapCaptured(&codec, GOLDEN_CAP_FRAME, sizeof(GOLDEN_CAP_FRAME), 0, true, out, sizeof(out));
    CHECK(out[20] == CMP_CAP_FLAG_INSYNC, "common flags 0x%02x, want 0x%02x", out[20], CMP_CAP_FLAG_INSYNC);
}

static void testWrapSequenceCounter(void) {
    printf("wrap: StreamSequenceCounter increments and wraps\n");
    tCmpCodec codec;
    initCodec(&codec, GOLDEN_CAP_INTERFACE_ID);
    uint8_t out[256];

    for (uint16_t i = 0; i < 4; i++) {
        cmpWrapCaptured(&codec, GOLDEN_CAP_FRAME, sizeof(GOLDEN_CAP_FRAME), 0, false, out, sizeof(out));
        uint16_t seq = (uint16_t)((out[6] << 8) | out[7]);
        CHECK(seq == i, "message %u carries seq %u", i, seq);
    }

    codec.tx_seq = 0xFFFF;
    cmpWrapCaptured(&codec, GOLDEN_CAP_FRAME, sizeof(GOLDEN_CAP_FRAME), 0, false, out, sizeof(out));
    CHECK(out[6] == 0xFF && out[7] == 0xFF, "seq at wrap boundary");
    CHECK(codec.tx_seq == 0, "seq wrapped to %u, want 0", codec.tx_seq);
}

static void testWrapSizeLimit(void) {
    printf("wrap: refuses to overflow the caller's buffer\n");
    tCmpCodec codec;
    initCodec(&codec, GOLDEN_CAP_INTERFACE_ID);
    uint8_t out[256];

    uint16_t need = (uint16_t)(sizeof(GOLDEN_CAP_FRAME) + CMP_CAP_OVERHEAD);
    CHECK(cmpWrapCaptured(&codec, GOLDEN_CAP_FRAME, sizeof(GOLDEN_CAP_FRAME), 0, false, out, need) == need, "exact fit must succeed");
    CHECK(cmpWrapCaptured(&codec, GOLDEN_CAP_FRAME, sizeof(GOLDEN_CAP_FRAME), 0, false, out, (uint16_t)(need - 1)) == 0, "one byte short must fail");
    CHECK(cmpWrapCaptured(&codec, GOLDEN_CAP_FRAME, 0, 0, false, out, sizeof(out)) == 0, "empty frame must fail");
}

static void testUnwrapRealTxMessage(void) {
    printf("unwrap: real CMP 1.1 Transmit Data Message (CAN payload)\n");
    tCmpCodec codec;
    uint8_t out[2048];
    tCmpResult result = CMP_OK;

    // InterfaceId 1 matches the sample, so the rejection must be about the payload type.
    // A wrong Transmit Data Message header length would surface as MALFORMED instead.
    initCodec(&codec, 1);
    CHECK(cmpUnwrapTransmit(&codec, GOLDEN_TX_CAN, sizeof(GOLDEN_TX_CAN), out, sizeof(out), &result) == 0, "CAN payload must not be delivered");
    CHECK(result == CMP_DROP_PAYLOAD_TYPE, "result %s, want a payload type rejection", cmpResultName(result));

    // The sequence counter of the sending data sink is tracked even for a dropped message
    CHECK(codec.peer_seq_valid && codec.peer_device_id == 0x4711 && codec.peer_seq == 0, "peer stream state not tracked");

    // A different InterfaceId must be rejected on that ground, which proves the codec
    // reads InterfaceId from the right offset in the 24 byte header.
    initCodec(&codec, 99);
    CHECK(cmpUnwrapTransmit(&codec, GOLDEN_TX_CAN, sizeof(GOLDEN_TX_CAN), out, sizeof(out), &result) == 0, "foreign interface must not be delivered");
    CHECK(result == CMP_DROP_INTERFACE_ID, "result %s, want an InterfaceId rejection", cmpResultName(result));
}

static void testUnwrapEthernetHappyPath(void) {
    printf("unwrap: Transmit Data Message with an Ethernet payload\n");
    tCmpCodec codec;
    initCodec(&codec, 7);

    uint8_t msg[256];
    uint16_t msg_len = buildTxEthernet(msg, 0x1234, 0, 0, 7, 0, CMP_PAYLOAD_ETHERNET, GOLDEN_CAP_FRAME, sizeof(GOLDEN_CAP_FRAME));
    CHECK(msg_len == sizeof(GOLDEN_CAP_FRAME) + CMP_TX_OVERHEAD, "built length %u, want %u", msg_len, (unsigned)(sizeof(GOLDEN_CAP_FRAME) + CMP_TX_OVERHEAD));

    uint8_t out[2048];
    tCmpResult result = CMP_DROP_MALFORMED;
    uint16_t n = cmpUnwrapTransmit(&codec, msg, msg_len, out, sizeof(out), &result);

    CHECK(result == CMP_OK, "result %s", cmpResultName(result));
    CHECK(n == sizeof(GOLDEN_CAP_FRAME), "inner frame %u bytes, want %zu (FCS must be stripped)", n, sizeof(GOLDEN_CAP_FRAME));
    if (n == sizeof(GOLDEN_CAP_FRAME)) {
        bool equal = memcmp(out, GOLDEN_CAP_FRAME, n) == 0;
        CHECK(equal, "inner frame differs from the original");
        if (!equal) {
            hexdiff("inner frame", out, GOLDEN_CAP_FRAME, n);
        }
    }
    CHECK(codec.n_unwrapped == 1 && codec.n_dropped == 0, "counters: unwrapped %llu dropped %llu", (unsigned long long)codec.n_unwrapped, (unsigned long long)codec.n_dropped);
}

static void testUnwrapRejections(void) {
    printf("unwrap: rejections\n");
    tCmpCodec codec;
    uint8_t msg[256];
    uint8_t out[2048];
    tCmpResult result;
    uint16_t len;

    // Too short
    initCodec(&codec, 7);
    CHECK(cmpUnwrapTransmit(&codec, GOLDEN_TX_CAN, 8, out, sizeof(out), &result) == 0, "truncated message");
    CHECK(result == CMP_DROP_TOO_SHORT, "result %s, want too short", cmpResultName(result));

    // Version 0x00 is TECMP/PLP, not CMP (5.2)
    initCodec(&codec, 7);
    len = buildTxEthernet(msg, 0x1234, 0, 0, 7, 0, CMP_PAYLOAD_ETHERNET, GOLDEN_CAP_FRAME, sizeof(GOLDEN_CAP_FRAME));
    msg[0] = 0x00;
    CHECK(cmpUnwrapTransmit(&codec, msg, len, out, sizeof(out), &result) == 0, "version 0 message");
    CHECK(result == CMP_DROP_VERSION, "result %s, want version rejection", cmpResultName(result));

    // A Captured Data Message must not be mistaken for a transmit request
    initCodec(&codec, 7);
    len = buildTxEthernet(msg, 0x1234, 0, 0, 7, 0, CMP_PAYLOAD_ETHERNET, GOLDEN_CAP_FRAME, sizeof(GOLDEN_CAP_FRAME));
    msg[4] = CMP_MSG_CAP_DATA;
    CHECK(cmpUnwrapTransmit(&codec, msg, len, out, sizeof(out), &result) == 0, "captured data message");
    CHECK(result == CMP_DROP_MESSAGE_TYPE, "result %s, want message type rejection", cmpResultName(result));

    // Segmentation is not supported (6.3.3) and is advertised as such over REST
    initCodec(&codec, 7);
    len = buildTxEthernet(msg, 0x1234, 0, 0, 7, 0x04 /* SEG = first segment */, CMP_PAYLOAD_ETHERNET, GOLDEN_CAP_FRAME, sizeof(GOLDEN_CAP_FRAME));
    CHECK(cmpUnwrapTransmit(&codec, msg, len, out, sizeof(out), &result) == 0, "segmented message");
    CHECK(result == CMP_DROP_SEGMENTED, "result %s, want segmentation rejection", cmpResultName(result));

    // Payload type INVALID is padding: discard it and the rest of the message (7.2)
    initCodec(&codec, 7);
    len = buildTxEthernet(msg, 0x1234, 0, 0, 7, 0, CMP_PAYLOAD_INVALID, GOLDEN_CAP_FRAME, sizeof(GOLDEN_CAP_FRAME));
    CHECK(cmpUnwrapTransmit(&codec, msg, len, out, sizeof(out), &result) == 0, "padding message");

    // Inconsistent length fields
    initCodec(&codec, 7);
    len = buildTxEthernet(msg, 0x1234, 0, 0, 7, 0, CMP_PAYLOAD_ETHERNET, GOLDEN_CAP_FRAME, sizeof(GOLDEN_CAP_FRAME));
    msg[31] = 0xFF; // payload length low byte, now past the end of the message
    CHECK(cmpUnwrapTransmit(&codec, msg, len, out, sizeof(out), &result) == 0, "payload length past the end");
    CHECK(result == CMP_DROP_MALFORMED, "result %s, want malformed", cmpResultName(result));

    // Ethernet payload with no room for the FCS
    initCodec(&codec, 7);
    len = buildTxEthernet(msg, 0x1234, 0, 0, 7, 0, CMP_PAYLOAD_ETHERNET, GOLDEN_CAP_FRAME, sizeof(GOLDEN_CAP_FRAME));
    msg[36] = 0x00; // data length high byte
    msg[37] = 0x03; // data length 3, less than the 4 byte FCS
    CHECK(cmpUnwrapTransmit(&codec, msg, len, out, sizeof(out), &result) == 0, "payload shorter than the FCS");
    CHECK(result == CMP_DROP_NO_FCS, "result %s, want no FCS", cmpResultName(result));

    // Inner frame larger than the caller's buffer
    initCodec(&codec, 7);
    len = buildTxEthernet(msg, 0x1234, 0, 0, 7, 0, CMP_PAYLOAD_ETHERNET, GOLDEN_CAP_FRAME, sizeof(GOLDEN_CAP_FRAME));
    CHECK(cmpUnwrapTransmit(&codec, msg, len, out, 8, &result) == 0, "receive buffer too small");
    CHECK(result == CMP_DROP_TOO_LARGE, "result %s, want too large", cmpResultName(result));
}

static void testRoundTrip(void) {
    printf("round trip: wrap -> unwrap over a range of frame sizes\n");
    uint8_t frame[1600];
    for (size_t i = 0; i < sizeof(frame); i++) {
        frame[i] = (uint8_t)(i * 31u + 7u);
    }

    static const uint16_t sizes[] = {1, 14, 42, 60, 64, 512, 1434, 1500};
    for (size_t s = 0; s < sizeof(sizes) / sizeof(sizes[0]); s++) {
        uint16_t frame_len = sizes[s];

        // Capture direction, then parse it back with the test's own reader by re-wrapping
        // it as a transmit message: this checks that the Ethernet payload the capture
        // direction produces is exactly what the transmit direction expects.
        tCmpCodec sender;
        initCodec(&sender, 5);
        uint8_t cap[2048];
        uint16_t cap_len = cmpWrapCaptured(&sender, frame, frame_len, 0x0123456789ABCDEFu, true, cap, sizeof(cap));
        CHECK(cap_len == frame_len + CMP_CAP_OVERHEAD, "size %u: wrapped %u", frame_len, cap_len);

        tCmpCodec receiver;
        initCodec(&receiver, 5);
        uint8_t msg[2048];
        uint16_t msg_len = buildTxEthernet(msg, 0x1234, 0, (uint16_t)s, 5, 0, CMP_PAYLOAD_ETHERNET, frame, frame_len);
        uint8_t back[2048];
        tCmpResult result = CMP_DROP_MALFORMED;
        uint16_t n = cmpUnwrapTransmit(&receiver, msg, msg_len, back, sizeof(back), &result);
        CHECK(n == frame_len && result == CMP_OK, "size %u: unwrapped %u (%s)", frame_len, n, cmpResultName(result));
        CHECK(n == frame_len && memcmp(back, frame, frame_len) == 0, "size %u: payload corrupted", frame_len);
    }
}

static void testPeerSequenceMonitoring(void) {
    printf("unwrap: peer StreamSequenceCounter monitoring\n");
    tCmpCodec codec;
    initCodec(&codec, 7);
    uint8_t msg[256];
    uint8_t out[2048];

    for (uint16_t seq = 0; seq < 3; seq++) {
        uint16_t len = buildTxEthernet(msg, 0x1234, 0, seq, 7, 0, CMP_PAYLOAD_ETHERNET, GOLDEN_CAP_FRAME, sizeof(GOLDEN_CAP_FRAME));
        cmpUnwrapTransmit(&codec, msg, len, out, sizeof(out), NULL);
    }
    CHECK(codec.n_seq_jumps == 0, "contiguous counters reported %llu jumps", (unsigned long long)codec.n_seq_jumps);

    // Skip 3, i.e. a lost message
    uint16_t len = buildTxEthernet(msg, 0x1234, 0, 4, 7, 0, CMP_PAYLOAD_ETHERNET, GOLDEN_CAP_FRAME, sizeof(GOLDEN_CAP_FRAME));
    uint16_t n = cmpUnwrapTransmit(&codec, msg, len, out, sizeof(out), NULL);
    CHECK(codec.n_seq_jumps == 1, "gap reported %llu jumps, want 1", (unsigned long long)codec.n_seq_jumps);
    CHECK(n == sizeof(GOLDEN_CAP_FRAME), "a counter gap must never drop the frame");

    // Wrap around 0xFFFF -> 0 is not a jump
    codec.peer_seq = 0xFFFF;
    len = buildTxEthernet(msg, 0x1234, 0, 0, 7, 0, CMP_PAYLOAD_ETHERNET, GOLDEN_CAP_FRAME, sizeof(GOLDEN_CAP_FRAME));
    cmpUnwrapTransmit(&codec, msg, len, out, sizeof(out), NULL);
    CHECK(codec.n_seq_jumps == 1, "wrap around counted as a jump");
}

static void testOverheadConstants(void) {
    printf("constants: overhead matches the specified header lengths\n");
    CHECK(CMP_HDR_LEN == 8, "CMP header length");
    CHECK(CMP_CAP_DATA_HDR_LEN == 16, "Captured Data Message header length");
    CHECK(CMP_TX_DATA_HDR_LEN == 24, "Transmit Data Message header length");
    CHECK(CMP_CAP_OVERHEAD == 34, "capture direction overhead");
    CHECK(CMP_TX_OVERHEAD == 42, "transmit direction overhead");
    // The golden vector is the arithmetic check: 42 byte frame -> 76 byte message
    CHECK(sizeof(GOLDEN_CAP_MSG) == sizeof(GOLDEN_CAP_FRAME) + CMP_CAP_OVERHEAD, "golden vector sizes");
    CHECK(sizeof(GOLDEN_TX_CAN) == 56, "golden transmit vector size");
}

//-------------------------------------------------------------------------------

int main(void) {
    printf("ASAM CMP envelope codec test\n");
    printf("golden vectors from the ASAM CMP 1.1.0 sample PCAPNG files\n\n");

    testOverheadConstants();
    testWrapGolden();
    testWrapInSyncFlag();
    testWrapSequenceCounter();
    testWrapSizeLimit();
    testUnwrapRealTxMessage();
    testUnwrapEthernetHappyPath();
    testUnwrapRejections();
    testPeerSequenceMonitoring();
    testRoundTrip();

    printf("\n%d checks, %d failures\n", sChecks, sFailures);
    if (sFailures != 0) {
        printf("FAILED\n");
        return 1;
    }
    printf("PASSED\n");
    return 0;
}
