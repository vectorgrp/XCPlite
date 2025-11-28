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

#include "xcplib.h"

// Options for variadic measurement/event macros/templates
// Note: The template versions do not provide static markers for event creation and triggering
// #define OPTION_USE_VARIADIC_MACROS
#define OPTION_USE_VARIADIC_TEMPLATES

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

#ifdef OPTION_USE_VARIADIC_TEMPLATES

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

// Convenience macros that stringify the event name and capture the caller's stack frame
#define DaqEventVar(event_name, ...) xcplib::DaqEventTemplate(#event_name, __VA_ARGS__)
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
// Trigger event: trg__AASD__##event_name

// Main template functions for event handling
template <typename... Measurements> XCPLIB_ALWAYS_INLINE void DaqEventExtTemplate(const char *event_name, const void *base, Measurements &&...measurements) {
    if (XcpIsActivated()) {
        static thread_local tXcpEventId event_id = XCP_UNDEFINED_EVENT_ID;
        if (event_id == XCP_UNDEFINED_EVENT_ID) {
            event_id = XcpCreateEvent(event_name, 0, 0);
            if (A2lOnceLock()) {
                A2lSetAutoAddrMode__i(event_id, (const uint8_t *)xcp_get_frame_addr(), (const uint8_t *)base);
                (registerMeasurement(measurements), ...);
            }
        }
        XcpEventExt_Var(event_id, 2, (const uint8_t *)xcp_get_frame_addr(), (const uint8_t *)base);
    }
}

template <typename... Measurements> XCPLIB_ALWAYS_INLINE void DaqEventTemplate(const char *event_name, Measurements &&...measurements) {
    if (XcpIsActivated()) {
        static thread_local tXcpEventId event_id = XCP_UNDEFINED_EVENT_ID;
        if (event_id == XCP_UNDEFINED_EVENT_ID) {
            event_id = XcpCreateEvent(event_name, 0, 0);
            if (A2lOnceLock()) {
                A2lSetAutoAddrMode__i(event_id, (const uint8_t *)xcp_get_frame_addr(), NULL);
                (registerMeasurement(measurements), ...);
            }
        }
        XcpEventExt_Var(event_id, 1, (const uint8_t *)xcp_get_frame_addr());
    }
}

} // namespace xcplib

#endif

#ifdef OPTION_USE_VARIADIC_MACROS

#define A2L_MEAS_PHYS
#define A2L_MEAS

// Macro to count arguments in a tuple
#define A2L_TUPLE_SIZE_(...) A2L_TUPLE_SIZE_IMPL_(__VA_ARGS__, 5, 4, 3, 2, 1, 0)
#define A2L_TUPLE_SIZE_IMPL_(_1, _2, _3, _4, _5, N, ...) N

// Dispatch macro based on tuple size
#define A2L_UNPACK_AND_REG_DISPATCH_(tuple) A2L_UNPACK_AND_REG_DISPATCH_IMPL_ tuple
#define A2L_UNPACK_AND_REG_DISPATCH_IMPL_(...) A2L_UNPACK_AND_REG_SELECT_(A2L_TUPLE_SIZE_(__VA_ARGS__))(__VA_ARGS__)

// Select the appropriate registration macro based on argument count
#define A2L_UNPACK_AND_REG_SELECT_(N) A2L_UNPACK_AND_REG_SELECT_IMPL_(N)
#define A2L_UNPACK_AND_REG_SELECT_IMPL_(N) A2L_UNPACK_AND_REG_##N##_

// Measurement: (var, comment)
#define A2L_UNPACK_AND_REG_2_(var, comment) A2lCreateMeasurement_(NULL, #var, A2lGetTypeId(var), &(var), NULL, 0.0, 0.0, comment);

// Physical measurement: (var, comment, unit_or_conversion, min, max)
#define A2L_UNPACK_AND_REG_5_(var, comment, unit_or_conversion, min, max) A2lCreateMeasurement_(NULL, #var, A2lGetTypeId(var), &(var), unit_or_conversion, min, max, comment);

// Main unpacking macro - dispatches to the right version
#define A2L_UNPACK_AND_REG_(...) A2L_UNPACK_AND_REG_DISPATCH_((__VA_ARGS__))

// Macro helpers for FOR_EACH pattern
// These expand the variadic arguments and apply a macro to each one
#define XCPLIB_FOR_EACH_MEAS_(macro, ...) XCPLIB_FOR_EACH_MEAS_IMPL_(macro, __VA_ARGS__)

// Implementation helper - handles up to 16 measurements
// Each XCPLIB_APPLY_ expands to macro(args) where args is (var, comment)
#define XCPLIB_FOR_EACH_MEAS_IMPL_(m, ...)                                                                                                                                         \
    XCPLIB_GET_MACRO_(__VA_ARGS__, XCPLIB_FE_16_, XCPLIB_FE_15_, XCPLIB_FE_14_, XCPLIB_FE_13_, XCPLIB_FE_12_, XCPLIB_FE_11_, XCPLIB_FE_10_, XCPLIB_FE_9_, XCPLIB_FE_8_,            \
                      XCPLIB_FE_7_, XCPLIB_FE_6_, XCPLIB_FE_5_, XCPLIB_FE_4_, XCPLIB_FE_3_, XCPLIB_FE_2_, XCPLIB_FE_1_, XCPLIB_FE_0_)(m, __VA_ARGS__)

// Selector macro - picks the right expander based on argument count
#define XCPLIB_GET_MACRO_(_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16, NAME, ...) NAME

// Expander macros for different argument counts
#define XCPLIB_FE_0_(m)
#define XCPLIB_FE_1_(m, x1) XCPLIB_APPLY_(m, x1)
#define XCPLIB_FE_2_(m, x1, x2) XCPLIB_APPLY_(m, x1) XCPLIB_APPLY_(m, x2)
#define XCPLIB_FE_3_(m, x1, x2, x3) XCPLIB_APPLY_(m, x1) XCPLIB_APPLY_(m, x2) XCPLIB_APPLY_(m, x3)
#define XCPLIB_FE_4_(m, x1, x2, x3, x4) XCPLIB_APPLY_(m, x1) XCPLIB_APPLY_(m, x2) XCPLIB_APPLY_(m, x3) XCPLIB_APPLY_(m, x4)
#define XCPLIB_FE_5_(m, x1, x2, x3, x4, x5) XCPLIB_APPLY_(m, x1) XCPLIB_APPLY_(m, x2) XCPLIB_APPLY_(m, x3) XCPLIB_APPLY_(m, x4) XCPLIB_APPLY_(m, x5)
#define XCPLIB_FE_6_(m, x1, x2, x3, x4, x5, x6) XCPLIB_APPLY_(m, x1) XCPLIB_APPLY_(m, x2) XCPLIB_APPLY_(m, x3) XCPLIB_APPLY_(m, x4) XCPLIB_APPLY_(m, x5) XCPLIB_APPLY_(m, x6)
#define XCPLIB_FE_7_(m, x1, x2, x3, x4, x5, x6, x7)                                                                                                                                \
    XCPLIB_APPLY_(m, x1) XCPLIB_APPLY_(m, x2) XCPLIB_APPLY_(m, x3) XCPLIB_APPLY_(m, x4) XCPLIB_APPLY_(m, x5) XCPLIB_APPLY_(m, x6) XCPLIB_APPLY_(m, x7)
#define XCPLIB_FE_8_(m, x1, x2, x3, x4, x5, x6, x7, x8)                                                                                                                            \
    XCPLIB_APPLY_(m, x1) XCPLIB_APPLY_(m, x2) XCPLIB_APPLY_(m, x3) XCPLIB_APPLY_(m, x4) XCPLIB_APPLY_(m, x5) XCPLIB_APPLY_(m, x6) XCPLIB_APPLY_(m, x7) XCPLIB_APPLY_(m, x8)
#define XCPLIB_FE_9_(m, x1, x2, x3, x4, x5, x6, x7, x8, x9)                                                                                                                        \
    XCPLIB_APPLY_(m, x1)                                                                                                                                                           \
    XCPLIB_APPLY_(m, x2) XCPLIB_APPLY_(m, x3) XCPLIB_APPLY_(m, x4) XCPLIB_APPLY_(m, x5) XCPLIB_APPLY_(m, x6) XCPLIB_APPLY_(m, x7) XCPLIB_APPLY_(m, x8) XCPLIB_APPLY_(m, x9)
#define XCPLIB_FE_10_(m, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10)                                                                                                                  \
    XCPLIB_APPLY_(m, x1)                                                                                                                                                           \
    XCPLIB_APPLY_(m, x2)                                                                                                                                                           \
    XCPLIB_APPLY_(m, x3) XCPLIB_APPLY_(m, x4) XCPLIB_APPLY_(m, x5) XCPLIB_APPLY_(m, x6) XCPLIB_APPLY_(m, x7) XCPLIB_APPLY_(m, x8) XCPLIB_APPLY_(m, x9) XCPLIB_APPLY_(m, x10)
#define XCPLIB_FE_11_(m, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11)                                                                                                             \
    XCPLIB_APPLY_(m, x1)                                                                                                                                                           \
    XCPLIB_APPLY_(m, x2)                                                                                                                                                           \
    XCPLIB_APPLY_(m, x3)                                                                                                                                                           \
    XCPLIB_APPLY_(m, x4) XCPLIB_APPLY_(m, x5) XCPLIB_APPLY_(m, x6) XCPLIB_APPLY_(m, x7) XCPLIB_APPLY_(m, x8) XCPLIB_APPLY_(m, x9) XCPLIB_APPLY_(m, x10) XCPLIB_APPLY_(m, x11)
#define XCPLIB_FE_12_(m, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12)                                                                                                        \
    XCPLIB_APPLY_(m, x1)                                                                                                                                                           \
    XCPLIB_APPLY_(m, x2)                                                                                                                                                           \
    XCPLIB_APPLY_(m, x3)                                                                                                                                                           \
    XCPLIB_APPLY_(m, x4)                                                                                                                                                           \
    XCPLIB_APPLY_(m, x5) XCPLIB_APPLY_(m, x6) XCPLIB_APPLY_(m, x7) XCPLIB_APPLY_(m, x8) XCPLIB_APPLY_(m, x9) XCPLIB_APPLY_(m, x10) XCPLIB_APPLY_(m, x11) XCPLIB_APPLY_(m, x12)
#define XCPLIB_FE_13_(m, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13)                                                                                                   \
    XCPLIB_APPLY_(m, x1)                                                                                                                                                           \
    XCPLIB_APPLY_(m, x2)                                                                                                                                                           \
    XCPLIB_APPLY_(m, x3)                                                                                                                                                           \
    XCPLIB_APPLY_(m, x4)                                                                                                                                                           \
    XCPLIB_APPLY_(m, x5)                                                                                                                                                           \
    XCPLIB_APPLY_(m, x6) XCPLIB_APPLY_(m, x7) XCPLIB_APPLY_(m, x8) XCPLIB_APPLY_(m, x9) XCPLIB_APPLY_(m, x10) XCPLIB_APPLY_(m, x11) XCPLIB_APPLY_(m, x12) XCPLIB_APPLY_(m, x13)
#define XCPLIB_FE_14_(m, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14)                                                                                              \
    XCPLIB_APPLY_(m, x1)                                                                                                                                                           \
    XCPLIB_APPLY_(m, x2)                                                                                                                                                           \
    XCPLIB_APPLY_(m, x3)                                                                                                                                                           \
    XCPLIB_APPLY_(m, x4)                                                                                                                                                           \
    XCPLIB_APPLY_(m, x5)                                                                                                                                                           \
    XCPLIB_APPLY_(m, x6)                                                                                                                                                           \
    XCPLIB_APPLY_(m, x7) XCPLIB_APPLY_(m, x8) XCPLIB_APPLY_(m, x9) XCPLIB_APPLY_(m, x10) XCPLIB_APPLY_(m, x11) XCPLIB_APPLY_(m, x12) XCPLIB_APPLY_(m, x13) XCPLIB_APPLY_(m, x14)
#define XCPLIB_FE_15_(m, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15)                                                                                         \
    XCPLIB_APPLY_(m, x1)                                                                                                                                                           \
    XCPLIB_APPLY_(m, x2)                                                                                                                                                           \
    XCPLIB_APPLY_(m, x3)                                                                                                                                                           \
    XCPLIB_APPLY_(m, x4)                                                                                                                                                           \
    XCPLIB_APPLY_(m, x5)                                                                                                                                                           \
    XCPLIB_APPLY_(m, x6)                                                                                                                                                           \
    XCPLIB_APPLY_(m, x7)                                                                                                                                                           \
    XCPLIB_APPLY_(m, x8) XCPLIB_APPLY_(m, x9) XCPLIB_APPLY_(m, x10) XCPLIB_APPLY_(m, x11) XCPLIB_APPLY_(m, x12) XCPLIB_APPLY_(m, x13) XCPLIB_APPLY_(m, x14) XCPLIB_APPLY_(m, x15)
#define XCPLIB_FE_16_(m, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16)                                                                                    \
    XCPLIB_APPLY_(m, x1)                                                                                                                                                           \
    XCPLIB_APPLY_(m, x2)                                                                                                                                                           \
    XCPLIB_APPLY_(m, x3)                                                                                                                                                           \
    XCPLIB_APPLY_(m, x4)                                                                                                                                                           \
    XCPLIB_APPLY_(m, x5)                                                                                                                                                           \
    XCPLIB_APPLY_(m, x6)                                                                                                                                                           \
    XCPLIB_APPLY_(m, x7)                                                                                                                                                           \
    XCPLIB_APPLY_(m, x8)                                                                                                                                                           \
    XCPLIB_APPLY_(m, x9) XCPLIB_APPLY_(m, x10) XCPLIB_APPLY_(m, x11) XCPLIB_APPLY_(m, x12) XCPLIB_APPLY_(m, x13) XCPLIB_APPLY_(m, x14) XCPLIB_APPLY_(m, x15) XCPLIB_APPLY_(m, x16)

// Apply macro to unpacked tuple arguments
// Strips the outer parentheses from (var, comment) and passes to macro as two separate arguments
#define XCPLIB_APPLY_(m, args) m args

// Create the daq event (just for unique naming scheme)
#define XcpCreateDaqEvent DaqCreateEvent

// Register measurements once and trigger an already created event with stack addressing mode
#define XcpTriggerDaqEvent(event_name, ...)                                                                                                                                        \
    do {                                                                                                                                                                           \
        if (A2lOnceLock()) {                                                                                                                                                       \
            A2lSetStackAddrMode__s(#event_name, xcp_get_frame_addr());                                                                                                             \
            XCPLIB_FOR_EACH_MEAS_(A2L_UNPACK_AND_REG_, __VA_ARGS__)                                                                                                                \
        }                                                                                                                                                                          \
        DaqTriggerEvent(event_name);                                                                                                                                               \
    } while (0)

// Register measurements once and trigger an already created event with stack or relative addressing mode
#define XcpTriggerDaqEventExt(event_name, base, ...)                                                                                                                               \
    do {                                                                                                                                                                           \
        if (A2lOnceLock()) {                                                                                                                                                       \
            A2lSetAutoAddrMode__s(#event_name, xcp_get_frame_addr(), (const uint8_t *)base);                                                                                       \
            XCPLIB_FOR_EACH_MEAS_(A2L_UNPACK_AND_REG_, __VA_ARGS__)                                                                                                                \
        }                                                                                                                                                                          \
        DaqTriggerEventExt(event_name, base);                                                                                                                                      \
    } while (0)

// =============================================================================
// Variadic DAQ macros which create, register variables and trigger events in one call

// Create event, register stack measurements once and trigger an event with stack addressing mode
#define DaqEventVar(event_name, ...)                                                                                                                                               \
    do {                                                                                                                                                                           \
        if (XcpIsActivated()) {                                                                                                                                                    \
            static THREAD_LOCAL tXcpEventId evt__##event_name = XCP_UNDEFINED_EVENT_ID;                                                                                            \
            if (evt__##event_name == XCP_UNDEFINED_EVENT_ID) {                                                                                                                     \
                evt__##event_name = XcpCreateEvent(#event_name, 0, 0);                                                                                                             \
                if (A2lOnce()) {                                                                                                                                                   \
                    A2lSetStackAddrMode__s(#event_name, xcp_get_frame_addr());                                                                                                     \
                    XCPLIB_FOR_EACH_MEAS_(A2L_UNPACK_AND_REG_, __VA_ARGS__)                                                                                                        \
                }                                                                                                                                                                  \
            }                                                                                                                                                                      \
            static THREAD_LOCAL tXcpEventId trg__AAS__##event_name = evt__##event_name;                                                                                            \
            XcpEventExt_Var(trg__AAS__##event_name, 1, xcp_get_frame_addr());                                                                                                      \
        }                                                                                                                                                                          \
    } while (0)

// Create event, register once and trigger an event with stack or relative addressing mode
#define XcpDaqEventExt(event_name, base, ...)                                                                                                                                      \
    do {                                                                                                                                                                           \
        if (XcpIsActivated()) {                                                                                                                                                    \
            static THREAD_LOCAL tXcpEventId evt__##event_name = XCP_UNDEFINED_EVENT_ID;                                                                                            \
            if (evt__##event_name == XCP_UNDEFINED_EVENT_ID) {                                                                                                                     \
                evt__##event_name = XcpCreateEvent(#event_name, 0, 0);                                                                                                             \
                if (A2lOnce()) {                                                                                                                                                   \
                    A2lSetAutoAddrMode__s(#event_name, xcp_get_frame_addr(), (const uint8_t *)base);                                                                               \
                    XCPLIB_FOR_EACH_MEAS_(A2L_UNPACK_AND_REG_, __VA_ARGS__)                                                                                                        \
                }                                                                                                                                                                  \
            }                                                                                                                                                                      \
            static THREAD_LOCAL tXcpEventId trg__AASD__##event_name = evt__##event_name;                                                                                           \
            XcpEventExt_Var(trg__AASD__##event_name, 2, xcp_get_frame_addr(), (const uint8_t *)base);                                                                              \
        }                                                                                                                                                                          \
    } while (0)

#endif
