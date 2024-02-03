#pragma once
/* xcpCanServer.h */

/* Copyright(c) Vector Informatik GmbH.All rights reserved.
   Licensed under the MIT license.See LICENSE file in the project root for details. */

extern BOOL XcpCanServerInit(BOOL useCANFD, uint16_t croId, uint16_t dtoId, uint32_t bitRate);
extern BOOL XcpCanServerShutdown();
extern BOOL XcpCanServerStatus();

