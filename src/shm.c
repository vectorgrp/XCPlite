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
#include <signal.h>   // for kill
#include <stdarg.h>   // for va_list, va_start, va_arg, va_end
#include <stdbool.h>  // for bool
#include <stdint.h>   // for uint8_t, uint16_t, ...
#include <stdio.h>    // for printf
#include <stdlib.h>   // for size_t, NULL, abort
#include <string.h>   // for memcpy, memset
#ifdef OPTION_SHM_MODE
#include <unistd.h> // for getpid()
#endif
#include "shm.h" // for shared memory management

#include "dbg_print.h" // for DBG_LEVEL, DBG_PRINT3, DBG_PRINTF4, DBG...
#include "platform.h"  // for atomics

#ifdef OPTION_SHM_MODE // check required options and settings for SHM mode
#ifndef XCP_ENABLE_DAQ_EVENT_LIST
#error "XCP_ENABLE_DAQ_EVENT_LIST must be defined for SHM mode"
#endif
#ifndef XCP_ENABLE_CALSEG_LIST
#error "XCP_ENABLE_CALSEG_LIST must be defined for SHM mode"
#endif

/****************************************************************************/
/* Protocol layer state data                                                */
/****************************************************************************/

/*
All state of the XCP singleton is stored in tXcpData and tXcpLocalData
tXcpData may optionally be allocated in shared memory and accessed by multiple processes.
It has all the information about the XCP state needed to run calibration and measurement
Only the leader process runs the XCP transport layer and has ownership of tXcpData and the transmit queue in shared memory.
The first process allocates the shared memory for tXcpData and the queue and becomes the leader.
*/

extern tXcpData *gXcpData;

extern tXcpLocalData gXcpLocalData;
#define local (*(const tXcpLocalData *)&gXcpLocalData) // Read-only access to process-local state

static bool isInitialized_(tXcpData *xcp_data) { return xcp_data != NULL && xcp_data->shm_header.magic == SHM_MAGIC && xcp_data->shm_header.version == SHM_VERSION; }

static bool isActivated_(tXcpData *xcp_data) {
    return xcp_data != NULL && xcp_data->shm_header.magic == SHM_MAGIC && xcp_data->shm_header.version == SHM_VERSION && 0 != (xcp_data->session_status & SS_ACTIVATED);
}

/**************************************************************************/
// Get current application process state infos
/**************************************************************************/

// Check operating modes
bool XcpShmIsLeader(void) { return (local.init_mode & XCP_MODE_SHM) != 0 && local.shm_leader; }
bool XcpShmIsXcpServer(void) { return (local.init_mode & XCP_MODE_SHM) != 0 && local.shm_server; }
bool XcpShmIsFollower(void) { return (local.init_mode & XCP_MODE_SHM) != 0 && !local.shm_leader; }

// Returns this process's application id (the slot index in shm_header.app_list)
uint8_t XcpShmGetAppId(void) { return local.shm_app_id; }

// Returns the EPK string of the overall ECU
// Computes a FNV-1a 64-bit hash over all application EPK strings and returns it as a 16-char hex string.
// The result is always 16 ASCII hex characters, fits in XCP_ECU_EPK_MAX_LENGTH, and changes whenever any app EPK changes.
const char *XcpShmGetEcuEpk(void) {
    const tShmHeader *hdr = &gXcpData->shm_header;
    assert(hdr != NULL);
    if (hdr == NULL) {
        return "";
    }

    // FNV-1a 64-bit hash over all app EPK strings in slot order
    // A separator byte is hashed between EPKs to prevent "AB"+"C" == "A"+"BC" collisions
    uint64_t hash = 14695981039346656037ULL; // FNV-1a 64-bit offset basis
    uint32_t app_count = (uint32_t)atomic_load(&hdr->app_count);
    for (uint32_t i = 0; i < app_count; i++) {
        const uint8_t *p = (const uint8_t *)hdr->app_list[i].epk;
        while (*p) {
            hash = (hash ^ (uint64_t)*p++) * 1099511628211ULL; // FNV prime
        }
        hash = (hash ^ (uint64_t)'\0') * 1099511628211ULL; // end-of-string separator
    }

    SNPRINTF(hdr->ecu_epk, sizeof(hdr->ecu_epk), "%016" PRIx64, hash);
    return (const char *)hdr->ecu_epk;
}

// Get current server
// Return SHM_INVALID_APP_ID if not found or not active in SHM mode
uint8_t XcpShmGetServer(void) {
    const tShmHeader *hdr = &gXcpData->shm_header;
    uint32_t app_count = (uint32_t)atomic_load(&hdr->app_count);
    for (uint32_t i = 0; i < app_count; i++) {
        if (hdr->app_list[i].is_server) {
            return (uint8_t)i;
        }
    }
    return SHM_INVALID_APP_ID; // Not found
}

// Get current leader
// Return SHM_INVALID_APP_ID if not found or not active in SHM mode
uint8_t XcpShmGetLeader(void) {
    const tShmHeader *hdr = &gXcpData->shm_header;
    uint32_t app_count = (uint32_t)atomic_load(&hdr->app_count);
    for (uint32_t i = 0; i < app_count; i++) {
        if (hdr->app_list[i].is_leader) {
            return (uint8_t)i;
        }
    }
    return SHM_INVALID_APP_ID; // Not found
}

/**************************************************************************/
// Init and attach
/**************************************************************************/

tXcpData *XcpShmAttachOrCreate(bool *out_is_leader) {

    tXcpData *xcp_data = NULL;
    bool is_leader = false;

    DBG_PRINT5(ANSI_COLOR_BLUE "XcpShmAttachOrCreate: Open SHM data '/xcpdata'\n" ANSI_COLOR_RESET);
    xcp_data = (tXcpData *)platformShmOpen("/xcpdata", "/tmp/xcpdata.lock", sizeof(tXcpData), &is_leader);
    if (xcp_data == NULL) {
        DBG_PRINT_ERROR("XcpShmAttachOrCreate: failed to open shared memory\n");
        *out_is_leader = false;
        return NULL;
    }

    if (!is_leader) {

        // Check if init and activated
        // Wait up to 50ms for the leader to complete his XcpInit(), very rare situation
        if (!isActivated_(xcp_data)) {
            DBG_PRINT5(ANSI_COLOR_BLUE "XcpShmAttachOrCreate: waiting for leader to activate XCP ...\n" ANSI_COLOR_RESET);
            for (int i = 0; i < 50 && !isActivated_(xcp_data); i++) {
                sleepUs(1000);
            }
        }
        if (isActivated_(xcp_data)) {
            DBG_PRINTF5(ANSI_COLOR_BLUE "XcpShmAttachOrCreate: Attached to existing shared memory '/xcpdata' from leader pid=%u\n" ANSI_COLOR_RESET,
                        xcp_data->shm_header.leader_pid);
            // Existing shared memory is valid and active, we can attach as a follower
            *out_is_leader = false;
            return xcp_data;
        }

        // Invalid SHM
        // Reclaim ownership: unmap + unlink, then re-open as new leader.
        DBG_PRINT_ERROR("XcpShmAttachOrCreate: shared memory is not in activated state or has invalid magic or version\n");
        DBG_PRINT_WARNING("XcpShmAttachOrCreate: unlink '/xcpdata', reclaiming as leader\n");
        platformShmClose("/xcpdata", xcp_data, sizeof(tXcpData), true /* unlink */);
        xcp_data = (tXcpData *)platformShmOpen("/xcpdata", "/tmp/xcpdata.lock", sizeof(tXcpData), &is_leader);
        if (xcp_data == NULL) {
            DBG_PRINT_ERROR("XcpShmAttachOrCreate: failed to recreate shared memory\n");
            return NULL;
        }
        assert(is_leader); // We just unlinked it, so we must win the O_CREAT|O_EXCL race
    }

    *out_is_leader = is_leader;
    return xcp_data;
}

// Unlink the shared memory region, so no new processes can join, but keep the existing mapping valid for existing users until they exit and unmap themselves
void XcpShmUnlink(void) {
    DBG_PRINT3(ANSI_COLOR_BLUE "XcpShmUnlink: Unlink SHM data '/xcpdata'\n" ANSI_COLOR_RESET);
    platformShmClose("/xcpdata", gXcpData, sizeof(tXcpData), true /* unlink */);
    gXcpData = NULL;
}

/**************************************************************************/
// Debug print
/**************************************************************************/

// Print the status and information in tXcpData, for debugging purposes.
void XcpShmDebugPrint(void) {

    if (gXcpData == NULL)
        return;
    const tShmHeader *hdr = &gXcpData->shm_header;

    // --- SHM header ---
    uint32_t app_count = (uint32_t)atomic_load(&hdr->app_count);
    printf(ANSI_COLOR_BLUE "SHM Header:\n" ANSI_COLOR_RESET);
    printf("  magic=0x%016" PRIX64 ", version=%06X, size=%u\n", (uint64_t)hdr->magic, hdr->version, hdr->size);
    printf("  leader_pid=%u, app_count=%u, a2l_finalize_requested=%u\n", hdr->leader_pid, app_count, (unsigned)atomic_load(&hdr->a2l_finalize_requested));
    printf("  ecu_epk='%s'\n", hdr->ecu_epk);
    printf(ANSI_COLOR_RESET);

    // --- App list ---
    printf(ANSI_COLOR_BLUE "Apps (%u):\n" ANSI_COLOR_RESET, app_count);
    for (uint32_t i = 0; i < app_count && i < SHM_MAX_APP_COUNT; i++) {
        const tApp *app = &hdr->app_list[i];
        bool a2l_fin = atomic_load(&app->a2l_finalized) != 0;
        uint32_t alive_cnt = (int32_t)atomic_load(&app->alive_counter);
        printf("  [%u] '%s', epk='%s', %s pid=%u%s%s, init_mode=%02X, a2l_name='%s', alive_counter=%u\n", i, app->project_name, app->epk, //
               app->pid != 0 ? "alive" : "stale",                                                                                         //
               app->pid,                                                                                                                  //
               app->is_leader ? " leader" : "",                                                                                           //
               app->is_server ? " server" : "",                                                                                           //
               app->xcp_init_mode,                                                                                                        //
               a2l_fin ? app->a2l_name : "pending", alive_cnt);
    }
    printf(ANSI_COLOR_RESET);

    // --- Event list ---
    uint16_t event_count = XcpGetEventCount();
    printf(ANSI_COLOR_BLUE "Events (%u):\n" ANSI_COLOR_RESET, event_count);
    for (uint16_t id = 0; id < event_count; id++) {
        const tXcpEvent *ev = XcpGetEvent(id);
        if (ev == NULL)
            printf("  [%u] not found\n", id);
        else
            printf("  [%u] '%s', cycle_ns=%u, index=%u, app_id=%u, daq_first=%u, flags=0x%02X\n", id, XcpGetEventName(id), ev->cycle_time_ns, ev->index, ev->app_id, ev->daq_first,
                   ev->flags);
    }
    printf(ANSI_COLOR_RESET);

    // --- Calibration segment list ---
    uint16_t calseg_count = XcpGetCalSegCount();
    printf(ANSI_COLOR_BLUE "CalSegs (%u):\n" ANSI_COLOR_RESET, calseg_count);
    for (uint16_t i = 0; i < calseg_count; i++) {
        const tXcpCalSeg *cs = XcpGetCalSeg(i);
        if (cs == NULL)
            printf("  [%u] not found\n", i);
        else
            printf("  [%u] '%s', size=%u, app_id=%u, seg_num=%u\n", i, cs->h.name, cs->h.size, cs->h.app_id, cs->h.calseg_number);
    }
    printf(ANSI_COLOR_RESET);
}

/**************************************************************************/
// A2L file management
/**************************************************************************/

// Signal all processes to finalize their A2L file immediately
// From now, no other processes may join the XCP session
void XcpShmRequestA2lFinalize(void) {
    assert(isActivated_(gXcpData));
    atomic_store(&gXcpData->shm_header.a2l_finalize_requested, 1U);
    DBG_PRINT5("XcpShmRequestA2lFinalize: Requested A2L finalization for all applications\n");
}

// Returns true once the leader has set the A2L finalize request flag.
bool XcpShmIsA2lFinalizeRequested(void) {
    if (!isInitialized_(gXcpData))
        return false;
    return atomic_load(&gXcpData->shm_header.a2l_finalize_requested) != 0;
}

// Returns true when this app process has finalized its A2L file and set its a2l_finalized flag in the app list
bool XcpShmIsA2lFinalized(uint8_t app_id) {
    if (!isInitialized_(gXcpData))
        return false;
    if (app_id >= SHM_MAX_APP_COUNT) {
        assert(0);
        return false;
    }
    const tApp *app = &gXcpData->shm_header.app_list[app_id];
    return atomic_load(&app->a2l_finalized) != 0;
}

// Update any application slot with its A2L filename and mark it as finalized.
void XcpShmSetA2lFinalized(uint8_t app_id, const char *a2l_name) {
    assert(isActivated_(gXcpData));
    if (a2l_name == NULL || a2l_name[0] == '\0') {
        assert(0);
        return;
    }
    if (app_id >= SHM_MAX_APP_COUNT) {
        assert(0);
        return;
    }
    tApp *app = &gXcpData->shm_header.app_list[app_id];
    strncpy(app->a2l_name, a2l_name, XCP_A2L_FILENAME_MAX_LENGTH);
    app->a2l_name[XCP_A2L_FILENAME_MAX_LENGTH] = '\0';
    atomic_store(&app->a2l_finalized, 1U);
    DBG_PRINTF3(ANSI_COLOR_BLUE "XcpShmSetA2lFinalized: app_id=%u a2l_name='%s'\n" ANSI_COLOR_RESET, app_id, a2l_name);
}

// Wait up to timeout_ms for all registered to set their a2l_finalized flag,
// then populate filenames[] with the a2l_name strings (pointers into SHM)
// Returns the number of entries finalized after waiting, which may be less than the total app count if some apps failed to finalize within the timeout.
int XcpShmCollectA2lFiles(uint32_t timeout_ms, const char *filenames[], int max_count) {

    assert(isActivated_(gXcpData));

    // Wait
    uint32_t elapsed = 0;
    const uint32_t poll_ms = 50;
    while (elapsed < timeout_ms) {
        bool all_ready = true;
        uint32_t app_count = (uint32_t)atomic_load(&gXcpData->shm_header.app_count);
        for (uint32_t i = 0; i < app_count && i < SHM_MAX_APP_COUNT; i++) {
            const tApp *app = &gXcpData->shm_header.app_list[i];
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

    // Collect
    int count = 0;
    uint32_t app_count = (uint32_t)atomic_load(&gXcpData->shm_header.app_count);
    for (uint32_t i = 0; i < app_count && i < SHM_MAX_APP_COUNT && count < max_count; i++) {
        const tApp *app = &gXcpData->shm_header.app_list[i];
        if (atomic_load(&app->a2l_finalized) != 0 && app->a2l_name[0] != '\0') {
            filenames[count++] = app->a2l_name;
        } else if (!atomic_load(&app->a2l_finalized)) {
            DBG_PRINTF_WARNING(ANSI_COLOR_BLUE "XcpShmCollectA2lFiles: app %u ('%s') not finalized\n" ANSI_COLOR_RESET, i, app->project_name);
        }
    }
    DBG_PRINTF5("XcpShmCollectA2lFiles: collected %d A2L file(s)\n", count);
    return count;
}

/**************************************************************************/
// Public getter/setters
/**************************************************************************/

// Get the project name of the ECU
// @@@@ TODO: Implement setting the ECU project name
const char *XcpShmGetEcuProjectName(void) { return SHM_PROJECT_NAME; }

// Get the number of registered applications in SHM mode.
uint8_t XcpShmGetAppCount(void) {
    assert(isActivated_(gXcpData));
    return (uint8_t)atomic_load(&gXcpData->shm_header.app_count);
}

// Get the number of registered and alive applications in SHM mode.
uint8_t XcpShmGetActiveAppCount(void) {
    assert(isActivated_(gXcpData));
    uint8_t count = 0;
    uint32_t app_count = (uint32_t)atomic_load(&gXcpData->shm_header.app_count);
    for (uint32_t i = 0; i < app_count && i < SHM_MAX_APP_COUNT; i++) {
        const tApp *app = &gXcpData->shm_header.app_list[i];
        if (atomic_load(&app->alive_counter) > 0 && app->pid != 0) {
            count++;
        }
    }
    return count;
}

// Get the project name of an app slot by its app_id index.
// Returns NULL if the slot is vacant or out of range.
const char *XcpShmGetAppProjectName(uint8_t app_id) {
    assert(isActivated_(gXcpData));
    if (app_id >= SHM_MAX_APP_COUNT)
        return NULL;
    const tApp *app = &gXcpData->shm_header.app_list[app_id];
    return app->project_name;
}

// Get the EPK of an app slot by its app_id index.
// Returns NULL if the slot is vacant or out of range.
const char *XcpShmGetAppEpk(uint8_t app_id) {
    assert(isActivated_(gXcpData));
    if (app_id >= SHM_MAX_APP_COUNT)
        return NULL;
    const tApp *app = &gXcpData->shm_header.app_list[app_id];
    return app->epk;
}

// Get the init mode of an app slot by its app_id index.
// Returns 0 if the slot is vacant or out of range.
uint8_t XcpShmGetInitMode(uint8_t app_id) {
    assert(isActivated_(gXcpData));
    if (app_id >= SHM_MAX_APP_COUNT)
        return 0;
    const tApp *app = &gXcpData->shm_header.app_list[app_id];
    return app->xcp_init_mode;
}

/**************************************************************************/
// Alive counter management to detect stale followers
/**************************************************************************/

// Increment this process's alive_counter so the leader can detect stale followers.
void XcpShmIncrementAliveCounter(void) {

    if (!isInitialized_(gXcpData))
        return;

    uint8_t slot = XcpShmGetAppId();
    if (slot >= SHM_MAX_APP_COUNT)
        return;
    atomic_fetch_add(&gXcpData->shm_header.app_list[slot].alive_counter, 1U);
}

// Reset the alive_counter of all app slots
static void XcpShmResetAliveCounters_(void) {
    if (!isInitialized_(gXcpData))
        return;
    uint32_t app_count = (uint32_t)atomic_load(&gXcpData->shm_header.app_count);
    for (uint32_t i = 0; i < app_count && i < SHM_MAX_APP_COUNT; i++) {
        atomic_store(&gXcpData->shm_header.app_list[i].alive_counter, 0U);
    }
}

// Get the alive_counter of an app slot
uint32_t XcpShmGetAliveCounter(uint8_t app_id) {
    if (!isInitialized_(gXcpData))
        return 0;
    if (app_id >= SHM_MAX_APP_COUNT)
        return 0;
    return (uint32_t)atomic_load(&gXcpData->shm_header.app_list[app_id].alive_counter);
}

// Check the alive counter and set application states accordingly
// This should be called periodically by any process to detect stale applications that died without notifying
void XcpShmCheckAliveCounters(void) {

    // Reset inactive applications
    for (uint32_t i = 0; i < SHM_MAX_APP_COUNT; i++) {
        tApp *app = &gXcpData->shm_header.app_list[i];
        uint32_t alive_count = (uint32_t)atomic_load(&app->alive_counter);
        if (alive_count == 0 && app->pid != 0) {

            // Check if the process is still alive by sending signal 0 (no-op)
            // If kill returns -1 and errno is ESRCH, the process does not exist anymore, so we can consider it stale and reset its slot.
            // If the process is still alive but not incrementing its alive counter, we will detect it as stale in the next check after a few seconds, which is acceptable.
            int res = kill((pid_t)app->pid, 0);
            printf("XcpShmCheckAliveCounters: app %u:'%s' (pid=%u) alive_counter=0, kill res=%d errno=%d\n", i, app->project_name, app->pid, res, errno);
            if (res == 0 /* success */ || errno != ESRCH /* Does not exist */) {
                // Process is still alive, but not incrementing alive counter, maybe it's stuck or paused. We will detect it as stale in the next check after a few seconds, which
                // is acceptable.
                DBG_PRINTF_WARNING("XcpShmCheckAliveCounters: Detected aliv e_counter==0 application %u:'%s' (pid=%u), but process is still alive, waiting for next check...\n", i,
                                   app->project_name, app->pid);
                continue;
            }

            // This app is stale, reset its state
            app->pid = 0;
            app->is_leader = 0;
            app->is_server = 0;
            atomic_store(&app->alive_counter, 0U);
            // Rest of the state is kept in shared memory
            // app->a2l_finalized == 0;
            // app->a2l_name[0] = '\0';
            // app->xcp_init_mode = 0;
            DBG_PRINTF_WARNING("XcpShmCheckAliveCounters: Detected stale application %u:'%s', resetting slot\n", i, app->project_name);
        }
    }

    // Print changes since last check
    if (DBG_LEVEL >= 3) {
        static uint32_t last_count = 0;
        uint32_t current_count = XcpShmGetActiveAppCount(); // Apps with alive_count > 0
        if (last_count != current_count) {
            XcpShmDebugPrint();
            last_count = current_count;
        }
    }

    // Reset alive counters, so applications must increment them to prove they are alive
    XcpShmResetAliveCounters_();
}

/**************************************************************************/
// Registration
/**************************************************************************/

// Register this process in the SHM application list
// Returns the allocated application id  (slot index) or -1 on error
// If a slot with a matching project_name already exists (process restart), it is reused
int16_t XcpShmRegisterApp(const char *name, const char *epk, uint32_t pid, uint8_t xcp_init_mode, bool is_leader, bool is_server) {

    assert(isInitialized_(gXcpData));
    assert(name != NULL);
    assert(epk != NULL);

    tShmHeader *hdr = &gXcpData->shm_header;

    // Scan for an existing slot with this application name and epk
    uint32_t count = (uint32_t)atomic_load(&hdr->app_count);
    for (uint32_t i = 0; i < count; i++) {
        if (strncmp(hdr->app_list[i].project_name, name, XCP_PROJECT_NAME_MAX_LENGTH) == 0) {
            tApp *app = &hdr->app_list[i];

            // If the epk version also matches, we consider this a process restart or a pre registered application and reuse the existing id
            // Otherwise we consider this application as new
            if (strncmp(app->epk, epk, XCP_EPK_MAX_LENGTH) == 0) {
                if (app->pid != 0) {
                    DBG_PRINTF_WARNING(
                        "XcpShmRegisterApp: Application '%s' with matching EPK '%s' is already registered in slot %u, alive with with pid %u, reusing slot for new process\n", name,
                        epk, i, app->pid);
                    // @@@@ TODO: Register application twice handling
                    // Would be ok to continue when the application just died, and the server did not detect this, which is a rare race condition
                    // This also happen if the application is started twice
                    return -1;
                }
                atomic_store(&app->alive_counter, 0U); // reset alive counter on restart
                app->pid = pid;
                app->is_leader = is_leader;
                app->is_server = is_server;
                assert(app->xcp_init_mode == xcp_init_mode);
                bool a2l_fin = atomic_load(&app->a2l_finalized) != 0;
                // @@@@ TODO: Check if the A2L file still exists and reset the a2l_finalized flag if not ?
                DBG_PRINTF5("XcpShmRegisterApp: Registered application %u:'%s', epk=%s, a2l_finalized=%u, a2l_name='%s'\n", i, name, epk, a2l_fin,
                            a2l_fin ? app->a2l_name : "pending");
                return (uint8_t)i;
            } else {
                DBG_PRINTF_ERROR("XcpShmRegisterApp:Application %u:'%s' has different epk %s, reset please !!!\n", i, name, epk);
                return -1;
            }
        }
    }

    // Atomically claim a fresh slot
    uint32_t slot = (uint32_t)atomic_fetch_add(&hdr->app_count, 1U);
    if (slot >= SHM_MAX_APP_COUNT) {
        DBG_PRINT_ERROR("XcpShmRegisterApp: Application list full\n");
        atomic_fetch_sub(&hdr->app_count, 1U); // undo the increment
        return -1;
    }
    tApp *app = &hdr->app_list[slot];

    // Initialize the new slot
    memset(app->b, 0, sizeof(app->b));
    strncpy(app->project_name, name, XCP_PROJECT_NAME_MAX_LENGTH);
    app->project_name[XCP_PROJECT_NAME_MAX_LENGTH] = '\0';
    strncpy(app->epk, epk, XCP_EPK_MAX_LENGTH);
    app->epk[XCP_EPK_MAX_LENGTH] = '\0';
    app->pid = pid;
    app->is_leader = is_leader;
    app->is_server = is_server;
    app->xcp_init_mode = xcp_init_mode;
    app->a2l_name[0] = '\0';
    atomic_store(&app->a2l_finalized, 0U);
    atomic_store(&app->alive_counter, 0U);

    // Update the ECU EPK hash
    XcpShmGetEcuEpk();

    DBG_PRINTF5("XcpShmRegisterApp: Registered application %u:'%s' (pid=%u, mode=%02X %s %s)\n", slot, name, (unsigned)getpid(), xcp_init_mode, is_leader ? "leader" : "",
                is_server ? "server" : "");
    return (int16_t)slot;
}

// Reset an application slot to offline
void XcpShmShutdownApp(uint8_t app_id) {

    if (app_id >= SHM_MAX_APP_COUNT)
        return;
    tApp *app = &gXcpData->shm_header.app_list[app_id];
    app->pid = 0;
    app->alive_counter = 0;
    app->is_leader = 0;
    app->is_server = 0;
    // Rest of the state is kept in shared memory
    // app->a2l_finalized == 0;
    // app->a2l_name[0] = '\0';
    // app->xcp_init_mode = 0;
    DBG_PRINTF3(ANSI_COLOR_BLUE "XcpShmShutdownApp: Set application %u:'%s' to offline\n" ANSI_COLOR_RESET, app_id, app->project_name);

    // Check if there are any more active applications (pid!=0 and alive_counter>0),
    //  if not we can reset the whole SHM state for the next leader
    if (XcpShmGetActiveAppCount() == 0) {
        DBG_PRINT3(ANSI_COLOR_BLUE "XcpShmShutdownApp: No more active applications, resetting shared memory state\n" ANSI_COLOR_RESET);
        // @@@@ TODO: Check If it is a benefit to do this ?
        XcpShmUnlink(); // Unlink the shared memory, so the next leader will load the binary file and create a fresh one
    }
}

#endif // SHM_MODE
