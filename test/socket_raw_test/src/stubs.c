// socket_raw_test - fake Ethernet HAL and platform stubs
//
// eth_hal_send() captures the frame instead of transmitting it, so the test can inspect
// exactly what socket_raw.c put on the wire. Everything else is a minimal stub: the test
// drives socket_raw.c directly and never opens a real interface.

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "platform.h"
#include "socket_raw_hal.h"
#include "xcptl_cfg.h" // for XCPTL_MAX_SEGMENT_SIZE, the capture buffer is sized from the configuration

#define TEST_BUF_SIZE (42 + XCPTL_MAX_SEGMENT_SIZE + 64)

// Captured transmit frame, see test/socket_raw_test/src/main.c
extern uint8_t gTxFrame[TEST_BUF_SIZE];
extern uint16_t gTxLen;
extern int gTxCount;

uint8_t gXcpLogLevel = 0;

uint64_t clockGet(void) { return 0; }
uint64_t clockGetMonotonicNs(void) { return 0; }

void mutexInit(MUTEX *m, bool recursive, uint32_t spinCount) {
    (void)m;
    (void)recursive;
    (void)spinCount;
}
void mutexDestroy(MUTEX *m) { (void)m; }

bool eth_hal_open(const char *config, tEthHalCtx **ctx) {
    (void)config;
    (void)ctx;
    return false;
}
void eth_hal_close(tEthHalCtx *ctx) { (void)ctx; }
bool eth_hal_get_mac(tEthHalCtx *ctx, uint8_t *mac) {
    (void)ctx;
    (void)mac;
    return false;
}

int16_t eth_hal_send(tEthHalCtx *ctx, const uint8_t *frame, uint16_t len) {
    (void)ctx;
    if (len <= sizeof(gTxFrame))
        memcpy(gTxFrame, frame, len);
    gTxLen = len;
    gTxCount++;
    return (int16_t)len;
}

int16_t eth_hal_recv(tEthHalCtx *ctx, uint8_t *frame, uint16_t max_len, uint32_t timeout_ms) {
    (void)ctx;
    (void)frame;
    (void)max_len;
    (void)timeout_ms;
    return 0;
}

void eth_hal_wakeup(tEthHalCtx *ctx) { (void)ctx; }
