#pragma once
/* xcpCanTl.h */

/* Copyright(c) Vector Informatik GmbH.All rights reserved.
   Licensed under the MIT license.See LICENSE file in the project root for details. */

/* ETH transport Layer functions called by application */
extern BOOL XcpCanTlInit(BOOL useCANFD, uint32_t croId, uint32_t dtoId, uint32_t bitRate);

