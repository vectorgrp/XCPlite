#pragma once

/* xcpServer.h */

/* Copyright(c) Vector Informatik GmbH.All rights reserved.
   Licensed under the MIT license.See LICENSE file in the project root for details. */

#ifdef __cplusplus
extern "C" {
#endif

extern int XcpServerInit(const uint8_t *addr, uint16_t port, BOOL useTCP);
extern int XcpServerShutdown();
extern int XcpServerStatus();

#ifdef __cplusplus
}
#endif
