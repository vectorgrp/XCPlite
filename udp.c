/*----------------------------------------------------------------------------
| File:
|   udpserver.c
|
| Description:
|   XCP on UDP transport layer
|   Linux (Raspberry Pi) and Windows version
 ----------------------------------------------------------------------------*/

#include "xcpLite.h"
#include "xcpAppl.h"

#include "udp.h"


#ifdef _WIN 
#ifdef XCP_ENABLE_XLAPI

static tUdpSock udpSock;

#define HTONS(x) ((((x)&0x00ff) << 8) | (((x)&0xff00) >> 8))

#define IPV4 0x0800
#define ARP  0x0806
#define IPV6 0x86dd
#define UDP  17

struct udphdr
{
    vuint16 source;
    vuint16 dest;
    vuint16 len;
    vuint16 check;
};

struct iphdr
{
    vuint16 ver_ihl_dscp_ecn; // unsigned int version : 4;  unsigned int ihl : 4; unsigned int dscp : 6; unsigned int ecn : 2;
    vuint16 tot_len;
    vuint16 id;
    vuint16 frag_off;
    vuint8 ttl;
    vuint8 protocol;
    vuint16 check;
    vuint8 saddr[4]; // src addr, high byte first 
    vuint8 daddr[4]; // dst addr 
    /*The options start here. */
};

struct arp
{
    vuint16	hrd;		/* format of hardware address */
#define ARPHRD_ETHER 1	/* ethernet hardware format */
    vuint16	pro;		/* format of protocol address */
    vuint8	hln;		/* length of hardware address */
    vuint8	pln;		/* length of protocol address */
    vuint16	op;		/* one of: */
#define	ARPOP_REQUEST 1	/* request to resolve address */
#define	ARPOP_REPLY	2	/* response to previous request */
    vuint8 sha[6];	/* sender hardware address */
    vuint8 spa[4];	/* sender protocol address */
    vuint8 tha[6];	/* target hardware address */
    vuint8 tpa[4];	/* target protocol address */
};



static unsigned short ipChecksum(unsigned short* buf, int nwords) {

    unsigned long sum;
    for (sum = 0; nwords > 0; nwords--) sum += *buf++; // No HTONS here, one complement sum is independant from Motorola or Intel format
    while (sum >> 16) sum = (sum & 0xffff) + (sum >> 16);
    return (unsigned short)(~sum);
}


static void udpInitIpHdr(struct iphdr* ip, vuint8* src, vuint8* dst) {

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


static void udpInitUdpHdr(struct udphdr* udp, vuint16 src, vuint16 dst) {

    // UDP header (8 byte)
    udp->source = HTONS(src);         // Source Port
    udp->dest = HTONS(dst);           // Destination Port 
    udp->len = HTONS(sizeof(struct udphdr));    // Length: 8
    udp->check = 0; // Checsum not calculated                         

}





static int printIPv4Frame(const char* dir, const T_XL_ETH_FRAMEDATA* frameData, unsigned int frameLen) {

    if (frameData->ethFrame.etherType == HTONS(IPV4)) { // IPv4
        struct iphdr* ip = (struct iphdr*)&frameData->ethFrame.payload[0];
        unsigned short s = ipChecksum((unsigned short*)ip, 10);
        printf("%s: IPv4 %04X l=%u id=%u, fo=%u, ttl=%u, proto=%u, csum=%u %s ", dir, HTONS(ip->ver_ihl_dscp_ecn), HTONS(ip->tot_len), HTONS(ip->id), HTONS(ip->frag_off), ip->ttl, ip->protocol, HTONS(ip->check), s != 0 ? "NOK!" : "OK");
        printf("srcIP %u.%u.%u.%u ", ip->saddr[0], ip->saddr[1], ip->saddr[2], ip->saddr[3]);
        printf("dstIP %u.%u.%u.%u ", ip->daddr[0], ip->daddr[1], ip->daddr[2], ip->daddr[3]);
        if (ip->protocol == UDP) { // UDP
            struct udphdr* udp = (struct udphdr*)&frameData->ethFrame.payload[sizeof(struct iphdr)];
            printf("UDP l=%u %u->%u s=%u ", HTONS(udp->len), HTONS(udp->source), HTONS(udp->dest), HTONS(udp->check));
            for (unsigned int byte = 0; byte < HTONS(udp->len)-sizeof(struct udphdr); byte++) {
                printf("%02X ", frameData->ethFrame.payload[sizeof(struct iphdr) + sizeof(struct udphdr) + byte]);
            }
            printf("\n");

        }
        return 1;
    }
    return 0;
}

static int printARPFrame(const char *dir, const T_XL_ETH_FRAMEDATA* frameData, unsigned int frameLen) {
    if (frameData->ethFrame.etherType == HTONS(ARP)) { // ARP
        struct arp* arp = (struct arp*)&frameData->ethFrame.payload[0];
        printf("%s: ARP %u 0x%04X %u/%u %s", dir, HTONS(arp->hrd), HTONS(arp->pro), arp->hln, arp->pln, (1 == arp->op) ? "Req " : "Res ");
        printf("sha 0x%02X:0x%02X:0x%02X:0x%02X:0x%02X:0x%02X ", arp->sha[0], arp->sha[1], arp->sha[2], arp->sha[3], arp->sha[4], arp->sha[5]);
        printf("spa %u.%u.%u.%u ", arp->spa[0], arp->spa[1], arp->spa[2], arp->spa[3]);
        printf("tha 0x%02X:0x%02X:0x%02X:0x%02X:0x%02X:0x%02X ", arp->tha[0], arp->tha[1], arp->tha[2], arp->tha[3], arp->tha[4], arp->tha[5]);
        printf("tpa %u.%u.%u.%u\n", arp->tpa[0], arp->tpa[1], arp->tpa[2], arp->tpa[3]);
        return 1;
    }
    return 0;
}


static void printEthFrame(const T_XL_ETH_FRAMEDATA* frameData, unsigned int frameLen) {

            for (unsigned int byte = 0; byte < frameLen; byte++) {
                printf("0x%02X ", frameData->ethFrame.payload[byte]);
                if (byte % 16 == 15) printf("\n");
            }
            printf("\n");
}



static void printEthRxFrame(const T_XL_NET_ETH_DATAFRAME_RX* frame) {

    if (!printARPFrame("RX",&frame->frameData, frame->dataLen)) {
        if (!printIPv4Frame("RX",&frame->frameData, frame->dataLen)) {
            printf("TX: %u err=%u type=0x%04X: ", frame->dataLen, frame->errorFlags, HTONS(frame->frameData.ethFrame.etherType));
            printf("srcMAC 0x%02X:0x%02X:0x%02X:0x%02X:0x%02X:0x%02X ", frame->sourceMAC[0], frame->sourceMAC[1], frame->sourceMAC[2], frame->sourceMAC[3], frame->sourceMAC[4], frame->sourceMAC[5]);
            printf("dstMAC 0x%02X:0x%02X:0x%02X:0x%02X:0x%02X:0x%02X  ", frame->destMAC[0], frame->destMAC[1], frame->destMAC[2], frame->destMAC[3], frame->destMAC[4], frame->destMAC[5]);
            printEthFrame(&frame->frameData, frame->dataLen);
        }
    }
}

static void printEthTxFrame(const T_XL_NET_ETH_DATAFRAME_TX* frame) {

    if (!printARPFrame("TX",&frame->frameData, frame->dataLen)) {
        if (!printIPv4Frame("TX", &frame->frameData, frame->dataLen)) {
            printf("TX: %u type=0x%04X: ", frame->dataLen, HTONS(frame->frameData.ethFrame.etherType));
            printf("srcMAC 0x%02X:0x%02X:0x%02X:0x%02X:0x%02X:0x%02X ", frame->sourceMAC[0], frame->sourceMAC[1], frame->sourceMAC[2], frame->sourceMAC[3], frame->sourceMAC[4], frame->sourceMAC[5]);
            printf("dstMAC 0x%02X:0x%02X:0x%02X:0x%02X:0x%02X:0x%02X  ", frame->destMAC[0], frame->destMAC[1], frame->destMAC[2], frame->destMAC[3], frame->destMAC[4], frame->destMAC[5]);
            printEthFrame(&frame->frameData, frame->dataLen);
        }
    }
}


static void printEthernetEvent(const T_XL_NET_ETH_EVENT* pRxEvent) {

    switch (pRxEvent->tag) {
    case XL_ETH_EVENT_TAG_FRAMERX_ERROR_SIMULATION:
        printf("XL_ETH_EVENT_TAG_FRAMERX_ERROR_SIMULATION \n");
        break;
    case XL_ETH_EVENT_TAG_FRAMETX_ERROR_SIMULATION:
        printf("XL_ETH_EVENT_TAG_FRAMETX_ERROR_SIMULATION \n");
        break;
    case XL_ETH_EVENT_TAG_FRAMETX_ACK_SIMULATION:
        printf("TX Ack %llu\n",pRxEvent->timeStampSync);
        break;
    case XL_ETH_EVENT_TAG_FRAMERX_SIMULATION:
        printEthRxFrame(&pRxEvent->tagData.frameMeasureRx);
        break;
    case XL_ETH_EVENT_TAG_FRAMERX_ERROR_MEASUREMENT:
        printf("XL_ETH_EVENT_TAG_FRAMERX_ERROR_MEASUREMENT \n");
        break;
    case XL_ETH_EVENT_TAG_FRAMETX_ERROR_MEASUREMENT:
        printf("XL_ETH_EVENT_TAG_FRAMETX_ERROR_MEASUREMENT \n");
        break;
    case XL_ETH_EVENT_TAG_FRAMETX_MEASUREMENT:
        printf("XL_ETH_EVENT_TAG_FRAMETX_MEASUREMENT \n");
        break;
    case XL_ETH_EVENT_TAG_FRAMERX_MEASUREMENT:
        printf("XL_ETH_EVENT_TAG_FRAMERX_MEASUREMENT\n");
        break;
    case XL_ETH_EVENT_TAG_CHANNEL_STATUS:
        printf("LINK %s\n", (unsigned int)pRxEvent->tagData.channelStatus.link == XL_ETH_STATUS_LINK_UP ? "UP" : "DOWN");
        break;
    default:
        printf("Unknown Event\n");
        break;
    }
}


static int udpSendARPResponse(tUdpSock* sock, unsigned char sha[], unsigned char spa[]) {

    XLstatus err;
    T_XL_NET_ETH_DATAFRAME_TX frame;

    memset(&frame, 0, sizeof(T_XL_NET_ETH_DATAFRAME_TX));
    // header
    frame.dataLen = XL_ETH_PAYLOAD_SIZE_MIN + 2; // payload length +2 (with ethertype) 28 or XL_ETH_PAYLOAD_SIZE_MIN ???
    frame.flags |= XL_ETH_DATAFRAME_FLAGS_USE_SOURCE_MAC;
    memcpy(&frame.sourceMAC, sock->localAddr.sin_mac, sizeof(frame.sourceMAC));
    memcpy(&frame.destMAC, sha, sizeof(frame.destMAC));
    frame.frameData.ethFrame.etherType = HTONS(ARP);
    // payload
    struct arp* arp = (struct arp*)&frame.frameData.ethFrame.payload[0];
    arp->hrd = HTONS(ARPHRD_ETHER);		/* format of hardware address */
    arp->pro = HTONS(0x0800);		/* format of protocol address */
    arp->hln = 6;		/* length of hardware address */
    arp->pln = 4;		/* length of protocol address */
    arp->op = HTONS(ARPOP_REPLY);		/* one of: */
    memcpy(arp->sha, sock->localAddr.sin_mac, 6);
    memcpy(arp->spa, sock->localAddr.sin_addr, 4);
    memcpy(arp->tha, sha, 6);
    memcpy(arp->tpa, spa, 4);

    if (gXcpDebugLevel >= 1) {
        ApplXcpPrint("Send ARP response\n");
        if (gXcpDebugLevel >= 2) printEthTxFrame(&frame);
    }
    if (XL_SUCCESS != (err = xlNetEthSend(sock->networkHandle, sock->portHandle, (XLuserHandle)1, &frame))) {
        printf("error : xlNetEthSend failed with error: %s (%d)!\n", xlGetErrorString(err), err);
        return 0;
    }

    return 1;
}


int udpRecvFrom(tUdpSock *sock, unsigned char* data, unsigned int size, int mode, tUdpSockAddr* addr, int* socket_addr_size) {

    XLstatus err;
    T_XL_NET_ETH_EVENT rxEvent;
    XLrxHandle rxHandles[128];
    unsigned int rxCount = 128;

    for (;;) {

        err = xlNetEthReceive(sock->networkHandle, &rxEvent, &rxCount, rxHandles);
        switch (err) {
        case XL_SUCCESS:
            break;
        case XL_ERR_QUEUE_IS_EMPTY:
            return 0;
        case XL_ERR_INSUFFICIENT_BUFFER:
            printf("error: insufficient receive buffer!\n");
            return -1;
        default:
            printf("xlNetEthReceive failed with errir :x%x\n", err);
            return -1;
        }

        if (rxEvent.flagsChip & XL_ETH_QUEUE_OVERFLOW) {
            printf("error: receive buffer overflow!\n");
        }


        if (rxEvent.tag == XL_ETH_EVENT_TAG_FRAMERX_SIMULATION) {

            T_XL_NET_ETH_DATAFRAME_RX* frameRx = &rxEvent.tagData.frameSimRx;

            // ARP
            if (frameRx->frameData.ethFrame.etherType == HTONS(ARP)) {

                struct arp* arp = (struct arp*)&frameRx->frameData.ethFrame.payload[0];
                if (arp->hrd == HTONS(ARPHRD_ETHER) && arp->pro == HTONS(0x0800) && arp->op == HTONS(ARPOP_REQUEST)) { // Request
                    if (memcmp(arp->tpa, sock->localAddr.sin_addr, 4) == 0) {
                        udpSendARPResponse(sock,arp->sha,arp->spa);
                    }
                }
            }

            // IPV4
            else if (frameRx->frameData.ethFrame.etherType == HTONS(IPV4))
            {
                struct iphdr* ip = (struct iphdr*)&frameRx->frameData.ethFrame.payload[0];
                if (memcmp(ip->daddr, sock->localAddr.sin_addr, 4) == 0) { // for us
                    if (gXcpDebugLevel >= 3) printEthernetEvent(&rxEvent);
                    if (ip->protocol == UDP) {
                        struct udphdr* udp = (struct udphdr*)&frameRx->frameData.ethFrame.payload[sizeof(struct iphdr)];
                        if (HTONS(udp->dest) == sock->localAddr.sin_port) { // for us
                            // return payload
                            if ((unsigned int)HTONS(udp->len) < size) size = HTONS(udp->len);
                            memcpy(data, &frameRx->frameData.ethFrame.payload[sizeof(struct iphdr) + sizeof(struct udphdr)], size); 
                            // return remote addr
                            memcpy(addr->sin_mac, frameRx->sourceMAC, 6); 
                            memcpy(addr->sin_addr, ip->saddr, 4);
                            addr->sin_port = HTONS(udp->source);
                            return size;
                        }
                    }
                }
            }
        }

    }

    return 0;
}

int udpSendTo(tUdpSock *sock, const unsigned char* data, unsigned int size, int mode, tUdpSockAddr* addr, int addr_size) {

        XLstatus err;
        T_XL_NET_ETH_DATAFRAME_TX frame;
        unsigned short iplen,udplen;

        memset(&frame, 0, sizeof(T_XL_NET_ETH_DATAFRAME_TX));
        
        // header
        frame.flags |= XL_ETH_DATAFRAME_FLAGS_USE_SOURCE_MAC;
        memcpy(&frame.sourceMAC, sock->localAddr.sin_mac, sizeof(frame.sourceMAC));
        memcpy(&frame.destMAC, addr->sin_mac, sizeof(frame.destMAC));
        frame.frameData.ethFrame.etherType = HTONS(IPV4);
        
        // payload
        struct iphdr* ip = (struct iphdr*)&frame.frameData.ethFrame.payload[0];
        udpInitIpHdr(ip, sock->localAddr.sin_addr,addr->sin_addr);
        struct udphdr* udp = (struct udphdr*)&frame.frameData.ethFrame.payload[sizeof(struct iphdr)];
        udpInitUdpHdr(udp, sock->localAddr.sin_port,addr->sin_port);

        // IP header
        iplen = (unsigned short)(sizeof(struct iphdr) + sizeof(struct udphdr) + size); // Total Length
        ip->tot_len = HTONS(iplen);
        ip->check = 0;
        unsigned short s = ipChecksum((unsigned short*)ip, 10);
        ip->check = s; // HTONS(s);

        // UDP header
        //udplen = (sizeof(struct udphdr) + 1 + size) & 0xFFFE; // UDP packet length must be even
        udplen = (sizeof(struct udphdr) + size); 
        udp->len = HTONS(udplen);
        udp->check = 0;

        // Data
        memcpy(&frame.frameData.ethFrame.payload[sizeof(struct iphdr) + sizeof(struct udphdr)], data, size);

        frame.dataLen = 2 + sizeof(struct iphdr) + udplen; // XL-API frame len = payload length + 2 for ethertype 
        if (frame.dataLen < XL_ETH_PAYLOAD_SIZE_MIN + 2) frame.dataLen = XL_ETH_PAYLOAD_SIZE_MIN + 2; 
        if (gXcpDebugLevel>=3) printEthTxFrame(&frame);
        if (XL_SUCCESS != (err = xlNetEthSend(sock->networkHandle, sock->portHandle, (XLuserHandle)1, &frame))) {
            printf("error : xlNetEthSend failed with error: %s (%d)!\n", xlGetErrorString(err), err);
            return 0;
        }

        return size;
}

int udpGetLastError() {
    return 0;
}


// appname, net, seg, ipaddr, port -> netHandle, vpHandle, event
int udpInit(tUdpSock** pSock, XLhandle* pEvent, tUdpSockAddr* addr) {

    XLstatus err = XL_SUCCESS;
    
    tUdpSock* sock;
    sock = malloc(sizeof(tUdpSock));
    sock->localAddr = *addr;

    if (XL_SUCCESS != (err = xlOpenDriver())) {
        printf("error: xlOpenDriver failed with error: %s (%d)!\n", xlGetErrorString(err), err);
        return 0;
    }
    
    if (XL_SUCCESS != (err = xlNetEthOpenNetwork(XCP_SLAVE_NET, &sock->networkHandle, XCP_SLAVE_NAME, XL_ACCESS_TYPE_RELIABLE, 8 * 1024 * 1024))) {
        printf("error: xlNetEthOpenNetwork(NET1) failed with error: %s (%d)!\n", xlGetErrorString(err), err);
        return 0;
    }

    if (XL_SUCCESS != (err = xlNetAddVirtualPort(sock->networkHandle, XCP_SLAVE_SEG, XCP_SLAVE_NAME, &sock->portHandle, 1))) {
        printf("error : xlNetAddVirtualPort SEG1 failed with error: %s (%d)!\n", xlGetErrorString(err), err);
        return 0;
    }

    // Set notification handle (required to use event handling via WaitForMultipleObjects)
    if (XL_SUCCESS != (err = xlNetSetNotification(sock->networkHandle, pEvent, 1))) { // (1) - Notify for each single event in queue
        printf("error : xlNetSetNotification failed with error: %s (%d)!\n", xlGetErrorString(err), err);
        return 0;
    }

    if (XL_SUCCESS != (err = xlNetActivateNetwork(sock->networkHandle))) {
        printf("error: xlNetActivateNetwork failed: %s (%d)\n", xlGetErrorString(err), err);
        return 0;
    }

    printf("Init socket on virtual port " XCP_SLAVE_NET "-" XCP_SLAVE_SEG " with IP " XCP_SLAVE_IP_S " on UDP port %u \n", XCP_SLAVE_PORT);

    *pSock = sock;
    return 1;
}


void udpShutdown(tUdpSock* sock) {

    XLstatus err = XL_SUCCESS;

    if (XL_SUCCESS != (err = xlNetCloseNetwork(sock->networkHandle))) {
        printf("error: xlNetCloseNetwork failed: %s (%d)\n", xlGetErrorString(err), err);
    }

    free(sock);

    if (XL_SUCCESS != (err = xlCloseDriver())) {
        printf("error: xlCloseDriver failed: %s (%d)\n", xlGetErrorString(err), err);
    }
}


#endif
#endif

