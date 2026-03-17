/*****************************************************************************
| File:
|   xcplite.c
|
|  Description:
|    Shared memory management for XCPlite
|
| Copyright (c) Vector Informatik GmbH. All rights reserved.
| Licensed under the MIT license. See LICENSE file in the project root for details.
|***************************************************************************/

#include "xcp_cfg.h"    // XCP protocol layer configuration parameters (XCP_xxx)
#include "xcplib_cfg.h" // for OPTION_xxx
#include "xcptl_cfg.h"  // XCP transport layer configuration parameters (XCPTL_xxx)

#include "xcplite.h" // XCP protocol layer interface functions

#include <assert.h>   // for assert
#include <inttypes.h> // for PRIx32, PRIu64
#include <stdarg.h>   // for va_list, va_start, va_arg, va_end
#include <stdbool.h>  // for bool
#include <stdint.h>   // for uint8_t, uint16_t, ...
#include <stdio.h>    // for printf
#include <stdlib.h>   // for size_t, NULL, abort
#include <string.h>   // for memcpy, memset, strlen
#include <unistd.h>   // for getpid()

#include "shm.h" // for shared memory management

#include "dbg_print.h" // for DBG_LEVEL, DBG_PRINT3, DBG_PRINTF4, DBG...
#include "platform.h"  // for atomics

#ifdef OPTION_SHM_MODE

#ifndef XCP_ENABLE_DAQ_EVENT_LIST
#error "XCP_ENABLE_DAQ_EVENT_LIST must be defined for SHM mode"
#endif
#ifndef XCP_ENABLE_CALSEG_LIST
#error "XCP_ENABLE_CALSEG_LIST must be defined for SHM mode"
#endif

/****************************************************************************/
/* Protocol layer state data                                                */
/****************************************************************************/

extern tXcpData *gXcpData;

extern tXcpLocalData gXcpLocalData;
#define local (*(const tXcpLocalData *)&gXcpLocalData) // Read-only access to process-local state

static bool isInitialized_(tXcpData *xcp_data) {
    return xcp_data->shm_header.magic == SHM_MAGIC && xcp_data->shm_header.version == SHM_VERSION && (SS_INITIALIZED == (xcp_data->session_status & SS_INITIALIZED));
}
static bool isActivated_(tXcpData *xcp_data) {
    return xcp_data->shm_header.magic == SHM_MAGIC && xcp_data->shm_header.version == SHM_VERSION &&
           (SS_ACTIVATED | SS_INITIALIZED) == (xcp_data->session_status & (SS_ACTIVATED | SS_INITIALIZED));
}

/**************************************************************************/
/* Memory organization and global state                                   */
/**************************************************************************/

/*
All state of the XCP singleton is stored in tXcpData and tXcpLocalData
tXcpData may optionally be allocated in shared memory and accessed by multiple processes.
It has all the information about the XCP state needed to run calibration and measurement
Only the leader process runs the XCP transport layer and has ownership of tXcpData and the transmit queue in shared memory.
The first process allocates the shared memory for tXcpData and the queue and becomes the leader.
*/

// Returns this process's application id (the slot index in shm_header.app_list)
uint8_t XcpShmGetAppId(void) { return local.shm_app_id; }

// Check operating modes
bool XcpShmIsActive(void) { return local.init_mode >= XCP_MODE_SHM; }
bool XcpShmIsLeader(void) { return local.init_mode >= XCP_MODE_SHM && local.shm_leader; }
bool XcpShmIsServer(void) { return local.init_mode >= XCP_MODE_SHM && local.shm_server; }
bool XcpShmIsFollower(void) { return local.init_mode >= XCP_MODE_SHM && !local.shm_leader; }

#ifdef DBG_LEVEL

// Init shared memory header
void XcpShmInit(tXcpData *xcp_data) {

    tShmHeader *hdr = &xcp_data->shm_header;
    hdr->magic = SHM_MAGIC;
    hdr->version = SHM_VERSION;
    hdr->size = (uint32_t)sizeof(tXcpData);
    hdr->leader_pid = (uint32_t)getpid();
    atomic_store(&hdr->app_count, 0U);
    atomic_store(&hdr->a2l_finalize_requested, 0U);
}

tXcpData *XcpShmAttachOrCreate(bool *out_is_leader) {

    tXcpData *xcp_data = NULL;
    bool is_leader = false;

    xcp_data = (tXcpData *)platformShmOpen("/xcpdata", "/tmp/xcpdata.lock", sizeof(tXcpData), &is_leader);
    if (xcp_data == NULL) {
        DBG_PRINT_ERROR("XcpInit: failed to open shared memory\n");
        *out_is_leader = false;
        return NULL;
    }

    if (!is_leader) {

        // Check if init and activate

        // Wait up to 500ms for the leader to complete his XcpInit() (indicated by SS_ACTIVATED)
        if (!isActivated_(xcp_data)) {
            DBG_PRINT3("XcpInit: waiting for leader to activate XCP ...\n");
            for (int i = 0; i < 500 && !isActivated_(xcp_data); i++) {
                sleepUs(1000);
            }
        }

        if (isActivated_(xcp_data)) {

            DBG_PRINTF3("XcpInit: attached to existing shared memory from leader PID %u\n", xcp_data->shm_header.leader_pid);

            // @@@@ TODO: What if the leader died, but the SHM is still there and not yet reclaimed by another leader?
            // Existing shared memory is valid and active, we can attach as a follower
            *out_is_leader = false;
            return xcp_data;
        }

        // Invalid SHM layout version
        if (xcp_data->shm_header.magic != SHM_MAGIC || xcp_data->shm_header.version != SHM_VERSION) {
            DBG_PRINT_ERROR("XcpInit: shared memory is corrupt or has invalid magic or version\n");
        }

        // Stale SHM: the previous leader died without calling platformShmClose
        // Reclaim ownership: unmap + unlink, then re-open as new leader.
        DBG_PRINT5("XcpInit: stale or corrupt shared memory detected, reclaiming as leader\n");
        platformShmClose("/xcpdata", xcp_data, sizeof(tXcpData), true /* unlink */);
        xcp_data = (tXcpData *)platformShmOpen("/xcpdata", "/tmp/xcpdata.lock", sizeof(tXcpData), &is_leader);
        if (xcp_data == NULL) {
            DBG_PRINT_ERROR("XcpInit: failed to recreate shared memory\n");
            return NULL;
        }
        assert(is_leader); // We just unlinked it, so we must win the O_CREAT|O_EXCL race
    }

    *out_is_leader = is_leader;
    return xcp_data;
}

// Print the status and information in tXcpData, for debugging purposes.
void XcpShmDebugPrint(tXcpData *xcp_data) {

    assert(xcp_data != NULL);
    if (xcp_data == NULL)
        return;

    // --- SHM header ---
    const tShmHeader *hdr = &xcp_data->shm_header;
    uint32_t app_count = (uint32_t)atomic_load(&hdr->app_count);
    printf("SHM Header:\n");
    printf("  magic=0x%016" PRIX64 ", version=%06X, size=%u\n", (uint64_t)hdr->magic, hdr->version, hdr->size);
    printf("  leader_pid=%u, app_count=%u, a2l_finalize_requested=%u\n", hdr->leader_pid, app_count, (unsigned)atomic_load(&hdr->a2l_finalize_requested));

    // --- App list ---
    printf("App list (%u registered):\n", app_count);
    for (uint32_t i = 0; i < app_count && i < SHM_MAX_APP_COUNT; i++) {
        const tApp *app = &hdr->app_list[i];
        printf("  [%u] name='%s', epk='%s', pid=%u, %s %s, alive=%u, a2l_finalized=%u, a2l_name='%s'\n", i, app->project_name, app->epk, app->pid,
               app->is_leader ? "leader" : "follower", app->is_server ? "server" : "",

               (unsigned)atomic_load(&app->alive_counter), (unsigned)atomic_load(&app->a2l_finalized), app->a2l_name);
    }

    // --- Session status ---
    uint16_t ss = xcp_data->session_status;
    printf("Session status: 0x%04X [%s%s%s%s%s]\n", ss, (ss & SS_INITIALIZED) ? "INIT " : "", (ss & SS_ACTIVATED) ? "ACTV " : "", (ss & SS_STARTED) ? "STRT " : "",
           (ss & SS_CONNECTED) ? "CONN " : "", (ss & SS_LEGACY_MODE) ? "LEGACY " : "");
    printf("  daq_running=%u, daq_overflow_count=%u\n", (unsigned)atomic_load_explicit(&xcp_data->daq_running, memory_order_relaxed), xcp_data->daq_overflow_count);

    // --- Event list ---
    uint16_t event_count = XcpGetEventCount();
    printf("Events (%u):\n", event_count);
    for (uint16_t id = 0; id < event_count; id++) {
        const tXcpEvent *ev = XcpGetEvent(id);
        if (ev == NULL)
            printf("  [%u] not found\n", id);
        else
            printf("  [%u] name='%s', cycle_ns=%u, index=%u, app_id=%u, daq_first=%u, flags=0x%02X\n", id, XcpGetEventName(id), ev->cycle_time_ns, ev->index, ev->app_id,
                   ev->daq_first, ev->flags);
    }

    // --- Calibration segment list ---
    uint16_t calseg_count = XcpGetCalSegCount();
    printf("CalSegs (%u):\n", calseg_count);
    for (uint16_t i = 0; i < calseg_count; i++) {
        const tXcpCalSeg *cs = XcpGetCalSeg(i);
        if (cs == NULL)
            printf("  [%u] not found\n", i);
        else
            printf("  [%u] name='%s', size=%u, app_id=%u, seg_num=%u\n", i, cs->h.name, cs->h.size, cs->h.app_id, cs->h.calseg_number);
    }

    // --- DAQ lists ---
    // printf("DAQ lists (%u):\n", shared.daq_lists.daq_count);
    // for (uint16_t daq = 0; daq < shared.daq_lists.daq_count; daq++) {
    //     XcpPrintDaqList(daq);
    // }
}

#endif

// Signal all follower processes to finalize their A2L file immediately
// From now, no other processes may join the XCP session
void XcpShmRequestA2lFinalize(void) {

    assert(XcpShmIsActive());
    assert(isActivated_(gXcpData));

    if (!XcpShmIsServer())
        return;

    atomic_store(&gXcpData->shm_header.a2l_finalize_requested, 1U);

    // @@@@ TODO Unlink or not, a process might want to join running measurement
    platformShmUnlink("/xcpdata");
    platformShmUnlink("/xcpqueue");

    DBG_PRINT3("XcpShmRequestA2lFinalize: requested A2L finalization for all followers, unlinked /xcpdata and /xcpqueue\n");
}

// Returns true once the leader has set the A2L finalize request flag.
bool XcpShmIsA2lFinalizeRequested(void) {

    assert(XcpShmIsActive());
    assert(isActivated_(gXcpData));
    return atomic_load(&gXcpData->shm_header.a2l_finalize_requested) != 0;
}

// Update this process's app slot with its A2L filename and mark it as finalized.
void XcpShmNotifyA2lFinalized(const char *a2l_name) {
    assert(XcpShmIsActive());
    assert(isActivated_(gXcpData));

    uint8_t slot = XcpShmGetAppId();
    if (slot >= SHM_MAX_APP_COUNT)
        return;
    tApp *app = &gXcpData->shm_header.app_list[slot];
    if (a2l_name != NULL && a2l_name[0] != '\0') {
        STRNCPY(app->a2l_name, a2l_name, XCP_A2L_FILENAME_MAX_LENGTH);
        app->a2l_name[XCP_A2L_FILENAME_MAX_LENGTH] = '\0';
    }
    atomic_store(&app->a2l_finalized, 1U);
    DBG_PRINTF3("XcpShmNotifyA2lFinalized: app_id=%u a2l='%s'\n", XcpShmGetAppId(), a2l_name ? a2l_name : "");
}

// Get the project name of the ECU
const char *XcpShmGetEcuProjectName(void) { return "shm"; } // @@@@ TODO: store in SHM header?

// Get the number of registered applications in SHM mode.
uint8_t XcpShmGetAppCount(void) {
    if (!XcpShmIsActive())
        return 0;
    assert(isActivated_(gXcpData));
    return (uint8_t)atomic_load(&gXcpData->shm_header.app_count);
}

// Get the project name of an app slot by its app_id index.
// Returns NULL if the slot is vacant or out of range.
const char *XcpShmGetAppProjectName(uint8_t app_id) {
    assert(XcpShmIsActive());
    assert(isActivated_(gXcpData));

    if (app_id >= SHM_MAX_APP_COUNT)
        return NULL;
    const tApp *app = &gXcpData->shm_header.app_list[app_id];
    if (app->pid == 0)
        return NULL; // vacant slot
    return app->project_name;
}

// Get the EPK of an app slot by its app_id index.
// Returns NULL if the slot is vacant or out of range.
const char *XcpShmGetAppEpk(uint8_t app_id) {
    assert(XcpShmIsActive());
    assert(isActivated_(gXcpData));

    if (app_id >= SHM_MAX_APP_COUNT)
        return NULL;
    const tApp *app = &gXcpData->shm_header.app_list[app_id];
    if (app->pid == 0)
        return NULL; // vacant slot
    return app->epk;
}

// Wait up to timeout_ms for all registered non-leader apps to set a2l_finalized,
// then populate filenames[] with their a2l_name strings (pointers into SHM, valid
// as long as /xcpdata is mapped).  Returns the number of entries written.
int XcpShmCollectA2lFiles(uint32_t timeout_ms, const char *filenames[], int max_count) {

    assert(XcpShmIsActive());
    assert(isActivated_(gXcpData));

    uint32_t elapsed = 0;
    const uint32_t poll_ms = 50;
    while (elapsed < timeout_ms) {
        bool all_ready = true;
        uint32_t app_count = (uint32_t)atomic_load(&gXcpData->shm_header.app_count);
        for (uint32_t i = 0; i < app_count && i < SHM_MAX_APP_COUNT; i++) {
            const tApp *app = &gXcpData->shm_header.app_list[i];
            // if (app->pid == 0 || app->is_leader)
            //     continue;
            if (!atomic_load(&app->a2l_finalized)) {
                all_ready = false;
                break;
            }
        }
        if (all_ready)
            break;
        sleepMs(poll_ms);
        elapsed += poll_ms;
    }
    if (elapsed >= timeout_ms)
        DBG_PRINTF_WARNING("XcpShmCollectA2lFiles: timeout after %u ms\n", timeout_ms);

    int count = 0;
    uint32_t app_count = (uint32_t)atomic_load(&gXcpData->shm_header.app_count);
    for (uint32_t i = 0; i < app_count && i < SHM_MAX_APP_COUNT && count < max_count; i++) {
        const tApp *app = &gXcpData->shm_header.app_list[i];
        if (atomic_load(&app->a2l_finalized) && app->a2l_name[0] != '\0') {
            filenames[count++] = app->a2l_name;
        } else if (!atomic_load(&app->a2l_finalized)) {
            DBG_PRINTF_WARNING("XcpShmCollectA2lFiles: app %u ('%s') not finalized\n", i, app->project_name);
        }
    }
    DBG_PRINTF3("XcpShmCollectA2lFiles: collected %d A2L file(s)\n", count);
    return count;
}

// Increment this process's alive_counter so the leader can detect stale followers.
void XcpShmIncrementAliveCounter(void) {

    assert(XcpShmIsActive());
    assert(isActivated_(gXcpData));

    uint8_t slot = XcpShmGetAppId();
    if (slot >= SHM_MAX_APP_COUNT)
        return;
    atomic_fetch_add(&gXcpData->shm_header.app_list[slot].alive_counter, 1U);
}

// Register this process in the SHM application list.
// Returns the allocated shm_app_id (slot index= or SHM_MAX_APP_COUNT on error.
// If a slot with a matching project_name already exists (process restart), it is reused.
uint8_t XcpShmRegisterApp(const char *name, const char *epk, bool is_leader, bool is_server) {

    assert(XcpShmIsActive());
    assert(isInitialized_(gXcpData));

    tShmHeader *hdr = &gXcpData->shm_header;

    // Scan for an existing slot with the same project_name (handles process restart or preload from BIN file)
    uint32_t count = (uint32_t)atomic_load(&hdr->app_count);
    for (uint32_t i = 0; i < count; i++) {
        if (strncmp(hdr->app_list[i].project_name, name, XCP_PROJECT_NAME_MAX_LENGTH) == 0) {
            tApp *app = &hdr->app_list[i];
            app->pid = (uint32_t)getpid();
            app->is_leader = is_leader;
            app->is_server = is_server;
            STRNCPY(app->epk, epk, XCP_EPK_MAX_LENGTH);
            app->epk[XCP_EPK_MAX_LENGTH] = '\0';
            app->a2l_name[0] = '\0';
            atomic_store(&app->a2l_finalized, 0U);
            DBG_PRINTF3("XcpShmRegisterApp: reused app slot %u for '%s' (pid=%u)\n", i, name, (unsigned)getpid());
            return (uint8_t)i;
        }
    }

    // Atomically claim a fresh slot
    uint32_t slot = (uint32_t)atomic_fetch_add(&hdr->app_count, 1U);
    if (slot >= SHM_MAX_APP_COUNT) {
        DBG_PRINT_ERROR("XcpShmRegisterApp: application list full\n");
        atomic_fetch_sub(&hdr->app_count, 1U); // undo the increment
        return SHM_MAX_APP_COUNT;              // error sentinel
    }
    tApp *app = &hdr->app_list[slot];
    memset(app->b, 0, sizeof(app->b));
    STRNCPY(app->project_name, name, XCP_PROJECT_NAME_MAX_LENGTH);
    app->project_name[XCP_PROJECT_NAME_MAX_LENGTH] = '\0';
    STRNCPY(app->epk, epk, XCP_EPK_MAX_LENGTH);
    app->epk[XCP_EPK_MAX_LENGTH] = '\0';
    app->pid = (uint32_t)getpid();
    app->is_leader = is_leader;
    app->is_server = is_server;
    atomic_store(&app->alive_counter, 0U);
    atomic_store(&app->a2l_finalized, 0U);
    DBG_PRINTF3("XcpShmRegisterApp: registered app slot %u for '%s' (pid=%u, %s, %s)\n", slot, name, (unsigned)getpid(), is_leader ? "leader" : "follower",
                is_server ? "server" : "");
    return (uint8_t)slot;
}

#endif // OPTION_SHM_MODE
