/*----------------------------------------------------------------------------
| File:
|   xl_udp.c
|
| Description:
|   Vector XL-API for Ethernet V3 UDP stack
|
| Copyright (c) Vector Informatik GmbH. All rights reserved.
| Licensed under the MIT license. See LICENSE file in the project root for details.
|
| Restrictions:
|   - IPv4 only
|   - Limited to a single network
|   - Each ports opens a virtual switch port in its V3 network segment (net,seg) and creates a virtual endpoint (ip/mac)
|   - Only one socket per virtual endpoint (ip/mac)
|
| Remarks:
|   - IP addresses are represented by a uint8 array with high byte first
|   - IP ports are uint16 in network byte order (Motorola format HTONS)

 ----------------------------------------------------------------------------*/

#include "main.h"
#include "main_cfg.h"
#include "../src/platform.h"

#if OPTION_ENABLE_XLAPI_V3

// Need to link with vxlapi.lib
#ifdef _WIN64
#pragma comment(lib, "../xlapi/vxlapi64.lib")
#else
#ifdef _WIN32
#pragma comment(lib, "../xlapi/vxlapi.lib")
#endif
#endif

#include "vxlapi.h"
#include "xl_udp.h"

#if OPTION_ENABLE_PCAP
#include "xl_pcap.h"
#endif

#if OPTION_ENABLE_PTP
#include "ptp.h"
#endif

static int gUdpDebugLevel = 5;


// Ethertype
#define IPV4 0x0800
#define ARP  0x0806
#define IPV6 0x86dd

// Protocol
#define ICMP 2
#define IGMP 2
#define TCP  6
#define UDP  17


#pragma pack(push,1) 

// 8 bytes
struct udphdr
{
    uint16_t source;
    uint16_t dest;
    uint16_t len;
    uint16_t check;
};

// 14 bytes
struct iphdr
{
    uint16_t ver_ihl_dscp_ecn; // unsigned int version : 4;  unsigned int ihl : 4; unsigned int dscp : 6; unsigned int ecn : 2;
    uint16_t tot_len;
    uint16_t id;
    uint16_t frag_off;
    uint8_t ttl;
    uint8_t protocol;
    uint16_t check;
    uint8_t saddr[4]; // src addr, high byte first 
    uint8_t daddr[4]; // dst addr 
    /* options start here */
};

struct arp
{
    uint16_t	hrd;	/* format of hardware address */
#define ARPHRD_ETHER 1	/* ethernet hardware format */
    uint16_t	pro;	/* format of protocol address */
    uint8_t	hln;		/* length of hardware address */
    uint8_t	pln;		/* length of protocol address */
    uint16_t	op;		/* one of: */
#define	ARPOP_REQUEST 1	/* request to resolve address */
#define	ARPOP_REPLY	2	/* response to previous request */
    uint8_t sha[6];	    /* sender hardware address */
    uint8_t spa[4];	    /* sender protocol address */
    uint8_t tha[6];	    /* target hardware address */
    uint8_t tpa[4];	    /* target protocol address */
};

#pragma pack(pop) 


#define HTONS(x) (uint16_t)((((x)&0x00ff) << 8) | (((x)&0xff00) >> 8))

typedef struct {
    uint16_t port; // is in network byteorder
    uint8_t addr[4];
    uint8_t mac[6];
} tUdpSockAddrXl;

typedef struct {   
    XLnetworkHandle networkHandle; // Network handle
    XLethPortHandle portHandle; // VP handle
    char portName[32+1]; // VP name
    int index; // socketList index of this socket
    BOOL bound; // socket bound to unicast addr and port
    BOOL join; // socket joined to multicast addr and port
    tUdpSockAddrXl localAddr; // Local address
    tUdpSockAddrXl remoteAddr; // Remote address
    XLhandle event; // Notofication event
} tUdpSockXl;




#define MAX_SOCKETS 8

static struct {
    BOOL init; // Is initialized
    char app_name[16+1]; // Unique name 
    tUdpSockXl *socketList[MAX_SOCKETS]; // Socket list
} gXlUdp;





// Helper to convert XL_SOCKET to tUdpSockXl
static tUdpSockXl* getSock(XL_SOCKET sock) {
    tUdpSockXl* s;
    if (!gXlUdp.init || sock < 0 || sock >= MAX_SOCKETS || (s = gXlUdp.socketList[sock]) == NULL) {
        printf("ERROR: invalid socket!\n");
        return NULL;
    }
    return s;
}

//---------------------------------------------------------------------------------------------------------------------------------------------------------

static int printFrame(tUdpSockXl* s, const unsigned char* destMac, const unsigned char* srcMac, uint16_t ethertype, const unsigned char* data, uint16_t dataLen, unsigned int fcs, XLuint64 timeStamp) {
    (void)fcs;
    (void)data;

    char type[32];
    switch (HTONS(ethertype)) {
    case IPV4: strcpy(type, "IPv4"); break;
    case IPV6: strcpy(type, "IPv6"); break;
    case ARP: strcpy(type,"ARP"); break;
      default: sprintf(type, "%04X", (unsigned int)HTONS(ethertype));
    }
    printf("RX %s: t=%llu src=%02X:%02X:%02X:%02X:%02X:%02X dst=%02X:%02X:%02X:%02X:%02X:%02X type=%s, len=%u\n",
        s->portName, timeStamp,
        srcMac[0], srcMac[1], srcMac[2], srcMac[3], srcMac[4], srcMac[5],
        destMac[0], destMac[1], destMac[2], destMac[3], destMac[4], destMac[5],
        type, (unsigned int)dataLen);
    return TRUE;
}

static int printIPV4Frame(const T_XL_ETH_FRAMEDATA* frameData, unsigned int frameLen) {
    (void)frameLen;

    // IPv4
    if (frameData->ethFrame.etherType == HTONS(IPV4)) { 

        struct iphdr* ip = (struct iphdr*) & frameData->ethFrame.payload[0];
        printf("  IPv4 l=%u ", HTONS(ip->tot_len));
        printf("%u.%u.%u.%u->", ip->saddr[0], ip->saddr[1], ip->saddr[2], ip->saddr[3]);
        printf("%u.%u.%u.%u\n", ip->daddr[0], ip->daddr[1], ip->daddr[2], ip->daddr[3]);
        
        // UDP
        if (ip->protocol == UDP) { 
            struct udphdr* udp = (struct udphdr*) & frameData->ethFrame.payload[sizeof(struct iphdr)];
            printf("    UDP udpl=%u %u->%u s=%u ", (uint32_t)(HTONS(udp->len) - sizeof(struct udphdr)), HTONS(udp->source), HTONS(udp->dest), HTONS(udp->check));
            
            // UDP MULTICAST
            if ((ip->daddr[0] >> 4) == 0x0E) {
                printf("MULTICAST ");

                // PTP
#if OPTION_ENABLE_PTP
                if (HTONS(udp->source) == 319 || HTONS(udp->source) == 320) {
                    struct ptphdr* ptp = (struct ptphdr*) & frameData->ethFrame.payload[sizeof(struct iphdr) + sizeof(struct udphdr)];
                    const char* types = "";
                    switch (ptp->type) {
                    case 0x0: types = "SYNC"; break;
                    case 0x8: types = "FOLLOWUP"; break;
                    case 0xB: types = "ANNOUNCE"; break;
                    }
                    printf("PTP %s (%04X), domain=%u, corr_ns=%llu, time_s=%u, time_ns=%u", types, ptp->type, ptp->domain, htonll(ptp->correction)>>16, htonl(ptp->timestamp.timestamp_s), htonl(ptp->timestamp.timestamp_ns));
                }
#endif
            }

            if (gUdpDebugLevel >= 3) {
                for (unsigned int byte = 0; byte < HTONS(udp->len) - sizeof(struct udphdr); byte++) {
                    printf("%02X ", frameData->ethFrame.payload[sizeof(struct iphdr) + sizeof(struct udphdr) + byte]);
                }
            }
        } // UDP
        else if (ip->protocol == TCP) {
            printf("    TCP");
        }
        else if (ip->protocol == ICMP) {
            printf("    ICMP");
        }
        else if (ip->protocol == IGMP) {
            printf("    IGMP");
        }
        else {
            printf("    protocol=%u", ip->protocol);
        }
        printf("\n");
        return TRUE;
    }

    return FALSE;
}

static int printARPFrame(const T_XL_ETH_FRAMEDATA* frameData, unsigned int frameLen) {
    (void)frameLen;
    if (frameData->ethFrame.etherType == HTONS(ARP)) { // ARP
        struct arp* arp = (struct arp*) & frameData->ethFrame.payload[0];
        printf("  ARP %u 0x%04X %u/%u %s", HTONS(arp->hrd), HTONS(arp->pro), arp->hln, arp->pln, (1 == arp->op) ? "Req " : "Res ");
        printf("sha 0x%02X:0x%02X:0x%02X:0x%02X:0x%02X:0x%02X ", arp->sha[0], arp->sha[1], arp->sha[2], arp->sha[3], arp->sha[4], arp->sha[5]);
        printf("spa %u.%u.%u.%u ", arp->spa[0], arp->spa[1], arp->spa[2], arp->spa[3]);
        printf("tha 0x%02X:0x%02X:0x%02X:0x%02X:0x%02X:0x%02X ", arp->tha[0], arp->tha[1], arp->tha[2], arp->tha[3], arp->tha[4], arp->tha[5]);
        printf("tpa %u.%u.%u.%u\n", arp->tpa[0], arp->tpa[1], arp->tpa[2], arp->tpa[3]);
        return TRUE;
    }
    return FALSE;
}


static void printRxFrame(tUdpSockXl* s, XLuint64 timestamp, const T_XL_NET_ETH_DATAFRAME_RX* frame) {
    

    printFrame(s, frame->destMAC, frame->sourceMAC, frame->frameData.ethFrame.etherType, frame->frameData.rawData, frame->dataLen, frame->fcs, timestamp);
   
    if (!printARPFrame(&frame->frameData, frame->dataLen)) {
        if (!printIPV4Frame(&frame->frameData, frame->dataLen)) {
            // printFrame(s, frame->destMAC, frame->sourceMAC, frame->frameData.rawData, frame->dataLen, frame->fcs, timestamp);
        }
    }
}


static int printEvent(tUdpSockXl* s, const T_XL_NET_ETH_EVENT* pRxEvent) {
    printf("%s: ", s->portName);
    switch (pRxEvent->tag) {
    case XL_ETH_EVENT_TAG_FRAMERX_ERROR_MEASUREMENT:
        printf("XL_ETH_EVENT_TAG_FRAMERX_ERROR_MEASUREMENT \n");
        break;
    case XL_ETH_EVENT_TAG_FRAMETX_ERROR_MEASUREMENT:
        printf("XL_ETH_EVENT_TAG_FRAMETX_ERROR_MEASUREMENT \n");
        break;
    case XL_ETH_EVENT_TAG_FRAMERX_MEASUREMENT:
        printf("XL_ETH_EVENT_TAG_FRAMERX_MEASUREMENT \n");
        break;
    case XL_ETH_EVENT_TAG_FRAMETX_MEASUREMENT:
        printf("XL_ETH_EVENT_TAG_FRAMETX_MEASUREMENT \n");
        break; 
    case XL_ETH_EVENT_TAG_FRAMETX_ACK_SIMULATION:
        printf("XL_ETH_EVENT_TAG_FRAMETX_ACK_SIMULATION \n");
        break;
    case XL_ETH_EVENT_TAG_LOSTEVENT:
        printf("XL_ETH_EVENT_TAG_LOSTEVENT \n");
        break;
    case XL_ETH_EVENT_TAG_ERROR:
        printf("XL_ETH_EVENT_TAG_ERROR \n");
        break;
    case XL_ETH_EVENT_TAG_CHANNEL_STATUS:
        printf("LINK %s\n", (unsigned int)pRxEvent->tagData.channelStatus.link == XL_ETH_STATUS_LINK_UP ? "UP" : "DOWN");
        return TRUE;
    }
    return FALSE;
}

//------------------------------------------------------------------------------------------------------------------------------

static uint16_t ipChecksum(uint16_t* buf, int nwords) {

    uint32_t sum;
    for (sum = 0; nwords > 0; nwords--) sum += *buf++; // No HTONS here, one complement sum is independant from Motorola or Intel format
    while (sum >> 16) sum = (sum & 0xffff) + (sum >> 16);
    return (uint16_t)(~sum);
}


static void udpInitIpHdr(struct iphdr* ip, const uint8_t* src, const uint8_t* dst) {

    // IP header(20 byte)
    ip->ver_ihl_dscp_ecn = HTONS(0x4500); // Version 4, header length 5
    // ip->tot_len = 0; // Total Length 
    ip->id = HTONS(54321);              // Identification: 0xd431 (45109???)
    ip->frag_off = 0;                   // Fragment offset : 0
    ip->ttl = 64;                       // Time to live: 64
    ip->protocol = UDP;                  // Protocol: UDP (17)
    memcpy(ip->saddr, src, 4);   // Source
    memcpy(ip->daddr, dst, 4);   // Destination
    // ip->check = 0; // Checksum has to be calculated later depending on final tot_len
}


static void udpInitUdpHdr(struct udphdr* udp, uint16_t src, uint16_t dst) {

    // UDP header (8 byte)
    udp->source = src;         // Source Port
    udp->dest = dst;           // Destination Port 
    udp->len = HTONS(sizeof(struct udphdr));    // Length: 8
    udp->check = 0; // Checsum not calculated                         

}


static int udpSendARPResponse(tUdpSockXl* s, unsigned char sha[], unsigned char spa[]) {

    XLstatus err;
    T_XL_NET_ETH_DATAFRAME_TX frame;

    memset(&frame, 0, sizeof(T_XL_NET_ETH_DATAFRAME_TX));
    // header
    frame.dataLen = XL_ETH_PAYLOAD_SIZE_MIN + 2; // payload length +2 (with ethertype) 28 or XL_ETH_PAYLOAD_SIZE_MIN ???
    frame.flags |= XL_ETH_DATAFRAME_FLAGS_USE_SOURCE_MAC /*| XL_ETH_DATAFRAME_FLAGS_NO_TX_EVENT_GEN*/;
    memcpy(&frame.sourceMAC, s->localAddr.mac, sizeof(frame.sourceMAC));
    memcpy(&frame.destMAC, sha, sizeof(frame.destMAC));
    frame.frameData.ethFrame.etherType = HTONS(ARP);
    // payload
    struct arp* arp = (struct arp*)&frame.frameData.ethFrame.payload[0];
    arp->hrd = HTONS(ARPHRD_ETHER);		/* format of hardware address */
    arp->pro = HTONS(0x0800);		/* format of protocol address */
    arp->hln = 6;		/* length of hardware address */
    arp->pln = 4;		/* length of protocol address */
    arp->op = HTONS(ARPOP_REPLY);		/* one of: */
    memcpy(arp->sha, s->localAddr.mac, 6);
    memcpy(arp->spa, s->localAddr.addr, 4);
    memcpy(arp->tha, sha, 6);
    memcpy(arp->tpa, spa, 4);

    if (gUdpDebugLevel >= 3) {
        printf("TX %s: ", s->portName);
        printARPFrame(&frame.frameData, frame.dataLen); //T_XL_ETH_FRAMEDATA
    }
    if (XL_SUCCESS != (err = xlNetEthSend(s->networkHandle, s->portHandle, (XLuserHandle)1, &frame))) {
        printf("ERROR: xlNetEthSend failed with error: %s (%d)!\n", xlGetErrorString(err), err);
        return FALSE;
    }
#if OPTION_ENABLE_PCAP
    if (gOptionPCAP) pcapWriteFrameTx(0,&frame);
#endif
    return TRUE;
}




//------------------------------------------------------------------------------------------------

BOOL xlUdpSocketStartup( char *app_name ) {

    XLstatus err;

    if (!gXlUdp.init) {

        if (XL_SUCCESS != (err = xlOpenDriver())) {
            printf("ERROR: xlOpenDriver failed with ERROR: %s (%d)!\n", xlGetErrorString(err), err);
            return FALSE;
        }

        memset(&gXlUdp, 0, sizeof(tUdpSockXl));
        strncpy(gXlUdp.app_name, app_name, 16);
        gXlUdp.init = TRUE;

#if OPTION_ENABLE_PCAP
        // Init PCAP logger
        if (gOptionPCAP) {
            if (!pcapOpen(gOptionPCAP_File)) {
                gOptionPCAP = 0;
            }
        }
#endif

    }
    return TRUE;
}

void xlUdpSocketCleanup() {

    XLstatus err;
    int i;
    uint64_t sock;

    if (gXlUdp.init) {

        gXlUdp.init = FALSE;

        for (i = 0; i < MAX_SOCKETS; i++) {
            sock = i;
            xlUdpSocketClose(&sock);
        }

        if (XL_SUCCESS != (err = xlCloseDriver())) {
            printf("ERROR: xlCloseDriver failed: %s (%d)\n", xlGetErrorString(err), err);
        }
    }
}

BOOL xlUdpSocketOpen(XL_SOCKET* sockp, BOOL useTCP, BOOL nonBlocking, BOOL reuseaddr) {

    tUdpSockXl* sock;
    int i;

    (void)reuseaddr;
    if (!gXlUdp.init || sockp == NULL || nonBlocking || useTCP) {
        printf("ERROR: invalid parameter\n");
        return FALSE;
    }

    // Find a free socket
    for (i = 0; i < MAX_SOCKETS; i++) {
        if (gXlUdp.socketList[i] == NULL) {
            break;
        }
    }
    if (i==MAX_SOCKETS) {
        printf("ERROR: Too many sockets!\n");
        return 0;
    }

    // Create socket
    sock = (tUdpSockXl*)malloc(sizeof(tUdpSockXl));
    if (sock == NULL) {
        printf("ERROR: Out of memory!\n");
        return FALSE;
    }
    memset(sock, 0, sizeof(tUdpSockXl));
    gXlUdp.socketList[i] = sock;
    sock->index = i;
    *sockp = i;

    return TRUE;
}


BOOL xlUdpSocketClose(XL_SOCKET* sockp) {

    XLstatus err;
    tUdpSockXl* s;

    if (sockp==NULL || (s = getSock(*sockp)) == NULL) return FALSE;

    if (XL_SUCCESS != (err = xlNetDeactivateNetwork(s->networkHandle))) {
        printf("ERROR: xlNetDeactivateNetwork failed: %s (%d)\n", xlGetErrorString(err), err);
        return FALSE;
    }

    if (XL_SUCCESS != (err = xlNetCloseNetwork(s->networkHandle))) {
        printf("ERROR: xlNetCloseNetwork failed: %s (%d)\n", xlGetErrorString(err), err);
    }

    // Cleanup socket
    *sockp = INVALID_SOCKET;
    free(s);

    return TRUE;
}


BOOL xlUdpSocketBind(XL_SOCKET sock, char* netName, char* segName, uint8_t* mac, uint8_t* addr, uint16_t port) {

    XLstatus err;
    tUdpSockXl* s;

    if ((s = getSock(sock)) == NULL) return FALSE;
    if (s->bound) {
        printf("ERROR: socket in use!\n");
        return FALSE;
    }

    s->localAddr.port = HTONS(port);
    if (addr != NULL) memcpy(s->localAddr.addr, addr, 4);
    assert(mac != NULL);
    memcpy(s->localAddr.mac, mac, 6);
    s->join = FALSE;
    sprintf(s->portName, "s%d", s->index);

    if (XL_SUCCESS != (err = xlNetEthOpenNetwork(netName, &s->networkHandle, s->portName, XL_ACCESS_TYPE_RELIABLE, 8 * 1024 * 1024))) {
        printf("ERROR: xlNetEthOpenNetwork(NET1) failed with ERROR: %s (%d)!\n", xlGetErrorString(err), err);
        return FALSE;
    }

    if (XL_SUCCESS != (err = xlNetAddVirtualPort(s->networkHandle, segName, s->portName, &s->portHandle, (XLrxHandle)s->index))) {
        printf("ERROR: xlNetAddVirtualPort %s failed with ERROR: %s (%d)!\n", segName, xlGetErrorString(err), err);
        return FALSE;
    }

    // Set notification handle (required to use event handling via WaitForMultipleObjects)
    s->event = CreateEvent(NULL, 0, 0, NULL);
    if (XL_SUCCESS != (err = xlNetSetNotification(s->networkHandle, &s->event, 1))) { // 1 - Notify for each single event in queue
        printf("ERROR: xlNetSetNotification failed with ERROR: %s (%d)!\n", xlGetErrorString(err), err);
        CloseHandle(s->event);
        return FALSE;
    }

    if (XL_SUCCESS != (err = xlNetActivateNetwork(s->networkHandle))) {
        printf("ERROR: xlNetActivateNetwork failed: %s (%d)\n", xlGetErrorString(err), err);
        CloseHandle(s->event);
        return FALSE;
    }

    if (gUdpDebugLevel >= 1) printf("Socket %u bound to %02X:%02X:%02X:%02X:%02X:%02X %u.%u.%u.%u:%u\n", s->index, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], s->localAddr.addr[0], s->localAddr.addr[1], s->localAddr.addr[2], s->localAddr.addr[3], port);
    
    s->bound = TRUE;
    return TRUE;
}

BOOL xlUdpSocketJoin(XL_SOCKET sock, uint8_t* addr) {

    tUdpSockXl* s;

    if ((s = getSock(sock)) == NULL) return FALSE;
    if (!s->bound) {
        printf("ERROR: socket not bound!\n");
        return FALSE;
    }

    // Overwrite unicast addr
    memcpy(s->localAddr.addr, addr, 4);
    // Create multicast mac 01:00:5E:a4:a5:a6
    uint8_t mac[6] = { 0x01, 0x00, 0x5E, 0,0,0 };
    memcpy(&mac[3], s->localAddr.addr, 3);
    memcpy(s->localAddr.mac, mac, 6);

    if (gUdpDebugLevel >= 1) printf("Socket %u join to %02X:%02X:%02X:%02X:%02X:%02X %u.%u.%u.%u:%u\n", s->index, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], addr[0], addr[1], addr[2], addr[3], HTONS(s->localAddr.port));

    s->join = TRUE;
    return TRUE;
}

int xlUdpSocketListen(XL_SOCKET sock) {
    (void)sock;
    // TCP not supportet
    return FALSE;
}

int xlUdpSocketAccept(XL_SOCKET sock, uint8_t addr[]) {
    (void)sock;
    (void)addr;
    // TCP not supportet
    return FALSE;
}

int xlUdpSocketShutdown(XL_SOCKET sock) {
    (void)sock;
    // TCP not supported
    return FALSE;
}

int16_t xlUdpSocketRecv(XL_SOCKET sock, uint8_t* buffer, uint16_t bufferSize) {
    (void)sock;
    (void)buffer;
    (void)bufferSize;
    // TCP not supported
    return 0;
}

int16_t xlUdpSocketSend(XL_SOCKET sock, const uint8_t* buffer, uint16_t bufferSize) {
    (void)sock;
    (void)buffer;
    (void)bufferSize;
    // TCP not supported
    return 0;
}


int16_t xlUdpSocketRecvFrom(XL_SOCKET sock, uint8_t* data, uint16_t size, uint8_t* addr, uint16_t* port) {

    XLstatus err;
    tUdpSockXl* s;
    T_XL_NET_ETH_EVENT rxEvent;
    XLrxHandle rxHandles[128];
    unsigned int rxCount = 128;
    
    if ((s = getSock(sock)) == NULL) return FALSE;
    if (!s->bound) {
        printf("ERROR: socket not bound!\n");
        return FALSE;
    }

    // Block
    HANDLE event_array[1];
    event_array[0] = s->event;
    WaitForMultipleObjects(1, event_array, 0, 100);

    // Read queue until empty
    for (;;) {

        err = xlNetEthReceive(s->networkHandle, &rxEvent, &rxCount, rxHandles);
        switch (err) {
        case XL_SUCCESS:
            break;
        case XL_ERR_QUEUE_IS_EMPTY:
            return 0;
        case XL_ERR_INSUFFICIENT_BUFFER:
            printf("ERROR: insufficient receive buffer!\n");
            return -1;
        default:
            printf("ERROR: xlNetEthReceive failed with error: x%x\n", err);
            return -1;
        }

        if (rxEvent.flagsChip & XL_ETH_QUEUE_OVERFLOW) {
            printf("ERROR: receive buffer overflow!\n");
        }

        if (rxEvent.tag == XL_ETH_EVENT_TAG_FRAMERX_SIMULATION) {

            T_XL_NET_ETH_DATAFRAME_RX* frameRx = &rxEvent.tagData.frameSimRx;
#if OPTION_ENABLE_PCAP
            if (gOptionPCAP) pcapWriteFrameRx(rxEvent.timeStampSync, frameRx);
#endif
            if (gUdpDebugLevel >= 5) printRxFrame(s, rxEvent.timeStampSync, frameRx); // Print all

            // ARP
            if (frameRx->frameData.ethFrame.etherType == HTONS(ARP)) {
                struct arp* arp = (struct arp*) & frameRx->frameData.ethFrame.payload[0];
                if (arp->hrd == HTONS(ARPHRD_ETHER) && arp->pro == HTONS(0x0800) && arp->op == HTONS(ARPOP_REQUEST)) { // Request
                    if (memcmp(arp->tpa, s->localAddr.addr, 4) == 0) {
                        udpSendARPResponse(s, arp->sha, arp->spa);
                        return 0;
                    }
                }
            } // ARP

            // IPV4
            else if (frameRx->frameData.ethFrame.etherType == HTONS(IPV4)) {
                struct iphdr* ip = (struct iphdr*) & frameRx->frameData.ethFrame.payload[0];

                if (ip->protocol == UDP) {

                    struct udphdr* udp = (struct udphdr*) & frameRx->frameData.ethFrame.payload[sizeof(struct iphdr)];
                    uint16_t l = HTONS(udp->len);
                    if (l <= sizeof(struct udphdr)) return 0;
                    l -= sizeof(struct udphdr);
                    if (l < size) size = l;

                    // Socket unicast or multicast addr and port
                    if (memcmp(ip->daddr, s->localAddr.addr, 4) == 0 && udp->dest == s->localAddr.port) { 
                        memcpy(data, &frameRx->frameData.ethFrame.payload[sizeof(struct iphdr) + sizeof(struct udphdr)], size); // return payload
                        memcpy(s->remoteAddr.mac, frameRx->sourceMAC, 6); // @@@@@@@@@@
                        memcpy(s->remoteAddr.addr, ip->saddr, 4);
                        if (addr) memcpy(addr, ip->saddr, 4);
                        if (port) *port = udp->source;
                        return size; // Receive size bytes
                    }
                        
                } // UDP

            } // IPv4

        } // XL_ETH_EVENT_TAG_FRAMERX_SIMULATION

        else if (rxEvent.tag == XL_ETH_EVENT_TAG_FRAMETX_ACK_SIMULATION) {
            // ignore
        }
        else { // Other XL-API events 
            if (!printEvent(s,&rxEvent)) printf("ERROR: xlNetEthReceive unexpected event tag %u!\n", rxEvent.tag);
        }

    } // for (;;)
}

// Returns <0 on error, 0 on would block, size when ok
// port in network byte order
int16_t xlUdpSocketSendTo(XL_SOCKET sock, const uint8_t* data, uint16_t size, const uint8_t* addr, uint16_t port) {

    XLstatus err;
    tUdpSockXl* s;
    T_XL_NET_ETH_DATAFRAME_TX frame;
    uint16_t iplen, udplen;

    if ((s = getSock(sock)) == NULL) return FALSE;
    if (!s->bound) {
        printf("ERROR: socket not bound!\n");
        return FALSE;
    }

    memset(&frame, 0, sizeof(T_XL_NET_ETH_DATAFRAME_TX));

    // header
    frame.flags |= XL_ETH_DATAFRAME_FLAGS_USE_SOURCE_MAC /*| XL_ETH_DATAFRAME_FLAGS_NO_TX_EVENT_GEN*/;
    memcpy(&frame.sourceMAC, s->localAddr.mac, sizeof(frame.sourceMAC));
    memcpy(&frame.destMAC, s->remoteAddr.mac, sizeof(frame.destMAC));
    frame.frameData.ethFrame.etherType = HTONS(IPV4);

    // payload
    struct iphdr* ip = (struct iphdr*) &frame.frameData.ethFrame.payload[0];
    udpInitIpHdr(ip, s->localAddr.addr, addr);
    struct udphdr* udp = (struct udphdr*) &frame.frameData.ethFrame.payload[sizeof(struct iphdr)];
    udpInitUdpHdr(udp, s->localAddr.port, port);

    // IP header
    iplen = (uint16_t)(sizeof(struct iphdr) + sizeof(struct udphdr) + size); // Total Length
    ip->tot_len = HTONS(iplen);
    ip->check = 0;
    uint16_t sum = ipChecksum((uint16_t*)ip, 10);
    ip->check = sum;

    // UDP header
    udplen = (uint16_t)(sizeof(struct udphdr) + size);
    udp->len = HTONS(udplen);
    udp->check = 0;

    // Data
    memcpy(&frame.frameData.ethFrame.payload[sizeof(struct iphdr) + sizeof(struct udphdr)], data, size);

    frame.dataLen = 2 + sizeof(struct iphdr) + udplen; // XL-API frame len = payload length + 2 for ethertype 
    if (frame.dataLen < XL_ETH_PAYLOAD_SIZE_MIN + 2) frame.dataLen = XL_ETH_PAYLOAD_SIZE_MIN + 2;
    if (gUdpDebugLevel >= 3) {
        printf("TX %s: ", s->portName);
        printIPV4Frame(&frame.frameData, frame.dataLen); //T_XL_ETH_FRAMEDATA
    }

    if (XL_SUCCESS != (err = xlNetEthSend(s->networkHandle, s->portHandle, (XLuserHandle)1, &frame))) {
        if (err == XL_ERR_QUEUE_IS_FULL) return 0; // Would block
        printf("ERROR: xlNetEthSend failed with ERROR: %s (%d)!\n", xlGetErrorString(err), err);
        return err>0 ? -err:err;
}

#if OPTION_ENABLE_PCAP
    if (gOptionPCAP) pcapWriteFrameTx(0, &frame);
#endif

    return size;
}


#endif


