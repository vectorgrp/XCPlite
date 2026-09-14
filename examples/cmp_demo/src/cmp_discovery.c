/*----------------------------------------------------------------------------
| File:
|   cmp_discovery.c
|
| Description:
|   CMP_CM_DISCOVERY responder (12.1.1). See cmp_discovery.h.
|
| Code released into public domain, no attribution required
 ----------------------------------------------------------------------------*/

#include <arpa/inet.h>
#include <errno.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#if defined(__linux__)
#include <netpacket/packet.h> // struct sockaddr_ll, for the interface MAC
#else
#include <net/if_dl.h> // struct sockaddr_dl, LLADDR
#endif

#include "cmp_discovery.h"

//-------------------------------------------------------------------------------
// Wire format, 12.1.1
//
// "All command fields are encoded in little endian byte order" (12.1), so the XCP header
// length, the ports and the string lengths are little endian. The address and MAC fields
// are byte arrays in "descending byte significance", i.e. 192.168.0.1 is 192,168,0,1.

#define XCP_HEADER_LEN 4 // A_UINT16 length + A_UINT16 reserved
#define XCP_CMD_TL 0xF2  // CC_TRANSPORT_LAYER_CMD
#define XCP_SUB_DISCOVERY 0x10
#define XCP_PID_RESPONSE 0xFF

#define REQUEST_LEN 0x15  // 21 bytes after the XCP header, fixed by Table 78
#define RESPONSE_FIXED 47 // bytes 0..46 of the response, before the two strings

#define DISCOVERY_BUF_MAX 512

static int sFd = -1;
static tCmpDiscoveryConfig sConfig;
static uint64_t sCount = 0;

//-------------------------------------------------------------------------------
// Little endian helpers. The demo already assumes a little endian host elsewhere, but
// these keep the wire format explicit rather than punning a struct over the buffer.

static uint16_t get_u16_le(const uint8_t *p) { return (uint16_t)(p[0] | ((uint16_t)p[1] << 8)); }

static void put_u16_le(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)(v >> 8);
}

// Append an A_UINT16 length followed by a zero terminated A_UTF8 string padded with 0x00
// to a multiple of two bytes.
//
// The length counts the PADDED bytes, not the characters: 12.1.1's own example encodes
// "Dev1" as 44 65 76 31 00 00, six bytes, and the empty string as 00 00, two bytes. The
// offset column of Table 79 disagrees with itself by one about where the next field
// starts; the "N is length before" wording used for the identical construction in the
// status message payload (8.2.1) is what settles it. See the repository docs/XCP_DISCOVERY.md.
static size_t appendString(uint8_t *out, size_t out_max, size_t off, const char *s) {
    if (s == NULL) {
        s = "";
    }
    size_t len = strlen(s) + 1; // include the zero termination
    if (len % 2 != 0) {
        len++; // pad to 16 bit
    }
    if (off + 2 + len > out_max) {
        return 0; // does not fit
    }
    put_u16_le(out + off, (uint16_t)len);
    off += 2;
    memset(out + off, 0, len);
    memcpy(out + off, s, strlen(s));
    return off + len;
}

//-------------------------------------------------------------------------------
// Local address, prefix length and MAC
//
// The CMP transport binds INADDR_ANY, so the address a Data Sink should use is not known
// until somebody asks. Connecting a scratch UDP socket to the requester and reading back
// the local end is the portable way to ask the routing table "which of my addresses would
// you use to reach this peer" - no packet is sent by connect() on a datagram socket.

static bool localAddrFor(const struct in_addr *peer, struct in_addr *local) {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        return false;
    }
    struct sockaddr_in to;
    memset(&to, 0, sizeof(to));
    to.sin_family = AF_INET;
    to.sin_port = htons(CMP_DISCOVERY_PORT);
    to.sin_addr = *peer;
    bool ok = false;
    if (connect(fd, (struct sockaddr *)&to, sizeof(to)) == 0) {
        struct sockaddr_in me;
        socklen_t len = sizeof(me);
        if (getsockname(fd, (struct sockaddr *)&me, &len) == 0) {
            *local = me.sin_addr;
            ok = true;
        }
    }
    close(fd);
    return ok;
}

// Prefix length and MAC of the interface that owns local_ip. Both are best effort: the
// prefix is informational and 12.1.1 makes the gateway explicitly optional.
static void interfaceInfoFor(const struct in_addr *local_ip, uint8_t *prefix_len, uint8_t *mac) {
    *prefix_len = 0;
    memset(mac, 0, 6);

    struct ifaddrs *ifa_list = NULL;
    if (getifaddrs(&ifa_list) != 0) {
        return;
    }

    // First pass: find the interface carrying our address and take its netmask
    char ifname[IF_NAMESIZE] = {0};
    for (struct ifaddrs *ifa = ifa_list; ifa != NULL; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == NULL || ifa->ifa_addr->sa_family != AF_INET || ifa->ifa_netmask == NULL) {
            continue;
        }
        const struct sockaddr_in *a = (const struct sockaddr_in *)(void *)ifa->ifa_addr;
        if (a->sin_addr.s_addr != local_ip->s_addr) {
            continue;
        }
        const struct sockaddr_in *m = (const struct sockaddr_in *)(void *)ifa->ifa_netmask;
        uint32_t mask = ntohl(m->sin_addr.s_addr);
        while (mask & 0x80000000u) { // count the leading ones
            (*prefix_len)++;
            mask <<= 1;
        }
        snprintf(ifname, sizeof(ifname), "%s", ifa->ifa_name);
        break;
    }

    // Second pass: the link layer address of that same interface
    if (ifname[0] != '\0') {
        for (struct ifaddrs *ifa = ifa_list; ifa != NULL; ifa = ifa->ifa_next) {
            if (ifa->ifa_addr == NULL || strcmp(ifa->ifa_name, ifname) != 0) {
                continue;
            }
#if defined(__linux__)
            if (ifa->ifa_addr->sa_family == AF_PACKET) {
                const struct sockaddr_ll *ll = (const struct sockaddr_ll *)(void *)ifa->ifa_addr;
                if (ll->sll_halen == 6) {
                    memcpy(mac, ll->sll_addr, 6);
                    break;
                }
            }
#else
            if (ifa->ifa_addr->sa_family == AF_LINK) {
                const struct sockaddr_dl *dl = (const struct sockaddr_dl *)(void *)ifa->ifa_addr;
                if (dl->sdl_alen == 6) {
                    memcpy(mac, LLADDR(dl), 6);
                    break;
                }
            }
#endif
        }
    }

    freeifaddrs(ifa_list);
}

//-------------------------------------------------------------------------------

bool cmpDiscoveryStart(const tCmpDiscoveryConfig *config) {

    if (config == NULL) {
        return false;
    }
    sConfig = *config;

    sFd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sFd < 0) {
        printf("ERROR: cmpDiscoveryStart: socket failed (errno=%d, %s)\n", errno, strerror(errno));
        return false;
    }

    int one = 1;
    if (setsockopt(sFd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)) < 0) {
        printf("WARNING: cmpDiscoveryStart: SO_REUSEADDR failed (errno=%d, %s)\n", errno, strerror(errno));
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(CMP_DISCOVERY_PORT);
    if (bind(sFd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        printf("ERROR: cmpDiscoveryStart: bind to UDP %u failed (errno=%d, %s)\n", CMP_DISCOVERY_PORT, errno, strerror(errno));
        close(sFd);
        sFd = -1;
        return false;
    }

    // Join the group on EVERY multicast capable interface, not with imr_interface
    // INADDR_ANY. "Any" does not mean "all": it lets the stack pick one interface from the
    // routing table, and a capture module does not know which interface a Data Sink will
    // appear on. On macOS an INADDR_ANY join receives nothing at all, and a join on a LAN
    // interface does not see this host's own traffic - only a loopback join does, which is
    // why loopback is deliberately included and is what makes a same-host test work.
    int joined = 0;
    struct ifaddrs *ifa_list = NULL;
    if (getifaddrs(&ifa_list) == 0) {
        for (struct ifaddrs *ifa = ifa_list; ifa != NULL; ifa = ifa->ifa_next) {
            if (ifa->ifa_addr == NULL || ifa->ifa_addr->sa_family != AF_INET) {
                continue;
            }
            if ((ifa->ifa_flags & IFF_UP) == 0 || (ifa->ifa_flags & IFF_MULTICAST) == 0) {
                continue;
            }
            const struct sockaddr_in *a = (const struct sockaddr_in *)(void *)ifa->ifa_addr;
            struct ip_mreq mreq;
            memset(&mreq, 0, sizeof(mreq));
            mreq.imr_multiaddr.s_addr = inet_addr(CMP_DISCOVERY_GROUP);
            mreq.imr_interface = a->sin_addr;
            if (setsockopt(sFd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) == 0) {
                joined++;
            }
            // A failed join is normal: an interface may already be a member through
            // another address, or may not route multicast. Only zero joins is fatal.
        }
        freeifaddrs(ifa_list);
    }

    if (joined == 0) {
        printf("ERROR: cmpDiscoveryStart: could not join %s on any interface\n", CMP_DISCOVERY_GROUP);
        close(sFd);
        sFd = -1;
        return false;
    }

    printf("  CMP discovery: listening on %s:%u (%d interface%s), advertising HTTP port %u\n", CMP_DISCOVERY_GROUP, CMP_DISCOVERY_PORT, joined, joined == 1 ? "" : "s",
           sConfig.http_port);
    return true;
}

int cmpDiscoveryFd(void) { return sFd; }

uint64_t cmpDiscoveryCount(void) { return sCount; }

void cmpDiscoveryStop(void) {
    if (sFd >= 0) {
        close(sFd);
        sFd = -1;
    }
}

//-------------------------------------------------------------------------------

void cmpDiscoveryService(void) {

    if (sFd < 0) {
        return;
    }

    uint8_t in[DISCOVERY_BUF_MAX];
    struct sockaddr_in from;
    socklen_t from_len = sizeof(from);
    ssize_t n = recvfrom(sFd, in, sizeof(in), 0, (struct sockaddr *)&from, &from_len);
    if (n < 0) {
        return;
    }

    // Ignore anything that is not a CMP_CM_DISCOVERY request. The group and port are
    // shared with whatever else a tool multicasts, and 12.1.2 CMP_IP_ADDRESS_ASSIGNMENT
    // arrives here too - we do not implement it, so it is dropped silently.
    if ((size_t)n < XCP_HEADER_LEN + REQUEST_LEN) {
        return;
    }
    if (get_u16_le(in) != REQUEST_LEN) {
        return;
    }
    const uint8_t *cmd = in + XCP_HEADER_LEN;
    if (cmd[0] != XCP_CMD_TL || cmd[1] != XCP_SUB_DISCOVERY) {
        return;
    }
    if (cmd[20] & 0x01) {
        printf("WARNING: cmpDiscoveryService: IPv6 requested, this demo is IPv4 only\n");
        return;
    }

    // 12.1.1: "A Capture Module shall send its response to the multicast address and port
    // given in the command request." The request carries both, so the responder never has
    // to know anything about the network it sits on.
    uint16_t reply_port = get_u16_le(cmd + 2);
    struct sockaddr_in to;
    memset(&to, 0, sizeof(to));
    to.sin_family = AF_INET;
    to.sin_port = htons(reply_port);
    memcpy(&to.sin_addr.s_addr, cmd + 4, 4); // already in network byte order
    if (to.sin_addr.s_addr == 0 || reply_port == 0) {
        // Not covered by the spec. Answering the sender is more useful than dropping the
        // request, and it is what a tool that left the field empty most likely wants.
        to.sin_addr = from.sin_addr;
        if (reply_port == 0) {
            to.sin_port = from.sin_port;
        }
    }

    // Our own address, as seen from the peer that asked
    struct in_addr local;
    if (!localAddrFor(&from.sin_addr, &local)) {
        local.s_addr = 0; // 12.1.1: "shall be set to 0.0.0.0" when there is no valid address
    }
    uint8_t prefix_len = 0;
    uint8_t mac[6];
    interfaceInfoFor(&local, &prefix_len, mac);

    // Build the positive response, Table 79
    uint8_t out[DISCOVERY_BUF_MAX];
    memset(out, 0, RESPONSE_FIXED + XCP_HEADER_LEN);
    uint8_t *rsp = out + XCP_HEADER_LEN;
    rsp[0] = XCP_PID_RESPONSE;
    rsp[1] = XCP_SUB_DISCOVERY;
    memcpy(rsp + 2, &local.s_addr, 4); // 2..17, IPv4 uses the first four bytes
    rsp[18] = prefix_len;
    // 19..34 gateway: "This value is optional. In such a case the address 0.0.0.0 is
    // used." Reading the default route is not portable enough to be worth it here.
    memcpy(rsp + 35, mac, 6);
    rsp[41] = 0x00; // IP version: IPv4
    rsp[42] = 0x00; // reserved
    put_u16_le(rsp + 43, sConfig.http_port);

    size_t off = appendString(rsp, sizeof(out) - XCP_HEADER_LEN, 45, sConfig.description);
    if (off == 0) {
        printf("WARNING: cmpDiscoveryService: DeviceDescription does not fit, not answering\n");
        return;
    }
    off = appendString(rsp, sizeof(out) - XCP_HEADER_LEN, off, sConfig.serial);
    if (off == 0) {
        printf("WARNING: cmpDiscoveryService: SerialNumber does not fit, not answering\n");
        return;
    }

    // XCP header: length of everything after it, then the reserved word
    put_u16_le(out, (uint16_t)off);
    put_u16_le(out + 2, 0);

    // A multicast response has to leave through the interface the request came in on,
    // otherwise the stack sends it out the default route and the asking tool never sees
    // it. local is exactly that interface's address.
    if (IN_MULTICAST(ntohl(to.sin_addr.s_addr)) && local.s_addr != 0) {
        if (setsockopt(sFd, IPPROTO_IP, IP_MULTICAST_IF, &local, sizeof(local)) < 0) {
            printf("WARNING: cmpDiscoveryService: IP_MULTICAST_IF failed (errno=%d, %s)\n", errno, strerror(errno));
        }
    }

    if (sendto(sFd, out, XCP_HEADER_LEN + off, 0, (struct sockaddr *)&to, sizeof(to)) < 0) {
        printf("WARNING: cmpDiscoveryService: sending the response failed (errno=%d, %s)\n", errno, strerror(errno));
        return;
    }

    sCount++;

    char from_s[INET_ADDRSTRLEN] = {0};
    char to_s[INET_ADDRSTRLEN] = {0};
    char local_s[INET_ADDRSTRLEN] = {0};
    inet_ntop(AF_INET, &from.sin_addr, from_s, sizeof(from_s));
    inet_ntop(AF_INET, &to.sin_addr, to_s, sizeof(to_s));
    inet_ntop(AF_INET, &local, local_s, sizeof(local_s));
    printf("  CMP discovery: request from %s, answered to %s:%u with %s/%u, HTTP port %u\n", from_s, to_s, ntohs(to.sin_port), local_s, prefix_len, sConfig.http_port);
}
