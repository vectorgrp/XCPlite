


#include "udpraw.h"
#include "udpserver.h"



static unsigned short csum(unsigned short *buf, int nwords)
{
  unsigned long sum;
  for(sum=0; nwords>0; nwords--)
  sum += *buf++;
  sum = (sum >> 16) + (sum &0xffff);
  sum += (sum >> 16);
  return (unsigned short)(~sum);
}


#define PCKT_LEN 8192
char buffer[PCKT_LEN];
struct iphdr* ip = (struct iphdr*)buffer;
struct udphdr* udp = (struct udphdr*)(buffer + sizeof(struct iphdr));
//XCP_DTO_MESSAGE* rxcp = (XCP_DTO_MESSAGE*)(buffer + sizeof(struct iphdr) + sizeof(struct udphdr));
unsigned char * rxcp = (XCP_DTO_MESSAGE*)(buffer + sizeof(struct iphdr) + sizeof(struct udphdr));
int sd = 0;


int udpRawSend( struct sockaddr_in* dst, unsigned char *buf, unsigned int len) {

  // IP header
  ip->daddr = dst->sin_addr.s_addr;
  ip->tot_len = (unsigned short)(sizeof(struct iphdr) + sizeof(struct udphdr) + 4 + len); // Total Length

  ip->check = 0; //  csum((unsigned short*)ip, sizeof(struct iphdr) / 2);
  
  // UDP header
  udp->dest = dst->sin_port;
  udp->len = htons( (sizeof(struct udphdr) + 4 + 1 + len)&0xFFFE ); // UDP packet length must be even
  udp->check = 0;

  unsigned char tmp[32];
  inet_ntop(AF_INET, &dst->sin_addr, tmp, sizeof(tmp));
  //printf("dst = sin_family=%u, addr=%s, port=%u\n", dst->sin_family, tmp, ntohs(dst->sin_port));
  //printf("len = %u, total = %u\n", len, ip->tot_len);

  // XCP transport layer header header
  //rxcp->ctr = gLastResCtr++;
  //rxcp->dlc = (unsigned short)len;

  // XCP message
  for (int i = 0; i < len; i++)  rxcp[i] = buf[i]; //rxcp->data[i] = buf[i];
  
  // Send
  if (sendto(sd, buffer, ip->tot_len, 0, (struct sockaddr*)dst, sizeof(struct sockaddr)) <= 0)  {
    perror("sendto()");
    return 0;
  }
     
  return 1;
}


int udpRawInit(struct sockaddr_in* src ) {

    printf("udpRawInit()\n");
    
    char tmp[32];
    inet_ntop(AF_INET, &src->sin_addr, tmp, sizeof(tmp));
    printf("src = sin_family=%u, addr=%s, port=%u\n", src->sin_family, tmp, ntohs(src->sin_port));


    memset(buffer, 0, PCKT_LEN);
        
    // create a raw socket with UDP protocol
    sd = socket(AF_INET, SOCK_RAW, IPPROTO_UDP);
    if (sd < 0) {
        perror("socket() error");
        return 0;
    }
    printf("socket = %u\n",sd);

    // inform the kernel not to fill up the packet 
    int opt = 1;
    if (setsockopt(sd, IPPROTO_IP, IP_HDRINCL, &opt, sizeof(int)) < 0) {
        perror("setsockopt() error");
        return 0;
    }
    printf("socket option = IP_HDRINCL\n");

    // Bind the socket
    /*
    if (bind(sd, (struct sockaddr*)&gServerAddr, sizeof(gServerAddr)) < 0) {
        perror("Cannot bind on UDP port");
        return 0;
    }
    */

    // IP header (20 byte)
    ip->ihl = 0;
    ip->frag_off = 0;                   // Fragment offset : 0
    ip->version = 4;                    // Version: 4
    ip->ihl = 5;                        // Header Length: 20 bytes (5*4)
    ip->tos = 16;                       // Differentiated Services Field: 0x10 (DSCP: Unknown, ECN: Not-ECT) low delay
    ip->tot_len = sizeof(struct iphdr) + sizeof(struct udphdr); // Total Length 
    ip->id = htons(54321);              // Identification: 0xd431 (54321)
    ip->ttl = 64;                       // Time to live: 64
    ip->protocol = 17;                  // Protocol: UDP (17)
    ip->check = 0;                      // Header checksum : 0x0f47[validation disabled]
    ip->saddr = src->sin_addr.s_addr;   // Source: 172.31.31.194
    ip->daddr = 0;                      // Destination: not yet

    // UDP header (8 byte)
    udp->source = src->sin_port;         // Source Port
    udp->dest = 0;                              // Destination Port 
    udp->len = htons(sizeof(struct udphdr));    // Length: 8
    udp->check = 0;                             // Checksum: 0x0000

    return 1;
}

