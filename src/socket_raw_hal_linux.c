/*----------------------------------------------------------------------------
| File:
|   socket_raw_hal_linux.c
|
| Description:
|   Raw Ethernet HAL backend for Linux, using AF_PACKET (see socket_raw_hal.h)
|
|   Requires CAP_NET_RAW:
|     sudo setcap cap_net_raw+ep <binary>     (or run as root)
|
|   SOCK_RAW (not SOCK_DGRAM) because socket_raw.c builds its own Ethernet header,
|   ETH_P_ALL (not ETH_P_IP) because ARP frames must be seen as well.
|
|   A blocked eth_hal_recv() is unblocked with an eventfd rather than SO_RCVTIMEO,
|   so shutdown is immediate instead of up to one timeout slice late, and so an
|   infinite timeout stays interruptible.
|
| Code released into public domain, no attribution required
 ----------------------------------------------------------------------------*/

#include "platform.h" // for the platform defines (_LINUX) and OPTION_xxx via xcplib_cfg.h

// Not compiled when the application supplies its own backend, see OPTION_UDP_RAW_HAL_EXTERNAL
#if defined(OPTION_ENABLE_UDP_RAW) && defined(_LINUX) && !defined(OPTION_UDP_RAW_HAL_EXTERNAL)

#include <arpa/inet.h>       // for htons
#include <errno.h>           // for errno
#include <linux/if_ether.h>  // for ETH_P_ALL
#include <linux/if_packet.h> // for sockaddr_ll, PACKET_OUTGOING, PACKET_IGNORE_OUTGOING
#include <net/if.h>          // for ifreq, IFNAMSIZ
#include <poll.h>            // for poll
#include <stdlib.h>          // for malloc, free
#include <string.h>          // for memset, memcpy, strncpy, strerror
#include <sys/eventfd.h>     // for eventfd
#include <sys/ioctl.h>       // for ioctl, SIOCGIFINDEX, SIOCGIFHWADDR
#include <sys/socket.h>      // for socket, bind, setsockopt, recvfrom
#include <sys/types.h>       // for ssize_t
#include <unistd.h>          // for close, read, write

#include "assert.h"
#include "dbg_print.h"
#include "socket_raw_hal.h"

#define ETH_HDR_LEN 14 // Ethernet header, not covered by the interface MTU

struct eth_hal_ctx {
    int fd;                // AF_PACKET socket
    int wakeup_fd;         // eventfd used by eth_hal_wakeup()
    int ifindex;           // interface index
    unsigned int mtu;      // interface MTU, i.e. the largest IP packet, excluding the Ethernet header
    uint8_t mac[6];        // interface MAC address
    char ifname[IFNAMSIZ]; // interface name
};

bool eth_hal_open(const char *config, tEthHalCtx **ctxp) {

    assert(ctxp != NULL);
    *ctxp = NULL;

    const char *ifname = (config != NULL && config[0] != 0) ? config : OPTION_UDP_RAW_IFNAME;

    tEthHalCtx *ctx = (tEthHalCtx *)malloc(sizeof(tEthHalCtx));
    if (ctx == NULL) {
        DBG_PRINT_ERROR("eth_hal_open: out of memory\n");
        return false;
    }
    memset(ctx, 0, sizeof(tEthHalCtx));
    ctx->fd = -1;
    ctx->wakeup_fd = -1;
    strncpy(ctx->ifname, ifname, sizeof(ctx->ifname) - 1);

    // Raw packet socket, all EtherTypes (IPv4 and ARP are both needed)
    ctx->fd = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
    if (ctx->fd < 0) {
        if (errno == EPERM || errno == EACCES) {
            DBG_PRINTF_ERROR("eth_hal_open: AF_PACKET socket denied (errno=%d, %s).\n"
                             "  The raw Ethernet transport needs CAP_NET_RAW:\n"
                             "    sudo setcap cap_net_raw+ep <binary>\n",
                             errno, strerror(errno));
        } else {
            DBG_PRINTF_ERROR("eth_hal_open: AF_PACKET socket failed (errno=%d, %s)\n", errno, strerror(errno));
        }
        goto error;
    }

    // Interface index
    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, ctx->ifname, IFNAMSIZ - 1);
    if (ioctl(ctx->fd, SIOCGIFINDEX, &ifr) < 0) {
        DBG_PRINTF_ERROR("eth_hal_open: interface '%s' not found (errno=%d, %s)\n", ctx->ifname, errno, strerror(errno));
        goto error;
    }
    ctx->ifindex = ifr.ifr_ifindex;

    // Interface MAC address
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, ctx->ifname, IFNAMSIZ - 1);
    if (ioctl(ctx->fd, SIOCGIFHWADDR, &ifr) < 0) {
        DBG_PRINTF_ERROR("eth_hal_open: SIOCGIFHWADDR for '%s' failed (errno=%d, %s)\n", ctx->ifname, errno, strerror(errno));
        goto error;
    }
    memcpy(ctx->mac, ifr.ifr_hwaddr.sa_data, 6);

    // Interface MTU, used to explain an EMSGSIZE on send. The MTU is the largest IP packet,
    // the 14 byte Ethernet header comes on top of it.
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, ctx->ifname, IFNAMSIZ - 1);
    if (ioctl(ctx->fd, SIOCGIFMTU, &ifr) < 0) {
        DBG_PRINTF_WARNING("eth_hal_open: SIOCGIFMTU for '%s' failed (errno=%d, %s)\n", ctx->ifname, errno, strerror(errno));
        ctx->mtu = 1500; // assume standard Ethernet, only used for diagnostics
    } else {
        ctx->mtu = (unsigned int)ifr.ifr_mtu;
    }

    // Bind to this interface only
    struct sockaddr_ll sll;
    memset(&sll, 0, sizeof(sll));
    sll.sll_family = AF_PACKET;
    sll.sll_protocol = htons(ETH_P_ALL);
    sll.sll_ifindex = ctx->ifindex;
    if (bind(ctx->fd, (struct sockaddr *)&sll, sizeof(sll)) < 0) {
        DBG_PRINTF_ERROR("eth_hal_open: bind to '%s' failed (errno=%d, %s)\n", ctx->ifname, errno, strerror(errno));
        goto error;
    }

    // Do not loop our own transmitted frames back into the receive path.
    // PACKET_IGNORE_OUTGOING needs Linux >= 4.20, the PACKET_OUTGOING check in
    // eth_hal_recv() is the portable fallback and stays in place regardless.
#ifdef PACKET_IGNORE_OUTGOING
    int one = 1;
    if (setsockopt(ctx->fd, SOL_PACKET, PACKET_IGNORE_OUTGOING, &one, sizeof(one)) < 0) {
        DBG_PRINTF_WARNING("eth_hal_open: PACKET_IGNORE_OUTGOING not available (errno=%d, %s), using the sll_pkttype filter only\n", errno, strerror(errno));
    }
#endif

    // Used to unblock a receive in progress
    ctx->wakeup_fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (ctx->wakeup_fd < 0) {
        DBG_PRINTF_ERROR("eth_hal_open: eventfd failed (errno=%d, %s)\n", errno, strerror(errno));
        goto error;
    }

    DBG_PRINTF3("  Raw Ethernet HAL on %s (index %d), MAC=%02X:%02X:%02X:%02X:%02X:%02X, MTU=%u (max frame %u bytes)\n", ctx->ifname, ctx->ifindex, ctx->mac[0], ctx->mac[1],
                ctx->mac[2], ctx->mac[3], ctx->mac[4], ctx->mac[5], ctx->mtu, ctx->mtu + ETH_HDR_LEN);

    *ctxp = ctx;
    return true;

error:
    if (ctx->fd >= 0)
        close(ctx->fd);
    if (ctx->wakeup_fd >= 0)
        close(ctx->wakeup_fd);
    free(ctx);
    return false;
}

void eth_hal_close(tEthHalCtx *ctx) {
    if (ctx == NULL)
        return;
    if (ctx->fd >= 0)
        close(ctx->fd);
    if (ctx->wakeup_fd >= 0)
        close(ctx->wakeup_fd);
    free(ctx);
}

bool eth_hal_get_mac(tEthHalCtx *ctx, uint8_t *mac) {
    assert(ctx != NULL);
    assert(mac != NULL);
    memcpy(mac, ctx->mac, 6);
    return true;
}

int16_t eth_hal_send(tEthHalCtx *ctx, const uint8_t *frame, uint16_t len) {

    assert(ctx != NULL);
    assert(frame != NULL);

    for (;;) {
        ssize_t n = write(ctx->fd, frame, len);
        if (n < 0) {
            if (errno == EINTR)
                continue; // interrupted before sending, retry
            // The frame is larger than MTU + 14 for this interface. Report it separately and
            // name the interface MTU: only the HAL knows that, and it is what has to be fixed.
            if (errno == EMSGSIZE) {
                DBG_PRINTF_ERROR("eth_hal_send: frame of %u bytes is too large for interface %s (MTU %u, so at most %u bytes per frame)\n", len, ctx->ifname, ctx->mtu,
                                 ctx->mtu + ETH_HDR_LEN);
                return ETH_HAL_ERROR_SIZE;
            }
            DBG_PRINTF_ERROR("eth_hal_send: write failed (errno=%d, %s)\n", errno, strerror(errno));
            return ETH_HAL_ERROR;
        }
        if (n != (ssize_t)len) {
            DBG_PRINTF_ERROR("eth_hal_send: partial write %zd of %u bytes\n", n, len);
            return ETH_HAL_ERROR;
        }
        return (int16_t)len;
    }
}

int16_t eth_hal_recv(tEthHalCtx *ctx, uint8_t *frame, uint16_t max_len, uint32_t timeout_ms) {

    assert(ctx != NULL);
    assert(frame != NULL);

    struct pollfd pfd[2];
    pfd[0].fd = ctx->fd;
    pfd[0].events = POLLIN;
    pfd[0].revents = 0;
    pfd[1].fd = ctx->wakeup_fd;
    pfd[1].events = POLLIN;
    pfd[1].revents = 0;

    int r = poll(pfd, 2, (int)timeout_ms);
    if (r < 0) {
        if (errno == EINTR)
            return 0; // treat as timeout, the caller re-evaluates its deadline
        DBG_PRINTF_ERROR("eth_hal_recv: poll failed (errno=%d, %s)\n", errno, strerror(errno));
        return ETH_HAL_ERROR;
    }
    if (r == 0)
        return 0; // timeout

    // Wakeup requested by eth_hal_wakeup(): drain and report "no frame".
    // socket_raw.c re-checks its shutdown flag at the top of its receive loop.
    if (pfd[1].revents & POLLIN) {
        uint64_t v;
        ssize_t rd = read(ctx->wakeup_fd, &v, sizeof(v));
        (void)rd;
        return 0;
    }

    if (!(pfd[0].revents & POLLIN))
        return 0;

    struct sockaddr_ll from;
    socklen_t fromlen = sizeof(from);
    memset(&from, 0, sizeof(from));
    ssize_t n = recvfrom(ctx->fd, frame, max_len, 0, (struct sockaddr *)&from, &fromlen);
    if (n < 0) {
        if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
            return 0;
        DBG_PRINTF_ERROR("eth_hal_recv: recvfrom failed (errno=%d, %s)\n", errno, strerror(errno));
        return ETH_HAL_ERROR;
    }

    // Portable fallback for PACKET_IGNORE_OUTGOING: drop our own transmitted frames
    if (from.sll_pkttype == PACKET_OUTGOING)
        return 0;

    return (int16_t)n;
}

void eth_hal_wakeup(tEthHalCtx *ctx) {
    if (ctx == NULL || ctx->wakeup_fd < 0)
        return;
    uint64_t v = 1;
    ssize_t n = write(ctx->wakeup_fd, &v, sizeof(v));
    (void)n;
}

#endif // OPTION_ENABLE_UDP_RAW && _LINUX && !OPTION_UDP_RAW_HAL_EXTERNAL
