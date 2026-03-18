#pragma once
#define __SHM_H__

/*----------------------------------------------------------------------------
| File:
|   shm.h
|
| Description:
|   XCPlite internal header file for shared memory management
|
| All functions, types and constants intended to be public API are declared in xcplib.h
|
| Copyright (c) Vector Informatik GmbH. All rights reserved.
| See LICENSE file in the project root for details.
|
 ----------------------------------------------------------------------------*/

#include <stdbool.h> // for bool
#include <stdint.h>  // for uint16_t, uint32_t, uint8_t

#ifdef __cplusplus
extern "C" {
#endif

#ifdef OPTION_SHM_MODE

struct XcpData; // Forward declaration
typedef struct XcpData tXcpData;

/****************************************************************************/
/* Protocol layer state data                                                */
/****************************************************************************/

// Shared-memory application registration table

#define SHM_MAGIC 0x5843504C4954455F // "XCPLITE_" in little-endian ASCII
#define SHM_VERSION 0x00010000       // Version 1.0.0
#define SHM_MAX_APP_COUNT 8          // Maximum number of concurrently registered processes

// Per-application entry in tShmHeader.app_list.
// Each participating process registers exactly one slot identified by its project_name.
// Restarting the same application (same project_name) reuses the existing slot.
typedef union {
    struct {
        // Identity (written at registration, constant afterwards)
        char project_name[XCP_PROJECT_NAME_MAX_LENGTH + 1]; // unique app name (null-terminated)
        char epk[XCP_EPK_MAX_LENGTH + 1];                   // build version  (null-terminated)
        char a2l_name[XCP_A2L_FILENAME_MAX_LENGTH + 1];     // A2L filename without ext
        uint32_t pid;                                       // OS process ID; 0 = slot is vacant
        uint8_t is_leader;                                  // != 0 this process created the shared memory segment
        uint8_t is_server;                                  // != 0 this process is the XCP server (handles client connections and DAQ)
        uint8_t pad1[2];                                    // explicit padding for deterministic cross-compiler layout
#ifdef __cplusplus
        // GCC C++ does not allow atomic (non-trivially constructible) members in anonymous aggregates.
        // Use plain uint32_t here; shmtool accesses these via read_u32/write_u32 volatile casts.
        uint32_t alive_counter; // same layout as atomic_uint_least32_t
        uint32_t a2l_finalized; // same layout as atomic_uint_least32_t
#else
        atomic_uint_least32_t alive_counter; // incremented periodically by each process's background thread; allows the leader to detect stale/dead followers
        atomic_uint_least32_t a2l_finalized; // 1 when this app's A2L file is complete and a2l_name is valid
#endif
    };
    uint8_t b[512]; // pad slot to 512 bytes for future extensions
} tApp;
static_assert(sizeof(tApp) == 512, "sizeof tApp must be 512 bytes");

// Shared-memory header
// Control area (first 64 bytes = one cache line) followed by the per-process application list.
typedef struct {
    // --- Control area (64 bytes, one cache line) ---
    uint64_t magic;                               // SHM_MAGIC: marks as valid
    uint32_t version;                             // SHM_VERSION: layout version for forward compatibility
    uint32_t size;                                // total mmap size in bytes
    uint32_t leader_pid;                          // PID of the process that created the shared memory region, 0 until ready
    atomic_uint_least32_t app_count;              // number of registered slots (grows up to SHM_MAX_APP_COUNT)
    atomic_uint_least32_t a2l_finalize_requested; // leader writes 1 here on the first XCP client CONNECT;
                                                  //   each follower's background thread polls this and calls A2lFinalize()
    uint8_t pad[64 - 8 - 4 - 4 - 4 - 4 - 4];      // 36 bytes: pad control area to exactly 64 bytes
    // --- Per-process application list ---
    tApp app_list[SHM_MAX_APP_COUNT]; // 512 * 8 = 4096 bytes
} tShmHeader;
static_assert(sizeof(tShmHeader) % 64 == 0, "sizeof tShmHeader must be a multiple of 64 bytes");

uint8_t XcpShmGetAppId(void);              // Get this application process's id
const char *XcpShmGetEcuProjectName(void); // Get the project name of the ECU

bool XcpShmIsActive(void);   // true when this process is in SHM_MODE
bool XcpShmIsServer(void);   // true when this process is the XCP server
bool XcpShmIsLeader(void);   // true when this process created the shared memory region
bool XcpShmIsFollower(void); // true when this process is a follower attached to a leader

void XcpShmInit(tXcpData *xcp_data);                 // Initalize shared memory for this process, and register this process in the app list
tXcpData *XcpShmAttachOrCreate(bool *out_is_leader); // Attach to an existing shared memory region created by another process or create a new one

void XcpShmRequestA2lFinalize(void);             // Leader: signals all followers to finalize their A2L file now
bool XcpShmIsA2lFinalizeRequested(void);         // Follower: returns true when leader has set the finalize flag
void XcpShmNotifyA2lFinalized(const char *name); // Update this process's A2L file name and mark it as finalized

void XcpShmIncrementAliveCounter(void);                                                 // Follower background thread: prove this process is still alive
int XcpShmCollectA2lFiles(uint32_t timeout_ms, const char *filenames[], int max_count); // Leader: wait and collect follower partial A2L filenames

uint8_t XcpShmGetAppCount(void);                     // Get the number of registered applications in SHM mode
const char *XcpShmGetAppProjectName(uint8_t app_id); // Get project name of an app slot by app_id index
const char *XcpShmGetAppEpk(uint8_t app_id);         // Get EPK of an app slot by app_id index

// Register this process in the SHM application list; returns allocated application id (slot index) or -1 on error
int16_t XcpShmRegisterApp(const char *name, const char *epk, bool is_leader, bool is_server);

#ifdef DBG_LEVEL
void XcpShmDebugPrint(tXcpData *xcp_data); // Print the status and information in tXcpData, for debugging purposes.
#endif

#else // OPTION_SHM_MODE

#define XcpShmGetAppCount() 0
#

#endif

#ifdef __cplusplus
} // extern "C"
#endif
