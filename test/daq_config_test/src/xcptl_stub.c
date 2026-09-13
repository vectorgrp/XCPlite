// Transport layer stub for daq_config_test
// Command responses are irrelevant to this protocol-layer test and are intentionally discarded.

#include <stdbool.h> // for bool
#include <stdint.h>  // for uintxx_t

#include "xcptl.h"

bool XcpTlWaitForTransmitQueueEmpty(uint16_t timeout_ms) {
    (void)timeout_ms;
    return true;
}

void XcpTlSendCrm(const uint8_t *data, uint8_t size) {
    (void)data;
    (void)size;
}
