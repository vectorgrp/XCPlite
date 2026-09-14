/*----------------------------------------------------------------------------
| File:
|   cmp_transport_udp.c
|
| Description:
|   CMP over UDP (6.4.2). One ordinary AF_INET/SOCK_DGRAM socket: the kernel does the
|   outer IPv4/UDP and the ARP for the path to the Data Sink, so this needs no AF_PACKET,
|   no CAP_NET_RAW and no root.
|
|   Do not confuse the two UDP layers. The INNER Ethernet/IPv4/UDP frame is built by
|   xcplib's raw transport and is what the Data Sink decodes out of the CMP payload - it
|   is the traffic of the emulated ECU. The OUTER UDP datagram implemented here is the
|   measurement network between capture module and Data Sink.
|
| Code released into public domain, no attribution required
 ----------------------------------------------------------------------------*/

#include <arpa/inet.h>  // for inet_pton, inet_ntop, htons
#include <errno.h>      // for errno
#include <fcntl.h>      // for fcntl, O_NONBLOCK
#include <netinet/in.h> // for sockaddr_in
#include <poll.h>       // for poll
#include <stdio.h>      // for printf, snprintf
#include <stdlib.h>     // for malloc, free
#include <string.h>     // for memset, memcpy, strerror
#include <sys/socket.h> // for socket, bind, recvfrom, sendto
#include <unistd.h>     // for close, read, write

#include "cmp_transport.h"

// IPv4 header + UDP header. Subtracted from the path MTU to get the largest CMP message
// that fits into one un-fragmented datagram (6.4.2).
#define IP4_UDP_HDR_LEN 28

struct cmp_transport {
    int fd;        // the UDP socket
    int wakeup_rd; // self pipe, portable equivalent of an eventfd
    int wakeup_wr;
    uint16_t local_port;
    uint16_t max_message; // largest CMP message this path can carry

    bool sink_known;
    bool sink_learned; // adopted from a received message rather than configured
    struct sockaddr_in sink;
};

//-------------------------------------------------------------------------------

static bool setNonBlocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    return flags >= 0 && fcntl(fd, F_SETFL, flags | O_NONBLOCK) >= 0;
}

static void formatAddr(const struct sockaddr_in *addr, char *ip, size_t ip_size, uint16_t *port) {
    if (ip != NULL && ip_size > 0) {
        if (inet_ntop(AF_INET, &addr->sin_addr, ip, (socklen_t)ip_size) == NULL) {
            ip[0] = 0;
        }
    }
    if (port != NULL) {
        *port = ntohs(addr->sin_port);
    }
}

//-------------------------------------------------------------------------------

bool cmpTransportOpen(const tCmpTransportConfig *config, tCmpTransport **transportp) {

    if (config == NULL || transportp == NULL) {
        return false;
    }
    *transportp = NULL;

    if (config->outer_mtu <= IP4_UDP_HDR_LEN) {
        printf("ERROR: cmpTransportOpen: outer MTU %u is too small\n", config->outer_mtu);
        return false;
    }

    tCmpTransport *t = (tCmpTransport *)malloc(sizeof(tCmpTransport));
    if (t == NULL) {
        printf("ERROR: cmpTransportOpen: out of memory\n");
        return false;
    }
    memset(t, 0, sizeof(*t));
    t->fd = -1;
    t->wakeup_rd = -1;
    t->wakeup_wr = -1;
    t->local_port = config->local_port;
    t->max_message = (uint16_t)(config->outer_mtu - IP4_UDP_HDR_LEN);

    if (config->sink_ip != NULL && config->sink_ip[0] != 0) {
        t->sink.sin_family = AF_INET;
        t->sink.sin_port = htons(config->sink_port);
        if (inet_pton(AF_INET, config->sink_ip, &t->sink.sin_addr) != 1) {
            printf("ERROR: cmpTransportOpen: '%s' is not a valid IPv4 address\n", config->sink_ip);
            goto error;
        }
        if (config->sink_port == 0) {
            printf("ERROR: cmpTransportOpen: a Data Sink port is required with a Data Sink address\n");
            goto error;
        }
        t->sink_known = true;
    }

    t->fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (t->fd < 0) {
        printf("ERROR: cmpTransportOpen: socket failed (errno=%d, %s)\n", errno, strerror(errno));
        goto error;
    }

    int one = 1;
    if (setsockopt(t->fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)) < 0) {
        printf("WARNING: cmpTransportOpen: SO_REUSEADDR failed (errno=%d, %s)\n", errno, strerror(errno));
    }

    struct sockaddr_in local;
    memset(&local, 0, sizeof(local));
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = htonl(INADDR_ANY);
    local.sin_port = htons(config->local_port);
    if (bind(t->fd, (struct sockaddr *)&local, sizeof(local)) < 0) {
        printf("ERROR: cmpTransportOpen: bind to UDP port %u failed (errno=%d, %s)\n", config->local_port, errno, strerror(errno));
        goto error;
    }
    if (config->local_port == 0) { // ephemeral, find out which one we got
        socklen_t len = sizeof(local);
        if (getsockname(t->fd, (struct sockaddr *)&local, &len) == 0) {
            t->local_port = ntohs(local.sin_port);
        }
    }

    // Self pipe for eth_hal_wakeup(). A pipe rather than an eventfd so this file stays
    // portable; the write end is only ever poked with a single byte.
    int pipefd[2];
    if (pipe(pipefd) < 0) {
        printf("ERROR: cmpTransportOpen: pipe failed (errno=%d, %s)\n", errno, strerror(errno));
        goto error;
    }
    t->wakeup_rd = pipefd[0];
    t->wakeup_wr = pipefd[1];
    if (!setNonBlocking(t->wakeup_rd) || !setNonBlocking(t->wakeup_wr)) {
        printf("WARNING: cmpTransportOpen: could not make the wakeup pipe non blocking\n");
    }

    *transportp = t;
    return true;

error:
    cmpTransportClose(t);
    return false;
}

void cmpTransportClose(tCmpTransport *t) {
    if (t == NULL) {
        return;
    }
    if (t->fd >= 0) {
        close(t->fd);
    }
    if (t->wakeup_rd >= 0) {
        close(t->wakeup_rd);
    }
    if (t->wakeup_wr >= 0) {
        close(t->wakeup_wr);
    }
    free(t);
}

//-------------------------------------------------------------------------------

int32_t cmpTransportSend(tCmpTransport *t, const uint8_t *msg, uint16_t len) {

    if (t == NULL || msg == NULL) {
        return -1;
    }
    if (!t->sink_known) {
        // Nothing to send to yet. Not an error: with a learned sink address the capture
        // direction simply stays quiet until the Data Sink first speaks to us.
        return 0;
    }

    for (;;) {
        ssize_t n = sendto(t->fd, msg, len, 0, (struct sockaddr *)&t->sink, sizeof(t->sink));
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EMSGSIZE) {
                printf("ERROR: cmpTransportSend: CMP message of %u bytes exceeds the path MTU\n", len);
                return -1;
            }
            printf("ERROR: cmpTransportSend: sendto failed (errno=%d, %s)\n", errno, strerror(errno));
            return -1;
        }
        if (n != (ssize_t)len) {
            printf("ERROR: cmpTransportSend: partial send %zd of %u bytes\n", n, len);
            return -1;
        }
        return (int32_t)len;
    }
}

int32_t cmpTransportRecv(tCmpTransport *t, uint8_t *msg, uint16_t max_len, uint32_t timeout_ms) {

    if (t == NULL || msg == NULL) {
        return -1;
    }

    struct pollfd pfd[2];
    pfd[0].fd = t->fd;
    pfd[0].events = POLLIN;
    pfd[0].revents = 0;
    pfd[1].fd = t->wakeup_rd;
    pfd[1].events = POLLIN;
    pfd[1].revents = 0;

    int r = poll(pfd, 2, (int)timeout_ms);
    if (r < 0) {
        if (errno == EINTR) {
            return 0; // the caller re-evaluates its own deadline
        }
        printf("ERROR: cmpTransportRecv: poll failed (errno=%d, %s)\n", errno, strerror(errno));
        return -1;
    }
    if (r == 0) {
        return 0; // timeout
    }

    if ((pfd[1].revents & POLLIN) != 0) { // wakeup requested, drain and report no message
        uint8_t drain[64];
        while (read(t->wakeup_rd, drain, sizeof(drain)) > 0) {
        }
        return 0;
    }
    if ((pfd[0].revents & POLLIN) == 0) {
        return 0;
    }

    struct sockaddr_in from;
    socklen_t fromlen = sizeof(from);
    memset(&from, 0, sizeof(from));
    ssize_t n = recvfrom(t->fd, msg, max_len, 0, (struct sockaddr *)&from, &fromlen);
    if (n < 0) {
        if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
            return 0;
        }
        printf("ERROR: cmpTransportRecv: recvfrom failed (errno=%d, %s)\n", errno, strerror(errno));
        return -1;
    }

    // Learn where the Data Sink lives, if it was not configured. Transmission requests are
    // sent unicast to the Capture Module (7.2.2), so the source of one is a usable reply
    // address - the same trick socket_raw.c uses to learn the peer MAC.
    if (!t->sink_known && from.sin_family == AF_INET) {
        t->sink = from;
        t->sink_known = true;
        t->sink_learned = true;
        char ip[16] = {0};
        uint16_t port = 0;
        formatAddr(&t->sink, ip, sizeof(ip), &port);
        printf("  CMP: Data Sink learned as %s:%u\n", ip, port);
    }

    return (int32_t)n;
}

void cmpTransportWakeup(tCmpTransport *t) {
    if (t == NULL || t->wakeup_wr < 0) {
        return;
    }
    uint8_t one = 1;
    ssize_t nw = write(t->wakeup_wr, &one, 1);
    (void)nw; // a full pipe already means a wakeup is pending
}

//-------------------------------------------------------------------------------

uint16_t cmpTransportMaxMessage(const tCmpTransport *t) { return (t != NULL) ? t->max_message : 0; }

bool cmpTransportGetSink(const tCmpTransport *t, char *ip, size_t ip_size, uint16_t *port) {
    if (t == NULL || !t->sink_known) {
        return false;
    }
    formatAddr(&t->sink, ip, ip_size, port);
    return true;
}

bool cmpTransportGetLocal(const tCmpTransport *t, char *ip, size_t ip_size, uint16_t *port) {

    if (t == NULL) {
        return false;
    }
    if (port != NULL) {
        *port = t->local_port;
    }
    if (ip == NULL || ip_size == 0) {
        return true;
    }
    ip[0] = 0;

    // We bind to INADDR_ANY, so there is no single local address to report. Ask the routing
    // table which source address would be used towards the Data Sink: connecting a throwaway
    // datagram socket sends nothing, it only fixes the source. Without a known sink there is
    // nothing sensible to report and CmpListeningIP stays empty.
    if (!t->sink_known) {
        return false;
    }
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0) {
        return false;
    }
    bool ok = false;
    if (connect(s, (const struct sockaddr *)&t->sink, sizeof(t->sink)) == 0) {
        struct sockaddr_in local;
        socklen_t len = sizeof(local);
        memset(&local, 0, sizeof(local));
        if (getsockname(s, (struct sockaddr *)&local, &len) == 0) {
            formatAddr(&local, ip, ip_size, NULL);
            ok = ip[0] != 0;
        }
    }
    close(s);
    return ok;
}
