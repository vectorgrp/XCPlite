// SPDX-FileCopyrightText: 2024 Vector Informatik GmbH
//
// SPDX-License-Identifier: MIT

#pragma once

#include <a2l.hpp>
#include <xcplib.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>

// XCP clock integration for SIL Kit.
// We use the SIL Kit virtual simulation time:
//
// ApplXcpGetClock64() is the single clock source used for both:
//   - DAQ sample timestamps (called inside DaqTriggerEventExt)
//   - GET_DAQ_CLOCK responses (called when CANape requests time correlation)
//     This value is at most one simulation step old
//
// In async mode (XcpUpdateSimTime never called), g_simTimeNs stays 0 and we fall back to
// steady_clock so that XCP is at least functional.

namespace XcpSilKitClock {

static std::atomic<uint64_t> g_simTimeNs{0xFFFFFFFFFFFFFFFF};

static uint64_t getClockCallback() {
    uint64_t t = g_simTimeNs.load(std::memory_order_relaxed);

    // Async mode fallback: use wall clock
    if (t != 0xFFFFFFFFFFFFFFFF)
        return t;
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}

} // namespace XcpSilKitClock

// Update the SIL Kit virtual simulation time used for the next DAQ trigger timestamp.
// Called immediately before DaqTriggerEventExt() in DoWorkSync(now) and in DataMessageEvent callbacks.
inline void XcpUpdateSimTime(uint64_t time_ns) {

    if (!XcpIsInShmMode())
        return;

    uint64_t t = XcpSilKitClock::g_simTimeNs.load(std::memory_order_relaxed);
    if (t != 0xFFFFFFFFFFFFFFFF) {
        if (t == time_ns) {
            return;
        }
        if (t > time_ns) {
            printf("Declining sim time: %lu ns\n", static_cast<unsigned long>(time_ns));
            return;
        }
    }

    // printf("Updating sim time: %lu ns\n", static_cast<unsigned long>(time_ns));
    XcpSilKitClock::g_simTimeNs.store(time_ns, std::memory_order_relaxed);
}

//-----------------------------------------------------------------------------------------------------
// XCP parameters

constexpr bool OPTION_USE_TCP = false;                 // TCP or UDP
constexpr uint8_t OPTION_SERVER_ADDR[] = {0, 0, 0, 0}; // Bind addr, 0.0.0.0 = ANY
constexpr uint16_t OPTION_SERVER_PORT = 5555;          // Port
constexpr uint16_t OPTION_QUEUE_SIZE = (1024 * 32);    // Size of the queue in bytes, should be large enough to cover at least 10ms of expected traffic
constexpr int OPTION_LOG_LEVEL = 5;                    // Log level, 0 = no log, 1 = error, 2 = warning, 3 = info, 4 = debug

// A2L generation mode:
constexpr uint8_t OPTION_A2L_MODE = (A2L_MODE_WRITE_ONCE | A2L_MODE_FINALIZE_ON_CONNECT | A2L_MODE_AUTO_GROUPS);

// Initialize the XCPlite ETH server and A2L writer for a SIL Kit participant.
// name : participant name, used as XCP slave name and A2L file base name
// epk  : software version identifier string (e.g. "1.0")
// port : TCP port for the XCP server
// mode:
//   XCP_MODE_LOCAL     : Disable multi application mode, create an XCP server on the specified port
//   XCP_MODE_SHM       : Enable multi application mode, do not attempt to become the XCP server
//   XCP_MODE_SHM_AUTO  :  Enable multi application mode, the first participant creates an XCP server on the specified port, the others switch to shared memory communication
//   XCP_MODE_SERVER    : Enable multi application mode, this application creates an XCP server on the specified port
// XCP_MODE_SHM_XXX requires the XCP library to be compiled in multi application mode (OPTION_SHM_MODE)
inline void XcpServerInit(const std::string &name, const std::string &epk, uint16_t port, uint8_t mode) {

    XcpSetLogLevel(OPTION_LOG_LEVEL);

    // XcpInit(name.c_str(), epk.c_str(),  XCP_MODE_LOCAL );
    // Xcp will automatically fall back to normal mode, when libxcplite is not compiled with OPTION_SHM_MODE
    // In this case the ports must be unique per participant and there is one XCP server per participant

    mode |= (XCP_MODE_PERSISTENCE);
    XcpInit(name.c_str(), epk.c_str(), mode);
    ApplXcpRegisterGetClockCallback(XcpSilKitClock::getClockCallback);

    if (XcpGetInitMode() & XCP_MODE_SHM) {
        printf("XCP in SHM mode, using standard port 5555\n");
        port = 5555;
    } else {
        printf("XCP in non-SHM mode, using port %u for XCP server\n", port);
    }
    uint8_t addr[4] = {0, 0, 0, 0};
    XcpEthServerInit(addr, port, OPTION_USE_TCP, OPTION_QUEUE_SIZE);

    A2lInit(addr, port, OPTION_USE_TCP, OPTION_A2L_MODE);
}

inline void XcpServerShutdown() {

    XcpDisconnect();        // Force disconnect the XCP client
    A2lFinalize();          // Finalize A2L generation, if not done yet
    XcpFreeze();            // Server saves current calibration segments to binary persistence file
    XcpEthServerShutdown(); // Stop the XCP server
}
