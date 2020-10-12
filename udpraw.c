/*----------------------------------------------------------------------------
| File:
|   udpraw.cpp
|
| Description:
|   Simple UDP Stack for DAQ message transmission
|   Linux (Raspberry Pi) Version
 ----------------------------------------------------------------------------*/




#include "udpserver.h"
#include "udpraw.h"


#ifdef DTO_SEND_RAW

int gRawSock = 0;

#ifdef UDPRAW_CHECKSUM
static unsigned short csum(unsigned short *buf, int nwords)
{
  unsigned long sum;
  for(sum=0; nwords>0; nwords--)
  sum += *buf++;
  sum = (sum >> 16) + (sum &0xffff);
  sum += (sum >> 16);
  return (unsigned short)(~sum);
}
#endif



int udpRawSend( DTO_BUFFER *buf, struct sockaddr_in *dst) {

  // Assume dst saddr and sin_port are already set in the ip and udp headers
  // Just fill in the acual sizes of ip and udp 
  // Checksums are not calculated and filled in, for performance reasons

  // IP header
  buf->ip.tot_len = (unsigned short)(sizeof(struct iphdr) + sizeof(struct udphdr) + 4 + buf->xcp_size); // Total Length
  //ip->check = 0; //  csum((unsigned short*)ip, sizeof(struct iphdr) / 2);
  
  // UDP header
  buf->udp.len = htons( (sizeof(struct udphdr) + 4 + 1 + buf->xcp_size)&0xFFFE ); // UDP packet length must be even
  // udp->check = 0;

#if defined ( XCP_ENABLE_TESTMODE )
  if (gXcpDebugLevel >= 2) {
      unsigned char tmp[32];
      inet_ntop(AF_INET, &dst->sin_addr, tmp, sizeof(tmp));
      printf("dst = sin_family=%u, addr=%s, port=%u\n", dst->sin_family, tmp, ntohs(dst->sin_port));
      inet_ntop(AF_INET, &buf->ip.daddr, tmp, sizeof(tmp));
      printf("ip_addr=%s, udp_port=%u\n", tmp, ntohs(buf->udp.dest));
      printf("xcp_len = %u, tot_len = %u\n", buf->xcp_size, buf->ip.tot_len);
  }
#endif
      
  // Send layer 3 packet
  if (sendto(gRawSock, &buf->ip, buf->ip.tot_len, 0, (struct sockaddr*)dst, sizeof(struct sockaddr_in)) <= 0)  {
    perror("udpRawSend() sendto() failed");
    return 0;
  }
     
  return 1;
}


void udpRawInitIpHeader(struct iphdr *ip, struct sockaddr_in* src, struct sockaddr_in* dst) {

    // IP header(20 byte)
    ip->ihl = 0;
    ip->frag_off = 0;                   // Fragment offset : 0
    ip->version = 4;                    // Version: 4
    ip->ihl = 5;                        // Header Length: 20 bytes (5*4)
    ip->tos = 16;                       // Differentiated Services Field: 0x10 (DSCP: Unknown, ECN: Not-ECT) low delay
    ip->tot_len = sizeof(struct iphdr) + sizeof(struct udphdr); // Total Length 
    ip->id = htons(54321);              // Identification: 0xd431 
    ip->ttl = 64;                       // Time to live: 64
    ip->protocol = 17;                  // Protocol: UDP (17)
    ip->check = 0;                      // Header checksum 
    ip->saddr = src ? src->sin_addr.s_addr : 0;   // Source: 
    ip->daddr = dst ? dst->sin_addr.s_addr : 0;   // Destination: 

}


void udpRawInitUdpHeader(struct udphdr *udp, struct sockaddr_in* src, struct sockaddr_in* dst) {

    // UDP header (8 byte)
    udp->source = src ? src->sin_port : 0;         // Source Port
    udp->dest = dst ? dst->sin_port : 0;           // Destination Port 
    udp->len = htons(sizeof(struct udphdr));    // Length: 8
    udp->check = 0;                             // Checksum: 0x0000

}

int udpRawInit(struct sockaddr_in *src, struct sockaddr_in *dst) {

    if (gRawSock) return 1;

#ifdef XCP_ENABLE_TESTMODE
    if (gXcpDebugLevel >= 1) {
        char tmp[32];
        printf("udpRawInit()\n");
        inet_ntop(AF_INET, &src->sin_addr, tmp, sizeof(tmp));
        printf("src = addr=%s, port=%u\n", tmp, ntohs(src->sin_port));
        inet_ntop(AF_INET, &dst->sin_addr, tmp, sizeof(tmp));
        printf("dst = addr=%s, port=%u\n", tmp, ntohs(dst->sin_port));
    }
#endif
        
    // Create a raw socket with UDP protocol
    gRawSock = socket(AF_INET, SOCK_RAW, IPPROTO_UDP);
    if (gRawSock < 0) {
        perror("socket() error");
        return 0;
    }
    
    // Don't fill up the packet 
    int opt = 1;
    if (setsockopt(gRawSock, IPPROTO_IP, IP_HDRINCL, &opt, sizeof(int)) < 0) {
        perror("setsockopt() error");
        return 0;
    }
    
    // Bind the socket
    /*
    if (bind(gRawSock, (struct sockaddr*)&src, sizeof(struct sockaddr)) < 0) {
        perror("Cannot bind on UDP port");
        return 0;
    }
    */

    return 1;
}

#endif

