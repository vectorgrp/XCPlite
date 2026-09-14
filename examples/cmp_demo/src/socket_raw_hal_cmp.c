/*----------------------------------------------------------------------------
| File:
|   socket_raw_hal_cmp.c
|
| Description:
|   Ethernet HAL backend for cmp_demo, implementing src/socket_raw_hal.h of xcplib.
|
|   This file lives in the demo, NOT in the library: CMP serves testing of XCP tools
|   through capture modules, it is not an ECU developer feature, so nothing about it
|   belongs in libxcplite. The library is used as installed and unmodified. Because
|   libxcplite is a static library and this object defines all six eth_hal_* symbols,
|   the linker never pulls the built in backend out of the archive.
|
|   The demo emulates a Capture Module which tunnels one XCP ECU:
|
|     eth_hal_send   the frame xcplib built is what the capture module just captured on
|                    its interface, so it is wrapped as a Captured Data Message and sent
|                    to the Data Sink
|     eth_hal_recv   a Transmit Data Message from the Data Sink is unwrapped and its
|                    inner frame handed to xcplib, which parses it as ordinary
|                    Ethernet/IPv4/UDP and answers the XCP command inside
|
|   Layering:
|     cmp.c              the envelope codec, pure, no I/O
|     cmp_transport_udp.c  the outer transport, CMP over UDP (6.4.2)
|     here               the six eth_hal_* functions that join the two
|
| Code released into public domain, no attribution required
 ----------------------------------------------------------------------------*/

#include <stdio.h>  // for printf
#include <stdlib.h> // for malloc, free
#include <string.h> // for memset, memcpy

#include "cmp.h"
#include "cmp_backend.h"
#include "cmp_transport.h"
#include "socket_raw_hal.h" // the interface this file implements, installed with xcplib
// socket_raw_hal.h pulls in platform.h, which provides clockGet() and OPTION_MTU

//-------------------------------------------------------------------------------
// Configuration, set by main.c before XcpEthServerInit()

static tCmpBackendConfig sConfig = {
    .device_id = 1,
    .stream_id = 0,
    .interface_id = 1,
    .local_port = 55555,
    .sink_ip = NULL,
    .sink_port = 0,
    .outer_mtu = 1500,
    .ecu_mac = {0, 0, 0, 0, 0, 0},
};

void cmpBackendConfigure(const tCmpBackendConfig *config) {
    if (config != NULL) {
        sConfig = *config;
    }
}

//-------------------------------------------------------------------------------

struct eth_hal_ctx {
    tCmpTransport *transport;
    tCmpCodec codec;
    uint8_t mac[6];           // MAC of the emulated ECU
    uint16_t max_inner_frame; // largest frame that fits into one CMP message
    bool mtu_warning;         // xcplib's segment size does not fit that budget
    uint64_t n_oversize;      // frames refused for that reason
    uint64_t n_drop_logged;   // how many receive drops have been logged so far
    uint8_t tx[CMP_MAX_MESSAGE];
    uint8_t rx[CMP_MAX_MESSAGE];
};

// The single instance, so cmpBackendGetStatus() can reach it from the REST thread
static tEthHalCtx *sCtx = NULL;

//-------------------------------------------------------------------------------

// The frames xcplib produces are at most 42 + XCPTL_MAX_SEGMENT_SIZE bytes, which is
// OPTION_MTU + 10 (socket_raw_hal.h). xcptl_cfg.h is not installed, but OPTION_MTU is
// visible because XCPLIB_CFG_OVERRIDE is a PUBLIC compile definition of the library
// target and the override header is installed alongside it.
#define XCPLIB_MAX_FRAME (OPTION_MTU + 10)

static void deriveMac(uint8_t *mac, uint16_t device_id) {
    // Locally administered unicast: bit 0 of the first byte clear, bit 1 set. socket_raw.c
    // rejects a multicast or all zero MAC, so both properties are load bearing.
    mac[0] = 0x02;
    mac[1] = 0x00;
    mac[2] = 0x00;
    mac[3] = 0x00;
    mac[4] = (uint8_t)(device_id >> 8);
    mac[5] = (uint8_t)(device_id & 0xFF);
    if (mac[4] == 0 && mac[5] == 0) {
        mac[5] = 1; // never hand out an all zero MAC
    }
}

//-------------------------------------------------------------------------------

bool eth_hal_open(const char *config, tEthHalCtx **ctxp) {

    if (ctxp == NULL) {
        return false;
    }
    *ctxp = NULL;

    // The opaque HAL config string is not used: this backend is configured through
    // cmpBackendConfigure() from main.c, which is typed and carries more than a name.
    (void)config;

    if (sConfig.outer_mtu > CMP_MAX_OUTER_MTU) {
        printf("ERROR: eth_hal_open: outer MTU %u exceeds the %u byte maximum of 6.4\n", sConfig.outer_mtu, CMP_MAX_OUTER_MTU);
        return false;
    }

    tEthHalCtx *ctx = (tEthHalCtx *)malloc(sizeof(tEthHalCtx));
    if (ctx == NULL) {
        printf("ERROR: eth_hal_open: out of memory\n");
        return false;
    }
    memset(ctx, 0, sizeof(tEthHalCtx));

    tCmpConfig codec_config = {
        .device_id = sConfig.device_id,
        .stream_id = sConfig.stream_id,
        .interface_id = sConfig.interface_id,
    };
    cmpCodecInit(&ctx->codec, &codec_config);

    static const uint8_t zero_mac[6] = {0, 0, 0, 0, 0, 0};
    if (memcmp(sConfig.ecu_mac, zero_mac, 6) == 0) {
        deriveMac(ctx->mac, sConfig.device_id);
    } else {
        memcpy(ctx->mac, sConfig.ecu_mac, 6);
    }

    tCmpTransportConfig transport_config = {
        .local_port = sConfig.local_port,
        .sink_ip = sConfig.sink_ip,
        .sink_port = sConfig.sink_port,
        .outer_mtu = sConfig.outer_mtu,
    };
    if (!cmpTransportOpen(&transport_config, &ctx->transport)) {
        free(ctx);
        return false;
    }

    uint16_t max_message = cmpTransportMaxMessage(ctx->transport);
    if (max_message > CMP_MAX_MESSAGE) {
        max_message = CMP_MAX_MESSAGE;
    }
    ctx->max_inner_frame = (max_message > CMP_CAP_OVERHEAD) ? (uint16_t)(max_message - CMP_CAP_OVERHEAD) : 0;
    ctx->mtu_warning = XCPLIB_MAX_FRAME > ctx->max_inner_frame;

    char local_ip[16] = {0};
    uint16_t local_port = 0;
    cmpTransportGetLocal(ctx->transport, local_ip, sizeof(local_ip), &local_port);

    printf("  CMP capture module: DeviceId %u (0x%04X), StreamId %u, InterfaceId %u\n", sConfig.device_id, sConfig.device_id, sConfig.stream_id, sConfig.interface_id);
    printf("  CMP transport: UDP, listening on %s:%u, outer MTU %u\n", local_ip[0] != 0 ? local_ip : "0.0.0.0", local_port, sConfig.outer_mtu);
    char sink_ip[16] = {0};
    uint16_t sink_port = 0;
    if (cmpTransportGetSink(ctx->transport, sink_ip, sizeof(sink_ip), &sink_port)) {
        printf("  CMP Data Sink: %s:%u\n", sink_ip, sink_port);
    } else {
        printf("  CMP Data Sink: not configured, will be learned from the first message received\n");
    }
    printf("  CMP emulated ECU MAC: %02X:%02X:%02X:%02X:%02X:%02X\n", ctx->mac[0], ctx->mac[1], ctx->mac[2], ctx->mac[3], ctx->mac[4], ctx->mac[5]);
    printf("  CMP frame budget: %u bytes per inner frame (%u byte CMP message - %u byte envelope)\n", ctx->max_inner_frame, max_message, CMP_CAP_OVERHEAD);

    if (ctx->mtu_warning) {
        // 6.4.2 forbids sending CMP messages in IP fragmented packets, so an oversized
        // frame cannot be split - it has to be refused. Say so once, with the remedy,
        // instead of only reporting it per frame later.
        printf("WARNING: xcplib can produce frames of up to %u bytes, but only %u fit into one\n"
               "         un-fragmented CMP message on this path. Small transfers work, saturated DAQ\n"
               "         will report SOCKET_ERROR_MSGSIZE. Remedies:\n"
               "           - raise the outer MTU (--mtu, up to %u, needs a jumbo capable path), or\n"
               "           - build xcplite with OPTION_MTU <= %u (currently %u) in xcplib_raw_cfg.h\n",
               (unsigned)XCPLIB_MAX_FRAME, ctx->max_inner_frame, CMP_MAX_OUTER_MTU, (unsigned)(ctx->max_inner_frame - 10), (unsigned)OPTION_MTU);
    }

    sCtx = ctx;
    *ctxp = ctx;
    return true;
}

void eth_hal_close(tEthHalCtx *ctx) {
    if (ctx == NULL) {
        return;
    }
    printf("  CMP: %llu frames captured, %llu transmit requests delivered, %llu messages dropped", (unsigned long long)ctx->codec.n_wrapped,
           (unsigned long long)ctx->codec.n_unwrapped, (unsigned long long)ctx->codec.n_dropped);
    if (ctx->codec.n_seq_jumps != 0) {
        printf(", %llu sequence counter gaps", (unsigned long long)ctx->codec.n_seq_jumps);
    }
    if (ctx->n_oversize != 0) {
        printf(", %llu frames refused as oversized", (unsigned long long)ctx->n_oversize);
    }
    printf("\n");

    sCtx = NULL;
    cmpTransportClose(ctx->transport);
    free(ctx);
}

bool eth_hal_get_mac(tEthHalCtx *ctx, uint8_t *mac) {
    if (ctx == NULL || mac == NULL) {
        return false;
    }
    // This is the MAC of the emulated ECU behind the capture module, not of the capture
    // module itself: it becomes the source MAC of the frames xcplib builds, which is what
    // the Data Sink sees inside the CMP payload.
    memcpy(mac, ctx->mac, 6);
    return true;
}

int16_t eth_hal_send(tEthHalCtx *ctx, const uint8_t *frame, uint16_t len) {

    if (ctx == NULL || frame == NULL) {
        return ETH_HAL_ERROR;
    }

    if (len > ctx->max_inner_frame) {
        ctx->n_oversize++;
        if (ctx->n_oversize == 1) {
            printf("ERROR: eth_hal_send: frame of %u bytes exceeds the %u byte CMP budget of this path.\n"
                   "       CMP messages must not be IP fragmented (6.4.2), so it cannot be sent.\n",
                   len, ctx->max_inner_frame);
        }
        return ETH_HAL_ERROR_SIZE;
    }

    // Apply the envelope into our own buffer. Never into the transmit queue headroom: CMP
    // must not participate in the zero copy path and must not influence XCPTL_TX_HEADROOM.
    //
    // The timestamp is taken here rather than passed in from xcplib: this is the moment the
    // emulated capture module sees the frame, and it is the only capture time that exists.
    // INSYNC is false because nothing synchronises us to a time provider (11.1.3).
    uint16_t msg_len = cmpWrapCaptured(&ctx->codec, frame, len, clockGet(), false, ctx->tx, (uint16_t)sizeof(ctx->tx));
    if (msg_len == 0) {
        ctx->n_oversize++;
        return ETH_HAL_ERROR_SIZE;
    }

    int32_t sent = cmpTransportSend(ctx->transport, ctx->tx, msg_len);
    if (sent < 0) {
        return ETH_HAL_ERROR;
    }

    // Report the length xcplib handed us, not the wrapped length: the envelope is invisible
    // above this layer. A return of 0 from the transport means the Data Sink is not known
    // yet - the frame is discarded, which is what a capture module with no configured sink
    // does, and reporting success keeps the XCP transmit path from treating it as an error.
    return (int16_t)len;
}

int16_t eth_hal_recv(tEthHalCtx *ctx, uint8_t *frame, uint16_t max_len, uint32_t timeout_ms) {

    if (ctx == NULL || frame == NULL) {
        return ETH_HAL_ERROR;
    }

    int32_t n = cmpTransportRecv(ctx->transport, ctx->rx, (uint16_t)sizeof(ctx->rx), timeout_ms);
    if (n < 0) {
        return ETH_HAL_ERROR;
    }
    if (n == 0) {
        return 0; // timeout or wakeup
    }

    tCmpResult result = CMP_OK;
    uint16_t inner = cmpUnwrapTransmit(&ctx->codec, ctx->rx, (uint16_t)n, frame, max_len, &result);
    if (inner == 0) {
        // Not for us. Returning 0 means "no frame", so the caller keeps waiting against its
        // own deadline. Log the first few and then every 1000th, so a misconfigured Data
        // Sink is visible without the log becoming the bottleneck.
        if (ctx->codec.n_dropped <= 5 || (ctx->codec.n_dropped % 1000) == 0) {
            printf("WARNING: eth_hal_recv: dropped a %d byte CMP message: %s (%llu so far)\n", (int)n, cmpResultName(result), (unsigned long long)ctx->codec.n_dropped);
        }
        return 0;
    }

    return (int16_t)inner;
}

void eth_hal_wakeup(tEthHalCtx *ctx) {
    if (ctx != NULL) {
        cmpTransportWakeup(ctx->transport);
    }
}

//-------------------------------------------------------------------------------

bool cmpBackendGetStatus(tCmpBackendStatus *status) {

    tEthHalCtx *ctx = sCtx;
    if (status == NULL || ctx == NULL) {
        return false;
    }
    memset(status, 0, sizeof(*status));

    status->open = true;
    status->sink_known = cmpTransportGetSink(ctx->transport, status->sink_ip, sizeof(status->sink_ip), &status->sink_port);
    cmpTransportGetLocal(ctx->transport, status->local_ip, sizeof(status->local_ip), &status->local_port);

    status->max_message = cmpTransportMaxMessage(ctx->transport);
    status->max_inner_frame = ctx->max_inner_frame;
    status->mtu_warning = ctx->mtu_warning;

    memcpy(status->ecu_mac, ctx->mac, 6);
    status->device_id = ctx->codec.config.device_id;
    status->stream_id = ctx->codec.config.stream_id;
    status->interface_id = ctx->codec.config.interface_id;

    status->n_wrapped = ctx->codec.n_wrapped;
    status->n_unwrapped = ctx->codec.n_unwrapped;
    status->n_dropped = ctx->codec.n_dropped;
    status->n_seq_jumps = ctx->codec.n_seq_jumps;
    status->n_aggregated_ignored = ctx->codec.n_aggregated_ignored;
    status->n_oversize = ctx->n_oversize;
    return true;
}
