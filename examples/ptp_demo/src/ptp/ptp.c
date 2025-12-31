/*----------------------------------------------------------------------------
| File:
|   ptp.c
|
| Description:
|   PTP observer and master with XCP instrumentation
|   For analyzing PTP masters and testing PTP client stability
|   Supports IEEE 1588-2008 PTPv2 over UDP/IPv4 in E2E mode
|
|  Code released into public domain, no attribution required
|
 ----------------------------------------------------------------------------*/

#include <assert.h>   // for assert
#include <inttypes.h> // for PRIu64
#include <math.h>     // for fabs
#include <signal.h>   // for signal handling
#include <stdbool.h>  // for bool
#include <stdint.h>   // for uintxx_t
#include <stdio.h>    // for printf
#include <stdlib.h>   // for malloc, free
#include <string.h>   // for sprintf

#include "dbg_print.h" // for DBG_PRINT_ERROR, DBG_PRINTF_WARNING, ...
#include "filter.h"    // for average filter
#include "platform.h"  // from xcplib for SOCKET, socketSendTo, socketGetSendTime, ...

#include "ptp.h"
#include "ptpHdr.h" // PTP protocol message structures
#include "ptp_master.h"
#include "ptp_observer.h"

//-------------------------------------------------------------------------------------------------------
// XCP

#ifdef OPTION_ENABLE_XCP

#include <a2l.h>    // for xcplib A2l generation
#include <xcplib.h> // for xcplib application programming interface

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
        if (ptp->log_level >= 4)
            printFrame("RX", (struct ptphdr *)buffer, addr, rxTime); // Print incoming PTP traffic
        for (struct ptp_master *m = ptp->master_list; m != NULL; m = m->next) {
            masterHandleFrame(ptp, m, n, (struct ptphdr *)buffer, addr, rxTime);
        }
        observerHandleFrame(ptp, n, (struct ptphdr *)buffer, addr, rxTime);
    }
    if (ptp->log_level >= 3)
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
        if (ptp->log_level >= 4)
            printFrame("RX", (struct ptphdr *)buffer, addr, 0); // Print incoming PTP traffic
        for (struct ptp_master *m = ptp->master_list; m != NULL; m = m->next) {
            masterHandleFrame(ptp, m, n, (struct ptphdr *)buffer, addr, 0);
        }
        observerHandleFrame(ptp, n, (struct ptphdr *)buffer, addr, 0);
    }
    if (ptp->log_level >= 3)
        printf("Terminate PTP multicast 320 thread\n");
    socketClose(&ptp->sock320);
    return 0;
}

//-------------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------------
// Public functions

// Start A PTP master or observer instance
// instance_name: Name of the PTP instance for XCP event creation
// mode: PTP_MODE_OBSERVER or PTP_MODE_MASTER
// domain: PTP domain to use (0-127)
// uuid: 8 byte clock UUID
// If if_addr = INADDR_ANY, bind to given interface
// Enable hardware timestamps on interface (requires root privileges)
tPtpInterfaceHandle ptpCreateInterface(const uint8_t *if_addr, const char *if_name, uint8_t log_level) {

    tPtp *ptp = (tPtp *)malloc(sizeof(tPtp));
    memset(ptp, 0, sizeof(tPtp));
    ptp->magic = PTP_MAGIC;

    ptp->log_level = log_level;

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

    if (ptp->log_level >= 2) {
        if (useBindToDevice)
            printf("  Bound PTP sockets to if_name %s\n", if_name);
        else
            printf("  Bound PTP sockets to %u.%u.%u.%u:320/319\n", if_addr[0], if_addr[1], if_addr[2], if_addr[3]);
    }

    // Join PTP multicast group
    if (ptp->log_level >= 2)
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

    return (tPtpInterfaceHandle)ptp;
}

// Perform background tasks
// This is called from the application on a regular basis
// Observer: It monitors the status and checks for reset requests via calibration parameter
// Master: Send SYNC and ANNOUNCE messages
bool ptpTask(tPtpInterfaceHandle ptp_handle) {

    tPtp *ptp = (tPtp *)ptp_handle;
    assert(ptp != NULL && ptp->magic == PTP_MAGIC);

    for (struct ptp_master *m = ptp->master_list; m != NULL; m = m->next) {
        if (!masterTask(ptp, m))
            return false;
    }
    return true;
}

// Stop PTP
void ptpShutdown(tPtpInterfaceHandle ptp_handle) {

    tPtp *ptp = (tPtp *)ptp_handle;
    assert(ptp != NULL && ptp->magic == PTP_MAGIC);

    cancel_thread(ptp->threadHandle320);
    cancel_thread(ptp->threadHandle319);
    sleepMs(200);
    socketClose(&ptp->sock319);
    socketClose(&ptp->sock320);

    for (struct ptp_master *m = ptp->master_list; m != NULL;) {
        struct ptp_master *next = m->next;
        free(m);
        m = next;
    }
    ptp->master_list = NULL;

    for (tPtpObserver *c = ptp->observer_list; c != NULL;) {
        tPtpObserver *next = c->next;
        free(c);
        c = next;
    }
    ptp->observer_list = NULL;

    ptp->magic = 0;
    free(ptp);
}

// Set auto observer mode (accept announce from any master and create a new observer instance)
bool ptpEnableAutoObserver(tPtpInterfaceHandle ptp_handle) {

    tPtp *ptp = (tPtp *)ptp_handle;
    assert(ptp != NULL && ptp->magic == PTP_MAGIC);
    ptp->auto_observer_enabled = true;
    return true;
}

void ptpPrintState(tPtpInterfaceHandle ptp_handle) {

    tPtp *ptp = (tPtp *)ptp_handle;
    assert(ptp != NULL && ptp->magic == PTP_MAGIC);

    if (ptp->master_list != NULL) {
        for (tPtpMaster *m = ptp->master_list; m != NULL; m = m->next) {
            masterPrintState(ptp, m);
        }
    }
    if (ptp->observer_list != NULL) {
        printf("\nPTP Observer States:\n");
        for (tPtpObserver *obs = ptp->observer_list; obs != NULL; obs = obs->next) {
            observerPrintState(obs);
        }
        printf("\n");
    }
}