#pragma once

/* xl_pcap.h */

#if OPTION_ENABLE_PCAP


#include "vxlapi.h"

extern int pcapOpen(const char* filename);
extern void pcapClose();
extern int pcapWriteEvent(T_XL_NET_ETH_EVENT* pRxEvent);
extern int pcapWriteFrame(unsigned char* pDestMAC, unsigned char* pSrcMAC, unsigned char* pData, uint16_t dataLen, unsigned int fcs, XLuint64 timeStamp);
extern int pcapWriteFrameRx(XLuint64 timestamp, T_XL_NET_ETH_DATAFRAME_RX* frame);
extern int pcapWriteFrameTx(XLuint64 timestamp, T_XL_NET_ETH_DATAFRAME_TX* frame);


#endif // OPTION_ENABLE_PCAP
