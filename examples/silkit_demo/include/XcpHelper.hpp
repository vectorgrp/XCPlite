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
//
// ApplXcpGetClock64() is the single clock source used for both:
//   - DAQ sample timestamps (called inside DaqTriggerEventExt)
//   - GET_DAQ_CLOCK responses (called when CANape requests time correlation)
//
// Both go through the same registered callback, so only one clock domain can be provided.
// We use the SIL Kit virtual simulation time:
//   - g_simTimeNs is updated via XcpUpdateSimTime() just before each DaqTriggerEventExt().
//     DAQ samples are then timestamped with the simulation timeline.
//   - GET_DAQ_CLOCK also returns g_simTimeNs. This value is at most one simulation step old
//     (1ms by default). CANape time correlation works against virtual simulation time, not
//     wall clock — which is intentional: the simulation relationship matters, not wall time.
//
// In async mode (XcpUpdateSimTime never called), g_simTimeNs stays 0 and we fall back to
// steady_clock so that XCP is at least functional.

namespace XcpSilKitClock {

static std::atomic<uint64_t> g_simTimeNs{0};

static uint64_t getClockCallback() {
    uint64_t t = g_simTimeNs.load(std::memory_order_relaxed);
    if (t != 0)
        return t;
    // Async mode fallback: use wall clock
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}

} // namespace XcpSilKitClock

// Update the SIL Kit virtual simulation time used for the next DAQ trigger timestamp.
// Call immediately before DaqTriggerEventExt() in DoWorkSync(now) and in DataMessageEvent callbacks.
inline void XcpUpdateSimTime(std::chrono::nanoseconds now) {

    printf("Updating sim time: %lu ns\n", static_cast<unsigned long>(now.count()));

    XcpSilKitClock::g_simTimeNs.store(static_cast<uint64_t>(now.count()), std::memory_order_relaxed);
}

// Initialize the XCPlite ETH server and A2L writer for a SIL Kit participant.
// name : participant name, used as XCP slave name and A2L file base name
// epk  : software version identifier string (e.g. "1.0")
// port : TCP port for the XCP server and A2L upload
inline void XcpServerInit(const std::string &name, const std::string &epk, uint16_t port) {

    XcpSetLogLevel(3);

    // XcpInit(name.c_str(), epk.c_str(),  XCP_MODE_LOCAL );
    XcpInit(name.c_str(), epk.c_str(), XCP_MODE_SHM_AUTO);

    ApplXcpRegisterGetClockCallback(XcpSilKitClock::getClockCallback);

    uint8_t addr[4] = {0, 0, 0, 0};
    XcpEthServerInit(addr, port, true, 1024 * 32);

    A2lInit(addr, port, true, A2L_MODE_WRITE_ALWAYS | A2L_MODE_FINALIZE_ON_CONNECT);
}

inline void XcpServerShutdown() {

    XcpDisconnect(); // Force disconnect the XCP client
    A2lFinalize();   // Finalize A2L generation, if not done yet
    // XcpFreeze(); // Save current calibration segments to binary persistence file
    XcpEthServerShutdown(); // Stop the XCP server
}
