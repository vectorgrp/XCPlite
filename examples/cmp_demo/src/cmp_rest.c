/*----------------------------------------------------------------------------
| File:
|   cmp_rest.c
|
| Description:
|   Minimal HTTP/1.1 server for the read only part of the ASAM CMP REST interface (12.3).
|   One thread, one connection at a time, Connection: close. See cmp_rest.h.
|
| Code released into public domain, no attribution required
 ----------------------------------------------------------------------------*/

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <platform.h> // for THREAD_HANDLE, create_thread, join_thread, sleepMs

#include "cmp.h"
#include "cmp_backend.h"
#include "cmp_discovery.h"
#include "cmp_rest.h"

#define REST_REQUEST_MAX 2048
#define REST_BODY_MAX 4096
#define REST_ACCEPT_POLL_MS 200

// Identification of this capture module (12.3.2, Table 84)
// VendorId 0 is not an ASAM registered vendor: this is an emulation, not a product.
#define REST_VENDOR_ID 0
#define REST_DEVICE_DESCRIPTION "XCPlite cmp_demo, emulated ASAM CMP capture module"
#define REST_HARDWARE_VERSION "none (software emulation)"
#define REST_SOFTWARE_VERSION "cmp_demo 1.0.0"

// Transport option for reception of Control and Transmit Data Messages (12.3.2, Table 84)
#define REST_TRANSPORT_UDP_IPV4 1

// Feature Support Bitmask for ETHERNET_DATA_MSG (8.3.1.2, Table 62).
// Capture: bits 0-3 and 7 are the mandatory flags and are fixed to 1; bits 4-6
// (FRAME_TOO_LONG_ERR, PHY_ERR, FRAME_TRUNCATED) are optional and we detect none of them.
#define REST_ETH_FEATURES_CAP 0x0000008Fu
// Transmit: only bit 8 FCS_SENDING is definable and we do not support a tool defined FCS.
#define REST_ETH_FEATURES_TX 0x00000000u

// Transmission Support Bitmask (12.3.4, Table 89).
// Bit 0 TIMESTAMP_IMMEDIATE only: we send every request straight away and support neither
// absolute nor relative scheduling, no deadline and no segmentation. 7.2.2 explicitly
// allows that - "If the CM does not support Timestamp, it shall always send immediately".
#define REST_TRANSMISSION_SUPPORT 0x00000001u

#define REST_INTERFACE_STATUS_UP 0x01 // 7.3.16, Table 56

//-------------------------------------------------------------------------------

static THREAD_HANDLE sThread;
static bool sRunning = false;
static volatile bool sThreadUp = false;
static volatile bool sStop = false;
static int sListenFd = -1;

//-------------------------------------------------------------------------------
// Response bodies

static int bodyVersionInfo(char *buf, size_t size) {
    // 12.3.1: CmpVersion is the CMP major version, ApiVersion 0x01 means {apiVersion} = v1
    return snprintf(buf, size, "{\"CmpVersion\":%u,\"ApiVersion\":1}", CMP_VERSION);
}

static int bodyIdentification(char *buf, size_t size, const tCmpBackendStatus *s) {
    return snprintf(buf, size,
                    "{"
                    "\"VendorId\":%u,"
                    "\"DeviceDescription\":\"%s\","
                    "\"SerialNumber\":\"cmp_demo-%04X\","
                    "\"HardwareVersion\":\"%s\","
                    "\"SoftwareVersion\":\"%s\","
                    "\"DeviceId\":%u,"
                    "\"CmpListeningTransportOption\":%u,"
                    "\"CmpListeningMac\":\"\","
                    "\"CmpListeningIP\":\"%s\","
                    "\"CmpListeningPort\":%u"
                    "}",
                    REST_VENDOR_ID, REST_DEVICE_DESCRIPTION, s->device_id, REST_HARDWARE_VERSION, REST_SOFTWARE_VERSION, s->device_id, REST_TRANSPORT_UDP_IPV4, s->local_ip,
                    s->local_port);
}

static int bodyInterfaces(char *buf, size_t size, const tCmpBackendStatus *s) {
    // One interface carrying one stream. DataMessagePayloadType 0x08 is ETHERNET_DATA_MSG:
    // the interface captures and transmits complete Ethernet frames.
    //
    // The Transmitter object is what tells the Data Sink that injection is possible
    // (7.2.2). AggregationMtu is the largest CMP message we can accept, which on this path
    // is bounded by the outer MTU because 6.4.2 forbids IP fragmentation; AggregationCount
    // is 1 because one CMP message carries exactly one frame.
    return snprintf(buf, size,
                    "{\"Interfaces\":[{"
                    "\"InterfaceId\":%u,"
                    "\"DataMessagePayloadType\":%u,"
                    "\"InterfaceStatus\":%u,"
                    "\"InterfaceDescription\":\"Emulated XCP ECU link\","
                    "\"FeatureSupportBitmask\":%u,"
                    "\"Streams\":[{"
                    "\"StreamId\":%u,"
                    "\"StreamDescription\":\"Captured ECU traffic\","
                    "\"SupportForDataSinkReadyToReceive\":false,"
                    "\"SinkDeviceId\":0,"
                    "\"TransportOption\":%u,"
                    "\"DestinationMac\":\"\","
                    "\"DestinationIp\":\"%s\","
                    "\"DestinationPort\":%u,"
                    "\"Mtu\":%u"
                    "}],"
                    "\"Transmitter\":{"
                    "\"TransmissionSupportBitmask\":%u,"
                    "\"FeatureSupportBitmask\":%u,"
                    "\"AggregationMtu\":%u,"
                    "\"AggregationCount\":1"
                    "}"
                    "}]}",
                    s->interface_id, CMP_PAYLOAD_ETHERNET, REST_INTERFACE_STATUS_UP, REST_ETH_FEATURES_CAP, s->stream_id, REST_TRANSPORT_UDP_IPV4, s->sink_ip, s->sink_port,
                    s->max_message, REST_TRANSMISSION_SUPPORT, REST_ETH_FEATURES_TX, s->max_message);
}

static int bodyMeasurement(char *buf, size_t size, const tCmpBackendStatus *s) {
    // 12.3.6, Table 96/97. The demo starts capturing on its own, so the stream is
    // "transmitting" as soon as a Data Sink address is known and "inactive" before that -
    // there is nowhere to send yet. We never enter inactive_error.
    return snprintf(buf, size,
                    "{"
                    "\"CaptureModuleState\":\"active\","
                    "\"Message\":\"captured %llu, transmitted %llu, dropped %llu\","
                    "\"StateOfStreams\":[{\"StreamId\":%u,\"State\":\"%s\"}]"
                    "}",
                    (unsigned long long)s->n_wrapped, (unsigned long long)s->n_unwrapped, (unsigned long long)s->n_dropped, s->stream_id,
                    s->sink_known ? "transmitting" : "inactive");
}

//-------------------------------------------------------------------------------

static void sendResponse(int fd, int status, const char *reason, const char *content_type, const char *body, size_t body_len) {
    char header[256];
    int n = snprintf(header, sizeof(header),
                     "HTTP/1.1 %d %s\r\n"
                     "Content-Type: %s\r\n"
                     "Content-Length: %zu\r\n"
                     "Cache-Control: no-store\r\n"
                     "Connection: close\r\n"
                     "\r\n",
                     status, reason, content_type, body_len);
    if (n <= 0) {
        return;
    }
    if (write(fd, header, (size_t)n) < 0) {
        return;
    }
    if (body_len > 0 && write(fd, body, body_len) < 0) {
        return;
    }
}

static void sendJson(int fd, const char *body, int body_len) {
    if (body_len < 0) {
        sendResponse(fd, 500, "Internal Server Error", "text/plain", "response too large\n", 19);
        return;
    }
    sendResponse(fd, 200, "OK", "application/json", body, (size_t)body_len);
}

static void handleRequest(int fd) {

    char request[REST_REQUEST_MAX];
    ssize_t n = read(fd, request, sizeof(request) - 1);
    if (n <= 0) {
        return;
    }
    request[n] = 0;

    // Only the request line matters: "<METHOD> <PATH> HTTP/1.x"
    char method[16] = {0};
    char path[256] = {0};
    if (sscanf(request, "%15s %255s", method, path) != 2) {
        sendResponse(fd, 400, "Bad Request", "text/plain", "malformed request line\n", 23);
        return;
    }
    // Ignore a query string, none of these methods take parameters
    char *query = strchr(path, '?');
    if (query != NULL) {
        *query = 0;
    }

    if (strcmp(method, "GET") != 0) {
        // Everything that would change configuration is deliberately absent, see cmp_rest.h
        sendResponse(fd, 405, "Method Not Allowed", "text/plain", "this capture module is read only\n", 33);
        return;
    }

    tCmpBackendStatus status;
    if (!cmpBackendGetStatus(&status)) {
        sendResponse(fd, 503, "Service Unavailable", "text/plain", "capture module not started yet\n", 31);
        return;
    }

    char body[REST_BODY_MAX];
    if (strcmp(path, "/asam-cmp/version-info") == 0) {
        sendJson(fd, body, bodyVersionInfo(body, sizeof(body)));
    } else if (strcmp(path, "/asam-cmp/v1/identification") == 0) {
        sendJson(fd, body, bodyIdentification(body, sizeof(body), &status));
    } else if (strcmp(path, "/asam-cmp/v1/interfaces") == 0) {
        sendJson(fd, body, bodyInterfaces(body, sizeof(body), &status));
    } else if (strcmp(path, "/asam-cmp/v1/measurement") == 0) {
        sendJson(fd, body, bodyMeasurement(body, sizeof(body), &status));
    } else {
        sendResponse(fd, 404, "Not Found", "text/plain",
                     "Implemented: /asam-cmp/version-info, /asam-cmp/v1/identification,\n"
                     "             /asam-cmp/v1/interfaces, /asam-cmp/v1/measurement\n",
                     125);
    }
}

// This thread services the whole control plane of the capture module: the HTTP listener
// and, when it is running, the CMP discovery socket (12.1.1). Discovery gets no thread of
// its own because it is stateless, answers one datagram at a time, and has to advertise
// the very HTTP port this thread serves.
static THREAD_FUNC_RETURN restThread(void *arg) {
    (void)arg;
    sThreadUp = true;
    while (!sStop) {
        struct pollfd pfd[2];
        pfd[0] = (struct pollfd){.fd = sListenFd, .events = POLLIN, .revents = 0};
        nfds_t nfds = 1;
        int discovery_fd = cmpDiscoveryFd();
        if (discovery_fd >= 0) {
            pfd[1] = (struct pollfd){.fd = discovery_fd, .events = POLLIN, .revents = 0};
            nfds = 2;
        }
        int r = poll(pfd, nfds, REST_ACCEPT_POLL_MS);
        if (r < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        if (r == 0) {
            continue; // timeout, re-check sStop
        }
        if (nfds == 2 && (pfd[1].revents & POLLIN) != 0) {
            cmpDiscoveryService();
        }
        if ((pfd[0].revents & POLLIN) == 0) {
            continue; // nothing to accept
        }
        int fd = accept(sListenFd, NULL, NULL);
        if (fd < 0) {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            }
            break;
        }
        handleRequest(fd);
        close(fd);
    }
    THREAD_FUNC_END;
}

//-------------------------------------------------------------------------------

bool cmpRestStart(uint16_t port) {

    if (sRunning) {
        return true;
    }
    sStop = false;

    sListenFd = socket(AF_INET, SOCK_STREAM, 0);
    if (sListenFd < 0) {
        printf("ERROR: cmpRestStart: socket failed (errno=%d, %s)\n", errno, strerror(errno));
        return false;
    }
    int one = 1;
    if (setsockopt(sListenFd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)) < 0) {
        printf("WARNING: cmpRestStart: SO_REUSEADDR failed (errno=%d, %s)\n", errno, strerror(errno));
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);
    if (bind(sListenFd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        printf("ERROR: cmpRestStart: bind to TCP port %u failed (errno=%d, %s)%s\n", port, errno, strerror(errno),
               (errno == EACCES) ? "\n  Ports below 1024 need privileges; 12.3 only says the REST interface SHOULD use port 80." : "");
        close(sListenFd);
        sListenFd = -1;
        return false;
    }
    if (listen(sListenFd, 4) < 0) {
        printf("ERROR: cmpRestStart: listen failed (errno=%d, %s)\n", errno, strerror(errno));
        close(sListenFd);
        sListenFd = -1;
        return false;
    }

    // create_thread() returns 0 on success on POSIX and Windows alike, but it still cannot
    // be tested PORTABLY: both FreeRTOS variants are statements which assert, so
    // "if (create_thread(...))" does not compile there. Wait for the thread to announce
    // itself instead - portable, and it proves the thread is running rather than merely
    // created. That matters here because the listen socket is already bound at this point,
    // so a thread that never starts would still accept connections at the kernel backlog
    // and then answer none of them, which looks like a hang rather than an error.
    sThreadUp = false;
    create_thread(&sThread, NULL, restThread, NULL);
    for (int i = 0; i < 100 && !sThreadUp; i++) {
        sleepMs(2);
    }
    if (!sThreadUp) {
        printf("ERROR: cmpRestStart: the REST thread did not start\n");
        close(sListenFd);
        sListenFd = -1;
        return false;
    }

    sRunning = true;
    printf("  CMP REST interface: http://<this host>:%u/asam-cmp/version-info\n", port);
    return true;
}

void cmpRestStop(void) {
    if (!sRunning) {
        return;
    }
    sStop = true;
    join_thread(sThread);
    if (sListenFd >= 0) {
        close(sListenFd);
        sListenFd = -1;
    }
    sRunning = false;
}
