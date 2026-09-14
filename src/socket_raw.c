/*----------------------------------------------------------------------------
| File:
|   socket_raw.c
|
| Description:
|   Raw-Ethernet XCP/UDP transport (OPTION_ENABLE_UDP_RAW)
|   Hand-crafted UDP/IPv4 layer over a raw Ethernet HAL (socket_raw_hal.h),
|   for targets without a TCP/IP stack. See docs/SOCKET_RAW.md
|
|   Implements the socket API subset used by the XCP Ethernet transport layer:
|     socketStartup, socketCleanup, socketGetErrorString, socketGetLastError,
|     socketOpen, socketBind, socketRecvFrom, socketSendTo,
|     socketSetTimeout, socketShutdown, socketClose
|
|   ARP is answer-only: XCP is always master initiated, so the peer MAC is learned
|   from the received frame and ARP Requests are never sent. Answering ARP Requests
|   for our IP is required, the XCP client stack resolves us before the first CONNECT.
|
|   Because the peer MAC is learned from the frame rather than resolved, no netmask
|   and no default gateway are needed: with a client behind a router, the router MAC
|   arrives as the frame source and the responses go back to it.
|
| Code released into public domain, no attribution required
 ----------------------------------------------------------------------------*/

#include "sockets.h"

#include <string.h> // for memcpy, memcmp, memset

#include "assert.h"
#include "dbg_print.h"
#include "socket_raw_hal.h" // for the Ethernet HAL and the backend selection
#include "xcptl_cfg.h"      // for XCPTL_MAX_SEGMENT_SIZE, XCPTL_MAX_CTO_SIZE

#ifdef OPTION_ENABLE_UDP_RAW

// All targets in scope are little endian (x86-64, ARM Cortex-M, Xtensa, Windows/XLAPI).
// The wire format stays big endian, but with the host endianness known that is a fixed
// byte swap rather than a portability question. MSVC defines no __BYTE_ORDER__ and
// targets little endian architectures only, so the guard simply does not fire there.
#if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__)
#error "OPTION_ENABLE_UDP_RAW assumes a little endian host"
#endif

//-------------------------------------------------------------------------------------------------------
// Wire format
// Host is little endian, the wire is big endian, so conversion is a plain byte swap.
// Uppercase to avoid any collision with a htons/ntohs macro from a platform header.

// Note: do NOT name this HTONS/NTOHS - BSD derived platforms (macOS) define those
// uppercase names in <sys/_endian.h> as in-place ASSIGNMENT macros, which is a silent
// semantic collision. BE16 is a plain value returning byte swap, used in both directions.
// It is a function, not a macro, so that arguments with side effects (BE16(ident++))
// are evaluated exactly once.
static inline uint16_t BE16(uint16_t v) { return (uint16_t)(((v & 0x00FFu) << 8) | ((v & 0xFF00u) >> 8)); }

#pragma pack(push, 1)

typedef struct {
    uint8_t dst[6];
    uint8_t src[6];
    uint16_t ethertype;
} tEthHdr;

typedef struct {
    uint8_t ver_ihl; // 0x45 = IPv4, header length 5 words
    uint8_t tos;
    uint16_t total_length; // IPv4 header + UDP header + payload
    uint16_t ident;
    uint16_t flags_frag; // DF set, no fragmentation
    uint8_t ttl;
    uint8_t protocol;
    uint16_t checksum;
    uint8_t src[4]; // network order, never byte swapped
    uint8_t dst[4];
} tIp4Hdr;

typedef struct {
    uint16_t src_port;
    uint16_t dst_port;
    uint16_t length; // UDP header + payload
    uint16_t checksum;
} tUdpHdr;

typedef struct {
    uint16_t htype;
    uint16_t ptype;
    uint8_t hlen;
    uint8_t plen;
    uint16_t oper;
    uint8_t sha[6]; // sender hardware address
    uint8_t spa[4]; // sender protocol address
    uint8_t tha[6]; // target hardware address
    uint8_t tpa[4]; // target protocol address
} tArpHdr;

typedef struct {
    uint8_t type;
    uint8_t code;
    uint16_t checksum;
} tIcmpHdr;

#pragma pack(pop)

#define ETH_HDR_LEN 14
#define IP4_HDR_LEN 20
#define UDP_HDR_LEN 8
#define ARP_LEN 28
#define RAW_HDR_LEN (ETH_HDR_LEN + IP4_HDR_LEN + UDP_HDR_LEN) // 42

// One XCP segment must fit into one Ethernet frame, enforced in xcptl_cfg.h
#define RAW_MAX_FRAME (RAW_HDR_LEN + XCPTL_MAX_SEGMENT_SIZE)

#if XCPTL_TX_HEADROOM > 0 && (XCPTL_TX_HEADROOM < RAW_HDR_LEN)
#error "XCPTL_TX_HEADROOM must be at least 42 bytes to hold the Ethernet, IPv4 and UDP header"
#endif

#define ETHERTYPE_IPV4 0x0800
#define ETHERTYPE_ARP 0x0806
#define ETHERTYPE_VLAN 0x8100

#define IP_PROTO_ICMP 1
#define IP_PROTO_UDP 17

#define ARP_OPER_REQUEST 1
#define ARP_OPER_REPLY 2

#define ICMP_TYPE_ECHO_REQUEST 8
#define ICMP_TYPE_ECHO_REPLY 0

static const uint8_t sBroadcastMac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

//-------------------------------------------------------------------------------------------------------
// Socket context

struct socket_raw {
    tEthHalCtx *hal;
    uint8_t local_mac[6];
    uint8_t local_ip[4]; // network order, set by socketBind
    uint16_t local_port; // host order
    uint8_t peer_mac[6]; // learned from the last accepted UDP datagram
    bool peer_mac_valid;
    uint16_t ip_ident;
    uint32_t recv_timeout_ms; // 0 = infinite
    volatile bool shutdown_requested;
    bool is_open;
    bool is_bound;
    MUTEX tx_mutex; // serializes header build and eth_hal_send, see docs/SOCKET_RAW.md
};

// The raw transport is one per target, no heap needed on bare metal targets
static struct socket_raw sSocketRaw;

// Backend specific interface selector, see socketRawSetInterface()
static const char *sInterfaceConfig = NULL;

// Receive buffer with the NET_IP_ALIGN lead pad: if the compiler places sRxBuf on a 4 byte
// boundary, which it does in practice for a static array, the IPv4 header lands 4 byte aligned too.
// This is deliberately not enforced with an alignment attribute: it is only a codegen hint, and the
// packed wire structs make every access alignment safe regardless.
static uint8_t sRxBuf[2 + RAW_MAX_FRAME + 4];
#define RX_FRAME (&sRxBuf[2])

// Buffer for ARP and ICMP replies, only used from the receive thread, under tx_mutex when sending
static uint8_t sCtrlBuf[RAW_MAX_FRAME];

// Last error
static int32_t sLastError = SOCKET_ERROR_NONE;

//-------------------------------------------------------------------------------------------------------
// Errors

int32_t socketGetLastError(void) { return sLastError; }

const char *socketGetErrorString(int32_t err) {
    switch (err) {
    case SOCKET_ERROR_NONE:
        return "no error";
    case SOCKET_ERROR_TIMEDOUT:
        return "timed out";
    case SOCKET_ERROR_BADF:
        return "socket closed";
    case SOCKET_ERROR_NOTCONN:
        return "not connected";
    case SOCKET_ERROR_HAL:
        return "Ethernet HAL error";
    case SOCKET_ERROR_TOOBIG:
        return "frame too large";
    case SOCKET_ERROR_NOPEER:
        return "peer MAC unknown";
    case SOCKET_ERROR_MSGSIZE:
        return "frame too large for the link MTU";
    default:
        return "unknown socket error";
    }
}

//-------------------------------------------------------------------------------------------------------
// Checksums (RFC 1071)
// The byte stream is summed as big endian 16 bit words, the numeric result is stored
// into the header field with BE16() so the high byte goes first on the wire.

static uint16_t checksum16(const uint8_t *p, uint16_t len, uint32_t sum) {
    while (len > 1) {
        sum += ((uint32_t)p[0] << 8) | (uint32_t)p[1];
        p += 2;
        len = (uint16_t)(len - 2);
    }
    if (len > 0) {
        sum += (uint32_t)p[0] << 8; // odd trailing byte, padded with zero
    }
    while ((sum >> 16) != 0) {
        sum = (sum & 0xFFFFu) + (sum >> 16);
    }
    return (uint16_t)(~sum & 0xFFFFu);
}

static uint16_t ipHeaderChecksum(tIp4Hdr *ip) {
    ip->checksum = 0;
    return checksum16((const uint8_t *)ip, IP4_HDR_LEN, 0);
}

#ifdef OPTION_UDP_RAW_UDP_CHECKSUM_COMPUTE
// UDP checksum over the IPv4 pseudo header and the UDP header + payload (RFC 768)
static uint16_t udpChecksum(const tIp4Hdr *ip, const uint8_t *udp, uint16_t udp_len) {
    uint32_t sum = 0;
    // Pseudo header: src ip, dst ip, zero + protocol, UDP length
    sum += ((uint32_t)ip->src[0] << 8) | ip->src[1];
    sum += ((uint32_t)ip->src[2] << 8) | ip->src[3];
    sum += ((uint32_t)ip->dst[0] << 8) | ip->dst[1];
    sum += ((uint32_t)ip->dst[2] << 8) | ip->dst[3];
    sum += (uint32_t)IP_PROTO_UDP;
    sum += (uint32_t)udp_len;
    uint16_t c = checksum16(udp, udp_len, sum);
    // An all zero checksum must be transmitted as all ones, 0 means "no checksum"
    return (c == 0) ? 0xFFFFu : c;
}
#endif

//-------------------------------------------------------------------------------------------------------
// Address helpers

static bool isValidLocalIp(const uint8_t *addr) {
    if (addr == NULL)
        return false;
    if (addr[0] == 0)
        return false; // 0.0.0.0 (ANY) has no meaning without an IP stack
    if (addr[0] == 255 && addr[1] == 255 && addr[2] == 255 && addr[3] == 255)
        return false; // broadcast
    if (addr[0] >= 224 && addr[0] <= 239)
        return false; // multicast
    if (addr[0] == 127)
        return false; // loopback, there is no loopback on a raw Ethernet link
    return true;
}

static bool isOurMac(const uint8_t *mac) { return memcmp(mac, sSocketRaw.local_mac, 6) == 0; }
static bool isBroadcastMac(const uint8_t *mac) { return memcmp(mac, sBroadcastMac, 6) == 0; }

//-------------------------------------------------------------------------------------------------------
// Transmit

// Send a fully built frame, serialized against the other transmit paths
static int16_t sendFrame(const uint8_t *frame, uint16_t len) {
    mutexLock(&sSocketRaw.tx_mutex);
    int16_t r = eth_hal_send(sSocketRaw.hal, frame, len);
    mutexUnlock(&sSocketRaw.tx_mutex);
    if (r < 0) {
        sLastError = (r == ETH_HAL_ERROR_SIZE) ? SOCKET_ERROR_MSGSIZE : SOCKET_ERROR_HAL;
    }
    return r;
}

//-------------------------------------------------------------------------------------------------------
// ARP - answer only
//
// We never send ARP Requests: the peer MAC is learned from the received XCP datagram.
// Answering Requests for our IP is mandatory, the XCP client stack resolves us first.
// The sender MAC of an ARP frame is deliberately NOT learned as the peer: an unrelated
// host asking for our IP must not be able to redirect the DAQ stream.

static void handleArp(const uint8_t *frame, uint16_t len) {

    if (len < ETH_HDR_LEN + ARP_LEN)
        return;

    const tArpHdr *arp = (const tArpHdr *)(frame + ETH_HDR_LEN);
    if (arp->htype != BE16(1))
        return; // not Ethernet
    if (arp->ptype != BE16(ETHERTYPE_IPV4))
        return; // not IPv4
    if (arp->hlen != 6 || arp->plen != 4)
        return;
    if (arp->oper != BE16(ARP_OPER_REQUEST))
        return; // we never send Requests, so Replies are of no interest
    if (memcmp(arp->tpa, sSocketRaw.local_ip, 4) != 0)
        return; // not asking for our IP

    // Build the Reply
    memset(sCtrlBuf, 0, ETH_HDR_LEN + ARP_LEN);
    tEthHdr *eth = (tEthHdr *)sCtrlBuf;
    memcpy(eth->dst, arp->sha, 6);
    memcpy(eth->src, sSocketRaw.local_mac, 6);
    eth->ethertype = BE16(ETHERTYPE_ARP);

    tArpHdr *rep = (tArpHdr *)(sCtrlBuf + ETH_HDR_LEN);
    rep->htype = BE16(1);
    rep->ptype = BE16(ETHERTYPE_IPV4);
    rep->hlen = 6;
    rep->plen = 4;
    rep->oper = BE16(ARP_OPER_REPLY);
    memcpy(rep->sha, sSocketRaw.local_mac, 6);
    memcpy(rep->spa, sSocketRaw.local_ip, 4);
    memcpy(rep->tha, arp->sha, 6);
    memcpy(rep->tpa, arp->spa, 4);

    DBG_PRINTF5("socket_raw: ARP request for %u.%u.%u.%u, sending reply\n", sSocketRaw.local_ip[0], sSocketRaw.local_ip[1], sSocketRaw.local_ip[2], sSocketRaw.local_ip[3]);
    sendFrame(sCtrlBuf, ETH_HDR_LEN + ARP_LEN);
}

#ifdef OPTION_UDP_RAW_GRATUITOUS_ARP
// Gratuitous ARP announcement: primes switch MAC tables and the ARP cache of the client.
// Not required for correctness, ARP Requests for our IP are always answered.
static void sendGratuitousArp(void) {

    memset(sCtrlBuf, 0, ETH_HDR_LEN + ARP_LEN);
    tEthHdr *eth = (tEthHdr *)sCtrlBuf;
    memcpy(eth->dst, sBroadcastMac, 6);
    memcpy(eth->src, sSocketRaw.local_mac, 6);
    eth->ethertype = BE16(ETHERTYPE_ARP);

    tArpHdr *arp = (tArpHdr *)(sCtrlBuf + ETH_HDR_LEN);
    arp->htype = BE16(1);
    arp->ptype = BE16(ETHERTYPE_IPV4);
    arp->hlen = 6;
    arp->plen = 4;
    arp->oper = BE16(ARP_OPER_REQUEST);
    memcpy(arp->sha, sSocketRaw.local_mac, 6);
    memcpy(arp->spa, sSocketRaw.local_ip, 4);
    memset(arp->tha, 0, 6);
    memcpy(arp->tpa, sSocketRaw.local_ip, 4); // announcement: target == sender

    DBG_PRINT3("  Sending gratuitous ARP announcement\n");
    sendFrame(sCtrlBuf, ETH_HDR_LEN + ARP_LEN);
}
#endif

//-------------------------------------------------------------------------------------------------------
// ICMP Echo - answer only
//
// A successful ping proves the Ethernet HAL, the MAC filter, the ARP reply, the IPv4
// header build and the header checksum all work, before any XCP tooling is involved.

#ifdef OPTION_UDP_RAW_ENABLE_ICMP_ECHO
static void handleIcmp(const uint8_t *frame, uint16_t len, const tIp4Hdr *ip, uint16_t ip_hdr_len) {

    uint16_t total_length = BE16(ip->total_length);
    if (total_length < ip_hdr_len + (uint16_t)sizeof(tIcmpHdr))
        return;
    uint16_t icmp_len = (uint16_t)(total_length - ip_hdr_len);
    if ((uint32_t)ETH_HDR_LEN + total_length > len)
        return; // truncated
    if ((uint32_t)ETH_HDR_LEN + IP4_HDR_LEN + icmp_len > sizeof(sCtrlBuf))
        return; // would not fit into the reply buffer

    const tIcmpHdr *icmp = (const tIcmpHdr *)(frame + ETH_HDR_LEN + ip_hdr_len);
    if (icmp->type != ICMP_TYPE_ECHO_REQUEST || icmp->code != 0)
        return;

    const tEthHdr *req_eth = (const tEthHdr *)frame;

    // Ethernet header: back to the sender
    tEthHdr *eth = (tEthHdr *)sCtrlBuf;
    memcpy(eth->dst, req_eth->src, 6);
    memcpy(eth->src, sSocketRaw.local_mac, 6);
    eth->ethertype = BE16(ETHERTYPE_IPV4);

    // IPv4 header: fresh, without any options of the request
    tIp4Hdr *rip = (tIp4Hdr *)(sCtrlBuf + ETH_HDR_LEN);
    memset(rip, 0, IP4_HDR_LEN);
    rip->ver_ihl = 0x45;
    rip->total_length = BE16((uint16_t)(IP4_HDR_LEN + icmp_len));
    rip->ident = BE16(sSocketRaw.ip_ident++);
    rip->flags_frag = BE16(0x4000); // DF
    rip->ttl = 64;
    rip->protocol = IP_PROTO_ICMP;
    memcpy(rip->src, sSocketRaw.local_ip, 4);
    memcpy(rip->dst, ip->src, 4);
    rip->checksum = BE16(ipHeaderChecksum(rip));

    // ICMP: copy the request, turn it into a reply and recompute the checksum.
    // Ping is a manual bring-up aid, not a hot path, so a full recompute is preferred
    // over an incremental update - it is simpler and has no carry handling to get wrong.
    uint8_t *ricmp = sCtrlBuf + ETH_HDR_LEN + IP4_HDR_LEN;
    memcpy(ricmp, frame + ETH_HDR_LEN + ip_hdr_len, icmp_len);
    ((tIcmpHdr *)ricmp)->type = ICMP_TYPE_ECHO_REPLY;
    ((tIcmpHdr *)ricmp)->checksum = 0;
    ((tIcmpHdr *)ricmp)->checksum = BE16(checksum16(ricmp, icmp_len, 0));

    DBG_PRINTF5("socket_raw: ICMP echo request from %u.%u.%u.%u, sending reply\n", ip->src[0], ip->src[1], ip->src[2], ip->src[3]);
    sendFrame(sCtrlBuf, (uint16_t)(ETH_HDR_LEN + IP4_HDR_LEN + icmp_len));
}
#endif

//-------------------------------------------------------------------------------------------------------
// Receive path
//
// handleFrame classifies one received frame. The filter order is cheapest and most
// discriminating first, so that on a busy link almost every foreign frame dies early.
// Returns: > 0  payload bytes copied to buffer, this is an XCP datagram for us
//          == 0 not for us, or consumed (ARP/ICMP answered) - the caller keeps looping

static int16_t handleFrame(const uint8_t *frame, uint16_t len, uint8_t *buffer, uint16_t bufferSize, uint8_t *srcAddr, uint16_t *srcPort) {

    if (len < ETH_HDR_LEN)
        return 0;

    const tEthHdr *eth = (const tEthHdr *)frame;
    uint16_t ethertype = BE16(eth->ethertype);

    if (ethertype == ETHERTYPE_ARP) {
        handleArp(frame, len);
        return 0;
    }
    if (ethertype != ETHERTYPE_IPV4) {
        if (ethertype == ETHERTYPE_VLAN) {
            // VLAN tagged frames are out of scope for this transport. Report it once per
            // frame at a high debug level: a trunk port is then diagnosable instead of
            // silently dead. See docs/SOCKET_RAW.md.
            DBG_PRINT5("socket_raw: VLAN tagged frame dropped (802.1Q is not supported)\n");
        }
        return 0;
    }

    // Our unicast MAC or broadcast only.
    // Normally redundant: the socket is not put into promiscuous mode, so the NIC hardware filter
    // already drops the unicast traffic of other hosts. It becomes load bearing as soon as anything
    // else enables promiscuous mode on the interface - running tcpdump on it while debugging is
    // enough - because then foreign unicast frames do arrive and must not enter the XCP path.
    if (!isOurMac(eth->dst) && !isBroadcastMac(eth->dst))
        return 0;

    if (len < ETH_HDR_LEN + IP4_HDR_LEN)
        return 0;
    const tIp4Hdr *ip = (const tIp4Hdr *)(frame + ETH_HDR_LEN);

    if ((ip->ver_ihl >> 4) != 4)
        return 0;
    uint16_t ip_hdr_len = (uint16_t)((ip->ver_ihl & 0x0F) * 4);
    if (ip_hdr_len < IP4_HDR_LEN)
        return 0;
    uint16_t total_length = BE16(ip->total_length);
    if (total_length < ip_hdr_len)
        return 0;
    if ((uint32_t)ETH_HDR_LEN + total_length > len)
        return 0; // truncated frame

    // No reassembly: a fragmented XCP datagram is a configuration error worth reporting
    if ((BE16(ip->flags_frag) & 0x3FFF) != 0) {
        DBG_PRINT_WARNING("socket_raw: fragmented IPv4 datagram dropped, the raw transport does not reassemble\n");
        return 0;
    }

    // Addressed to us
    if (memcmp(ip->dst, sSocketRaw.local_ip, 4) != 0) {
        static const uint8_t bcast_ip[4] = {255, 255, 255, 255};
        if (memcmp(ip->dst, bcast_ip, 4) != 0)
            return 0;
    }

#ifdef OPTION_UDP_RAW_VERIFY_RX_CHECKSUM
    // Summing a correct header including its checksum field yields 0.
    // On a switched link the Ethernet FCS already covers the wire, so this mostly
    // catches our own parser bugs - which is exactly the point during bring-up.
    if (checksum16((const uint8_t *)ip, ip_hdr_len, 0) != 0) {
        DBG_PRINT_WARNING("socket_raw: IPv4 header checksum error, frame dropped\n");
        return 0;
    }
#endif

#ifdef OPTION_UDP_RAW_ENABLE_ICMP_ECHO
    if (ip->protocol == IP_PROTO_ICMP) {
        handleIcmp(frame, len, ip, ip_hdr_len);
        return 0;
    }
#endif

    if (ip->protocol != IP_PROTO_UDP)
        return 0;

    if (total_length < ip_hdr_len + UDP_HDR_LEN)
        return 0;
    const tUdpHdr *udp = (const tUdpHdr *)(frame + ETH_HDR_LEN + ip_hdr_len);

    // Our port - on a busy link almost every remaining frame dies here
    if (BE16(udp->dst_port) != sSocketRaw.local_port)
        return 0;

    uint16_t udp_len = BE16(udp->length);
    if (udp_len < UDP_HDR_LEN)
        return 0;
    if (udp_len > (uint16_t)(total_length - ip_hdr_len))
        return 0; // inconsistent with the IPv4 total length
    uint16_t payload_len = (uint16_t)(udp_len - UDP_HDR_LEN);

    // Never truncate: a truncated XCP message fails the dlc check in xcpethtl.c and
    // surfaces as a confusing "Corrupt message received!"
    if (payload_len > bufferSize) {
        DBG_PRINTF_WARNING("socket_raw: UDP payload of %u bytes exceeds the receive buffer of %u bytes, frame dropped\n", payload_len, bufferSize);
        return 0;
    }

    memcpy(buffer, (const uint8_t *)udp + UDP_HDR_LEN, payload_len);
    if (srcAddr != NULL)
        memcpy(srcAddr, ip->src, 4);
    if (srcPort != NULL)
        *srcPort = BE16(udp->src_port);

    // Learn the peer - only from an accepted datagram, never from ARP or a broadcast
    if (!isBroadcastMac(eth->dst)) {
        memcpy(sSocketRaw.peer_mac, eth->src, 6);
        sSocketRaw.peer_mac_valid = true;
    }

    return (int16_t)payload_len;
}

//-------------------------------------------------------------------------------------------------------
// Socket API

bool socketStartup(void) {
    memset(&sSocketRaw, 0, sizeof(sSocketRaw));
    sLastError = SOCKET_ERROR_NONE;
    return true;
}

void socketCleanup(void) {}

// Select the Ethernet interface used by the raw transport.
// The string is backend specific and opaque here (Linux: interface name such as "eth0").
// Must be called before XcpEthServerInit(), defaults to OPTION_UDP_RAW_IFNAME.
void socketRawSetInterface(const char *config) { sInterfaceConfig = config; }

// Get the local MAC address, as reported by the Ethernet HAL in socketOpen()
bool socketRawGetLocalMac(SOCKET_HANDLE socket, uint8_t *mac) {
    if (socket == INVALID_SOCKET_HANDLE || mac == NULL || !socket->is_open)
        return false;
    memcpy(mac, socket->local_mac, 6);
    return true;
}

bool socketOpen(SOCKET_HANDLE *socketp, uint16_t flags) {

    assert(socketp != NULL);
    *socketp = INVALID_SOCKET_HANDLE;

    if ((flags & SOCKET_MODE_TCP) != 0) {
        DBG_PRINT_ERROR("socketOpen: the raw Ethernet transport does not support TCP\n");
        return false;
    }
    if (sSocketRaw.is_open) {
        DBG_PRINT_ERROR("socketOpen: the raw Ethernet transport supports one socket only\n");
        return false;
    }

    memset(&sSocketRaw, 0, sizeof(sSocketRaw));
    sSocketRaw.recv_timeout_ms = 0; // infinite until socketSetTimeout()

    if (!eth_hal_open(sInterfaceConfig, &sSocketRaw.hal)) {
        sLastError = SOCKET_ERROR_HAL;
        return false;
    }
    if (!eth_hal_get_mac(sSocketRaw.hal, sSocketRaw.local_mac)) {
        DBG_PRINT_ERROR("socketOpen: could not read the MAC address from the Ethernet HAL\n");
        eth_hal_close(sSocketRaw.hal);
        sSocketRaw.hal = NULL;
        sLastError = SOCKET_ERROR_HAL;
        return false;
    }
    // A multicast or all zero MAC means the HAL did not report a usable address
    if ((sSocketRaw.local_mac[0] & 0x01) != 0) {
        DBG_PRINT_ERROR("socketOpen: the Ethernet HAL reported a multicast MAC address\n");
        eth_hal_close(sSocketRaw.hal);
        sSocketRaw.hal = NULL;
        sLastError = SOCKET_ERROR_HAL;
        return false;
    }
    static const uint8_t zero_mac[6] = {0, 0, 0, 0, 0, 0};
    if (memcmp(sSocketRaw.local_mac, zero_mac, 6) == 0) {
        DBG_PRINT_ERROR("socketOpen: the Ethernet HAL reported an all zero MAC address\n");
        eth_hal_close(sSocketRaw.hal);
        sSocketRaw.hal = NULL;
        sLastError = SOCKET_ERROR_HAL;
        return false;
    }

    mutexInit(&sSocketRaw.tx_mutex, false, 1000);
    sSocketRaw.is_open = true;
    *socketp = &sSocketRaw;
    return true;
}

// Bind to the local IPv4 address and UDP port.
// There is no DHCP and no IP stack, so the address must be a concrete unicast address:
// the application passes it to XcpEthServerInit(), from where it reaches this function.
bool socketBind(SOCKET_HANDLE socket, const uint8_t *addr, uint16_t port) {

    assert(socket != INVALID_SOCKET_HANDLE);

    if (!isValidLocalIp(addr)) {
        DBG_PRINTF_ERROR("socketBind: the raw Ethernet transport needs a concrete local IPv4 address, got %u.%u.%u.%u.\n"
                         "  There is no IP stack and no DHCP: pass the address of this target to XcpEthServerInit().\n",
                         addr != NULL ? addr[0] : 0, addr != NULL ? addr[1] : 0, addr != NULL ? addr[2] : 0, addr != NULL ? addr[3] : 0);
        return false;
    }

    memcpy(socket->local_ip, addr, 4);
    socket->local_port = port;
    socket->is_bound = true;

    DBG_PRINTF3("  Raw Ethernet transport bound to %u.%u.%u.%u:%u, MAC=%02X:%02X:%02X:%02X:%02X:%02X\n", addr[0], addr[1], addr[2], addr[3], port, socket->local_mac[0],
                socket->local_mac[1], socket->local_mac[2], socket->local_mac[3], socket->local_mac[4], socket->local_mac[5]);

#ifdef OPTION_UDP_RAW_GRATUITOUS_ARP
    sendGratuitousArp();
#endif

    return true;
}

bool socketSetTimeout(SOCKET_HANDLE socket, uint32_t timeoutMs) {
    assert(socket != INVALID_SOCKET_HANDLE);
    socket->recv_timeout_ms = timeoutMs; // 0 = infinite
    return true;
}

bool socketShutdown(SOCKET_HANDLE socket) {
    if (socket == INVALID_SOCKET_HANDLE)
        return true;
    socket->shutdown_requested = true;
    eth_hal_wakeup(socket->hal); // unblock a receive in progress
    return true;
}

bool socketClose(SOCKET_HANDLE *socketp) {
    assert(socketp != NULL);
    SOCKET_HANDLE socket = *socketp;
    *socketp = INVALID_SOCKET_HANDLE;
    if (socket == INVALID_SOCKET_HANDLE || !socket->is_open)
        return true;
    socket->is_open = false;
    if (socket->hal != NULL) {
        eth_hal_close(socket->hal);
        socket->hal = NULL;
    }
    mutexDestroy(&socket->tx_mutex);
    return true;
}

// Receive one XCP datagram, blocking with the timeout set by socketSetTimeout().
//
// A raw socket sees every frame on the wire, not just ours. Returning 0 for each
// filtered frame would make the caller run its background tasks once per foreign
// frame, so this loops internally instead. The deadline is absolute and computed
// once on entry: filtered traffic consumes the timeout budget but never extends it
// and never causes an early return, so the blocking time per call stays bounded by
// what the caller asked for.
//
// Return values:  > 0  bytes received
//                == 0  timeout, no data - the caller does background work and loops
//                 < 0  socket closed or error - the caller exits its receive loop
int16_t socketRecvFrom(SOCKET_HANDLE socket, uint8_t *buffer, uint16_t bufferSize, uint8_t *srcAddr, uint16_t *srcPort, uint64_t *time) {

    assert(socket != INVALID_SOCKET_HANDLE);
    assert(buffer != NULL);

    // Cap on a single HAL wait, so shutdown is noticed even with an infinite timeout
    const uint32_t max_slice_ms = 100;

    bool infinite = (socket->recv_timeout_ms == 0);
    uint64_t deadline_ns = infinite ? 0 : clockGetMonotonicNs() + (uint64_t)socket->recv_timeout_ms * 1000000ULL;

    for (;;) {

        if (socket->shutdown_requested || !socket->is_open) {
            sLastError = SOCKET_ERROR_BADF;
            return -1;
        }

        uint32_t slice_ms = max_slice_ms;
        if (!infinite) {
            uint64_t now_ns = clockGetMonotonicNs();
            if (now_ns >= deadline_ns) {
                sLastError = SOCKET_ERROR_TIMEDOUT;
                return 0; // timeout
            }
            uint64_t remaining_ms = (deadline_ns - now_ns) / 1000000ULL;
            if (remaining_ms < slice_ms)
                slice_ms = (uint32_t)remaining_ms;
            if (slice_ms == 0)
                slice_ms = 1; // do not busy poll on the last fraction of a millisecond
        }

        int16_t n = eth_hal_recv(socket->hal, RX_FRAME, RAW_MAX_FRAME, slice_ms);
        if (n < 0) {
            sLastError = SOCKET_ERROR_HAL;
            return -1;
        }
        if (n == 0)
            continue; // slice expired or frame suppressed by the HAL, re-check the deadline

        int16_t r = handleFrame(RX_FRAME, (uint16_t)n, buffer, bufferSize, srcAddr, srcPort);
        if (r > 0) {
            if (time != NULL)
                *time = clockGet();
            return r;
        }
        // Not for us, or consumed by the ARP/ICMP handlers - keep waiting for our datagram
    }
}

// Build the Ethernet/IPv4/UDP header for a payload of payload_len bytes into hdr[0..41].
// Precondition: tx_mutex held - ip_ident is incremented and peer_mac is read here.
// If the UDP checksum is computed, the payload must already be contiguous behind the header.
static void buildFrameHeader(struct socket_raw *socket, uint8_t *hdr, uint16_t payload_len, const uint8_t *addr, uint16_t port) {

    tEthHdr *eth = (tEthHdr *)hdr;
    memcpy(eth->dst, socket->peer_mac, 6);
    memcpy(eth->src, socket->local_mac, 6);
    eth->ethertype = BE16(ETHERTYPE_IPV4);

    tIp4Hdr *ip = (tIp4Hdr *)(hdr + ETH_HDR_LEN);
    memset(ip, 0, IP4_HDR_LEN);
    ip->ver_ihl = 0x45;
    ip->total_length = BE16((uint16_t)(IP4_HDR_LEN + UDP_HDR_LEN + payload_len));
    ip->ident = BE16(socket->ip_ident);
    socket->ip_ident++;
    ip->flags_frag = BE16(0x4000); // DF, never fragment
    ip->ttl = 64;
    ip->protocol = IP_PROTO_UDP;
    memcpy(ip->src, socket->local_ip, 4);
    memcpy(ip->dst, addr, 4);
    ip->checksum = BE16(ipHeaderChecksum(ip));

    tUdpHdr *udp = (tUdpHdr *)(hdr + ETH_HDR_LEN + IP4_HDR_LEN);
    udp->src_port = BE16(socket->local_port);
    udp->dst_port = BE16(port);
    udp->length = BE16((uint16_t)(UDP_HDR_LEN + payload_len));
    udp->checksum = 0;
#ifdef OPTION_UDP_RAW_UDP_CHECKSUM_COMPUTE
    udp->checksum = BE16(udpChecksum(ip, (const uint8_t *)udp, (uint16_t)(UDP_HDR_LEN + payload_len)));
#endif
    // OPTION_UDP_RAW_UDP_CHECKSUM_ZERO: 0 is legal for IPv4 and means "no checksum" (RFC 768)
    // OPTION_UDP_RAW_UDP_CHECKSUM_HW:   0 as well, the EMAC inserts the checksum
}

// Common checks for both send paths, returns false and sets sLastError when the send must not happen
static bool checkSend(SOCKET_HANDLE socket, uint16_t bufferSize, int16_t *result) {

    if (!socket->is_open) {
        sLastError = SOCKET_ERROR_BADF;
        *result = 0; // closed socket
        return false;
    }
    if (bufferSize == 0 || bufferSize > XCPTL_MAX_SEGMENT_SIZE) {
        DBG_PRINTF_ERROR("socketSendTo: payload of %u bytes exceeds the maximum segment size of %u\n", bufferSize, (uint16_t)XCPTL_MAX_SEGMENT_SIZE);
        sLastError = SOCKET_ERROR_TOOBIG;
        *result = -1;
        return false;
    }
    // The peer MAC is learned from the received datagram, and the transport layer only
    // sends after it has received something, so this cannot normally happen
    if (!socket->peer_mac_valid) {
        DBG_PRINT_ERROR("socketSendTo: peer MAC unknown, nothing has been received yet\n");
        sLastError = SOCKET_ERROR_NOPEER;
        *result = -1;
        return false;
    }
    return true;
}

// Map an eth_hal_send result to the socket API return value
static int16_t mapSendResult(int16_t r, uint16_t bufferSize) {
    if (r < 0) {
        // The HAL reports a frame too large for the link separately: like the socket transport
        // with IP_MTU_DISCOVER, this surfaces as a distinct error rather than silent truncation.
        // There is no IPv4 fragmentation here, so this is always a configuration problem.
        if (r == ETH_HAL_ERROR_SIZE) {
            sLastError = SOCKET_ERROR_MSGSIZE;
            DBG_PRINTF_ERROR("socketSendTo: segment of %u bytes does not fit into one Ethernet frame on this link.\n"
                             "  Reduce OPTION_MTU (currently %u, giving XCPTL_MAX_SEGMENT_SIZE=%u), see the interface MTU reported above.\n",
                             bufferSize, (unsigned)OPTION_MTU, (unsigned)XCPTL_MAX_SEGMENT_SIZE);
        } else {
            sLastError = SOCKET_ERROR_HAL;
        }
        return -1;
    }
    return (int16_t)bufferSize; // the payload size, as the transport layer expects
}

// Send one UDP datagram to addr:port.
// The payload is copied into a frame buffer behind the header. Used for command responses, which
// are built on the stack and therefore have no headroom in front of them.
// Returns: bytes sent (the PAYLOAD size, not the frame size - XcpEthTlSend compares the result
//          against the payload size), 0 on closed socket, -1 on error
int16_t socketSendTo(SOCKET_HANDLE socket, const uint8_t *buffer, uint16_t bufferSize, const uint8_t *addr, uint16_t port, uint64_t *time) {

    assert(socket != INVALID_SOCKET_HANDLE);
    assert(buffer != NULL);
    assert(addr != NULL);

    int16_t early;
    if (!checkSend(socket, bufferSize, &early))
        return early;

    static uint8_t tx_frame[RAW_MAX_FRAME];

    mutexLock(&socket->tx_mutex);
    memcpy(tx_frame + RAW_HDR_LEN, buffer, bufferSize);
    buildFrameHeader(socket, tx_frame, bufferSize, addr, port);
    if (time != NULL)
        *time = clockGet();
    int16_t r = eth_hal_send(socket->hal, tx_frame, (uint16_t)(RAW_HDR_LEN + bufferSize));
    mutexUnlock(&socket->tx_mutex);

    return mapSendResult(r, bufferSize);
}

#if XCPTL_TX_HEADROOM > 0
// Send one UDP datagram from a buffer which has XCPTL_TX_HEADROOM writable bytes in front
// of it, so the header is written in place and the payload is not copied at all.
// Used for the DAQ transmit path, where the buffer is a transmit queue segment.
// Returns: as socketSendTo
int16_t socketSendToReserved(SOCKET_HANDLE socket, const uint8_t *buffer, uint16_t bufferSize, const uint8_t *addr, uint16_t port, uint64_t *time) {

    assert(socket != INVALID_SOCKET_HANDLE);
    assert(buffer != NULL);
    assert(addr != NULL);

    int16_t early;
    if (!checkSend(socket, bufferSize, &early))
        return early;

    // The caller guarantees the reservation, see the contract in sockets.h. The header is written
    // right justified into it, which is why the reservation may be larger than RAW_HDR_LEN.
    uint8_t *frame = (uint8_t *)(uintptr_t)buffer - RAW_HDR_LEN;

    mutexLock(&socket->tx_mutex);
    buildFrameHeader(socket, frame, bufferSize, addr, port);
    if (time != NULL)
        *time = clockGet();
    int16_t r = eth_hal_send(socket->hal, frame, (uint16_t)(RAW_HDR_LEN + bufferSize));
    mutexUnlock(&socket->tx_mutex);

    return mapSendResult(r, bufferSize);
}
#endif // XCPTL_TX_HEADROOM > 0

#endif // OPTION_ENABLE_UDP_RAW
