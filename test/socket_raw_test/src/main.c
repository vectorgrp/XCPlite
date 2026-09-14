// socket_raw_test - unit tests for the raw Ethernet transport (OPTION_ENABLE_UDP_RAW)
//
// Covers the parts of src/socket_raw.c that are pure logic and can be tested without a
// network: IPv4/ICMP checksums, wire struct packing, the Ethernet/IPv4/UDP frame build,
// the receive filter, and the ARP and ICMP Echo responders.
//
// socket_raw.c is included directly so the test can reach its static helpers and drive
// the socket context without a HAL. src/stubs.c provides a fake Ethernet HAL which
// captures the transmitted frame instead of sending it.

#include <stddef.h> // for offsetof
#include <stdio.h>
#include <string.h>

#include "socket_raw.c"

static int fails = 0;

#define CHECK(what, cond)                                                                                                                                                          \
    do {                                                                                                                                                                           \
        printf("%-52s %s\n", (what), (cond) ? "OK" : "FAIL");                                                                                                                      \
        if (!(cond))                                                                                                                                                               \
            fails++;                                                                                                                                                               \
    } while (0)

static void expect16(const char *what, uint16_t got, uint16_t want) {
    printf("%-52s got=0x%04X want=0x%04X  %s\n", what, got, want, got == want ? "OK" : "FAIL");
    if (got != want)
        fails++;
}

// Buffers are sized from the configuration, not hardcoded: OPTION_MTU may select jumbo frames
#define TEST_BUF_SIZE (RAW_MAX_FRAME + 64)

// Fake HAL capture, filled by eth_hal_send() in src/stubs.c
uint8_t gTxFrame[TEST_BUF_SIZE];
uint16_t gTxLen;
int gTxCount;

//-----------------------------------------------------------------------------------------------------
// Test fixture

static const uint8_t LOCAL_MAC[6] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};
static const uint8_t PEER_MAC[6] = {0x02, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE};
static const uint8_t LOCAL_IP[4] = {192, 168, 90, 2};
static const uint8_t PEER_IP[4] = {192, 168, 90, 1};
static const uint8_t BCAST_MAC[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
#define LOCAL_PORT 5555
#define PEER_PORT 50000

static void setupSocket(void) {
    memset(&sSocketRaw, 0, sizeof(sSocketRaw));
    memcpy(sSocketRaw.local_mac, LOCAL_MAC, 6);
    memcpy(sSocketRaw.local_ip, LOCAL_IP, 4);
    sSocketRaw.local_port = LOCAL_PORT;
    sSocketRaw.is_open = true;
    sSocketRaw.is_bound = true;
    gTxCount = 0;
    gTxLen = 0;
}

// Build an incoming UDP frame
static uint16_t buildRxUdp(uint8_t *f, const uint8_t *dst_mac, const uint8_t *dst_ip, uint16_t dst_port, const uint8_t *payload, uint16_t plen, uint16_t frag) {
    tEthHdr *eth = (tEthHdr *)f;
    memcpy(eth->dst, dst_mac, 6);
    memcpy(eth->src, PEER_MAC, 6);
    eth->ethertype = BE16(ETHERTYPE_IPV4);
    tIp4Hdr *ip = (tIp4Hdr *)(f + ETH_HDR_LEN);
    memset(ip, 0, IP4_HDR_LEN);
    ip->ver_ihl = 0x45;
    ip->total_length = BE16((uint16_t)(IP4_HDR_LEN + UDP_HDR_LEN + plen));
    ip->flags_frag = BE16(frag);
    ip->ttl = 64;
    ip->protocol = IP_PROTO_UDP;
    memcpy(ip->src, PEER_IP, 4);
    memcpy(ip->dst, dst_ip, 4);
    ip->checksum = BE16(ipHeaderChecksum(ip));
    tUdpHdr *udp = (tUdpHdr *)(f + ETH_HDR_LEN + IP4_HDR_LEN);
    udp->src_port = BE16(PEER_PORT);
    udp->dst_port = BE16(dst_port);
    udp->length = BE16((uint16_t)(UDP_HDR_LEN + plen));
    udp->checksum = 0;
    memcpy(f + RAW_HDR_LEN, payload, plen);
    return (uint16_t)(RAW_HDR_LEN + plen);
}

static uint16_t buildArpRequest(uint8_t *f, const uint8_t *target_ip, uint16_t oper) {
    tEthHdr *e = (tEthHdr *)f;
    memcpy(e->dst, BCAST_MAC, 6);
    memcpy(e->src, PEER_MAC, 6);
    e->ethertype = BE16(ETHERTYPE_ARP);
    tArpHdr *a = (tArpHdr *)(f + ETH_HDR_LEN);
    a->htype = BE16(1);
    a->ptype = BE16(ETHERTYPE_IPV4);
    a->hlen = 6;
    a->plen = 4;
    a->oper = BE16(oper);
    memcpy(a->sha, PEER_MAC, 6);
    memcpy(a->spa, PEER_IP, 4);
    memset(a->tha, 0, 6);
    memcpy(a->tpa, target_ip, 4);
    return ETH_HDR_LEN + ARP_LEN;
}

static uint16_t buildIcmpEcho(uint8_t *f, uint16_t data_len) {
    tEthHdr *e = (tEthHdr *)f;
    memcpy(e->dst, LOCAL_MAC, 6);
    memcpy(e->src, PEER_MAC, 6);
    e->ethertype = BE16(ETHERTYPE_IPV4);
    uint16_t icmp_len = (uint16_t)(sizeof(tIcmpHdr) + 4 + data_len); // header + id/seq + data
    tIp4Hdr *ip = (tIp4Hdr *)(f + ETH_HDR_LEN);
    memset(ip, 0, IP4_HDR_LEN);
    ip->ver_ihl = 0x45;
    ip->total_length = BE16((uint16_t)(IP4_HDR_LEN + icmp_len));
    ip->flags_frag = BE16(0x4000);
    ip->ttl = 64;
    ip->protocol = IP_PROTO_ICMP;
    memcpy(ip->src, PEER_IP, 4);
    memcpy(ip->dst, LOCAL_IP, 4);
    ip->checksum = BE16(ipHeaderChecksum(ip));
    uint8_t *icmp = f + ETH_HDR_LEN + IP4_HDR_LEN;
    memset(icmp, 0, icmp_len);
    icmp[0] = ICMP_TYPE_ECHO_REQUEST;
    icmp[4] = 0x12;
    icmp[5] = 0x34;
    icmp[7] = 0x01; // id, seq
    for (uint16_t i = 0; i < data_len; i++)
        icmp[8 + i] = (uint8_t)(i & 0xFF);
    ((tIcmpHdr *)icmp)->checksum = BE16(checksum16(icmp, icmp_len, 0));
    return (uint16_t)(ETH_HDR_LEN + IP4_HDR_LEN + icmp_len);
}

//-----------------------------------------------------------------------------------------------------
// Checksums and wire layout

static void test_checksums(void) {

    // Reference IPv4 header (RFC 1071 worked example), checksum field zeroed:
    // 4500 0073 0000 4000 4011 0000 c0a8 0001 c0a8 00c7  ->  0xb861
    uint8_t hdr[20] = {0x45, 0x00, 0x00, 0x73, 0x00, 0x00, 0x40, 0x00, 0x40, 0x11, 0x00, 0x00, 0xc0, 0xa8, 0x00, 0x01, 0xc0, 0xa8, 0x00, 0xc7};
    expect16("IPv4 header checksum (reference vector)", checksum16(hdr, 20, 0), 0xb861);

    uint16_t c = checksum16(hdr, 20, 0);
    hdr[10] = (uint8_t)(c >> 8);
    hdr[11] = (uint8_t)(c & 0xFF);
    expect16("verify: sum over header incl. checksum", checksum16(hdr, 20, 0), 0x0000);

    // Same header via the struct path used by socketSendTo: checks BE16 and packing
    tIp4Hdr ip;
    memset(&ip, 0, sizeof(ip));
    ip.ver_ihl = 0x45;
    ip.total_length = BE16(0x0073);
    ip.flags_frag = BE16(0x4000);
    ip.ttl = 0x40;
    ip.protocol = 17;
    ip.src[0] = 192;
    ip.src[1] = 168;
    ip.src[2] = 0;
    ip.src[3] = 1;
    ip.dst[0] = 192;
    ip.dst[1] = 168;
    ip.dst[2] = 0;
    ip.dst[3] = 199;
    expect16("struct path: same header via tIp4Hdr", ipHeaderChecksum(&ip), 0xb861);
    ip.checksum = BE16(ipHeaderChecksum(&ip));
    expect16("struct path: verify sums to 0", checksum16((const uint8_t *)&ip, 20, 0), 0x0000);

    uint8_t odd[3] = {0x12, 0x34, 0x56};
    expect16("odd length: trailing byte zero padded", checksum16(odd, 3, 0), (uint16_t)~(0x1234 + 0x5600));

    CHECK("wire struct packing (14/20/8/28/4)", sizeof(tEthHdr) == 14 && sizeof(tIp4Hdr) == 20 && sizeof(tUdpHdr) == 8 && sizeof(tArpHdr) == 28 && sizeof(tIcmpHdr) == 4);
    CHECK("RAW_HDR_LEN is 42", RAW_HDR_LEN == 42);
    // MTU independent: the frame is the 42 byte header plus one full segment.
    // With the default OPTION_MTU of 1504 that is 1514 bytes, but jumbo configurations are valid.
    CHECK("max frame == 42 + max segment", RAW_MAX_FRAME == RAW_HDR_LEN + XCPTL_MAX_SEGMENT_SIZE);
    CHECK("max frame == OPTION_MTU + 10", RAW_MAX_FRAME == OPTION_MTU + 10);
}

//-----------------------------------------------------------------------------------------------------
// Frame build and receive filter

static void test_frames(void) {

    static uint8_t rx[TEST_BUF_SIZE], out[TEST_BUF_SIZE];
    uint8_t srcAddr[4];
    uint16_t srcPort;
    const uint8_t payload[] = {0x02, 0x00, 0x00, 0x00, 0xFF, 0x00}; // XCP CONNECT message
    uint16_t n;
    int16_t r;

    setupSocket();
    n = buildRxUdp(rx, LOCAL_MAC, LOCAL_IP, LOCAL_PORT, payload, sizeof(payload), 0x4000);
    r = handleFrame(rx, n, out, sizeof(out), srcAddr, &srcPort);
    CHECK("RX: accepted, payload length", r == (int16_t)sizeof(payload));
    CHECK("RX: payload content", memcmp(out, payload, sizeof(payload)) == 0);
    CHECK("RX: source address extracted", memcmp(srcAddr, PEER_IP, 4) == 0);
    CHECK("RX: source port extracted", srcPort == PEER_PORT);
    CHECK("RX: peer MAC learned", sSocketRaw.peer_mac_valid && !memcmp(sSocketRaw.peer_mac, PEER_MAC, 6));

    setupSocket();
    n = buildRxUdp(rx, LOCAL_MAC, LOCAL_IP, 9999, payload, sizeof(payload), 0x4000);
    CHECK("RX: wrong UDP port dropped", handleFrame(rx, n, out, sizeof(out), srcAddr, &srcPort) == 0);
    CHECK("RX: wrong port does not learn the peer", !sSocketRaw.peer_mac_valid);

    setupSocket();
    const uint8_t other_mac[6] = {0x02, 0x99, 0x99, 0x99, 0x99, 0x99};
    n = buildRxUdp(rx, other_mac, LOCAL_IP, LOCAL_PORT, payload, sizeof(payload), 0x4000);
    CHECK("RX: foreign destination MAC dropped", handleFrame(rx, n, out, sizeof(out), srcAddr, &srcPort) == 0);

    setupSocket();
    const uint8_t other_ip[4] = {192, 168, 90, 77};
    n = buildRxUdp(rx, LOCAL_MAC, other_ip, LOCAL_PORT, payload, sizeof(payload), 0x4000);
    CHECK("RX: foreign destination IP dropped", handleFrame(rx, n, out, sizeof(out), srcAddr, &srcPort) == 0);

    setupSocket(); // MF set = fragment
    n = buildRxUdp(rx, LOCAL_MAC, LOCAL_IP, LOCAL_PORT, payload, sizeof(payload), 0x2000);
    CHECK("RX: IPv4 fragment dropped", handleFrame(rx, n, out, sizeof(out), srcAddr, &srcPort) == 0);

    setupSocket(); // must be dropped, never truncated
    static uint8_t big[600];
    memset(big, 0xA5, sizeof(big));
    n = buildRxUdp(rx, LOCAL_MAC, LOCAL_IP, LOCAL_PORT, big, sizeof(big), 0x4000);
    CHECK("RX: oversized payload dropped, not truncated", handleFrame(rx, n, out, 64, srcAddr, &srcPort) == 0);

    setupSocket();
    n = buildRxUdp(rx, LOCAL_MAC, LOCAL_IP, LOCAL_PORT, payload, sizeof(payload), 0x4000);
    rx[ETH_HDR_LEN + 10] ^= 0xFF; // corrupt the IPv4 header checksum
    CHECK("RX: bad IPv4 header checksum dropped", handleFrame(rx, n, out, sizeof(out), srcAddr, &srcPort) == 0);

    setupSocket();
    n = buildRxUdp(rx, LOCAL_MAC, LOCAL_IP, LOCAL_PORT, payload, sizeof(payload), 0x4000);
    ((tEthHdr *)rx)->ethertype = BE16(ETHERTYPE_VLAN);
    CHECK("RX: VLAN tagged frame dropped", handleFrame(rx, n, out, sizeof(out), srcAddr, &srcPort) == 0);

    // Transmit
    setupSocket();
    memcpy(sSocketRaw.peer_mac, PEER_MAC, 6);
    sSocketRaw.peer_mac_valid = true;
    int16_t sent = socketSendTo(&sSocketRaw, payload, sizeof(payload), PEER_IP, PEER_PORT, NULL);
    CHECK("TX: returns the PAYLOAD size, not the frame size", sent == (int16_t)sizeof(payload));
    CHECK("TX: one frame handed to the HAL", gTxCount == 1);
    CHECK("TX: frame length is 42 + payload", gTxLen == RAW_HDR_LEN + sizeof(payload));
    tEthHdr *te = (tEthHdr *)gTxFrame;
    CHECK("TX: destination MAC is the learned peer", memcmp(te->dst, PEER_MAC, 6) == 0);
    CHECK("TX: source MAC is ours", memcmp(te->src, LOCAL_MAC, 6) == 0);
    CHECK("TX: ethertype IPv4", BE16(te->ethertype) == ETHERTYPE_IPV4);
    tIp4Hdr *ti = (tIp4Hdr *)(gTxFrame + ETH_HDR_LEN);
    CHECK("TX: IPv4 header checksum valid", checksum16((uint8_t *)ti, IP4_HDR_LEN, 0) == 0);
    CHECK("TX: DF set, no fragmentation", (BE16(ti->flags_frag) & 0x3FFF) == 0 && (BE16(ti->flags_frag) & 0x4000) != 0);
    CHECK("TX: IPv4 total length field", BE16(ti->total_length) == IP4_HDR_LEN + UDP_HDR_LEN + sizeof(payload));
    CHECK("TX: protocol UDP, ttl 64", ti->protocol == IP_PROTO_UDP && ti->ttl == 64);
    tUdpHdr *tu = (tUdpHdr *)(gTxFrame + ETH_HDR_LEN + IP4_HDR_LEN);
    CHECK("TX: UDP ports", BE16(tu->src_port) == LOCAL_PORT && BE16(tu->dst_port) == PEER_PORT);
    CHECK("TX: UDP length field", BE16(tu->length) == UDP_HDR_LEN + sizeof(payload));
    CHECK("TX: payload copied intact", memcmp(gTxFrame + RAW_HDR_LEN, payload, sizeof(payload)) == 0);

    // Round trip: our own frame, addresses swapped, must parse back to the payload
    static uint8_t rt[TEST_BUF_SIZE];
    uint16_t rt_len = gTxLen;
    memcpy(rt, gTxFrame, rt_len);
    setupSocket();
    tEthHdr *re = (tEthHdr *)rt;
    memcpy(re->dst, LOCAL_MAC, 6);
    memcpy(re->src, PEER_MAC, 6);
    tIp4Hdr *ri = (tIp4Hdr *)(rt + ETH_HDR_LEN);
    memcpy(ri->src, PEER_IP, 4);
    memcpy(ri->dst, LOCAL_IP, 4);
    ri->checksum = BE16(ipHeaderChecksum(ri));
    tUdpHdr *ru = (tUdpHdr *)(rt + ETH_HDR_LEN + IP4_HDR_LEN);
    ru->src_port = BE16(PEER_PORT);
    ru->dst_port = BE16(LOCAL_PORT);
    r = handleFrame(rt, rt_len, out, sizeof(out), srcAddr, &srcPort);
    CHECK("Round trip: TX frame parses back to the payload", r == (int16_t)sizeof(payload) && !memcmp(out, payload, sizeof(payload)));

    // A maximum size segment must still fit into one Ethernet frame
    setupSocket();
    memcpy(sSocketRaw.peer_mac, PEER_MAC, 6);
    sSocketRaw.peer_mac_valid = true;
    static uint8_t maxp[XCPTL_MAX_SEGMENT_SIZE];
    memset(maxp, 0x5A, sizeof(maxp));
    sent = socketSendTo(&sSocketRaw, maxp, sizeof(maxp), PEER_IP, PEER_PORT, NULL);
    CHECK("TX: maximum segment accepted", sent == (int16_t)sizeof(maxp));
    CHECK("TX: maximum frame is 42 + max segment", gTxLen == RAW_HDR_LEN + XCPTL_MAX_SEGMENT_SIZE);

    // No peer learned yet -> must not send
    setupSocket();
    CHECK("TX: refuses to send before a peer is known", socketSendTo(&sSocketRaw, payload, sizeof(payload), PEER_IP, PEER_PORT, NULL) == -1 && gTxCount == 0);
}

//-----------------------------------------------------------------------------------------------------
// ARP and ICMP responders

static void test_arp_icmp(void) {

    static uint8_t f[TEST_BUF_SIZE], out[TEST_BUF_SIZE];
    uint8_t srcAddr[4];
    uint16_t srcPort;
    uint16_t n;

    setupSocket();
    n = buildArpRequest(f, LOCAL_IP, ARP_OPER_REQUEST);
    handleFrame(f, n, out, sizeof(out), srcAddr, &srcPort);
    CHECK("ARP: reply sent for a request for our IP", gTxCount == 1);
    CHECK("ARP: reply length 42", gTxLen == ETH_HDR_LEN + ARP_LEN);
    tEthHdr *re = (tEthHdr *)gTxFrame;
    tArpHdr *ra = (tArpHdr *)(gTxFrame + ETH_HDR_LEN);
    CHECK("ARP: unicast back to the requester", memcmp(re->dst, PEER_MAC, 6) == 0);
    CHECK("ARP: source MAC is ours", memcmp(re->src, LOCAL_MAC, 6) == 0);
    CHECK("ARP: operation is Reply(2)", BE16(ra->oper) == ARP_OPER_REPLY);
    CHECK("ARP: sender hw/proto are ours", !memcmp(ra->sha, LOCAL_MAC, 6) && !memcmp(ra->spa, LOCAL_IP, 4));
    CHECK("ARP: target hw/proto are the requester", !memcmp(ra->tha, PEER_MAC, 6) && !memcmp(ra->tpa, PEER_IP, 4));
    CHECK("ARP: does NOT learn the peer (anti hijack)", !sSocketRaw.peer_mac_valid);

    setupSocket();
    const uint8_t foreign_ip[4] = {192, 168, 90, 99};
    n = buildArpRequest(f, foreign_ip, ARP_OPER_REQUEST);
    handleFrame(f, n, out, sizeof(out), srcAddr, &srcPort);
    CHECK("ARP: request for a foreign IP ignored", gTxCount == 0);

    setupSocket();
    n = buildArpRequest(f, LOCAL_IP, ARP_OPER_REPLY);
    handleFrame(f, n, out, sizeof(out), srcAddr, &srcPort);
    CHECK("ARP: unsolicited Reply ignored", gTxCount == 0);

    setupSocket();
    n = buildIcmpEcho(f, 56); // classic ping payload
    handleFrame(f, n, out, sizeof(out), srcAddr, &srcPort);
    CHECK("ICMP: echo reply sent", gTxCount == 1);
    tEthHdr *ie = (tEthHdr *)gTxFrame;
    tIp4Hdr *ii = (tIp4Hdr *)(gTxFrame + ETH_HDR_LEN);
    uint8_t *ic = gTxFrame + ETH_HDR_LEN + IP4_HDR_LEN;
    uint16_t icmp_len = (uint16_t)(BE16(ii->total_length) - IP4_HDR_LEN);
    CHECK("ICMP: back to the requester MAC", memcmp(ie->dst, PEER_MAC, 6) == 0);
    CHECK("ICMP: IPv4 addresses swapped", !memcmp(ii->src, LOCAL_IP, 4) && !memcmp(ii->dst, PEER_IP, 4));
    CHECK("ICMP: IPv4 header checksum valid", checksum16((uint8_t *)ii, IP4_HDR_LEN, 0) == 0);
    CHECK("ICMP: type is Echo Reply(0)", ic[0] == ICMP_TYPE_ECHO_REPLY);
    CHECK("ICMP: checksum valid", checksum16(ic, icmp_len, 0) == 0);
    CHECK("ICMP: id and sequence preserved", ic[4] == 0x12 && ic[5] == 0x34 && ic[7] == 0x01);
    CHECK("ICMP: payload echoed back", ic[8] == 0 && ic[9] == 1 && ic[8 + 55] == 55);
    CHECK("ICMP: reply length matches the request", gTxLen == n);

    setupSocket();
    n = buildIcmpEcho(f, 1400); // ping -s 1400
    handleFrame(f, n, out, sizeof(out), srcAddr, &srcPort);
    CHECK("ICMP: large echo (1400 bytes) answered", gTxCount == 1 && gTxLen == n);
    ii = (tIp4Hdr *)(gTxFrame + ETH_HDR_LEN);
    ic = gTxFrame + ETH_HDR_LEN + IP4_HDR_LEN;
    CHECK("ICMP: large echo checksum valid", checksum16(ic, (uint16_t)(BE16(ii->total_length) - IP4_HDR_LEN), 0) == 0);

    setupSocket();
    // handleIcmp drops the request when it would not fit the reply buffer, i.e. when the echo
    // data exceeds one full segment. Derive it so this holds for a jumbo OPTION_MTU as well.
    n = buildIcmpEcho(f, (uint16_t)(XCPTL_MAX_SEGMENT_SIZE + 1));
    handleFrame(f, n, out, sizeof(out), srcAddr, &srcPort);
    CHECK("ICMP: oversized echo dropped, no reply", gTxCount == 0);
}

//-----------------------------------------------------------------------------------------------------
// Zero copy transmit (OPTION_UDP_RAW_ZERO_COPY)

#if XCPTL_TX_HEADROOM > 0

// Mimics a transmit queue segment: XCPTL_TX_HEADROOM writable bytes in front of the payload, laid
// out so the payload start has the same alignment as tXcpSegmentBuffer::msg_buffer.
// At file scope because a function local typedef is not portable inside offsetof (GCC rejects it).
typedef struct {
    uint32_t magic;
    uint16_t uncommitted;
    uint16_t size;
    uint8_t headroom[XCPTL_TX_HEADROOM];
    uint8_t msg_buffer[XCPTL_MAX_SEGMENT_SIZE];
} tTestSegment;

static void test_zero_copy(void) {

    const uint8_t payload[] = {0x02, 0x00, 0x00, 0x00, 0xFF, 0x00};

    static tTestSegment seg;

    CHECK("ZC: test fixture matches the queue layout", (offsetof(tTestSegment, msg_buffer) % XCPTL_PACKET_ALIGNMENT) == 0);

    setupSocket();
    memcpy(sSocketRaw.peer_mac, PEER_MAC, 6);
    sSocketRaw.peer_mac_valid = true;
    memset(&seg, 0, sizeof(seg));
    memcpy(seg.msg_buffer, payload, sizeof(payload));

    // Poison the headroom so we can see exactly which bytes the transport writes
    memset(seg.headroom, 0xCD, sizeof(seg.headroom));

    int16_t sent = socketSendToReserved(&sSocketRaw, seg.msg_buffer, sizeof(payload), PEER_IP, PEER_PORT, NULL);
    CHECK("ZC: returns the payload size", sent == (int16_t)sizeof(payload));
    CHECK("ZC: one frame handed to the HAL", gTxCount == 1);
    CHECK("ZC: frame length is 42 + payload", gTxLen == RAW_HDR_LEN + sizeof(payload));

    // The header must be written right justified, i.e. ending exactly where the payload starts
    const uint8_t *frame = seg.msg_buffer - RAW_HDR_LEN;
    CHECK("ZC: header written at payload - 42", frame == &seg.headroom[XCPTL_TX_HEADROOM - RAW_HDR_LEN]);
    CHECK("ZC: the 6 bytes before the header are untouched", seg.headroom[0] == 0xCD && seg.headroom[XCPTL_TX_HEADROOM - RAW_HDR_LEN - 1] == 0xCD);

    // The frame the HAL received must be the segment itself, header + payload contiguous
    CHECK("ZC: HAL frame equals segment header + payload", memcmp(gTxFrame, frame, gTxLen) == 0);
    CHECK("ZC: payload NOT copied, still in place", memcmp(seg.msg_buffer, payload, sizeof(payload)) == 0);

    const tEthHdr *eth = (const tEthHdr *)frame;
    const tIp4Hdr *ip = (const tIp4Hdr *)(frame + ETH_HDR_LEN);
    const tUdpHdr *udp = (const tUdpHdr *)(frame + ETH_HDR_LEN + IP4_HDR_LEN);
    CHECK("ZC: destination MAC is the peer", memcmp(eth->dst, PEER_MAC, 6) == 0);
    CHECK("ZC: ethertype IPv4", BE16(eth->ethertype) == ETHERTYPE_IPV4);
    CHECK("ZC: IPv4 header checksum valid", checksum16((const uint8_t *)ip, IP4_HDR_LEN, 0) == 0);
    CHECK("ZC: IPv4 header is 4 byte aligned", (((uintptr_t)ip) % 4) == 0);
    CHECK("ZC: total length field", BE16(ip->total_length) == IP4_HDR_LEN + UDP_HDR_LEN + sizeof(payload));
    CHECK("ZC: UDP ports and length", BE16(udp->src_port) == LOCAL_PORT && BE16(udp->dst_port) == PEER_PORT && BE16(udp->length) == UDP_HDR_LEN + sizeof(payload));

    // A full size segment must still produce exactly one frame of the expected length
    setupSocket();
    memcpy(sSocketRaw.peer_mac, PEER_MAC, 6);
    sSocketRaw.peer_mac_valid = true;
    memset(seg.msg_buffer, 0x5A, XCPTL_MAX_SEGMENT_SIZE);
    sent = socketSendToReserved(&sSocketRaw, seg.msg_buffer, XCPTL_MAX_SEGMENT_SIZE, PEER_IP, PEER_PORT, NULL);
    CHECK("ZC: full segment accepted", sent == (int16_t)XCPTL_MAX_SEGMENT_SIZE);
    CHECK("ZC: full frame is 42 + max segment", gTxLen == RAW_HDR_LEN + XCPTL_MAX_SEGMENT_SIZE);

    // Same datagram built by both paths must be byte identical apart from the IPv4 identification
    setupSocket();
    memcpy(sSocketRaw.peer_mac, PEER_MAC, 6);
    sSocketRaw.peer_mac_valid = true;
    socketSendTo(&sSocketRaw, payload, sizeof(payload), PEER_IP, PEER_PORT, NULL);
    static uint8_t copy_frame[TEST_BUF_SIZE];
    uint16_t copy_len = gTxLen;
    memcpy(copy_frame, gTxFrame, copy_len);
    sSocketRaw.ip_ident = 0; // both paths start from the same identification
    gTxCount = 0;
    memcpy(seg.msg_buffer, payload, sizeof(payload));
    socketSendToReserved(&sSocketRaw, seg.msg_buffer, sizeof(payload), PEER_IP, PEER_PORT, NULL);
    CHECK("ZC: copy and zero copy produce the same length", copy_len == gTxLen);
    // zero the identification in both before comparing
    ((tIp4Hdr *)(copy_frame + ETH_HDR_LEN))->ident = 0;
    ((tIp4Hdr *)(gTxFrame + ETH_HDR_LEN))->ident = 0;
    CHECK("ZC: copy and zero copy produce identical frames", memcmp(copy_frame, gTxFrame, copy_len) == 0);
}
#endif // XCPTL_TX_HEADROOM > 0

//-----------------------------------------------------------------------------------------------------

int main(void) {
    printf("\nsocket_raw_test - raw Ethernet transport unit tests\n");
    printf("\n--- checksums and wire layout ---\n");
    test_checksums();
    printf("\n--- frame build and receive filter ---\n");
    test_frames();
    printf("\n--- ARP and ICMP responders ---\n");
    test_arp_icmp();
#if XCPTL_TX_HEADROOM > 0
    printf("\n--- zero copy transmit ---\n");
    test_zero_copy();
#else
    printf("\n--- zero copy transmit: skipped (OPTION_UDP_RAW_ZERO_COPY off) ---\n");
#endif
    printf("\n%s (%d failures)\n\n", fails ? "FAILED" : "ALL PASSED", fails);
    return fails != 0;
}
