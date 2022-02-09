/*----------------------------------------------------------------------------
| File:
|   xl_pcap.c
|
| Description:
|   Vector XL-API for Ethernet V3 write events to PCAP
|
| Copyright (c) Vector Informatik GmbH. All rights reserved.
| Licensed under the MIT license. See LICENSE file in the project root for details.
 ----------------------------------------------------------------------------*/

#include "main.h"
#include "main_cfg.h"

#if OPTION_ENABLE_XLAPI_V3
#if OPTION_ENABLE_PCAP

#include "../xlapi/xl_pcap.h"

#define HTONS(x) ((((x)&0x00ff) << 8) | (((x)&0xff00) >> 8))

// Global variables
T_XL_NET_ETH_EVENT xlEthEvent;
XLrxHandle xlRxHandles[128];
unsigned int xlRxCount = 128;
XLstatus xlLastError;

// Local variables
static FILE* gFile = 0;

static int pcapWriteHeader() {

    if (gFile == 0) return 0;

    // Magic number (Nanosecond version)
    unsigned int magic = 0xa1b23c4d;
    fwrite(&magic, 1, 4, gFile);
    // Version 2.4
    uint16_t versionMajor = 2;
    uint16_t versionMinor = 4;
    fwrite(&versionMajor, 1, 2, gFile);
    fwrite(&versionMinor, 1, 2, gFile);
    // GMT timezone
    unsigned int timezone = 0;
    fwrite(&timezone, 1, 4, gFile);
    // Accuracy timestamps
    unsigned int sigfigs = 0;
    fwrite(&sigfigs, 1, 4, gFile);
    // Snapshot length
    unsigned int snaplen = 65535;
    fwrite(&snaplen, 1, 4, gFile);
    // Use Ethernet as link-layer header type
    unsigned int network = 1;
    fwrite(&network, 1, 4, gFile);

    return 1;
}


// Store frame in PCAP format
int pcapWriteFrame(unsigned char* pDestMAC, unsigned char* pSrcMAC, unsigned char* pData, uint16_t dataLen, unsigned int fcs,  XLuint64 timeStamp) {

#define NANO_SEC   1000000000

    if (gFile == 0) return 0;

    // Write PCAP packet header
    // Timestamp in nano seconds
    unsigned int timestamp = (unsigned int)(timeStamp / NANO_SEC); // ns => s
    fwrite(&timestamp, 1, 4, gFile);
    timestamp = (unsigned int)(timeStamp - ((timeStamp / NANO_SEC) * NANO_SEC)); // fractional ns part
    fwrite(&timestamp, 1, 4, gFile);
    // Packet length
    unsigned int length = dataLen + 16;  // Add 12 bytes for MAC adresses and 4 for FCS
    fwrite(&length, 1, 4, gFile);
    fwrite(&length, 1, 4, gFile);
    // Store frame
    fwrite(pDestMAC, 1, 6, gFile);
    fwrite(pSrcMAC, 1, 6, gFile);
    fwrite(pData, 1, dataLen, gFile); // Store Ethertype and payload
    fwrite(&fcs, 1, 4, gFile);

    return 1;
}



int pcapWriteFrameRx(XLuint64 timestamp, T_XL_NET_ETH_DATAFRAME_RX* frame) {
    return pcapWriteFrame(frame->destMAC, frame->sourceMAC, frame->frameData.rawData, frame->dataLen, frame->fcs, timestamp);
}

int pcapWriteFrameTx(XLuint64 timestamp, T_XL_NET_ETH_DATAFRAME_TX* frame) {
    return pcapWriteFrame(frame->destMAC, frame->sourceMAC, frame->frameData.rawData, frame->dataLen, 0, timestamp);
}



int pcapWriteEvent(T_XL_NET_ETH_EVENT* pRxEvent) {

    switch (pRxEvent->tag) {

    case XL_ETH_EVENT_TAG_FRAMERX_ERROR_MEASUREMENT:
        break;
    case XL_ETH_EVENT_TAG_FRAMETX_ERROR_MEASUREMENT:
        break;

    case XL_ETH_EVENT_TAG_FRAMETX_MEASUREMENT:
        break;
    case XL_ETH_EVENT_TAG_FRAMERX_MEASUREMENT:
        return pcapWriteFrameRx(pRxEvent->timeStampSync, &pRxEvent->tagData.frameMeasureRx);
        break;

    case XL_ETH_EVENT_TAG_FRAMERX_SIMULATION:
        return pcapWriteFrameRx(pRxEvent->timeStampSync, &pRxEvent->tagData.frameSimRx);
        break;

    case XL_ETH_EVENT_TAG_CHANNEL_STATUS:
        //printf("LINK %s\n", (unsigned int)pRxEvent->tagData.channelStatus.link == XL_ETH_STATUS_LINK_UP ? "UP" : "DOWN");
        break;

    default:
        printf("ERROR: Unexpected Event\n");
        break;
    }

    return 0;
}



// Write PCAP global header
int pcapOpen(const char* filename) {

    gFile = 0;
    if (0 != fopen_s(&gFile, filename, "wb")) {
        printf("ERROR: Could not open file %s\n", filename);
        return 0;
    }
    else {
        if (!pcapWriteHeader()) return 0;
    }

    return 1;
}


void pcapClose() {

    fclose(gFile);
}

#endif
#endif
