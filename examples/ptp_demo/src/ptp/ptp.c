/*----------------------------------------------------------------------------
| File:
|   ptp.c
|
| Description:
|   PTP demo client, observer and master implementation
|   PTP observer and master with XCP instrumentation
|   For analyzing PTP masters and testing PTP client stability
|   Supports IEEE 1588-2008 PTPv2 over UDP/IPv4 in E2E mode
|
|  Code released into public domain, no attribution required
|
 ----------------------------------------------------------------------------*/

#define _GNU_SOURCE
#include <assert.h>   // for assert
#include <fcntl.h>    // for open
#include <inttypes.h> // for PRIu64
#include <math.h>     // for fabs
#include <net/if.h>
#include <signal.h>  // for signal handling
#include <stdbool.h> // for bool
#include <stdint.h>  // for uintxx_t
#include <stdio.h>   // for printf
#include <stdlib.h>  // for malloc, free
#include <string.h>  // for sprintf
#include <sys/ioctl.h>
#include <unistd.h> // for close

#include "platform.h" // from xcplib for SOCKET, socketSendTo, socketGetSendTime, ...

#include "filter.h" // for average filter

extern uint8_t ptp_log_level;
#define DBG_LEVEL ptp_log_level
#include "dbg_print.h" // for DBG_PRINT_ERROR, DBG_PRINTF_WARNING, ...

#ifdef _LINUX
#include "phc.h"
#endif

#include "ptp.h"

#include "ptpHdr.h" // PTP protocol message structures
#include "ptp_client.h"

#ifdef OPTION_ENABLE_PTP_MASTER
#include "ptp_master.h"
#endif
#ifdef OPTION_ENABLE_PTP_OBSERVER
#include "ptp_observer.h"
#endif

//-------------------------------------------------------------------------------------------------------

// Print a PTP frame
static void printFrame(char *prefix, struct ptphdr *ptp_msg, uint8_t *addr, uint64_t rx_timestamp) {

    const char *s = NULL;
    switch (ptp_msg->type) {
    case PTP_ANNOUNCE:
        s = "ANNOUNCE";
        break;
    case PTP_SYNC:
        s = "SYNC";
        break;
    case PTP_FOLLOW_UP:
        s = "FOLLOW_UP";
        break;
    case PTP_DELAY_REQ:
        s = "DELAY_REQ";
        break;
    case PTP_DELAY_RESP:
        s = "DELAY_RESP";
        break;
    case PTP_PDELAY_REQ:
        s = "PDELAY_REQ";
        break;
    case PTP_PDELAY_RESP:
        s = "PDELAY_RESP";
        break;
    case PTP_PDELAY_RESP_FOLLOW_UP:
        s = "PDELAY_RESP_FOLLOW_UP";
        break;
    case PTP_SIGNALING:
        s = "SIGNALING";
        break;
    case PTP_MANAGEMENT:
        s = "MANAGEMENT";
        break;
    default:
        s = "UNKNOWN";
        break;
    }
    if (s != NULL) {
        printf("%s: %s (seqId=%u, timestamp=%" PRIu64 " from %u.%u.%u.%u - %02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X", prefix, s, htons(ptp_msg->sequenceId), rx_timestamp, addr[0],
               addr[1], addr[2], addr[3], ptp_msg->clockId[0], ptp_msg->clockId[1], ptp_msg->clockId[2], ptp_msg->clockId[3], ptp_msg->clockId[4], ptp_msg->clockId[5],
               ptp_msg->clockId[6], ptp_msg->clockId[7]);
        if (ptp_msg->type == PTP_DELAY_RESP)
            printf("  to %02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X", ptp_msg->u.r.clockId[0], ptp_msg->u.r.clockId[1], ptp_msg->u.r.clockId[2], ptp_msg->u.r.clockId[3],
                   ptp_msg->u.r.clockId[4], ptp_msg->u.r.clockId[5], ptp_msg->u.r.clockId[6], ptp_msg->u.r.clockId[7]);
        printf("\n");
    }
}

//---------------------------------------------------------------------------------------
// PTP message sending

// Init constant values in PTP header
static void initHeader(tPtp *ptp, struct ptphdr *h, uint8_t domain, const uint8_t *uuid, uint8_t type, uint16_t len, uint16_t flags, uint16_t sequenceId) {

    memset(h, 0, sizeof(struct ptphdr));
    h->version = 2;
    h->domain = domain;
    memcpy(h->clockId, uuid, 8);
    h->sourcePortId = htons(1);
    h->logMessageInterval = 0;
    h->type = type;
    h->len = htons(len);
    h->flags = htons(flags);
    h->sequenceId = htons(sequenceId);

    // Deprecated
    switch (type) {
    case PTP_ANNOUNCE:
        h->controlField = 0x05;
        break;
    case PTP_SYNC:
        h->controlField = 0x00;
        break;
    case PTP_FOLLOW_UP:
        h->controlField = 0x02;
        break;
    case PTP_DELAY_REQ:
        h->controlField = 0x01;
        break;
    case PTP_DELAY_RESP:
        h->controlField = 0x03;
        break;
    default:
        assert(0);
    }
}

#ifdef OPTION_ENABLE_PTP_MASTER
bool ptpSendAnnounce(tPtp *ptp, uint8_t master_domain, const uint8_t *master_uuid, uint16_t sequenceId) {

    struct ptphdr h;
    int16_t l;

    initHeader(ptp, &h, master_domain, master_uuid, PTP_ANNOUNCE, 64, 0, sequenceId);
    h.u.a.utcOffset = htons(announce_params.utcOffset);
    h.u.a.stepsRemoved = htons(announce_params.stepsRemoved);
    memcpy(h.u.a.grandmasterId, master_uuid, 8);
    h.u.a.clockVariance = htons(announce_params.clockVariance); // Allan deviation
    h.u.a.clockAccuraccy = announce_params.clockAccuraccy;
    h.u.a.clockClass = announce_params.clockClass;
    h.u.a.priority1 = announce_params.priority1;
    h.u.a.priority2 = announce_params.priority2;
    h.u.a.timeSource = announce_params.timeSource;
    l = socketSendTo(ptp->sock320, (uint8_t *)&h, 64, ptp->maddr, 320, NULL);

    if (ptp_log_level >= 3) {
        printf("TX: ANNOUNCE %u %02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X\n", sequenceId, h.clockId[0], h.clockId[1], h.clockId[2], h.clockId[3], h.clockId[4], h.clockId[5],
               h.clockId[6], h.clockId[7]);
    }

    return (l == 64);
}

bool ptpSendSync(tPtp *ptp, uint8_t domain, const uint8_t *master_uuid, uint64_t *sync_txTimestamp, uint16_t sequenceId) {

    struct ptphdr h;
    int16_t l;

    assert(sync_txTimestamp != NULL);

    initHeader(ptp, &h, domain, master_uuid, PTP_SYNC, 44, PTP_FLAG_TWO_STEP, sequenceId);
    *sync_txTimestamp = 0xFFFFFFFFFFFFFFFF;
    l = socketSendTo(ptp->sock319, (uint8_t *)&h, 44, ptp->maddr, 319, sync_txTimestamp /* != NULL request tx time stamp */);
    if (l != 44) {
        printf("ERROR: ptpSendSync: socketSendTo failed, returned l = %d\n", l);
        return false;
    }
    if (*sync_txTimestamp == 0) { // If timestamp not obtained during send, get it now
        *sync_txTimestamp = socketGetSendTime(ptp->sock319);
        if (*sync_txTimestamp == 0) {
            printf("ERROR: ptpSendSync: socketGetSendTime failed, no tx timestamp available\n");
            return false;
        }
    }
    if (ptp_log_level >= 3) {
        printf("TX: SYNC %u, tx time = %" PRIu64 "\n", sequenceId, *sync_txTimestamp);
    }
    return true;
}

bool ptpSendSyncFollowUp(tPtp *ptp, uint8_t domain, const uint8_t *master_uuid, uint64_t sync_txTimestamp, uint16_t sequenceId) {

    struct ptphdr h;
    int16_t l;

    initHeader(ptp, &h, domain, master_uuid, PTP_FOLLOW_UP, 44, 0, sequenceId);
    uint64_t t1 = sync_txTimestamp;
    uint32_t ti;
    h.timestamp.timestamp_s_hi = 0;
    ti = (uint32_t)(t1 / CLOCK_TICKS_PER_S);
    h.timestamp.timestamp_s = htonl(ti);
    ti = (uint32_t)(t1 % CLOCK_TICKS_PER_S);
    h.timestamp.timestamp_ns = htonl(ti);

    l = socketSendTo(ptp->sock320, (uint8_t *)&h, 44, ptp->maddr, 320, NULL);

    if (ptp_log_level >= 3) {
        char ts[64];
        printf("TX: FLUP %u t1 = %s (%" PRIu64 ")\n", sequenceId, clockGetString(ts, sizeof(ts), t1), t1);
    }
    return (l == 44);
}

bool ptpSendDelayResponse(tPtp *ptp, uint8_t domain, const uint8_t *master_uuid, struct ptphdr *client_req, uint64_t delayreg_rxTimestamp) {

    struct ptphdr h;
    int16_t l;

    assert(client_req != NULL);
    assert(client_req->type == PTP_DELAY_REQ);

    initHeader(ptp, &h, domain, master_uuid, PTP_DELAY_RESP, 54, 0, htons(client_req->sequenceId)); // copy sequence id
    h.correction = client_req->correction;                                                          // copy correction
    h.u.r.sourcePortId = client_req->sourcePortId;                                                  // copy from request egress port id
    memcpy(h.u.r.clockId, client_req->clockId, 8);                                                  // copy from request clock id

    // Set t4
    uint64_t t4 = delayreg_rxTimestamp;
    uint32_t ti;
    h.timestamp.timestamp_s_hi = 0;
    ti = (uint32_t)(t4 / CLOCK_TICKS_PER_S);
    h.timestamp.timestamp_s = htonl(ti);
    ti = (uint32_t)(t4 % CLOCK_TICKS_PER_S);
    h.timestamp.timestamp_ns = htonl(ti);

    l = socketSendTo(ptp->sock320, (uint8_t *)&h, 54, ptp->maddr, 320, NULL);

    if (ptp_log_level >= 3) {
        char ts[64];
        struct ptphdr *ptp = &h;
        printf("TX: DELAY_RESP %u to %02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X  t4 = %s (%" PRIu64 ")\n", htons(h.sequenceId), ptp->u.r.clockId[0], ptp->u.r.clockId[1],
               ptp->u.r.clockId[2], ptp->u.r.clockId[3], ptp->u.r.clockId[4], ptp->u.r.clockId[5], ptp->u.r.clockId[6], ptp->u.r.clockId[7], clockGetString(ts, sizeof(ts), t4),
               t4);
    }

    return (l == 54);
}

#endif

bool ptpSendDelayRequest(tPtp *ptp, uint8_t domain, const uint8_t *client_uuid, uint16_t sequenceId, uint64_t *txTimestamp) {

    struct ptphdr h;
    int16_t l;

    assert(txTimestamp != NULL);

    initHeader(ptp, &h, domain, client_uuid, PTP_DELAY_REQ, 44, 0, sequenceId);
    *txTimestamp = 0xFFFFFFFFFFFFFFFF;
    l = socketSendTo(ptp->sock319, (uint8_t *)&h, 44, ptp->maddr, 319, txTimestamp /* != NULL request tx time stamp */);
    if (l == 44) {
        if (*txTimestamp == 0) { // If timestamp not obtained during send, get it now
            *txTimestamp = socketGetSendTime(ptp->sock319);
            if (*txTimestamp == 0) {
                DBG_PRINT_ERROR("ptpSendDelayRequest: socketGetSendTime failed, no tx timestamp available\n");
                return false;
            }
        }
        if (ptp_log_level >= 3) {
            printf("TX: DELAY_REQ %u, domain=%u, client_uuid=%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X, tx timestamp t3 = %" PRIu64 "\n", sequenceId, domain, client_uuid[0],
                   client_uuid[1], client_uuid[2], client_uuid[3], client_uuid[4], client_uuid[5], client_uuid[6], client_uuid[7], *txTimestamp);
        }
        return true;
    } else {
        return false;
    }
}

//-------------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------------
// PTP threads for socket handling (319, 320) for both master and observer mode

// Time critical event messages (Sync, Delay_Req) on port 319
#if defined(_WIN) // Windows
static DWORD WINAPI ptpThread319(LPVOID par)
#else
static void *ptpThread319(void *par)
#endif
{
    uint8_t buffer[256];
    uint8_t addr[4];
    uint64_t rxTime;
    int n;

    tPtp *ptp = (tPtp *)par;
    assert(ptp != NULL && ptp->magic == PTP_MAGIC);
    for (;;) {
        n = socketRecvFrom(ptp->sock319, buffer, (uint16_t)sizeof(buffer), addr, NULL, &rxTime);
        if (n <= 0)
            break; // Terminate on error or socket close
        if (ptp_log_level >= 4)
            printFrame("RX", (struct ptphdr *)buffer, addr, rxTime); // Print incoming PTP traffic
#ifdef OPTION_ENABLE_PTP_CLIENT
        ptpClientHandleFrame(ptp, n, (struct ptphdr *)buffer, addr, rxTime);
#endif
#ifdef OPTION_ENABLE_PTP_MASTER
        masterHandleFrame(ptp, n, (struct ptphdr *)buffer, addr, rxTime);
#endif
#ifdef OPTION_ENABLE_PTP_OBSERVER
        observerHandleFrame(ptp, n, (struct ptphdr *)buffer, addr, rxTime);
#endif
    }
    if (ptp_log_level >= 3)
        printf("Terminate PTP multicast 319 thread\n");
    socketClose(&ptp->sock319);
    return 0;
}

// General messages (Announce, Follow_Up, Delay_Resp) on port 320
#if defined(_WIN) // Windows
static DWORD WINAPI ptpThread320(LPVOID par)
#else
static void *ptpThread320(void *par)
#endif
{
    uint8_t buffer[256];
    uint8_t addr[4];
    uint64_t rxTime;
    int n;

    tPtp *ptp = (tPtp *)par;
    assert(ptp != NULL && ptp->magic == PTP_MAGIC);
    for (;;) {
        n = socketRecvFrom(ptp->sock320, buffer, (uint16_t)sizeof(buffer), addr, NULL, NULL);
        if (n <= 0)
            break; // Terminate on error or socket close
        if (ptp_log_level >= 4)
            printFrame("RX", (struct ptphdr *)buffer, addr, 0); // Print incoming PTP traffic
#ifdef OPTION_ENABLE_PTP_CLIENT
        ptpClientHandleFrame(ptp, n, (struct ptphdr *)buffer, addr, 0);
#endif
#ifdef OPTION_ENABLE_PTP_MASTER
        masterHandleFrame(ptp, n, (struct ptphdr *)buffer, addr, 0);
#endif
#ifdef OPTION_ENABLE_PTP_OBSERVER
        observerHandleFrame(ptp, n, (struct ptphdr *)buffer, addr, 0);
#endif
    }
    if (ptp_log_level >= 3)
        printf("Terminate PTP multicast 320 thread\n");
    socketClose(&ptp->sock320);
    return 0;
}

//-------------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------------
// Public functions

// Start a PTP interface instance
// If if_addr = INADDR_ANY, bind to given interface
// Enables hardware timestamps on interface (requires root privileges)
tPtp *ptpCreateInterface(const uint8_t *if_addr, const char *if_name, bool sync_phc) {

    tPtp *ptp = (tPtp *)malloc(sizeof(tPtp));
    memset(ptp, 0, sizeof(tPtp));
    ptp->magic = PTP_MAGIC;

    // PTP communication parameters
    memcpy(ptp->if_addr, if_addr, 4);
    strncpy(ptp->if_name, if_name ? if_name : "", sizeof(ptp->if_name) - 1);

    // Create sockets for event (319) and general messages (320)
    ptp->sock319 = ptp->sock320 = INVALID_SOCKET;

    // For multicast reception on a specific interface:
    // - When if_addr is INADDR_ANY and interface is specified: bind to ANY and use socketBindToDevice (SO_BINDTODEVICE)
    // - When if_addr is specific: bind to that address (works only if multicast source is on same subnet)
    bool useBindToDevice = (if_name != NULL && if_addr[0] == 0 && if_addr[1] == 0 && if_addr[2] == 0 && if_addr[3] == 0);

    // SYNC with tx (master) or rx (observer) timestamp, DELAY_REQ - with rx timestamps
    if (!socketOpen(&ptp->sock319, SOCKET_MODE_BLOCKING | SOCKET_MODE_TIMESTAMPING))
        return NULL;
    if (!socketBind(ptp->sock319, if_addr, 319))
        return NULL;
    if (useBindToDevice && !socketBindToDevice(ptp->sock319, if_name))
        return NULL;

// @@@@ Test: Read hardware clock to check if adjusted to PTP timescale
#ifdef _LINUX
    if (useBindToDevice && sync_phc) {

        // Get PHC device path for the network interface
        int phc_index = phc_get_index(if_name);
        DBG_PRINTF3("PHC index for %s is %d\n", if_name, phc_index);
        if (phc_index >= 0) {
            char phc_device[32];
            snprintf(phc_device, sizeof(phc_device), "/dev/ptp%d", phc_index);
            DBG_PRINTF3("Attempting to open %s...\n", phc_device);

            clockid_t clkid = phc_open(phc_device);
            if (clkid != CLOCK_INVALID) {
                struct timespec phc_ts, sys_ts;
                if (clock_gettime(clkid, &phc_ts) == 0 && clock_gettime(CLOCK_REALTIME, &sys_ts) == 0) {
                    DBG_PRINTF3("Interface %s uses %s\n", if_name, phc_device);

                    // Calculate time difference
                    long diff_sec = phc_ts.tv_sec - sys_ts.tv_sec;
                    long abs_diff_sec = labs(diff_sec); // absolute difference in seconds
                    long diff_nsec = phc_ts.tv_nsec - sys_ts.tv_nsec;
                    long abs_diff_nsec = labs(diff_nsec);

                    // Convert to human-readable format
                    struct tm phc_tm, sys_tm;
                    gmtime_r(&phc_ts.tv_sec, &phc_tm);
                    gmtime_r(&sys_ts.tv_sec, &sys_tm);

                    char phc_str[64], sys_str[64];
                    strftime(phc_str, sizeof(phc_str), "%Y-%m-%d %H:%M:%S UTC", &phc_tm);
                    strftime(sys_str, sizeof(sys_str), "%Y-%m-%d %H:%M:%S UTC", &sys_tm);

                    DBG_PRINTF3("PHC time:    %s (%ld.%09ld)\n", phc_str, phc_ts.tv_sec, phc_ts.tv_nsec);
                    DBG_PRINTF3("System time: %s (%ld.%09ld)\n", sys_str, sys_ts.tv_sec, sys_ts.tv_nsec);

                    // Check if clock appears to be synchronized (within 1 second)
                    if (abs_diff_sec == 0 && abs_diff_nsec < 1000000 /* 1 millisecond */) {
                        DBG_PRINTF3("PHC is synchronized (diff: %ld nanoseconds)\n", diff_nsec);
                    } else {
                        if (abs_diff_sec == 0) {
                            DBG_PRINTF_WARNING("PHC is NOT synchronized, diff = %ld ns, PHC is %s)\n", diff_nsec, diff_nsec < 0 ? "behind" : "ahead");
                        } else {
                            long hours = abs_diff_sec / 3600;
                            long mins = (abs_diff_sec % 3600) / 60;
                            long secs = abs_diff_sec % 60;
                            DBG_PRINTF_WARNING("PHC is NOT synchronized (diff = %ld s = %ldh %ldm %lds, PHC is %s)\n", abs_diff_sec, hours, mins, secs,
                                               diff_sec < 0 ? "behind" : "ahead");
                        }

                        // Initialize PHC to system time (best effort)
                        DBG_PRINT3("Sync PHC\n");
                        phc_init_to_system_time(ptp->if_name, 5000000 /* offset in ns */);
                    }
                } else {
                    DBG_PRINT_ERROR("Failed to read PHC time\n");
                }
                phc_close(clkid);
            } else {
                DBG_PRINTF_ERROR("Failed to open %s (check permissions or run with sudo)\n", phc_device);
            }
        }
    }
#endif // _LINUX

    // General messages ANNOUNCE, FOLLOW_UP, DELAY_RESP - without rx timestamps
    if (!socketOpen(&ptp->sock320, SOCKET_MODE_BLOCKING))
        return NULL;
    if (!socketBind(ptp->sock320, if_addr, 320))
        return NULL;
    if (useBindToDevice && !socketBindToDevice(ptp->sock320, if_name))
        return NULL;

    // Enable hardware timestamps for SYNC tx and DELAY_REQ messages (requires root privileges)
    if (!socketEnableHwTimestamps(ptp->sock319, if_name, true /* tx + rx PTP only*/)) {
        DBG_PRINT_ERROR("Hardware timestamping not enabled (may need root), using software timestamps\n");
        // return false;
    }

    if (ptp_log_level >= 3) {
        if (useBindToDevice)
            printf("  Bound PTP sockets to if_name %s\n", if_name);
        else
            printf("  Bound PTP sockets to %u.%u.%u.%u:320/319\n", if_addr[0], if_addr[1], if_addr[2], if_addr[3]);
    }

    // Join PTP multicast group
    if (ptp_log_level >= 3)
        printf("  Listening for PTP multicast on 224.0.1.129 %s\n", if_name ? if_name : "");
    uint8_t maddr[4] = {224, 0, 1, 129};
    memcpy(ptp->maddr, maddr, 4);
    if (!socketJoin(ptp->sock319, ptp->maddr, if_addr, if_name))
        return NULL;
    if (!socketJoin(ptp->sock320, ptp->maddr, if_addr, if_name))
        return NULL;

    // Start all PTP threads
    mutexInit(&ptp->mutex, true, 1000);
    create_thread_arg(&ptp->threadHandle320, ptpThread320, ptp);
    create_thread_arg(&ptp->threadHandle319, ptpThread319, ptp);

    return ptp;
}

// Perform background tasks
// This is called from the application on a regular basis
// Observer: It monitors the status
// Master: Send SYNC and ANNOUNCE messages
bool ptpTask(tPtp *ptp) {

    assert(ptp != NULL && ptp->magic == PTP_MAGIC);
#ifdef OPTION_ENABLE_PTP_CLIENT
    ptpClientTask(ptp);
#endif
#ifdef OPTION_ENABLE_PTP_MASTER
    masterTask(ptp);
#endif
#ifdef OPTION_ENABLE_PTP_OBSERVER
    observerTask(ptp);
#endif
    return true;
}

// Stop a PTP interface
void ptpShutdown(tPtp *ptp) {

    assert(ptp != NULL && ptp->magic == PTP_MAGIC);

    cancel_thread(ptp->threadHandle320);
    cancel_thread(ptp->threadHandle319);
    sleepMs(200);
    socketClose(&ptp->sock319);
    socketClose(&ptp->sock320);

#ifdef OPTION_ENABLE_PTP_CLIENT
    ptpClientShutdown(ptp);
#endif

#ifdef OPTION_ENABLE_PTP_MASTER
    for (int i = 0; i < ptp->master_count; i++) {
        ptpMasterShutdown(ptp->master_list[i]);
        ptp->master_list[i] = NULL;
    }
    ptp->master_count = 0;
#endif

#ifdef OPTION_ENABLE_PTP_OBSERVER
    for (int i = 0; i < ptp->observer_count; i++) {
        ptpObserverShutdown(ptp->observer_list[i]);
        ptp->observer_list[i] = NULL;
    }
    ptp->observer_count = 0;
#endif

    ptp->magic = 0;
    free(ptp);
}

// Set auto observer mode (accept announce from any master and create a new observer instance)
#ifdef OPTION_ENABLE_PTP_OBSERVER
bool ptpEnableAutoObserver(tPtp *ptp, bool active_mode) {

    assert(ptp != NULL && ptp->magic == PTP_MAGIC);
    ptp->auto_observer = true;
    ptp->auto_observer_active_mode = active_mode;
    return true;
}
#endif

// Print current state
void ptpPrintState(tPtp *ptp) {

    assert(ptp != NULL && ptp->magic == PTP_MAGIC);

#ifdef OPTION_ENABLE_PTP_MASTER
    if (ptp->master_count > 0) {
        printf("\nPTP Master States:\n");
        for (int i = 0; i < ptp->master_count; i++) {
            masterPrintState(ptp, i);
        }
    }
#endif
#ifdef OPTION_ENABLE_PTP_OBSERVER
    if (ptp->observer_count > 0) {
        char ts[64];
        uint64_t t = clockGet();
        printf("\nPTP Observer States: (Systemtime = %s\n", clockGetString(ts, sizeof(ts), t));
        for (int i = 0; i < ptp->observer_count; i++) {
            observerPrintState(ptp, ptp->observer_list[i]);
        }
        printf("\n");
    }
#endif
}