#pragma once
#define __XCPLIB_HPP__

/*----------------------------------------------------------------------------
| File:
|   xcplib.hpp - Public xcplib C++ API
|
| Description:
|   C++ header file for the XCPlite library xcplib application programming interface
|   Generic C++ RAII wrapper for calibration parameter segments
|   Provides automatic lock/unlock management using a guard pattern
|
| Copyright (c) Vector Informatik GmbH. All rights reserved.
| See LICENSE file in the project root for details.
|
 ----------------------------------------------------------------------------*/

#include <mutex> // for std::once_flag, std::call_once

#include <xcplib.h>

namespace xcplib {

// xcplib configuration parameters
extern "C" {
extern uint8_t XCP_ADDR_MODE_SEG;
}

// =============================================================================
// RAII wrapper for structs with calibration parameters
// =============================================================================

/// Generic RAII wrapper for structs with calibration parameters
/// Template parameter T must be the calibration parameter struct type
template <typename T> class CalSeg {
  private:
    tXcpCalSegIndex segment_index_;

  public:
    /// Constructor - creates the calibration segment struct wrapper
    /// @param name Name of the calibration segment
    /// @param default_params Default parameter values (reference page)
    CalSeg(const char *name, const T *default_params) {
        segment_index_ = XcpCreateCalSeg(name, default_params, sizeof(T));
        assert(segment_index_ != XCP_UNDEFINED_CALSEG); // Ensure the calibration segment was created successfully
    }

    /// Get the segment index (for direct XCP or A2L API calls if needed)
    tXcpCalSegIndex getIndex() const { return segment_index_; }

    /// RAII guard class for automatic lock/unlock
    class CalSegGuard {
      private:
        tXcpCalSegIndex segment_index_;
        const T *params_ptr_;

      public:
        /// Constructor - locks the calibration segment
        explicit CalSegGuard(tXcpCalSegIndex segment_index) : segment_index_(segment_index) {
            params_ptr_ = reinterpret_cast<const T *>(XcpLockCalSeg(segment_index_));
            assert(params_ptr_ != nullptr);
        }

        /// Destructor - unlocks the calibration segment
        ~CalSegGuard() { XcpUnlockCalSeg(segment_index_); }

        /// Access the locked parameters via pointer
        const T *operator->() const { return params_ptr_; }

        /// Access the locked parameters via reference
        const T &operator*() const { return *params_ptr_; }

        /// Get pointer to the locked parameters
        const T *get() const { return params_ptr_; }
    };

    /// Create a guard that automatically locks and unlocks the calibration segment
    CalSegGuard lock() const { return CalSegGuard(segment_index_); }

    /// Create the A2L instance description for this calibration segment
    /// Thread safe
    /// @param type_name The name of the type as it should appear in the A2L file
    /// @param comment Description for the A2L file
    void CreateA2lTypedefInstance(const char *type_name, const char *comment) {
        A2lLock();
        A2lSetSegmentAddrMode__i(segment_index_, NULL);
        // This requires segment relative addressing mode, if XCP_ADDR_EXT_SEG != 0 it will not work with CANape
        assert(XCP_ADDR_MODE_SEG == 0);
        A2lCreateInstance_(XcpGetCalSegName(segment_index_), type_name, 0, NULL, comment);
        A2lUnlock();
    }
};

/// Convenience function to create calibration segment wrappers
/// Usage: auto calseg = xcp::CreateCalSeg("Parameters", default_parameters);
template <typename T> CalSeg<T> CreateCalSeg(const char *name, const T *default_params) { return CalSeg<T>(name, default_params); }

} // namespace xcplib

// =============================================================================
// Convenience macros and variadic templates
// For XCP DAQ event creation, measurement registration and triggering
// =============================================================================

// Portable always inline attribute for C++
// Critical for functions that use xcp_get_frame_addr() to ensure they capture the caller's stack frame
#if defined(_MSC_VER)
#define XCPLIB_ALWAYS_INLINE __forceinline
#elif defined(__GNUC__) || defined(__clang__)
#define XCPLIB_ALWAYS_INLINE __attribute__((always_inline)) inline
#else
#define XCPLIB_ALWAYS_INLINE inline
#warning "XCPLIB_ALWAYS_INLINE may not guarantee inlining on this compiler - stack frame addresses may be incorrect"
#endif

// Helper macros for creating measurement info objects, variable name stringification and address capture
#define A2L_MEAS(var, comment) xcplib::MeasurementInfo(#var, &(var), var, comment)
#define A2L_MEAS_PHYS(var, comment, unit, min, max) xcplib::MeasurementInfo(#var, &(var), var, comment, unit, min, max)

/// Trigger an event, create the event once and register global and local measurement variables once
/// Supports absolute, stack and relative addressing mode measurements
#define DaqEventVar(event_name, ...) xcplib::DaqEventTemplate(#event_name, __VA_ARGS__)

/// Trigger an event, create the event once and register global, local and relative addressing mode measurement variables once
/// Supports absolute, stack and relative addressing mode measurements
#define DaqEventExtVar(event_name, base, ...) xcplib::DaqEventExtTemplate(#event_name, base, __VA_ARGS__)

namespace xcplib {

// Helper struct to hold measurement information
template <typename T> struct MeasurementInfo {
    const char *name;
    const T *addr;
    const T &value;
    const char *comment;
    const char *unit;
    double min;
    double max;

    // Constructor for basic measurement: (var, comment)
    constexpr MeasurementInfo(const char *n, const T *a, const T &v, const char *c) : name(n), addr(a), value(v), comment(c), unit(nullptr), min(0.0), max(0.0) {}

    // Constructor for physical measurement: (var, comment, unit, min, max)
    constexpr MeasurementInfo(const char *n, const T *a, const T &v, const char *c, const char *u, double mn, double mx)
        : name(n), addr(a), value(v), comment(c), unit(u), min(mn), max(mx) {}
};

// Helper to register a single measurement
template <typename T> XCPLIB_ALWAYS_INLINE void registerMeasurement(const MeasurementInfo<T> &info) {
    A2lCreateMeasurement_(nullptr, info.name, A2lTypeTraits::GetTypeIdFromExpr(info.value), (const void *)info.addr, info.unit, info.min, info.max, info.comment);
}

// @@@@ TODO Add markers
// Create event: evt__##event_name
// Trigger event: trg__CASD__##event_name

// Main template functions for event handling
template <typename... Measurements> XCPLIB_ALWAYS_INLINE void DaqEventExtTemplate(const char *event_name, const void *base, Measurements &&...measurements) {
    if (XcpIsActivated()) {
        static tXcpEventId event_id = XCP_UNDEFINED_EVENT_ID;
        static std::once_flag once_flag;
        const uint8_t *frame_addr = (const uint8_t *)xcp_get_frame_addr(); // Capture caller's frame address before lambda
        std::call_once(once_flag, [&]() {
            event_id = XcpCreateEvent(event_name, 0, 0);
            assert(event_id != XCP_UNDEFINED_EVENT_ID);
            A2lLock();
            A2lSetAutoAddrMode__i(event_id, frame_addr, (const uint8_t *)base);
            (registerMeasurement(measurements), ...);
            A2lUnlock();
        });
        XcpEventExt_Var(event_id, 2, frame_addr, (const uint8_t *)base);
    }
}

template <typename... Measurements> XCPLIB_ALWAYS_INLINE void DaqEventTemplate(const char *event_name, Measurements &&...measurements) {
    if (XcpIsActivated()) {
        static tXcpEventId event_id = XCP_UNDEFINED_EVENT_ID;
        static std::once_flag once_flag;
        const uint8_t *frame_addr = (const uint8_t *)xcp_get_frame_addr(); // Capture caller's frame address before lambda
        std::call_once(once_flag, [&]() {
            event_id = XcpCreateEvent(event_name, 0, 0);
            assert(event_id != XCP_UNDEFINED_EVENT_ID);
            A2lLock();
            A2lSetAutoAddrMode__i(event_id, frame_addr, NULL);
            (registerMeasurement(measurements), ...);
            A2lUnlock();
        });
        XcpEventExt_Var(event_id, 1, frame_addr);
    }
}

} // namespace xcplib
