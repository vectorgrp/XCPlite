/*----------------------------------------------------------------------------
| File:
|   ptp.c
|
| Description:
|   PTP implementation for XCP server
|   Based on ptpClient, the PTP protocol implementation
|   Note:
|     Unfortunatly, this is not a wallclock, because it based on local XL-API clock
|     The XL-API clock can not be read by request
|     ptpClockGet() returns the master clock value for the last received XL-API event
 ----------------------------------------------------------------------------*/

#include <assert.h>  // for assert
#include <signal.h>  // for signal handling
#include <stdbool.h> // for bool
#include <stdint.h>  // for uintxx_t
#include <stdio.h>   // for printf
#include <string.h>  // for sprintf

#include "dbg_print.h" // for DBG_LEVEL, DBG_PRINT3, DBG_PRINTF4, DBG...
#include "platform.h"

#include <a2l.h>    // for xcplib A2l generation
#include <xcplib.h> // for xcplib application programming interface

#include "ptp.h"
#include "ptpClient.h"
#include "util.h"
#include "xcpLite.h"

// Sync parameter set for master clock
typedef struct {
    bool Sync;                 // Sync status
    uint64_t InitialOffset;    // initial offset of local time
    int64_t RefOffset;         // filtered local clock offset to grandmaster
    int64_t errorCompensation; // Accumulated offset error
    uint64_t RefTime;          // estimated local time for RefOffset
    int32_t Drift;             // estimated and filtered local clock drift to grandmaster
    int64_t Offset;            // correction offset calculated and used in last clockGet() call
} tSync;

// PTP status
typedef struct {
    bool Enabled;   // PTP enabled
    uint8_t Domain; // Domain to be used
    tSync s;        // Current sync parameter set
    MUTEX Mutex;
} tPtp;

static tPtp gPtp;

//-------------------------------------------------------------------------------------------------------

static void syncInit(tSync *s) {
    assert(s);
    s->Sync = false;
    s->errorCompensation = 0;
    s->RefOffset = 0;
    s->RefTime = 0;
    s->Drift = 0;
    s->Offset = 0;
    s->InitialOffset = 0;
}

static uint64_t syncTime(uint64_t local_time) {

    uint64_t tc;
    uint64_t t = local_time - gPtp.s.InitialOffset;

    if (gPtp.s.Sync) {

        if (t < gPtp.s.RefTime) {
            return 0;
        }

        // Time passed since ref time/ref_offset
        int64_t td = (int64_t)(t - gPtp.s.RefTime);

        // Extrapolate current offset from ref_offset/ref_time with current drift estimation
        // Positive offset means local time is ahead, positive drift means offset is growing
        int64_t drift_corr = +(gPtp.s.Drift * td) / 1000000000;
        gPtp.s.Offset = gPtp.s.RefOffset + drift_corr;
        // printf("drift corr = %" PRId64 "\n", drift_corr);
    }

    // Calculate current synchronized time
    if (gPtp.s.Offset >= 0) {
        tc = t - (uint64_t)+gPtp.s.Offset;
    } else {
        tc = t + (uint64_t)-gPtp.s.Offset;
    }

    return tc;
}

#define DRIFT_LIMIT 100000 // 100 ppm
#define ERROR_LIMIT 100000 // 100 us

// Update clock servo to latest master/local timestamp pair and drift estimation
// Called asynchronously by ptpThread after new SYNC or DELAY_REQUEST, so it can be up to 30ms delayed in time
static void syncUpdate(tSync *s, uint64_t grandmaster_time, uint64_t local_time, int32_t drift) {

    tPtpMaster *m = ptpClientGetGrandmaster();

    if (m == NULL || local_time == 0) {
        s->Sync = false;
        printf("\nPTP: SYNC lost\n\n");
    } else {

        if (!s->Sync) { // ReSYNC
            if (drift > -DRIFT_LIMIT && drift < +DRIFT_LIMIT) {
                syncInit(s);
                s->InitialOffset = (uint64_t)(local_time - grandmaster_time);
                s->Sync = true;
                printf("\nPTP: SYNC\n\n");
            } else {
                return;
            }
        }

        uint64_t t0 = syncTime(local_time); // calculate old master time with current corrections

        // Calculate new master/server offset reference point (RefTime,RefOffset)
        uint64_t t = local_time - gPtp.s.InitialOffset;
        s->RefOffset = (int64_t)(t - grandmaster_time);
        s->RefTime = t;
        s->Drift = drift;

        uint64_t t1 = syncTime(local_time); // calculate new master time with new corrections

        int64_t diff = t1 - t0;
        if (diff < 0) {                   // Avoid possible declining timestamp by increasung offset, accumulate error
            s->RefOffset -= diff;         // increase offset
            s->errorCompensation += diff; // accumulate error
        } else {                          // Decrease error as far as possible
            if (diff >= -s->errorCompensation) {
                s->RefOffset += s->errorCompensation;
                s->errorCompensation = 0;
            } else {
                s->RefOffset -= diff;
                s->errorCompensation += diff;
            }
        }

        // Check limits
        if (s->errorCompensation < -ERROR_LIMIT || drift < -DRIFT_LIMIT || drift > +DRIFT_LIMIT) {
            s->Sync = false;
            printf("\nPTP: SYNC lost\n\n");
        }

        if (gPtpDebugLevel >= 3 && gXcpDebugLevel > 0) {
            sleepMs(20);
            uint64_t ptp_time = ptpClockGet64(); // Check current ptp clock, should be close to master
            printf("PTP: Master=%" PRIu64 "ns XL-API=%" PRIu64 "ns PC=%" PRIu64 "ns drift=%dns  -> ptpClock=%" PRIu64 "ns error=%" PRIi64 " lag=%" PRIi64 "us\n", grandmaster_time,
                   local_time, clockGet(), drift, ptp_time, s->errorCompensation, (ptp_time - grandmaster_time) / 1000);
        }
    }
}

//-------------------------------------------------------------------------------------------------------
// Get PTP clock

uint64_t ptpClockGet64() {

    uint64_t master_time;
    uint64_t local_time;

    static uint64_t last_master_time = 0;

#if OPTION_ENABLE_SELF_TEST
    static uint64_t declining_count = 0;
    static uint64_t ok_count = 0;
    uint64_t diff;
#endif

    if (!gPtp.Enabled) {
        return clockGet();
    }

    // Get the local (XL-API) time

    // Note:
    // XL-API time can not be read anytime
    // #ifndef OPTION_ENABLE_XLAPI_PC_TIME socketGetTime just returns the last received XL-API event timestamp
    // XCPsim application does not rely on accurate time, time is just used for GET_DAQ_CLOCK and to advance the simulation time
    assert(0);
    local_time = 0; // @@@@@@@@@@@@@@@@ socketGetTime();
    if (!gPtp.s.Sync) {
        last_master_time = 0;
        return local_time;
    }

    // Thread safe !!!
    mutexLock(&gPtp.Mutex);

    master_time = syncTime(local_time);

    // Avoid declining timestamps
    if (master_time < last_master_time) {
#if OPTION_ENABLE_SELF_TEST
        diff = last_master_time - master_time;
        declining_count++;
#endif
        master_time = last_master_time;
    } else {
#if OPTION_ENABLE_SELF_TEST
        diff = 0;
        ok_count++;
#endif
        last_master_time = master_time;
    }

    mutexUnlock(&gPtp.Mutex);

#if OPTION_ENABLE_SELF_TEST
    if (diff != 0 && gXcpDebugLevel >= 3) {
        printf("WARNING: declining PTP time clipped! (diff=%" PRIu64 "ns, %" PRIu64 "/%" PRIu64 ")\n", diff, ok_count, declining_count);
    }
#endif

    return master_time;
}

//--------------------------------------------------------------------------------------------------------------------------------
// Callbacks for XCP

// Get XCP clock state
uint8_t ptpClockGetState() {

    if (gPtp.Enabled) {
        tPtpMaster *m = ptpClientGetGrandmaster();
        if (m != NULL) {
            // if (gPtp.GmLost) return CLOCK_STATE_FREE_RUNNING | CLOCK_STATE_GRANDMASTER_STATE_SYNC;
            if (gPtp.s.Sync)
                return CLOCK_STATE_SYNCH | CLOCK_STATE_GRANDMASTER_STATE_SYNCH;
            return CLOCK_STATE_SYNCH_IN_PROGRESS | CLOCK_STATE_GRANDMASTER_STATE_SYNCH;
        } else {
            return CLOCK_STATE_FREE_RUNNING;
        }
    } else {
        return CLOCK_STATE_FREE_RUNNING;
    }
}

// Get grandmaster clock info for XCP
// Definitions for XCP epoch and stratumLevel in xcp.h
bool ptpClockGetXcpGrandmasterInfo(uint8_t *uuid, uint8_t *epoch, uint8_t *stratumLevel) {

    if (!gPtp.Enabled)
        return false;
    tPtpMaster *m = ptpClientGetGrandmaster();
    if (m == NULL)
        return false; // no grandmaster found yet
    if (uuid)
        memcpy(uuid, m->uuid, 8);
    if (stratumLevel)
        *stratumLevel = m->par.timeSource == PTP_TIME_SOURCE_INTERNAL  ? XCP_STRATUM_LEVEL_UNKNOWN
                        : m->par.timeSource == PTP_TIME_SOURCE_HANDSET ? XCP_STRATUM_LEVEL_RTC
                                                                       : XCP_STRATUM_LEVEL_GPS;
    if (epoch)
        *epoch = m->par.timeSource == PTP_TIME_SOURCE_INTERNAL ? XCP_EPOCH_ARB : XCP_EPOCH_TAI;
    return true;
}

// Prepare to start XCp DAQ
// Return false if not possible
bool ptpClockPrepareDaq() {

    if (!gPtp.Enabled)
        return true;

    if (gPtp.s.Sync) {
        return true;
    } else {
#ifdef ENABLE_DEBUG_PRINTS
        tPtpMaster *m = ptpClientGetGrandmaster();
        if (m == NULL) {
            DBG_PRINT1("WARNING: Start DAQ, no PTP grandmaster!\n");
        } else if (!gPtp.s.Sync) {
            DBG_PRINT1("WARNING: Start DAQ, no PTP sync!\n");
        }
#endif
        // return false // Prevent start of daq without sync
    }
    return true; // @@@@
}

//--------------------------------------------------------------------------------------------------------------------------------
// PTP

void ptpClientCallback(uint64_t grandmaster_time, uint64_t local_time, int32_t drift) { syncUpdate(&gPtp.s, grandmaster_time, local_time, drift); }

// Start PTP client
int ptpInit(uint8_t *uuid, uint8_t domain, uint8_t *addr) {

    printf("\nInit PTP\n");

    if (!ptpClientInit(uuid, domain, addr, ptpClientCallback))
        return false;
    syncInit(&gPtp.s);
    mutexInit(&gPtp.Mutex, 0, 1000);
    gPtp.Enabled = true;
    return 1;
}

// Stop PTP client
int ptpShutdown() {

    ptpClientShutdown();
    mutexDestroy(&gPtp.Mutex);
    gPtp.Enabled = false;
    return 1;
}

// Test
// Use XCP to measure PTP events and variables
#ifdef OPTION_ENABLE_PTP_TEST

#if OPTION_ENABLE_A2L_GEN
void ptpCreateA2lDescription() { ptpClientCreateA2lDescription(); }
#endif

void ptpCreateXcpEvents() { ptpClientCreateXcpEvents(); }

#endif