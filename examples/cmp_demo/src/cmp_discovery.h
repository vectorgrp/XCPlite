#pragma once

/*----------------------------------------------------------------------------
| File:
|   cmp_discovery.h
|
| Description:
|   IP multicast based discovery of this emulated Capture Module (12.1.1).
|
|   Section 12 requires a Capture Module to support AT LEAST ONE of three approaches to
|   address configuration and discovery:
|     - static configuration without any discovery   (what this demo did before)
|     - the XCP based approach of 12.1               <- implemented here
|     - Multicast DNS / DNS-SD of 12.2               (not implemented, see the repository docs/XCP_DISCOVERY.md)
|
|   12.1 is titled "XCP-based approach" and means it literally: the request is an ordinary
|   XCP packet in the ordinary XCP on Ethernet transport header, with command code 0xF2
|   (CC_TRANSPORT_LAYER_CMD) and sub command 0x10. It is answered before, and independently
|   of, any XCP session - a Data Sink uses it to learn our IP address and, decisively, the
|   HTTP port of the REST interface it then configures us through.
|
|   Deliberately NOT in libxcplite: the response advertises an HTTP port for a REST
|   interface, which is a CMP concept the library has no business knowing. The library's
|   own multicast code is also unusable here - docs/SOCKET_RAW.md excludes
|   XCPTL_ENABLE_MULTICAST from the raw transport because socketJoin is not implemented
|   there. This is its own socket, its own group, its own datagram.
|
|   No thread of its own: the socket is serviced by the REST thread, which is already in a
|   poll loop and already knows the HTTP port that the response has to carry.
|
| Code released into public domain, no attribution required
 ----------------------------------------------------------------------------*/

#include <stdbool.h>
#include <stdint.h>

// The group and port are fixed by 12.1: IPv4 destination 239.255.0.0, UDP destination
// port 5556. The spec notes that the port was never registered with IANA and that XCP
// uses it "because a private and closed network is assumed".
#define CMP_DISCOVERY_GROUP "239.255.0.0"
#define CMP_DISCOVERY_PORT 5556

typedef struct {
    uint16_t http_port;      // REST port to advertise (12.3), the point of the whole exchange
    const char *description; // DeviceDescription, not copied, must outlive the responder
    const char *serial;      // SerialNumber, not copied
} tCmpDiscoveryConfig;

// Open the discovery socket and join the multicast group.
// Returns false if the port cannot be bound or the group cannot be joined; the demo then
// runs without discovery, which 12 still permits as "static configuration".
bool cmpDiscoveryStart(const tCmpDiscoveryConfig *config);

// The socket, for the caller's poll(). Returns -1 when discovery is not running.
int cmpDiscoveryFd(void);

// Read and answer one datagram. Call when cmpDiscoveryFd() is readable.
void cmpDiscoveryService(void);

// Close the socket. Safe to call if discovery was never started.
void cmpDiscoveryStop(void);

// Number of discovery requests answered so far, for the test script and the log.
uint64_t cmpDiscoveryCount(void);
