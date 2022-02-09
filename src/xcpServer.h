#pragma once
/* xcpServer.h */

/* Copyright(c) Vector Informatik GmbH.All rights reserved.
   Licensed under the MIT license.See LICENSE file in the project root for details. */

extern BOOL XcpServerInit(const uint8_t *addr, uint16_t port, BOOL useTCP);
extern BOOL XcpServerShutdown();
extern BOOL XcpServerStatus();

