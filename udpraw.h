
/* udpraw.h */

#ifndef __UDPRAW_H_
#define __UDPRAW_H_

#ifdef DTO_SEND_RAW

#ifdef __cplusplus
extern "C" {
#endif

extern int gRawSock;

void udpRawInitIpHeader(struct iphdr *ip, struct sockaddr_in *src, struct sockaddr_in *dst);
void udpRawInitUdpHeader(struct udphdr *udp, struct sockaddr_in *src, struct sockaddr_in *dst);

int udpRawSend(tXcpDtoBuffer* buf, struct sockaddr_in* dst);
int udpRawInit(struct sockaddr_in *src, struct sockaddr_in* dst );

#ifdef __cplusplus
}
#endif

#endif

#endif

