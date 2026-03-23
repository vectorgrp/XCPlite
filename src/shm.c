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

static bool isInitialized_(tXcpData *xcp_data) { return xcp_data != NULL && xcp_data->shm_header.magic == SHM_MAGIC && xcp_data->shm_header.version == SHM_VERSION; }
static bool isActivated_(tXcpData *xcp_data) {
    return xcp_data != NULL && xcp_data->shm_header.magic == SHM_MAGIC && xcp_data->shm_header.version == SHM_VERSION && 0 != (xcp_data->session_status & SS_ACTIVATED);
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

// Returns the EPK string of the overall ECU
// Computes a FNV-1a 64-bit hash over all application EPK strings and returns it as a 16-char hex string.
// The result is always 16 ASCII hex characters, fits in XCP_ECU_EPK_MAX_LENGTH, and changes whenever any app EPK changes.
const char *XcpShmGetEcuEpk(void) {
    assert(XcpShmIsActive());
    if (!XcpShmIsActive()) {
        return "";
    }
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

// Check operating modes
bool XcpShmIsActive(void) { return (local.init_mode & XCP_MODE_SHM) != 0; }
bool XcpShmIsLeader(void) { return (local.init_mode & XCP_MODE_SHM) != 0 && local.shm_leader; }
bool XcpShmIsServer(void) { return (local.init_mode & XCP_MODE_SHM) != 0 && local.shm_server; }
bool XcpShmIsFollower(void) { return (local.init_mode & XCP_MODE_SHM) != 0 && !local.shm_leader; }

#ifdef DBG_LEVEL

// Init shared memory header
void XcpShmInitHeader(tShmHeader *hdr) {
    assert(hdr != NULL);
    if (hdr == NULL) {
        return;
    }

    hdr->magic = SHM_MAGIC;
    hdr->version = SHM_VERSION;
    hdr->size = (uint32_t)sizeof(tXcpData);
    hdr->leader_pid = (uint32_t)getpid();
    atomic_store(&hdr->app_count, 0U);
    atomic_store(&hdr->a2l_finalize_requested, 0U);
    memset(hdr->ecu_epk, 0, sizeof(hdr->ecu_epk));
}

tXcpData *XcpShmAttachOrCreate(bool *out_is_leader) {

    tXcpData *xcp_data = NULL;
    bool is_leader = false;

    xcp_data = (tXcpData *)platformShmOpen("/xcpdata", "/tmp/xcpdata.lock", sizeof(tXcpData), &is_leader);
    if (xcp_data == NULL) {
        DBG_PRINT_ERROR("XcpShmAttachOrCreate: failed to open shared memory\n");
        *out_is_leader = false;
        return NULL;
    }

    if (!is_leader) {

        // Check if init and activate

        // Wait up to 500ms for the leader to complete his XcpInit()
        if (!isActivated_(xcp_data)) {
            DBG_PRINT5("XcpShmAttachOrCreate: waiting for leader to activate XCP ...\n");
            for (int i = 0; i < 500 && !isActivated_(xcp_data); i++) {
                sleepUs(1000);
            }
        }

        if (isActivated_(xcp_data)) {
            DBG_PRINTF5("XcpShmAttachOrCreate: Attached to existing shared memory '/xcpdata' from leader pid=%u\n", xcp_data->shm_header.leader_pid);

            // @@@@ TODO: What if the leader died, but the SHM is still there and not yet reclaimed by another leader?
            // Existing shared memory is valid and active, we can attach as a follower
            *out_is_leader = false;
            return xcp_data;
        }

        // Invalid SHM layout version
        if (xcp_data->shm_header.magic != SHM_MAGIC || xcp_data->shm_header.version != SHM_VERSION) {
            DBG_PRINT_ERROR("XcpShmAttachOrCreate: shared memory is corrupt or has invalid magic or version\n");
        }

        // Stale SHM: the previous leader died without calling platformShmClose
        // Reclaim ownership: unmap + unlink, then re-open as new leader.
        DBG_PRINT5("XcpShmAttachOrCreate: stale or corrupt shared memory '/xcpdata' detected, reclaiming as leader\n");
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
    DBG_PRINT5("XcpShmUnlink: Unlink SHM data '/xcpdata'\n");
    platformShmClose("/xcpdata", gXcpData, sizeof(tXcpData), true /* unlink */);
}

/**************************************************************************/
// Debug print
/**************************************************************************/

// Print the status and information in tXcpData, for debugging purposes.
void XcpShmDebugPrint(void) {

    const tShmHeader *hdr = &gXcpData->shm_header;
    assert(hdr != NULL);
    if (hdr == NULL)
        return;

    // --- SHM header ---
    uint32_t app_count = (uint32_t)atomic_load(&hdr->app_count);
    printf(ANSI_COLOR_BLUE "SHM Header:\n" ANSI_COLOR_GREY);
    printf("  magic=0x%016" PRIX64 ", version=%06X, size=%u\n", (uint64_t)hdr->magic, hdr->version, hdr->size);
    printf("  leader_pid=%u, app_count=%u, a2l_finalize_requested=%u\n", hdr->leader_pid, app_count, (unsigned)atomic_load(&hdr->a2l_finalize_requested));
    printf("  ecu_epk='%s'\n", hdr->ecu_epk);
    printf(ANSI_COLOR_RESET);

    // --- App list ---
    printf(ANSI_COLOR_BLUE "Apps (%u):\n" ANSI_COLOR_GREY, app_count);
    for (uint32_t i = 0; i < app_count && i < SHM_MAX_APP_COUNT; i++) {
        const tApp *app = &hdr->app_list[i];
        bool a2l_fin = atomic_load(&app->a2l_finalized) != 0;
        printf("  [%u] '%s', epk='%s', mode=%02X, %s pid=%u, %s %s, a2l_fin=%u, a2l_name='%s'\n", i, app->project_name, app->epk, app->xcp_init_mode,
               (unsigned)atomic_load(&app->alive_counter) > 0 ? "alive" : "stale", app->pid, app->is_leader ? "leader" : "follower", app->is_server ? "server" : "", a2l_fin,
               a2l_fin ? app->a2l_name : "pending");
    }
    printf(ANSI_COLOR_RESET);

    // --- Event list ---
    uint16_t event_count = XcpGetEventCount();
    printf(ANSI_COLOR_BLUE "Events (%u):\n" ANSI_COLOR_GREY, event_count);
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
    printf(ANSI_COLOR_BLUE "CalSegs (%u):\n" ANSI_COLOR_GREY, calseg_count);
    for (uint16_t i = 0; i < calseg_count; i++) {
        const tXcpCalSeg *cs = XcpGetCalSeg(i);
        if (cs == NULL)
            printf("  [%u] not found\n", i);
        else
            printf("  [%u] '%s', size=%u, app_id=%u, seg_num=%u\n", i, cs->h.name, cs->h.size, cs->h.app_id, cs->h.calseg_number);
    }
    printf(ANSI_COLOR_RESET);
}

#endif

/**************************************************************************/
// A2L file management
/**************************************************************************/

// Signal all processes to finalize their A2L file immediately
// From now, no other processes may join the XCP session
void XcpShmRequestA2lFinalize(void) {
    assert(XcpShmIsActive());
    assert(isActivated_(gXcpData));
    atomic_store(&gXcpData->shm_header.a2l_finalize_requested, 1U);
    DBG_PRINT5("XcpShmRequestA2lFinalize: Requested A2L finalization for all applications\n");
}

// Returns true once the leader has set the A2L finalize request flag.
bool XcpShmIsA2lFinalizeRequested(void) {
    if (!XcpShmIsActive() || !isInitialized_(gXcpData))
        return false;
    return atomic_load(&gXcpData->shm_header.a2l_finalize_requested) != 0;
}

// Returns true when this app process has finalized its A2L file and set its a2l_finalized flag in the app list
bool XcpShmIsA2lFinalized(uint8_t app_id) {
    if (!XcpShmIsActive() || !isInitialized_(gXcpData))
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
    assert(XcpShmIsActive());
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
    STRNCPY(app->a2l_name, a2l_name, XCP_A2L_FILENAME_MAX_LENGTH);
    app->a2l_name[XCP_A2L_FILENAME_MAX_LENGTH] = '\0';
    atomic_store(&app->a2l_finalized, 1U);
    DBG_PRINTF5("XcpShmSetA2lFinalized: app_id=%u a2l_name='%s'\n", app_id, a2l_name);
}

// Wait up to timeout_ms for all registered to set their a2l_finalized flag,
// then populate filenames[] with the a2l_name strings (pointers into SHM)
// Returns the number of entries finalized after waiting, which may be less than the total app count if some apps failed to finalize within the timeout.
int XcpShmCollectA2lFiles(uint32_t timeout_ms, const char *filenames[], int max_count) {

    assert(XcpShmIsActive());
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
// @@@@ TODO: implement setting the ECU project name
const char *XcpShmGetEcuProjectName(void) { return SHM_PROJECT_NAME; }

// Get the number of registered applications in SHM mode.
uint8_t XcpShmGetAppCount(void) {
    if (!XcpShmIsActive())
        return 0;
    assert(isActivated_(gXcpData));
    return (uint8_t)atomic_load(&gXcpData->shm_header.app_count);
}

// Get the number of registered and alive applications in SHM mode.
uint8_t XcpShmGetActiveAppCount(void) {
    if (!XcpShmIsActive())
        return 0;
    assert(isActivated_(gXcpData));
    uint8_t count = 0;
    uint32_t app_count = (uint32_t)atomic_load(&gXcpData->shm_header.app_count);
    for (uint32_t i = 0; i < app_count && i < SHM_MAX_APP_COUNT; i++) {
        const tApp *app = &gXcpData->shm_header.app_list[i];
        if (atomic_load(&app->alive_counter) > 0) {
            count++;
        }
    }
    return count;
}

// Get the project name of an app slot by its app_id index.
// Returns NULL if the slot is vacant or out of range.
const char *XcpShmGetAppProjectName(uint8_t app_id) {
    assert(XcpShmIsActive());
    assert(isActivated_(gXcpData));
    if (app_id >= SHM_MAX_APP_COUNT)
        return NULL;
    const tApp *app = &gXcpData->shm_header.app_list[app_id];
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
    return app->epk;
}

// Get the init mode of an app slot by its app_id index.
// Returns 0 if the slot is vacant or out of range.
uint8_t XcpShmGetInitMode(uint8_t app_id) {
    assert(XcpShmIsActive());
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

    if (!XcpShmIsActive() || !isInitialized_(gXcpData))
        return;

    uint8_t slot = XcpShmGetAppId();
    if (slot >= SHM_MAX_APP_COUNT)
        return;
    atomic_fetch_add(&gXcpData->shm_header.app_list[slot].alive_counter, 1U);
}

// Reset the alive_counter of an app slot
void XcpShmResetAliveCounter(uint8_t app_id) {
    if (!XcpShmIsActive() || !isInitialized_(gXcpData))
        return;
    if (app_id >= SHM_MAX_APP_COUNT)
        return;
    atomic_store(&gXcpData->shm_header.app_list[app_id].alive_counter, 0U);
}

// Reset the alive_counter of all app slots
void XcpShmResetAliveCounters(void) {
    if (!XcpShmIsActive() || !isInitialized_(gXcpData))
        return;
    uint32_t app_count = (uint32_t)atomic_load(&gXcpData->shm_header.app_count);
    for (uint32_t i = 0; i < app_count && i < SHM_MAX_APP_COUNT; i++) {
        atomic_store(&gXcpData->shm_header.app_list[i].alive_counter, 0U);
    }
}

// Get the alive_counter of an app slot
uint32_t XcpShmGetAliveCounter(uint8_t app_id) {
    if (!XcpShmIsActive() || !isInitialized_(gXcpData))
        return 0;
    if (app_id >= SHM_MAX_APP_COUNT)
        return 0;
    return (uint32_t)atomic_load(&gXcpData->shm_header.app_list[app_id].alive_counter);
}

void XcpShmCheckAliveCounters(void) {

    if (DBG_LEVEL >= 3) {
        static uint32_t last_count = 0;
        uint32_t current_count = XcpShmGetActiveAppCount(); // Apps with alive_count > 0
        if (last_count != current_count) {
            if (last_count < current_count)
                DBG_PRINT3(ANSI_COLOR_BLUE "New applications:'\n" ANSI_COLOR_RESET);
            else
                DBG_PRINT3(ANSI_COLOR_BLUE "Applications lost:'\n" ANSI_COLOR_RESET);

            XcpShmDebugPrint();
            last_count = current_count;
        }
    }

    XcpShmResetAliveCounters(); // Reset alive counters, so applications must increment them to prove they are alive
}

/**************************************************************************/
// Registration
/**************************************************************************/

// Register this process in the SHM application list
// Returns the allocated application id  (slot index) or -1 on error
// If a slot with a matching project_name already exists (process restart), it is reused
int16_t XcpShmRegisterApp(const char *name, const char *epk, uint8_t xcp_init_mode, bool is_leader, bool is_server) {

    assert(XcpShmIsActive());
    assert(isInitialized_(gXcpData));
    assert(name != NULL);
    assert(epk != NULL);

    tShmHeader *hdr = &gXcpData->shm_header;

    // Scan for an existing slot with this application name
    uint32_t count = (uint32_t)atomic_load(&hdr->app_count);
    for (uint32_t i = 0; i < count; i++) {
        if (strncmp(hdr->app_list[i].project_name, name, XCP_PROJECT_NAME_MAX_LENGTH) == 0) {
            tApp *app = &hdr->app_list[i];

            // If the epk version also matches, we consider this a process restart or a pre registered application and reuse the existing id
            // Otherwise we consider this application as new
            if (strncmp(app->epk, epk, XCP_EPK_MAX_LENGTH) == 0) {
                app->pid = (uint32_t)getpid();         // Set current PID, the old process might have died and left a stale entry
                atomic_store(&app->alive_counter, 0U); // reset alive counter on restart
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
        DBG_PRINT_ERROR("XcpShmRegisterApp:Application list full\n");
        atomic_fetch_sub(&hdr->app_count, 1U); // undo the increment
        return -1;
    }
    tApp *app = &hdr->app_list[slot];

    // Initialize the new slot
    memset(app->b, 0, sizeof(app->b));
    STRNCPY(app->project_name, name, XCP_PROJECT_NAME_MAX_LENGTH);
    app->project_name[XCP_PROJECT_NAME_MAX_LENGTH] = '\0';
    STRNCPY(app->epk, epk, XCP_EPK_MAX_LENGTH);
    app->epk[XCP_EPK_MAX_LENGTH] = '\0';
    app->pid = (uint32_t)getpid();
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

void XcpShmUnRegisterApp(uint8_t app_id) {

    assert(XcpShmIsActive());

    if (app_id >= SHM_MAX_APP_COUNT)
        return;
    tApp *app = &gXcpData->shm_header.app_list[app_id];
    app->pid = 0; // Mark slot as vacant
    app->alive_counter = 0;
    app->is_leader = 0;
    app->is_server = 0;
    DBG_PRINTF5("XcpShmUnRegisterApp:Unregistered application %u:'%s'\n", app_id, app->project_name);
}

#endif // OPTION_SHM_MODE
