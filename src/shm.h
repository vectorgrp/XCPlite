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

#include "xcp_cfg.h" // for XCP_PROJECT_NAME_MAX_LENGTH, XCP_EPK_MAX_LENGTH, XCP_A2L_FILENAME_MAX_LENGTH

#ifdef __cplusplus
extern "C" {
#endif

#ifdef OPTION_SHM_MODE // shm.h guard, can be includes in both SHM and non-SHM mode, but the content is only relevant in SHM mode

struct XcpData; // Forward declaration
typedef struct XcpData tXcpData;

/****************************************************************************/
/* Protocol layer state data                                                */
/****************************************************************************/

// Shared-memory application registration table

#define SHM_MAGIC 0x5843504C4954455F // "XCPLITE_" in little-endian ASCII
#define SHM_VERSION 0x00010000       // Version 1.0.0
#define SHM_PROJECT_NAME "main"      // Name of the main A2L and BIN file
#define SHM_MAX_APP_COUNT 8          // Maximum number of concurrently registered processes

// Per-application entry in tShmHeader.app_list.
// Each participating process registers exactly one slot identified by its project_name.
// Restarting the same application (same project_name) reuses the existing slot.
typedef union {
    struct {
        char project_name[XCP_PROJECT_NAME_MAX_LENGTH + 1]; // unique app name (null-terminated)
        char epk[XCP_EPK_MAX_LENGTH + 1];                   // build version  (null-terminated)
        char a2l_name[XCP_A2L_FILENAME_MAX_LENGTH + 1];     // A2L filename without ext
        uint32_t pid;                                       // Application process ID, set to 0 on gracefull shutdown
        uint8_t is_leader;                                  // != 0 this process created the shared memory segment
        uint8_t is_server;                                  // != 0 this process is the XCP server (handles client connections and DAQ)
        uint8_t xcp_init_mode;                              // XCP init mode (mode given to XcpInit)
        uint8_t reserved[1];                                // reserved
        atomic_uint_least32_t alive_counter;                // incremented periodically by each process's background thread; allows the leader to detect stale/dead followers
        atomic_uint_least32_t a2l_finalized;                // 1 when this app's A2L file is completely generated and 'a2l_name' is valid
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
    uint8_t ecu_epk[XCP_EPK_MAX_LENGTH + 1];                            // EPK of the ECU, constructed from all registered applications EPKs
    uint8_t pad[64 - 8 - 4 - 4 - 4 - 4 - 4 - (XCP_EPK_MAX_LENGTH + 1)]; // pad control area to exactly 64 bytes
    // --- Per-process application list ---
    tApp app_list[SHM_MAX_APP_COUNT]; // 512 * 8 = 4096 bytes
} tShmHeader;
static_assert(sizeof(tShmHeader) % 64 == 0, "sizeof tShmHeader must be a multiple of 64 bytes");

#define SHM_INVALID_APP_ID 0xFF

uint8_t XcpShmGetAppId(void);              // Get this application process's id
const char *XcpShmGetEcuProjectName(void); // Get the project name of the ECU
const char *XcpShmGetEcuEpk(void);         // Get the EPK of the ECU, constructed from all registered applications EPKs

bool XcpShmIsXcpServer(void); // true when this app process is the XCP server
bool XcpShmIsLeader(void);    // true when this app process created the shared memory region
bool XcpShmIsFollower(void);  // true when this app process is a follower attached to a leader

tXcpData *XcpShmAttachOrCreate(bool *out_is_leader); // Attach to an existing shared memory region created by another process or create a new one
void XcpShmUnlink(void); // Unlink shared memory, so no new processes can join, but keep the existing mapping valid for existing users until they exit and unmap themselves

bool XcpShmIsA2lFinalizeRequested(void); // true when a global this app process  has set the finalize flag
void XcpShmRequestA2lFinalize(void);     // Leader: signals all followers to finalize their A2L file now

// Follower background thread: prove this process is still alive
int XcpShmCollectA2lFiles(uint32_t timeout_ms, const char *filenames[], int max_count); // Leader: wait and collect follower partial A2L filenames

uint8_t XcpShmGetAppCount(void);                     // Get the number of registered applications in SHM mode
uint8_t XcpShmGetActiveAppCount(void);               // Get the number of registered and active applications in SHM mode
uint8_t XcpShmGetServer(void);                       // Get the app slot id which is the server
uint8_t XcpShmGetLeader(void);                       // Get the app slot id which is the leader
const char *XcpShmGetAppProjectName(uint8_t app_id); // Get project name of an app slot by app_id index
const char *XcpShmGetAppEpk(uint8_t app_id);         // Get EPK of an app slot by app_id index
uint8_t XcpShmGetInitMode(uint8_t app_id);           // Get the XCP init mode of an app slot by app_id index

// Register this process in the SHM application list; returns allocated application id (slot index) or -1 on error
int16_t XcpShmRegisterApp(const char *name, const char *epk, uint32_t pid, uint8_t xcp_init_mode, bool is_leader, bool is_server);
void XcpShmShutdownApp(uint8_t app_id);

void XcpShmSetA2lFinalized(uint8_t app_id, const char *a2l_name); // Set A2L finalized flag and A2L filename for an app slot by app_id index, used by the leader when loading the
                                                                  // BIN file and pre-registering apps before they are started
bool XcpShmIsA2lFinalized(uint8_t app_id);                        // true when this app process has finalized its A2L file and set its a2l_finalized flag in the app list

void XcpShmIncrementAliveCounter(void); // Called from SHM background thread for XCP server receive thread to prove the application is still alive
void XcpShmCheckAliveCounters(void);    // Called from the XCP server every second to check for stale applications

void XcpShmDebugPrint(void); // Print the status and information in tXcpData, for debugging purposes.

#endif // SHM_MODE

#ifdef __cplusplus
} // extern "C"
#endif
