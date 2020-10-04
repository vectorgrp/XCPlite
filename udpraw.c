


#include "udpraw.h"


#ifdef DTO_SEND_RAW

#ifdef TEST
#define PCKT_LEN 8192
char buffer[PCKT_LEN];
struct iphdr* ip = (struct iphdr*)buffer;
struct udphdr* udp = (struct udphdr*)(buffer + sizeof(struct iphdr));
unsigned char* rxcp = (XCP_DTO_MESSAGE*)(buffer + sizeof(struct iphdr) + sizeof(struct udphdr));
#endif


int gRawSock = 0;



static unsigned short csum(unsigned short *buf, int nwords)
{
  unsigned long sum;
  for(sum=0; nwords>0; nwords--)
  sum += *buf++;
  sum = (sum >> 16) + (sum &0xffff);
  sum += (sum >> 16);
  return (unsigned short)(~sum);
}



int udpRawSend( DTO_BUFFER *buf ) {

  // IP header
  buf->ip.tot_len = (unsigned short)(sizeof(struct iphdr) + sizeof(struct udphdr) + 4 + buf->xcp_size); // Total Length
  //ip->check = 0; //  csum((unsigned short*)ip, sizeof(struct iphdr) / 2);
  
  // UDP header
  buf->udp.len = htons( (sizeof(struct udphdr) + 4 + 1 + buf->xcp_size)&0xFFFE ); // UDP packet length must be even
  // udp->check = 0;

  //unsigned char tmp[32];
  //inet_ntop(AF_INET, &dst->sin_addr, tmp, sizeof(tmp));
  //printf("dst = sin_family=%u, addr=%s, port=%u\n", dst->sin_family, tmp, ntohs(dst->sin_port));
  //printf("len = %u, total = %u\n", len, ip->tot_len);
      
  // Send
  if (sendto(gRawSock, &buf->ip, buf->ip.tot_len, 0, (struct sockaddr*)dst, sizeof(struct sockaddr)) <= 0)  {
    perror("sendto()");
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

    printf("udpRawInit()\n");
    
    char tmp[32];
    inet_ntop(AF_INET, &src->sin_addr, tmp, sizeof(tmp));
    printf("src = sin_family=%u, addr=%s, port=%u\n", src->sin_family, tmp, ntohs(src->sin_port));
    printf("dst = sin_family=%u, addr=%s, port=%u\n", dst->sin_family, tmp, ntohs(dst->sin_port));

    
        
    // create a raw socket with UDP protocol
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
    if (bind(gRawSock, (struct sockaddr*)&gServerAddr, sizeof(gServerAddr)) < 0) {
        perror("Cannot bind on UDP port");
        return 0;
    }
    */

#ifdef TEST
    memset(buffer, 0, PCKT_LEN);
    udpRawInitIpHeader(ip, src, dst);
    udpRawInitUdpHeader(udp, src, dst);
#endif

    return 1;
}

#endif

