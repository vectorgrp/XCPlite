#pragma once
#define __XCPLIB_HPP__

/*----------------------------------------------------------------------------
| File:
|   xcplib.hpp - Public XCPlite/libxcplite C++ API
|
| Description:
|   C++ header file for the XCPlite library XCPlite/libxcplite application programming interface
|
| Copyright (c) Vector Informatik GmbH. All rights reserved.
| See LICENSE file in the project root for details.
|
 ----------------------------------------------------------------------------*/

#include <mutex> // for std::once_flag, std::call_once

#include "xcplib_cfg.h" // for OPTION_xxx, must include the correct configuration override file XCPLIB_CFG_OVERRIDE
#ifndef XCPLITE_CONFIGURATION
#error "XCPLITE_CONFIGURATION must be defined to make sure the correct configuration override file is set"
#endif // XCPLITE_CONFIGURATION
#include <xcplib.h>
#ifdef OPTION_ENABLE_A2L_GENERATOR
#include <a2l.h>
#endif // OPTION_ENABLE_A2L_GENERATOR

// If A2L generation build option is turned off, we do not set segment address mode for the A2L generator
#ifndef OPTION_ENABLE_A2L_GENERATOR
#define A2lSetSegmentAddrMode__i(index, base)
#endif

namespace xcp {

// =============================================================================
// RAII wrappers for structs or values with calibration parameters
// =============================================================================

/// Generic RAII wrapper for structs with calibration parameters
/// Template parameter T must be the calibration parameter struct type
template <typename T> class CalSeg {
  private:
    const T *params_ptr_;
    tXcpCalSegIndex index_;

  public:
    /// Constructor - creates the calibration segment struct wrapper
    /// @param name Name of the calibration segment
    /// @param default_params Default parameter values (reference page)
    CalSeg(const char *name, const T *default_params) {
        if (XcpIsActivated()) {
            index_ = XcpCreateCalSeg(name, default_params, sizeof(T));
            assert(index_ != XCP_UNDEFINED_CALSEG); // Ensure the calibration segment was created successfully
            A2lSetSegmentAddrMode__i(index_, NULL);
        } else {
            params_ptr_ = default_params;
        }
    }

    /// Get the segment index (for direct XCP or A2L API calls if needed)
    tXcpCalSegIndex getIndex() const { return index_; }

    /// RAII guard class for automatic lock/unlock
    class CalSegGuard {
      private:
        const T *params_ptr_;
        tXcpCalSegIndex index_;

      public:
        /// Constructor - locks the calibration segment
        explicit CalSegGuard(tXcpCalSegIndex index, const T *params_ptr) : index_(index), params_ptr_(params_ptr) {
            if (XcpIsActivated()) {
                params_ptr_ = reinterpret_cast<const T *>(XcpLockCalSeg(index_));
            }
        }

        /// Destructor - unlocks the calibration segment
        ~CalSegGuard() {
            if (XcpIsActivated()) {
                XcpUnlockCalSeg(index_);
            }
        }

        /// Access the locked parameters via pointer
        const T *operator->() const { return params_ptr_; }

        /// Access the locked parameters via reference
        const T &operator*() const { return *params_ptr_; }

        /// Get pointer to the locked parameters
        const T *get() const { return params_ptr_; }
    };

    /// Create a guard that automatically locks and unlocks the calibration segment
    CalSegGuard lock() const { return CalSegGuard(index_, params_ptr_); }

#ifdef OPTION_ENABLE_A2L_GENERATOR
    /// Create the A2L instance description for this calibration segment
    /// Thread safe
    /// @param type_name The name of the type as it should appear in the A2L file
    /// @param comment Description for the A2L file
    void CreateA2lTypedefInstance(const char *type_name, const char *comment) {
        if (XcpIsActivated()) {
            A2lLock();
            A2lSetSegmentAddrMode__i(index_, NULL);
            A2lCreateInstance_(XcpGetCalSegName(index_), type_name, 0, NULL, comment);
            A2lUnlock();
        }
    }
#endif
};

/// Non-owning typed wrapper for a section-registered calibration segment.
/// The segment itself is registered by XcpInit() from the xcp_cals descriptor section.
template <typename T> class CalSegRef {
  private:
    tXcpCalSegIndex *indexp_;
    const T *default_params_;

  public:
    constexpr CalSegRef(tXcpCalSegIndex *indexp, const T *default_params) : indexp_(indexp), default_params_(default_params) {}

    /// Get the segment index initialized by XcpInit() section registration.
    tXcpCalSegIndex getIndex() const { return indexp_ != nullptr ? *indexp_ : XCP_UNDEFINED_CALSEG; }

    /// RAII guard class for automatic lock/unlock.
    /// Unlike CalSeg::CalSegGuard, this tolerates index == XCP_UNDEFINED_CALSEG (e.g. when queried before XcpInit()'s
    /// xcp_cals section scan has run - see CalSegRef above): the lock/unlock calls are simply skipped in that case,
    /// and get()/operator->()/operator*() then return the default_params passed in, unlocked.
    class CalSegGuard {
      private:
        tXcpCalSegIndex index_;
        const T *params_ptr_;

      public:
        /// Constructor - locks the calibration segment, unless index is XCP_UNDEFINED_CALSEG
        explicit CalSegGuard(tXcpCalSegIndex index, const T *default_params) : index_(index), params_ptr_(default_params) {
            if (XcpIsActivated() && index_ != XCP_UNDEFINED_CALSEG) {
                params_ptr_ = reinterpret_cast<const T *>(XcpLockCalSeg(index_));
            }
        }

        CalSegGuard(const CalSegGuard &) = delete;
        CalSegGuard &operator=(const CalSegGuard &) = delete;

        /// Move constructor - transfers ownership of the lock, leaving 'other' with no segment to unlock
        CalSegGuard(CalSegGuard &&other) : index_(other.index_), params_ptr_(other.params_ptr_) { other.index_ = XCP_UNDEFINED_CALSEG; }
        CalSegGuard &operator=(CalSegGuard &&) = delete;

        /// Destructor - unlocks the calibration segment, unless index is XCP_UNDEFINED_CALSEG
        ~CalSegGuard() {
            if (XcpIsActivated() && index_ != XCP_UNDEFINED_CALSEG) {
                XcpUnlockCalSeg(index_);
            }
        }

        /// Access the locked parameters via pointer
        const T *operator->() const { return params_ptr_; }

        /// Access the locked parameters via reference
        const T &operator*() const { return *params_ptr_; }

        /// Get pointer to the locked parameters
        const T *get() const { return params_ptr_; }
    };

    CalSegGuard lock() const { return CalSegGuard(getIndex(), default_params_); }

#ifdef OPTION_ENABLE_A2L_GENERATOR
    /// Create the A2L instance description for this calibration segment.
    void CreateA2lTypedefInstance(const char *type_name, const char *comment) const {
        const tXcpCalSegIndex index = getIndex();
        if (XcpIsActivated() && index != XCP_UNDEFINED_CALSEG) {
            A2lLock();
            A2lSetSegmentAddrMode__i(index, NULL);
            A2lCreateInstance_(XcpGetCalSegName(index), type_name, 0, NULL, comment);
            A2lUnlock();
        }
    }
#endif
};

/// Generic RAII wrapper for a single parameter of complex or simple type
template <typename T> class CalBlk {
  private:
    const T *params_ptr_;
    tXcpCalSegIndex index_;

  public:
    /// Constructor - creates the calibration segment struct wrapper
    /// @param name Name of the calibration segment
    /// @param default_params Default parameter values (reference page)
    CalBlk(const char *name, const T *default_params) {
        if (XcpIsActivated()) {
            index_ = XcpCreateCalBlk(name, default_params, sizeof(T));
            assert(index_ != XCP_UNDEFINED_CALSEG); // Ensure the calibration segment was created successfully
            A2lSetSegmentAddrMode__i(index_, NULL);
        } else {
            params_ptr_ = default_params;
        }
    }

    /// Get the segment index (for direct XCP or A2L API calls if needed)
    tXcpCalSegIndex getIndex() const { return index_; }

    /// RAII guard class for automatic lock/unlock
    class CalSegGuard {
      private:
        const T *params_ptr_;
        tXcpCalSegIndex calseg_index_;

      public:
        /// Constructor - locks the calibration segment
        explicit CalSegGuard(tXcpCalSegIndex calseg_index, const T *params_ptr) : calseg_index_(calseg_index), params_ptr_(params_ptr) {
            if (XcpIsActivated()) {
                params_ptr_ = reinterpret_cast<const T *>(XcpLockCalSeg(calseg_index_));
            }
        }

        /// Destructor - unlocks the calibration segment
        ~CalSegGuard() {
            if (XcpIsActivated()) {
                XcpUnlockCalSeg(calseg_index_);
            }
        }

        /// Access the locked parameters via pointer
        const T *operator->() const { return params_ptr_; }

        /// Access the locked parameters via reference
        const T &operator*() const { return *params_ptr_; }

        /// Get pointer to the locked parameters
        const T *get() const { return params_ptr_; }
    };

    /// Create a guard that automatically locks and unlocks the calibration segment
    CalSegGuard lock() const { return CalSegGuard(index_, params_ptr_); }

#ifdef OPTION_ENABLE_A2L_GENERATOR
    /// Create the A2L instance description for this calibration segment
    /// Thread safe
    /// @param type_name The name of the type as it should appear in the A2L file
    /// @param comment Description for the A2L file
    void CreateA2lTypedefInstance(const char *type_name, const char *comment) {
        if (XcpIsActivated()) {
            A2lLock();
            A2lSetSegmentAddrMode__i(index_, NULL);
            A2lCreateInstance_(XcpGetCalSegName(index_), type_name, 0, NULL, comment);
            A2lUnlock();
        }
    }
#endif
};

/// Convenience macro to create a calibration segment with automatic name stringification
/// Usage: auto calseg = CalSegCreate(initial_value);
/// Unlike the C macro of the same name (xcplib.h), this is an expression, not a statement: it always creates the
/// segment immediately, by calling XcpCreateCalSeg in xcp::CalSeg<T>'s constructor, and does not register an
/// xcp_cals section descriptor at all - so it cannot be pre-registered by XcpInit()'s section scan and must itself
/// run before the segment is first used. For the lazy, section-based equivalent (matching the C CalSegDecl), see
/// CalSegDecl/CalSegDeclRef below.
#define CalSegCreate(value) xcp::CalSeg<decltype(value)>(#value, &value)

/// Declare a section-registered global calibration segment and create a typed C++ handle.
/// Usage: CalSegDeclRef(parameters, parameters_calseg); auto parameters = parameters_calseg.lock();
/// Like the C macro CalSegDecl (xcplib.h), this only registers an xcp_cals section descriptor: the segment is not
/// created until XcpInit() pre-registers it via its section scan, so 'handle' is only valid for locking after
/// XcpInit() has run. On top of that, this also defines 'handle' as a typed, non-owning xcp::CalSegRef<T> over the
/// section-registered index - there is no C++ equivalent of the C CalSegCreate's immediate, self-sufficient creation.
#define CalSegDeclRef(value, handle)                                                                                                                                               \
    static tXcpCalSegIndex calseg_id_##value = XCP_UNDEFINED_CALSEG;                                                                                                               \
    static const tXcpCalSegDescriptor calseg__##value __asm__("calseg__" #value)                                                                                                   \
        XCP_CAL_SECTION_ATTR = {#value, (const void *)&value, &calseg_id_##value, sizeof(value), XCP_CALSEG_TYPE_SEGMENT};                                                         \
    static const xcp::CalSegRef<decltype(value)> handle(&calseg_id_##value, &value)

/// Declare a section-registered global calibration segment and create a typed C++ handle named <value>_calseg.
/// See CalSegDeclRef above for the section-based registration semantics this builds on.
#define CalSegDecl(value) CalSegDeclRef(value, value##_calseg)

/// Convenience macro to create a calibration value with automatic name stringification
/// Usage: auto calval = CalVal(initial_value);
#define CalBlkCreate(value) xcp::CalBlk<decltype(value)>(#value, &value)

} // namespace xcp

// =============================================================================
// Helper macros

// Portable always inline attribute for C++
// Critical for functions that use xcp_get_frame_addr() to ensure they capture the caller's stack frame
#if defined(_MSC_VER)
#define XCPLIB_ALWAYS_INLINE __forceinline
#elif defined(__GNUC__) || defined(__clang__)
#define XCPLIB_ALWAYS_INLINE __attribute__((always_inline)) inline
#else
#define XCPLIB_ALWAYS_INLINE inline
#error "XCPLIB_ALWAYS_INLINE may not guarantee inlining on this compiler - stack frame addresses may be incorrect"
#endif

namespace xcp {

// =============================================================================
// Convenience macros and variadic templates
// For XCP DAQ event creation, measurement registration and triggering
// =============================================================================

// @@@@ TODO: Support link time event registration
#ifdef OPTION_DAQ_EVENT_LIST

/// Trigger an event with variadic base address list
#define DaqTriggerEventVar(event_name, ...) xcp::DaqTriggerVarTemplate(#event_name, __VA_ARGS__)

// Main template function for event triggering with variadic base address list
template <typename... Bases> XCPLIB_ALWAYS_INLINE void DaqTriggerVarTemplate(const char *event_name, Bases const &...bases) {
    if (XcpIsActivated()) {
        static tXcpEventId event_id = XCP_UNDEFINED_EVENT_ID;
        static std::once_flag once_flag;
        std::call_once(once_flag, [&]() { event_id = XcpCreateEvent(event_name, 0, 0); });
        XcpEventExt_Var(event_id, sizeof...(Bases), &bases...);
    }
}

#endif // OPTION_DAQ_EVENT_LIST

// =============================================================================

#ifdef OPTION_ENABLE_A2L_GENERATOR

// Helper macros for creating measurement or instance info objects, variable name stringification and address capture
#define A2L_MEAS(var, comment) xcp::MeasurementInfo(#var, &(var), var, comment)
#define A2L_MEAS_PHYS(var, comment, unit, min, max) xcp::MeasurementInfo(#var, &(var), var, comment, unit, min, max)

#define A2L_MEAS_ARRAY(var, comment) xcp::MeasurementInfo(#var, &(var[0]), var[0], (uint16_t)(sizeof(var) / sizeof((var)[0])), comment)
#define A2L_MEAS_ARRAY_PHYS(var, comment, unit, min, max) xcp::MeasurementInfo(#var, &(var[0]), var[0], (uint16_t)(sizeof(var) / sizeof((var)[0])), comment, unit, min, max)

#define A2L_MEAS_INST(var, type_name, comment) xcp::InstanceInfo(#var, &(var), type_name, comment)
#define A2L_MEAS_INST_ARRAY(var, type_name, comment) xcp::InstanceInfo(#var, &(var), type_name, (uint16_t)(sizeof(var) / sizeof((var)[0])), comment)

#define A2L_MEAS_INST_PTR(var, ptr, type_name, comment) xcp::InstanceInfo(#var, ptr, type_name, comment)
#define A2L_MEAS_INST_ARRAY_PTR(var, ptr, type_name, comment) xcp::InstanceInfo(#var, ptr, type_name, (uint16_t)(sizeof(var) / sizeof((var)[0])), comment)

// Helper struct to hold measurement information
template <typename T> struct MeasurementInfo {
    const char *name;
    const T *addr;
    const T &value;
    const uint16_t dim; // 1 = scalar, >1 = array
    const char *comment;
    const char *unit;
    double min_value;
    double max_value;

    // Constructor for basic measurement (var, ptr, value, comment)
    constexpr MeasurementInfo(const char *name, const T *a, const T &v, const char *c)
        : name(name), addr(a), value(v), dim(1), comment(c), unit(nullptr), min_value(0.0), max_value(0.0) {}

    // Constructor for array of basic measurement (var, ptr, value, dim, comment)
    constexpr MeasurementInfo(const char *name, const T *a, const T &v, uint16_t dim, const char *c)
        : name(name), addr(a), value(v), dim(dim), comment(c), unit(nullptr), min_value(0.0), max_value(0.0) {}

    // Constructor for physical measurement (var, ptr, value, comment, unit, min, max)
    constexpr MeasurementInfo(const char *name, const T *a, const T &v, const char *c, const char *unit, double min_value, double max_value)
        : name(name), addr(a), value(v), dim(1), comment(c), unit(unit), min_value(min_value), max_value(max_value) {}

    // Constructor for array of physical measurement (var, ptr, value, dim, comment, unit, min, max)
    constexpr MeasurementInfo(const char *name, const T *a, const T &v, uint16_t dim, const char *c, const char *unit, double min_value, double max_value)
        : name(name), addr(a), value(v), dim(dim), comment(c), unit(unit), min_value(min_value), max_value(max_value) {}
};

// Helper struct to hold typedef instance information
template <typename T> struct InstanceInfo {
    const char *name;
    const T *addr;
    const char *type_name;
    const uint16_t dim; // 1 = scalar, >1 = array
    const char *comment;

    // Constructor (var, type_name, comment)
    constexpr InstanceInfo(const char *name, const T *a, const char *type_name, const char *c) : name(name), addr(a), type_name(type_name), dim(1), comment(c) {}

    // Constructor (var, type_name, dim, comment)
    constexpr InstanceInfo(const char *name, const T *a, const char *type_name, uint16_t dim, const char *c) : name(name), addr(a), type_name(type_name), dim(dim), comment(c) {}
};

// =============================================================================

// @@@@ TODO: Activate both variants, solve the name conflict with DaqEventVar
#ifdef USE_AUTO_ADDRESSING_MODE // not used

// Helper to register a single measurement
template <typename T> XCPLIB_ALWAYS_INLINE void registerMeasurement(const MeasurementInfo<T> &info) {
    A2lCreateMeasurement_(nullptr, info.name, xcp::a2l::GetTypeIdFromExpr(info.value), info.dim, (const void *)info.addr, info.unit, info.min_value, info.max_value, info.comment);
}

// Main template function for once event creation and registration with automatic addressing mode, and event triggering with base address
template <typename... Measurements> XCPLIB_ALWAYS_INLINE void DaqEventExtTemplate(const char *event_name, const void *base, Measurements &&...measurements) {
    if (XcpIsActivated()) {
        static tXcpEventId event_id = XCP_UNDEFINED_EVENT_ID;
        static std::once_flag once_flag;
        const uint8_t *frame_addr = (const uint8_t *)xcp_get_frame_addr(); // Capture caller's frame address before lambda
        std::call_once(once_flag, [&]() {
            event_id = XcpCreateEvent(event_name, 0, 0);
            A2lLock();
            A2lSetAutoAddrMode__i(event_id, frame_addr, (const uint8_t *)base);
            (registerMeasurement(measurements), ...);
            A2lUnlock();
        });
        XcpEventExt_Var(event_id, 2, frame_addr, (const uint8_t *)base);
        XCP_NO_TAIL_CALL();
    }
}

// @@@@ TODO: Support link time event registration

// Main template function for once event creation and registration with automatic addressing mode, and event triggering
template <typename... Measurements> XCPLIB_ALWAYS_INLINE void DaqEventTemplate(const char *event_name, Measurements &&...measurements) {
    if (XcpIsActivated()) {
        static tXcpEventId event_id = XCP_UNDEFINED_EVENT_ID;
        static std::once_flag once_flag;
        const uint8_t *frame_addr = (const uint8_t *)xcp_get_frame_addr(); // Capture caller's frame address before lambda
        std::call_once(once_flag, [&]() {
            event_id = XcpCreateEvent(event_name, 0, 0);
            A2lLock();
            A2lSetAutoAddrMode__i(event_id, frame_addr, NULL);
            (registerMeasurement(measurements), ...);
            A2lUnlock();
        });
        XcpEventExt_Var(event_id, 1, frame_addr);
        XCP_NO_TAIL_CALL();
    }
}

// Trigger an event, create the event and register measurement variables once
// Auto detect absolute, stack and relative addressing mode (DaqEventExtVar with base for relative mode))
#define DaqEventExtVar(event_name, base, ...) xcp::DaqEventExtTemplate(#event_name, base, __VA_ARGS__)
#define DaqEventVar(event_name, ...) xcp::DaqEventTemplate(#event_name, __VA_ARGS__)

#else

// Helper template to register a single measurement with relative addressing mode XCP_ADDR_EXT_DYN + index
template <typename T> XCPLIB_ALWAYS_INLINE void registerDynMeasurement(uint8_t index, tXcpEventId event_id, const MeasurementInfo<T> &info) {
    A2lSetRelativeAddrMode__i(event_id, index, (const uint8_t *)info.addr);
    A2lCreateMeasurement_(nullptr, info.name, xcp::a2l::GetTypeIdFromExpr(info.value), info.dim, (const void *)info.addr, info.unit, info.min_value, info.max_value, info.comment);
}

// Helper template to register a single typedef instance with relative addressing mode XCP_ADDR_EXT_DYN + index
template <typename T> XCPLIB_ALWAYS_INLINE void registerDynMeasurement(uint8_t index, tXcpEventId event_id, const InstanceInfo<T> &info) {
    A2lSetRelativeAddrMode__i(event_id, index, (const uint8_t *)info.addr);
    A2lCreateInstance_(info.name, info.type_name, info.dim, (const void *)info.addr, info.comment);
}

#endif // OPTION_ENABLE_A2L_GENERATOR

// Main template function for once event creation and registration with individual relative addressing mode, and event triggering
template <typename... Measurements> XCPLIB_ALWAYS_INLINE void DaqEventVarTemplate(tXcpEventId event_id, uint64_t clock, Measurements &&...measurements) {
    if (XcpIsActivated()) {

// Once
#ifdef OPTION_ENABLE_A2L_GENERATOR
        static std::once_flag once_flag;
        std::call_once(once_flag, [&]() {
            // Register measurements with individual DYN address extensions
            A2lLock();
            uint8_t index = 1; // Start at 1, 0 is reserved for frame pointer relative addressing mode
            (registerDynMeasurement(index++, event_id, measurements), ...);
            A2lUnlock();
        });
#endif // OPTION_ENABLE_A2L_GENERATOR

        // Always
        // Create base pointer list and trigger
        const uint8_t *bases[] = {xcp_get_base_addr(), xcp_get_base_addr(), xcp_get_frame_addr(), (const uint8_t *)measurements.addr...};
        XcpEventExtAt_(event_id, (sizeof(bases) / sizeof(bases[0])), bases, clock);
        XCP_NO_TAIL_CALL();
    }
}

/// Trigger an event, create the event and register measurement variables once
/// Use individual relative addressing mode for each measurement variable
#define DaqEventVar(event_name, ...)                                                                                                                                               \
    {                                                                                                                                                                              \
        DaqCreateEvent(event_name);                                                                                                                                                \
        static tXcpEventId trg__AASDD__##event_name = XCP_UNDEFINED_EVENT_ID;                                                                                                      \
        (void)trg__AASDD__##event_name;                                                                                                                                            \
        xcp::DaqEventVarTemplate(evt_id_##event_name, 0, __VA_ARGS__);                                                                                                             \
    }
#define DaqEventAtVar(event_name, clock, ...)                                                                                                                                      \
    {                                                                                                                                                                              \
        DaqCreateEvent(event_name);                                                                                                                                                \
        static tXcpEventId trg__AASDD__##event_name = XCP_UNDEFINED_EVENT_ID;                                                                                                      \
        (void)trg__AASDD__##event_name;                                                                                                                                            \
        xcp::DaqEventVarTemplate(evt_id_##event_name, clock, __VA_ARGS__);                                                                                                         \
    }

#endif

} // namespace xcp
