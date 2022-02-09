/*----------------------------------------------------------------------------
| File:
|   ptp.c
|
| Description:
|   PTP server
|
 ----------------------------------------------------------------------------*/

#include "main.h"
#include "main_cfg.h"
#include "platform.h"
#include "util.h"
#include "xcpLite.h"
#if OPTION_ENABLE_A2L_GEN
#include "A2L.h"
#endif
#include "ptp.h"





 //-------------------------------------------------------------------------------------------------------

static BOOL gPtpEnabled = FALSE;
static tPtp gPtp;


#ifdef _WIN
static DWORD WINAPI ptpThread320(LPVOID lpParameter);
static DWORD WINAPI ptpThread319(LPVOID lpParameter);
#else
static  void* ptpThread320(void* par);
static  void* ptpThread319(void* par);
#endif


//-------------------------------------------------------------------------------------------------------
// Filter

static void average_init(filter_average_t* f, uint8_t s) {

    uint8_t i;
    if (s > FILTER_MAX_SIZE) s = FILTER_MAX_SIZE;
    f->size = f->am = s;
    f->ai = 0;
    f->as = 0;
    for (i = 0; i < FILTER_MAX_SIZE; i++) f->a[i] = 0;
}

static int64_t average_calc(filter_average_t* f, int64_t v) {

    uint8_t i, n;
    for (n = 0; n < f->am; n++) {
        i = f->ai;
        f->as -= f->a[i];
        f->as += v;
        f->a[i] = v;
        if (++i >= f->size) i = 0;
        f->ai = i;
    }
    if (f->am > 1) {
        f->am /= 2;
    }
    return (int64_t)(f->as / f->size);
}

static void median_init(filter_median_t* f, uint8_t s, uint64_t t) {

    uint8_t i;
    if (s > FILTER_MAX_SIZE) s = FILTER_MAX_SIZE;
    f->size = s;
    f->ai = 0;
    for (i = 0; i < FILTER_MAX_SIZE; i++) f->a[i] = t;
}

static uint64_t median_calc(filter_median_t* f, uint64_t v) {

    uint8_t i;

    i = f->ai;
    f->a[i] = v;
    if (++i >= f->size) i = 0;
    f->ai = i;
    return f->a[i] + (v - f->a[i]) / 2;
}



//-------------------------------------------------------------------------------------------------------
// Test

#if OPTION_ENABLE_PTP_TEST

static uint16_t gXcpEvent_PtpTest = 12;  // EVNO

void ptpCreateTestEvent() {
    
    gXcpEvent_PtpTest = XcpCreateEvent("PTP_Test", 0, 0, 0, 0);     // Standard event triggered by clockSync
    
}

#if OPTION_ENABLE_A2L_GEN
void ptpCreateTestA2lDescription() {

    A2lSetEvent(gXcpEvent_PtpTest);

#ifndef __cplusplus
#error "Compile as cplusplus or use C functions for A2L generation, see A2L.h"
#endif

    A2lCreateMeasurement(gPtp.Sync, "Clock is in sync with grandmaster");

    A2lCreatePhysMeasurement(gPtp.sync_master_time, "Master time SYNC message", 1.0, 0.0, "ns");
    A2lCreatePhysMeasurement(gPtp.sync_local_time, "Local time SYNC message", 1.0, 0.0, "ns");
    A2lCreatePhysMeasurement(gPtp.sync_correction, "Correction value SYNC message", 1.0, 0.0, "ns");
    A2lCreateMeasurement(gPtp.sync_seq, "Counter value SYNC message");
    A2lCreateMeasurement(gPtp.sync_steps, "Type of SYNC message");

    A2lCreatePhysMeasurement(gPtp.flup_master_time, "Master time SYNC message", 1.0, 0.0, "ns");
    A2lCreatePhysMeasurement(gPtp.flup_local_time, "Local time SYNC message", 1.0, 0.0, "ns");
    A2lCreatePhysMeasurement(gPtp.flup_correction, "Correction value SYNC message", 1.0, 0.0, "ns");
    A2lCreateMeasurement(gPtp.flup_seq, "Counter value SYNC message");

    A2lCreatePhysMeasurement(gPtp.RawOffset, "Last clock to grandmaster diff", 0.000001, 0.0, "ms");
    A2lCreatePhysMeasurement(gPtp.RefOffset, "Local clock to grandmaster offset", 0.000001, 0.0, "ms");
    A2lCreatePhysMeasurement(gPtp.RefTime, "Local clock to grandmaster offset time", 0.000001, 0.0, "ms");
    A2lCreatePhysMeasurement(gPtp.Drift, "Clock to grandmaster drift", 0.001, 0.0, "ppm");

    A2lCreatePhysMeasurement(gPtp.CorrOffset, "Clock to grandmaster offset correction", 0.000001, 0.0, "ms");
    //A2lCreatePhysMeasurement(gPtp.CorrDrift, "Clock to grandmaster drift correction", 0.001, 0.0, "ppm");

    A2lMeasurementGroup("PTP", 15,
        "gPtp.Sync", "gPtp.CorrOffset",
        "gPtp.sync_master_time","gPtp.sync_local_time","gPtp.sync_correction","gPtp.sync_seq","gPtp.sync_steps",
        "gPtp.flup_master_time","gPtp.flup_local_time","gPtp.flup_correction","gPtp.flup_seq",
        "gPtp.RawOffset", "gPtp.RefOffset", "gPtp.RefTime", "gPtp.Drift");
}
#endif

#endif


//-------------------------------------------------------------------------------------------------------
// Master list

static void ptpPrintMaster(tPtpMaster* m) {

    const char* timescale = (m->flags & PTP_FLAG_PTP_TIMESCALE) ? "PTP" : "ARB";
    const char* timesource = (m->timeSource == 0xA0) ? "internal oscilator" : (m->timeSource == 0x20) ? "GPS" : "Unknown";
    printf("    domain=%u timescale=%s timesource=%s (flags=%04X, source=%02X) utcOffset=%u\n"
           "    addr=%u.%u.%u.%u, id=%02X-%02X-%02X-%02X-%02X-%02X-%02X-%02X\n"
           "    prio1=%u, class=%u, acc=%u, var=%u, prio2=%u, steps=%u\n",
        m->domain, timescale, timesource, m->flags, m->timeSource, m->utcOffset,
        m->addr[0], m->addr[1], m->addr[2], m->addr[3], m->id[0], m->id[1], m->id[2], m->id[3], m->id[4], m->id[5], m->id[6], m->id[7],
        m->priority1, m->clockClass, m->clockAccuraccy, m->clockVariance, m->priority2, m->stepsRemoved );
}

static void ptpPrintStatus() {

    printf("PTP sync=%u, offset=%gms, drift=%gus, corr=%gms\n",
        gPtp.Sync, (double)gPtp.RefOffset / 1000000.0, (double)gPtp.Drift / 1000.0, (double)gPtp.CorrOffset / 1000000.0);
}

static tPtpMaster* ptpLookupMaster(uint8_t* id, uint8_t* addr, uint8_t domain) {

    unsigned int i;
    for (i = 0; i < gPtp.MasterCount; i++) {
        if (memcmp(gPtp.MasterList[i].id, id, 8) == 0 && memcmp(gPtp.MasterList[i].addr, addr, 4) == 0 && gPtp.MasterList[i].domain == domain) {
            return &gPtp.MasterList[i];
        }
    }
    return NULL;
}

//-------------------------------------------------------------------------------------------------------
// Sync

static tPtpMaster* ptpNewGrandmaster(uint8_t* id, uint8_t* addr, uint8_t domain) {

    tPtpMaster* m = &gPtp.MasterList[gPtp.MasterCount++];
    memset(m, 0, sizeof(tPtpMaster));
    m->index = gPtp.MasterCount;
    if (id!=NULL) memcpy(m->id, id, 8);
    if (addr!=NULL) memcpy(m->addr, addr, 4);
    m->domain = domain;
    printf("\nPTP master %u announced:\n", m->index);
    ptpPrintMaster(m);
    return m;
}

static void ptpSetGrandmaster(tPtpMaster* m) {
    assert(m != NULL);
    gPtp.Gm = m;
    gPtp.Sync = 0;
    gPtp.SyncCounter = 0; // Init sync startup
    printf("Active PTP grandmaster is %u: addr=%u.%u.%u.%u\n", m->index, m->addr[0], m->addr[1], m->addr[2], m->addr[3]);
}

static void ptpSyncReset() {
    gPtp.Gm = 0;
    gPtp.Sync = 0;
    gPtp.SyncCounter = 0;
}

static void ptpClockSync( uint64_t grandmaster_time, uint64_t local_time) {

    assert(gPtp.Gm != NULL);

    if (gDebugLevel >= 4) {
        char ts1[64], ts2[64];
        printf("ptpClockSync( master = %s (%" PRIu64 "), local = %s (%" PRIu64 ") )\n", clockGetString(ts1, grandmaster_time), grandmaster_time, clockGetString(ts2, local_time), local_time);
    }

    if (gPtp.SyncCounter == 0) { // First
        gPtp.RefOffset = gPtp.RawOffset = (int64_t)(grandmaster_time - local_time);
        average_init(&gPtp.OffsetFilter, OFFSET_FILTER_SIZE);
        median_init(&gPtp.OffsetTimeFilter, OFFSET_FILTER_SIZE,local_time);
        gPtp.Drift = 0;
        average_init(&gPtp.DriftFilter, DRIFT_FILTER_SIZE);
        gPtp.LastLocalTime = local_time;
        gPtp.Sync = 0;
        gPtp.SyncCounter++;
    }
    else {

        // Calculate and filter master/server offset reference point (RefTime,RefOffset)
        gPtp.RawOffset = (int64_t)(local_time - grandmaster_time);
        gPtp.RefOffset = average_calc(&gPtp.OffsetFilter, gPtp.RawOffset);
        gPtp.RefTime = median_calc(&gPtp.OffsetTimeFilter, local_time);
        

        if (gPtp.SyncCounter > 3) {

            // Calculate and filter master/server drift (per s)
            gPtp.Drift = (int32_t)average_calc(&gPtp.DriftFilter, (int64_t)((((double)(gPtp.RefOffset - gPtp.LastRefOffset)) / (((double)(local_time - gPtp.LastLocalTime)) / 1000000000.0))));

            if (gPtp.SyncCounter > 6) {
                if (gPtp.Drift<100000 && gPtp.Drift>-100000) {
                    if (!gPtp.Sync) {
                        printf("PTP sync with grandmaster %u\n", gPtp.Gm->index);
                        gPtp.Sync = 1;
#ifdef XCP_ENABLE_PTP
                        XcpSetGrandmasterClockInfo(gPtp.Gm->id, XCP_EPOCH_TAI, XCP_STRATUM_LEVEL_ARB);
#endif
                    }
                }
                else {
                    if (gPtp.Sync) {
                        ptpSyncReset();
                        printf("PTP sync lost\n");
                        return;
                    }
                }
            }
        }

        gPtp.LastLocalTime = local_time;
        gPtp.LastRefOffset = gPtp.RefOffset;
        gPtp.SyncCounter++;

        if (gDebugLevel >= 2 && !gPtp.Sync) {
            ptpPrintStatus();
        }

#if OPTION_ENABLE_PTP_TEST
        XcpEvent(gXcpEvent_PtpTest); // Trigger measurement data aquisition event
#endif
    }

}


//-------------------------------------------------------------------------------------------------------
// Protocol handler

static void ptpHandleFrame(int n, struct ptphdr* ptp, uint8_t *addr) {
    
    if (!gPtpEnabled) return;
    if (n >= 44 && n <= 64) {

        uint64_t t = clockGet64();
        tPtpMaster* m = ptpLookupMaster(ptp->clockId, addr, ptp->domain);

        if (m!=NULL && gPtp.Gm == m) { // Check if message is from active master
            m->lastSeenTime = t;

            if (ptp->type == 0x0) { // Sync
                gPtp.sync_local_time = t;
                gPtp.sync_master_time = htonl(ptp->timestamp.timestamp_s) * 1000000000ULL + htonl(ptp->timestamp.timestamp_ns);
                gPtp.sync_seq = htons(ptp->sequenceId);
                gPtp.sync_correction = (uint32_t)(htonll(ptp->correction) >> 16);
                gPtp.sync_steps = (htons(ptp->flags) & PTP_FLAGS_TWO_STEP) ? 2 : 1;
                if (gPtp.sync_steps == 1) ptpClockSync(gPtp.sync_master_time + gPtp.sync_correction, gPtp.sync_local_time);
                if (gDebugLevel >= 3) {
                    char ts1[64], ts2[64];
                    if (gPtp.sync_steps == 2) {
                        printf("PTP SYNC 2 step, corr_ns=%u, local=%s\n", gPtp.sync_correction >> 16, clockGetString(ts2, gPtp.sync_local_time));
                    }
                    else {
                        printf("PTP SYNC 1 step, corr_ns=%u, master=%s, local=%s\n", gPtp.sync_correction, clockGetString(ts1, gPtp.sync_master_time), clockGetString(ts2, gPtp.sync_local_time));
                    }
                }

            }
            else if (ptp->type == 0x8) { // Followup
                gPtp.flup_local_time = t;
                gPtp.flup_master_time = htonl(ptp->timestamp.timestamp_s) * 1000000000ULL + htonl(ptp->timestamp.timestamp_ns);
                gPtp.flup_seq = htons(ptp->sequenceId);
                gPtp.flup_correction = (uint32_t)(htonll(ptp->correction) >> 16);
                if (gDebugLevel >= 3) {
                    char ts1[64], ts2[64];
                    printf("PTP FLUP corr_ns=%u, master=%s, local=%s\n", gPtp.flup_correction >> 16, clockGetString(ts1, gPtp.flup_master_time), clockGetString(ts2, gPtp.flup_local_time));
                }
            }
            else if (ptp->type == 0x9) { // Delay_Resp
                // ignore
            }
            else if (ptp->type == PTP_ANNOUNCE) { // ANNOUNCE
            }
            else { // ????
                printf("PTP unknown packet type %u\n", ptp->type);
                return;
            }

            if (gPtp.sync_steps == 2 && gPtp.sync_seq == gPtp.flup_seq && gPtp.sync_seq != 0) {
                ptpClockSync(gPtp.flup_master_time + gPtp.sync_correction + gPtp.flup_correction, gPtp.sync_local_time); // 2 step
                gPtp.sync_seq = gPtp.flup_seq = 0;
            }

        } // from active grandmaster

        else if (m != NULL) { // from other known master
            m->lastSeenTime = t;

            // Sync message with correct domain
            if (ptp->type == PTP_SYNC && ptp->domain == gPtp.Domain) { // Sync

                if (gPtp.Gm == NULL) { // no active grandmaster
                    ptpSetGrandmaster(m);
                }
                else { // other grandmaster in the same domain
                    printf("WARNING: Conflicting PTP SYNC message from grandmaster %u received in domain %u\n", m->index, gPtp.Domain);
                }
            }
        } // from other known master

        else { // from unknown master

            // Remember all announced masters in a list
            if (ptp->type == PTP_ANNOUNCE) { // ANNOUNCE
                m = ptpNewGrandmaster(ptp->clockId, addr, ptp->domain);
                m->clockVariance = htons(ptp->u.a.clockVariance);
                m->clockAccuraccy = ptp->u.a.clockAccuraccy;
                m->clockClass = ptp->u.a.clockClass;
                m->priority1 = ptp->u.a.priority1;
                m->priority2 = ptp->u.a.priority2;
                m->utcOffset = htons(ptp->u.a.utcOffset);
                m->sourcePortId = htons(ptp->sourcePortId);
                m->flags = htons(ptp->flags);
                m->stepsRemoved = htons(ptp->u.a.stepsRemoved);
                m->timeSource = ptp->u.a.timeSource;
                memcpy(m->grandmasterId, ptp->u.a.grandmasterId, 8);
                m->lastSeenTime = t;
            }
        }
    }

}


//-------------------------------------------------------------------------------------------------------
// Threads

#ifdef _WIN
static DWORD WINAPI ptpThread319(LPVOID par)
#else
static  void* ptpThread319(void* par)
#endif
{
    uint8_t buffer[256];
    uint8_t addr[4];
    int n;
    (void)par;
    for (;;) {
        n = socketRecvFrom(gPtp.Sock319, buffer, (uint16_t)sizeof(buffer), addr, NULL);
        if (n < 0) break; // Terminate on error (socket close is used to terminate thread)
        ptpHandleFrame(n, (struct ptphdr*)buffer, addr);
    }
    printf("Terminate PTP multicast 319 thread\n");
    socketClose(&gPtp.Sock319);
    return 0;
}

#ifdef _WIN
static DWORD WINAPI ptpThread320(LPVOID par)
#else
static  void* ptpThread320(void* par)
#endif
{
    uint8_t buffer[256];
    uint8_t addr[4];
    int n;
    (void)par;
    for (;;) {
        n = socketRecvFrom(gPtp.Sock320, buffer, (uint16_t)sizeof(buffer), addr, NULL);
        if (n < 0) break; // Terminate on error (socket close is used to terminate thread)
        ptpHandleFrame(n, (struct ptphdr*)buffer, addr);
    }
    printf("Terminate PTP multicast 320 thread\n");
    socketClose(&gPtp.Sock320);
    return 0;
}


//-------------------------------------------------------------------------------------------------------
// Public functions

uint64_t ptpClockGet64() {

    uint64_t t = clockGet64();
    if (!gPtpEnabled) return t;

    // Clock servo
    // time since ref time
    int64_t td = (int64_t)(t - gPtp.RefTime);
    // Extrapolate drift from refOffset
    // Positive offset means local time is ahead, positive drift means offset is growing
    gPtp.CorrOffset = gPtp.RefOffset + (gPtp.Drift * td) / 1000000000;

    if (gPtp.CorrOffset >= 0) {
        return (t - (uint64_t)+gPtp.CorrOffset);
    }
    else {
        return (t + (uint64_t)-gPtp.CorrOffset);
    }

}

uint32_t ptpClockGet32() {

    return (uint32_t)ptpClockGet64();
}


void ptpClockCheckStatus() {

    if (!gPtpEnabled) return;

    if (gPtp.Gm != NULL) {

        uint64_t t = clockGet64();

        if (t - gPtp.Gm->lastSeenTime > (uint64_t)5 * CLOCK_TICKS_PER_S) { // Master timeout after 5s
            printf("WARNING: PTP master lost!\n");
            ptpSyncReset();
        }

        #if OPTION_ENABLE_PTP_TEST
        static uint64_t statusTimer = 0;
        if (t - statusTimer > 10*(uint64_t)(CLOCK_TICKS_PER_S)) {
            statusTimer = t;
            if (gPtp.Sync) ptpPrintStatus();
        }
        #endif

    }
}

uint8_t* ptpClockGetUUID() {

    if (!gPtpEnabled || gPtp.Gm==NULL) return NULL;
    return gPtp.Gm->id;
}

uint8_t ptpClockGetState() {

    if (gPtpEnabled) {
        if (gPtp.Gm != NULL) {
            if (gPtp.Sync) return CLOCK_STATE_SYNCH;
            return CLOCK_STATE_SYNCH_IN_PROGRESS| CLOCK_STATE_GRANDMASTER_STATE_SYNC;
        }
        else {
            return CLOCK_STATE_FREE_RUNNING;
        }
    }
    else {
        return CLOCK_STATE_FREE_RUNNING;
    }
}

BOOL ptpClockGetGrandmasterInfo(uint8_t* uuid, uint8_t* epoch, uint8_t* stratumLevel) {
   
        uint8_t* tmp = ptpClockGetUUID();
        if (tmp) {
            if (uuid) memcpy(uuid, tmp, 8);
            if (stratumLevel) *stratumLevel = CLOCK_STRATUM_LEVEL_UTC;
            if (epoch) *epoch = CLOCK_EPOCH_TAI;
            return TRUE;
        }
   
    return FALSE;
}

BOOL ptpClockPrepareDaq() {

    if (!gPtpEnabled) return TRUE;

    if (gPtp.Sync) {
        gPtp.CorrOffset = gPtp.RefOffset;
        return TRUE;
    }
    else {
        printf("WARNING: No PTP sync. PTP corrOffset=%" PRId64 "\n", gPtp.CorrOffset);
        // @@@@ return 0 to prevent start of daq without sync
    }
    return TRUE; 
}

int ptpInit(uint8_t domain) {

    printf("Init PTP\n");

    gPtp.Domain = domain; // look for grandmaster in this domain

    // Init
    gPtp.Gm = NULL;
    gPtp.Sync = 0;
    gPtp.SyncCounter = 0;
    gPtp.MasterCount = 0;
    gPtp.CorrOffset = 0;

    // Create sockets and threads for PTP (needs root on Linux!!)
    gPtp.Sock319 = gPtp.Sock320 = INVALID_SOCKET;
    if (!socketOpen(&gPtp.Sock319, 0 /* useTCP */, 0 /*nonblocking*/, 1 /*reusable*/)) return 0;
    if (!socketOpen(&gPtp.Sock320, 0 /* useTCP */, 0 /*nonblocking*/, 1 /*reusable*/)) return 0;
    printf("  Bind PTP sockets to ANY:320/319\n");
    if (!socketBind(gPtp.Sock320, NULL, 320)) return 0;
    if (!socketBind(gPtp.Sock319, NULL, 319)) return 0;
    printf("  Listening for PTP multicast on 224.0.1.129\n\n");
    uint8_t maddr[4] = { 224, 0, 1, 129 };
    if (!socketJoin(gPtp.Sock320, maddr)) return 0;
    if (!socketJoin(gPtp.Sock319, maddr)) return 0;
    // Start PTP threads
    create_thread(&gPtp.ThreadHandle320, ptpThread320);
    create_thread(&gPtp.ThreadHandle319, ptpThread319);
    sleepMs(2000);
    
    gPtpEnabled = TRUE;

    return 1;
}


int ptpShutdown() {

    gPtpEnabled = FALSE;

    socketClose(&gPtp.Sock319);
    socketClose(&gPtp.Sock320);
    sleepMs(200);
    cancel_thread(gPtp.ThreadHandle320);
    cancel_thread(gPtp.ThreadHandle319);
    return 1;
}


