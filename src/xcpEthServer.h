#pragma once
/* xcpServer.h */

/* Copyright(c) Vector Informatik GmbH.All rights reserved.
   Licensed under the MIT license.See LICENSE file in the project root for details. */

extern BOOL XcpEthServerInit(const uint8_t *addr, uint16_t port, BOOL useTCP, uint16_t segmentSize);
extern BOOL XcpEthServerShutdown();
extern BOOL XcpEthServerStatus();

