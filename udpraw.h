#pragma once

#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>

#include <sys/types.h>
#include <errno.h>

#include <sys/time.h>

#include <sys/socket.h>

#include <netinet/in.h>
#include <linux/ip.h>
#include <linux/udp.h>
#include <arpa/inet.h>


int udpRawSend(struct sockaddr_in* dst, unsigned char * buf, unsigned int len);
int udpRawInit(struct sockaddr_in* src);



