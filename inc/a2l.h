#pragma once
#define __A2L_H__

/*----------------------------------------------------------------------------
| File:
|   a2l.h - Public C API for A2L generation
|
| Description:
|   Public header for A2L generation
|     Macros and functions A2L_xxxx, A2lXxxx
|     tA2lTypeId
|     C++ type detection namespace xcp::a2l
|
| Copyright (c) Vector Informatik GmbH. All rights reserved.
| See LICENSE file in the project root for details.
|
 ----------------------------------------------------------------------------*/

///
/// A2L (ASAM-2 MCD-2 MC) description file generation for XCPlite
/// This header provides comprehensive functionality for automatic generation of A2L description files
/// during runtime. The A2L format is defined in the ASAM-2 MCD-2 MC standard and describes ECU internal
/// measurement and calibration values for use with XCP-based measurement and calibration tools.
///
/// The A2L generation system provides:
/// - Automatic type detection for both C and C++
/// - Support for different addressing modes (absolute, relative, stack, segment-based)
/// - Definition of measurement events
/// - Definition of calibration parameter segments
/// - Calibration parameter and measurement variable definitions
/// - Support for complex data structures (typedefs)
/// - Definition of groups
/// - Thread-safe operation with once-patterns or lock/unlock
///
/// Four addressing modes are supported:
/// - **Absolute**: Variables in global memory space
/// - **Relative**: Variables relative to a base address (e.g., heap objects)
/// - **Stack**: Variables on the stack relative to stack frame pointer
/// - **Segment**: Calibration parameters in calibration parameter segments
///
/// Basic Usage
///
/// 1. Initialize A2L generation with A2lInit()
/// 2. Set addressing mode for the following variables
/// 3. Create measurements and parameters using the provided macros
/// 4. Finalize the A2L file with A2lFinalize()

#include <assert.h>  // for assert
#include <stdbool.h> // for bool
#include <stddef.h>  // for offsetof
#include <stdint.h>  // for uintxx_t

#include "xcplib.h" // for tXcpEventId, tXcpCalSegIndex, ...

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// A2L generation modes

#define A2L_MODE_DEACTIVATE 0x00          // Deactivate A2L generation
#define A2L_MODE_WRITE_ALWAYS 0x01        // Always write A2L file, overwrite existing file
#define A2L_MODE_WRITE_ONCE 0x02          // Write A2L file only once for a epk version, do not overwrite existing file, use the binary persistence file to keep the A2L file valid
#define A2L_MODE_FINALIZE_ON_CONNECT 0x04 // Finalize A2L file on XCP connect
#define A2L_MODE_AUTO_GROUPS 0x08         // Automatically create groups for measurements and parameters
#define A2L_MODE_SYMBOL_PREFIX 0x10       // Prepend project name as prefix to all symbol names (measurements, parameters, typedefs, components)
#define A2L_MODE_WRITE_TEMPLATE 0x20      // Only write an A2L template with all settings, IF_DATA, events and calibration segments
#define A2L_MODE_EVENT_CONVERSION 0x40    // Create a enum conversion with all event ids
#define A2L_MODE_EMBED_AML_FILE 0x80      // Include AML file (e.g., XCP_104.aml) with /include directive, instead of embedding the AML file content into the A2L file

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// Constants

#define XCP_A2L_MAX_SYMBOL_NAME_LENGTH 64 // Maximum length of symbol names in A2L file (including null terminator)
#define XCP_A2L_MAX_LINE_LENGTH 512       // Maximum length of a line in A2L file (including null terminator)
#define XCP_A2L_MAX_COMMENT_LENGTH 256    // Maximum length of a comment in A2L file (including null terminator)

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

// Basic A2L types
typedef int8_t tA2lTypeId; // A2L type ID, positive for unsigned types, negative for signed types
#define A2L_TYPE_UINT8 (tA2lTypeId)1
#define A2L_TYPE_UINT16 (tA2lTypeId)2
#define A2L_TYPE_UINT32 (tA2lTypeId)4
#define A2L_TYPE_UINT64 (tA2lTypeId)8
#define A2L_TYPE_INT8 (tA2lTypeId)(-1)
#define A2L_TYPE_INT16 (tA2lTypeId)(-2)
#define A2L_TYPE_INT32 (tA2lTypeId)(-4)
#define A2L_TYPE_INT64 (tA2lTypeId)(-8)
#define A2L_TYPE_FLOAT (tA2lTypeId)(-9)
#define A2L_TYPE_DOUBLE (tA2lTypeId)(-10)
#define A2L_TYPE_UNDEFINED (tA2lTypeId)0

static_assert(sizeof(char) == 1, "sizeof(char) must be 1");
static_assert(sizeof(short) == 2, "sizeof(short) must be 2");
static_assert(sizeof(long long) == 8, "sizeof(long long) must be 8");

/// Value range of a basic A2L type, used as default limits, float, double and int64 are limited to +-1e12
double A2lGetTypeMax(tA2lTypeId type_id);
double A2lGetTypeMin(tA2lTypeId type_id);

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// Automatic, portable type detection macros for C

// C++ version using template meta-programming
#ifdef __cplusplus

namespace xcp {
namespace a2l {

template <typename T> struct TypeId {
    static const tA2lTypeId value = A2L_TYPE_UNDEFINED;
};
template <> struct TypeId<signed char> {
    static const tA2lTypeId value = A2L_TYPE_INT8;
};
template <> struct TypeId<unsigned char> {
    static const tA2lTypeId value = A2L_TYPE_UINT8;
};
template <> struct TypeId<bool> {
    static const tA2lTypeId value = A2L_TYPE_UINT8;
};
template <> struct TypeId<signed short> {
    static const tA2lTypeId value = A2L_TYPE_INT16;
};
template <> struct TypeId<unsigned short> {
    static const tA2lTypeId value = A2L_TYPE_UINT16;
};
template <> struct TypeId<signed int> {
    static const tA2lTypeId value = (tA2lTypeId)(-(int8_t)sizeof(int));
};
template <> struct TypeId<unsigned int> {
    static const tA2lTypeId value = (tA2lTypeId)(int8_t)sizeof(int);
};
template <> struct TypeId<signed long> {
    static const tA2lTypeId value = (tA2lTypeId)(-(int8_t)sizeof(long));
};
template <> struct TypeId<unsigned long> {
    static const tA2lTypeId value = (tA2lTypeId)(int8_t)sizeof(long);
};
template <> struct TypeId<signed long long> {
    static const tA2lTypeId value = A2L_TYPE_INT64;
};
template <> struct TypeId<unsigned long long> {
    static const tA2lTypeId value = A2L_TYPE_UINT64;
};
template <> struct TypeId<float> {
    static const tA2lTypeId value = A2L_TYPE_FLOAT;
};
template <> struct TypeId<double> {
    static const tA2lTypeId value = A2L_TYPE_DOUBLE;
};

// Helper to strip cv-qualifiers and references
template <typename T> struct RemoveCVRef {
    using type = T;
};
template <typename T> struct RemoveCVRef<const T> {
    using type = T;
};
template <typename T> struct RemoveCVRef<volatile T> {
    using type = T;
};
template <typename T> struct RemoveCVRef<const volatile T> {
    using type = T;
};
template <typename T> struct RemoveCVRef<T &> {
    using type = T;
};
template <typename T> struct RemoveCVRef<const T &> {
    using type = T;
};
template <typename T> struct RemoveCVRef<volatile T &> {
    using type = T;
};
template <typename T> struct RemoveCVRef<const volatile T &> {
    using type = T;
};

// Function to get type id at compile time
template <typename T> constexpr tA2lTypeId GetTypeId() { return TypeId<typename RemoveCVRef<T>::type>::value; }

// Function to get type id from expression (handles arrays, complex expressions)
template <typename T> constexpr tA2lTypeId GetTypeIdFromExpr(const T &) { return GetTypeId<T>(); }

} // namespace a2l
} // namespace xcp

/// Get the A2L type id of an expression (variable, struct member, array element), A2L_TYPE_UNDEFINED for non basic types
#define A2lGetTypeId(expr) xcp::a2l::GetTypeIdFromExpr(expr)

// Helper macros for array element type detection (works with multi-dimensional arrays)
#define A2lGetArray1DElementTypeId(array) xcp::a2l::GetTypeIdFromExpr((array)[0])
#define A2lGetArray2DElementTypeId(array) xcp::a2l::GetTypeIdFromExpr((array)[0][0])

#endif // __cplusplus

// C version using _Generic for simple expressions and fallback for complex ones
#ifndef __cplusplus

// Helper function to deduce type from pointer (for array elements)
static inline tA2lTypeId A2lGetTypeIdFromPtr_uint8(const uint8_t *p) {
    (void)p;
    return A2L_TYPE_UINT8;
}
static inline tA2lTypeId A2lGetTypeIdFromPtr_int8(const int8_t *p) {
    (void)p;
    return A2L_TYPE_INT8;
}
static inline tA2lTypeId A2lGetTypeIdFromPtr_uint16(const uint16_t *p) {
    (void)p;
    return A2L_TYPE_UINT16;
}
static inline tA2lTypeId A2lGetTypeIdFromPtr_int16(const int16_t *p) {
    (void)p;
    return A2L_TYPE_INT16;
}
static inline tA2lTypeId A2lGetTypeIdFromPtr_uint32(const uint32_t *p) {
    (void)p;
    return A2L_TYPE_UINT32;
}
static inline tA2lTypeId A2lGetTypeIdFromPtr_int32(const int32_t *p) {
    (void)p;
    return A2L_TYPE_INT32;
}
static inline tA2lTypeId A2lGetTypeIdFromPtr_uint64(const uint64_t *p) {
    (void)p;
    return A2L_TYPE_UINT64;
}
static inline tA2lTypeId A2lGetTypeIdFromPtr_int64(const int64_t *p) {
    (void)p;
    return A2L_TYPE_INT64;
}
static inline tA2lTypeId A2lGetTypeIdFromPtr_float(const float *p) {
    (void)p;
    return A2L_TYPE_FLOAT;
}
static inline tA2lTypeId A2lGetTypeIdFromPtr_double(const double *p) {
    (void)p;
    return A2L_TYPE_DOUBLE;
}
static inline tA2lTypeId A2lGetTypeIdFromPtr_bool(const bool *p) {
    (void)p;
    return A2L_TYPE_UINT8;
}

// For complex expressions (like array indexing), use pointer-based type detection
#define A2lGetTypeIdFromPtr(ptr)                                                                                                                                                   \
    _Generic((ptr),                                                                                                                                                                \
        const uint8_t *: A2lGetTypeIdFromPtr_uint8,                                                                                                                                \
        uint8_t *: A2lGetTypeIdFromPtr_uint8,                                                                                                                                      \
        const int8_t *: A2lGetTypeIdFromPtr_int8,                                                                                                                                  \
        int8_t *: A2lGetTypeIdFromPtr_int8,                                                                                                                                        \
        const uint16_t *: A2lGetTypeIdFromPtr_uint16,                                                                                                                              \
        uint16_t *: A2lGetTypeIdFromPtr_uint16,                                                                                                                                    \
        const int16_t *: A2lGetTypeIdFromPtr_int16,                                                                                                                                \
        int16_t *: A2lGetTypeIdFromPtr_int16,                                                                                                                                      \
        const uint32_t *: A2lGetTypeIdFromPtr_uint32,                                                                                                                              \
        uint32_t *: A2lGetTypeIdFromPtr_uint32,                                                                                                                                    \
        const int32_t *: A2lGetTypeIdFromPtr_int32,                                                                                                                                \
        int32_t *: A2lGetTypeIdFromPtr_int32,                                                                                                                                      \
        const uint64_t *: A2lGetTypeIdFromPtr_uint64,                                                                                                                              \
        uint64_t *: A2lGetTypeIdFromPtr_uint64,                                                                                                                                    \
        const int64_t *: A2lGetTypeIdFromPtr_int64,                                                                                                                                \
        int64_t *: A2lGetTypeIdFromPtr_int64,                                                                                                                                      \
        const float *: A2lGetTypeIdFromPtr_float,                                                                                                                                  \
        float *: A2lGetTypeIdFromPtr_float,                                                                                                                                        \
        const double *: A2lGetTypeIdFromPtr_double,                                                                                                                                \
        double *: A2lGetTypeIdFromPtr_double,                                                                                                                                      \
        const bool *: A2lGetTypeIdFromPtr_bool,                                                                                                                                    \
        bool *: A2lGetTypeIdFromPtr_bool,                                                                                                                                          \
        default: A2lGetTypeIdFromPtr_uint8)(ptr)

// Macro to generate A2L type id from an expression - supports both simple and complex expressions
#define A2lGetTypeId(expr)                                                                                                                                                         \
    _Generic(&(expr),                                                                                                                                                              \
        const uint8_t *: A2L_TYPE_UINT8,                                                                                                                                           \
        uint8_t *: A2L_TYPE_UINT8,                                                                                                                                                 \
        const int8_t *: A2L_TYPE_INT8,                                                                                                                                             \
        int8_t *: A2L_TYPE_INT8,                                                                                                                                                   \
        const uint16_t *: A2L_TYPE_UINT16,                                                                                                                                         \
        uint16_t *: A2L_TYPE_UINT16,                                                                                                                                               \
        const int16_t *: A2L_TYPE_INT16,                                                                                                                                           \
        int16_t *: A2L_TYPE_INT16,                                                                                                                                                 \
        const uint32_t *: A2L_TYPE_UINT32,                                                                                                                                         \
        uint32_t *: A2L_TYPE_UINT32,                                                                                                                                               \
        const int32_t *: A2L_TYPE_INT32,                                                                                                                                           \
        int32_t *: A2L_TYPE_INT32,                                                                                                                                                 \
        const uint64_t *: A2L_TYPE_UINT64,                                                                                                                                         \
        uint64_t *: A2L_TYPE_UINT64,                                                                                                                                               \
        const int64_t *: A2L_TYPE_INT64,                                                                                                                                           \
        int64_t *: A2L_TYPE_INT64,                                                                                                                                                 \
        const float *: A2L_TYPE_FLOAT,                                                                                                                                             \
        float *: A2L_TYPE_FLOAT,                                                                                                                                                   \
        const double *: A2L_TYPE_DOUBLE,                                                                                                                                           \
        double *: A2L_TYPE_DOUBLE,                                                                                                                                                 \
        const bool *: A2L_TYPE_UINT8,                                                                                                                                              \
        bool *: A2L_TYPE_UINT8,                                                                                                                                                    \
        default: A2L_TYPE_UINT8)

#endif // !__cplusplus

// Additional robust alternatives for complex type detection scenarios

#ifndef __cplusplus

#define A2lGetArray1DElementTypeId(array) A2lGetTypeId((array)[0])
#define A2lGetArray2DElementTypeId(array) A2lGetTypeId((array)[0][0])

#endif // !__cplusplus

// Get A2L names for the basic type of an expression, a 1D array element or a 2D array element, as static const char*
// The macros take an expression, the A2lGetA2lXxx functions below take a tA2lTypeId

/// A2L data type name (UBYTE, SWORD, FLOAT32_IEEE, ...)
#define A2lGetTypeName(type) A2lGetA2lTypeName(A2lGetTypeId(type))
#define A2lGetTypeName1D(type) A2lGetA2lTypeName(A2lGetArray1DElementTypeId(type))
#define A2lGetTypeName2D(type) A2lGetA2lTypeName(A2lGetArray2DElementTypeId(type))

/// Name of the predefined TYPEDEF_MEASUREMENT for the type (M_I8, M_U16, M_F32, ...)
#define A2lGetTypeName_M(type) A2lGetA2lTypeName_M(A2lGetTypeId(type))
#define A2lGetTypeName1D_M(type) A2lGetA2lTypeName_M(A2lGetArray1DElementTypeId(type))
#define A2lGetTypeName2D_M(type) A2lGetA2lTypeName_M(A2lGetArray2DElementTypeId(type))

/// Name of the predefined TYPEDEF_CHARACTERISTIC for the type (C_I8, C_U16, C_F32, ...)
#define A2lGetTypeName_C(type) A2lGetA2lTypeName_C(A2lGetTypeId(type))
#define A2lGetTypeName1D_C(type) A2lGetA2lTypeName_C(A2lGetArray1DElementTypeId(type))
#define A2lGetTypeName2D_C(type) A2lGetA2lTypeName_C(A2lGetArray2DElementTypeId(type))

/// Name of the predefined RECORD_LAYOUT for the type (I8, U16, F32, ...)
#define A2lGetRecordLayoutName(type) A2lGetA2lRecordLayoutName(A2lGetTypeId(type))
#define A2lGetRecordLayoutName1D(type) A2lGetA2lRecordLayoutName(A2lGetArray1DElementTypeId(type))
#define A2lGetRecordLayoutName2D(type) A2lGetA2lRecordLayoutName(A2lGetArray2DElementTypeId(type))

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// Addressing mode convenience macros
//
// Exactly one of these should be set before the A2lCreateParameter/A2lCreateMeasurement (or typedef component) macros that
// should use it; the mode stays active for all such calls until the next A2lSet*AddrMode call. See the "Four addressing
// modes" summary at the top of this file, and docs/TECHNICAL.md for how they map to the XCP address extension.
// Each mode has an `_s` variant taking the event/segment name as a plain string (event_name/seg_name) and an `_i` variant
// taking its numeric id/index (event_id/seg_index, e.g. as returned by DaqCreateEvent/XcpCreateCalSeg) - useful when the
// name isn't available as a bare identifier for the `#event_name` stringification the unsuffixed macro uses.

/// Set Segment addressing mode: subsequent A2lCreateParameter/A2lCreateCurve/A2lCreateAxis/A2lCreateMap calls register their
/// variables as offsets within calibration segment 'seg_index'/'seg_name' (or as absolute addresses, depending on the
/// CASDD/ACSDD build configuration, see docs/TECHNICAL.md) - this is what makes the segment's page switching, checksum and
/// persistence apply to them. Must be called after the segment has been created (e.g. via XcpCreateCalSeg).
/// @param seg_index, seg_name the calibration segment to address into, by index or by name; error if it does not exist
/// @param seg_instance the segment's instance variable in scope, used to compute member offsets
#define A2lSetSegmentAddrMode(seg_index, seg_instance) A2lSetSegmentAddrMode__i(seg_index, (const uint8_t *)&(seg_instance));
#define A2lSetSegmentAddrMode_s(seg_name, seg_instance) A2lSetSegmentAddrMode__s(seg_name, (const uint8_t *)&(seg_instance));

/// Set Relative addressing mode: subsequent A2lCreateMeasurement-family calls register their variables as offsets from
/// 'base_addr', re-evaluated each time event 'event_name' triggers - use this for heap-allocated objects or class members
/// reached through a pointer, where the address is not a stack frame. Uses address-extension slot 1 (slot 0 is reserved for
/// A2lSetStackAddrMode's stack frame pointer, so the two can be combined on the same event without clashing).
/// @param event_name, event_id the event this addressing mode applies to; error if it does not exist
/// @param base_addr pointer the registered variables are relative to
#define A2lSetRelativeAddrMode(event_name, base_addr) A2lSetRelativeAddrMode__s(#event_name, 1, (const uint8_t *)(base_addr));
#define A2lSetRelativeAddrMode_s(event_name, base_addr) A2lSetRelativeAddrMode__s(event_name, 1, (const uint8_t *)(base_addr));
#define A2lSetRelativeAddrMode_i(event_id, base_addr) A2lSetRelativeAddrMode__i(event_id, 1, (const uint8_t *)(base_addr));

/// Set Automatic addressing mode: like Relative, but decides for each variable individually, at generation time, whether its
/// address falls inside the stack frame given by xcp_get_frame_addr() - if so, Stack addressing is used, otherwise Relative
/// addressing against 'base_addr' is used. Useful for registering a mix of stack-local and heap/pointer-based variables under
/// one event without switching addressing mode explicitly between groups.
/// @param event_name, event_id the event this addressing mode applies to; error if it does not exist
/// @param base_addr pointer used for variables whose address is not within the current stack frame
void A2lSetAutoAddrMode(tXcpEventId event_id, const uint8_t *frame_ptr, const uint8_t *base_ptr);
#define A2lSetAutomaticAddrMode(event_name, base_addr) A2lSetAutoAddrMode__s(#event_name, xcp_get_frame_addr(), base_addr);
#define A2lSetAutomaticAddrMode_s(event_name_string, base_addr) A2lSetAutoAddrMode__s(event_name_string, xcp_get_frame_addr(), base_addr);
#define A2lSetAutomaticAddrMode_i(event_id, base_addr) A2lSetAutoAddrMode__i(event_id, xcp_get_frame_addr(), base_addr);

/// Set Stack addressing mode: subsequent A2lCreateMeasurement-family calls register their variables as offsets from the
/// stack frame active when event 'event_name' fires - use this for local variables of the function that triggers the event.
/// @param event_name, event_id the event this addressing mode applies to; error if it does not exist
#define A2lSetStackAddrMode(event_name) A2lSetStackAddrMode__s(#event_name, xcp_get_frame_addr());
#define A2lSetStackAddrMode_s(event_name_string) A2lSetStackAddrMode__s(event_name_string, xcp_get_frame_addr());
#define A2lSetStackAddrMode_i(event_id) A2lSetStackAddrMode__i(event_id, xcp_get_frame_addr());

/// Set Absolute addressing mode: subsequent A2lCreateMeasurement-family calls register their variables by their fixed,
/// static address - use this for globals and static locals. 'event_name' does not need to exist and does not affect the
/// addressing; if it does exist, the following registrations are additionally grouped under that event in the A2L file.
/// @param event_name, event_id default event used for A2L grouping only, may name a nonexistent event
#define A2lSetAbsoluteAddrMode(event_name) A2lSetAbsoluteAddrMode__s(#event_name);
#define A2lSetAbsoluteAddrMode_s(event_name_string) A2lSetAbsoluteAddrMode__s(event_name_string);
#define A2lSetAbsoluteAddrMode_i(event_id) A2lSetAbsoluteAddrMode__i(event_id);

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// Create parameters in calibration parameter segments or in global memory
//
// @param name variable identifier; must be in scope, its type is detected automatically and its address is registered
// @param comment free-text description
// @param unit physical unit string, or a "conv.<name>" reference to a conversion created with A2lCreateLinearConversion/A2lCreateEnumConversion (see below)
// @param min, max physical value limits shown to the XCP tool
// @param xdim, ydim number of elements along the respective array dimension

/// Create a scalar calibration parameter
#define A2lCreateParameter(name, comment, unit, min, max) A2lCreateParameter_(#name, A2lGetTypeId(name), (uint8_t *)&(name), comment, unit, min, max);

/// Create a 1-dimensional calibration curve (no shared axis, tool creates a default axis)
#define A2lCreateCurve(name, xdim, comment, unit, min, max) A2lCreateCurve_(#name, A2lGetArray1DElementTypeId(name), (uint8_t *)&(name)[0], xdim, comment, unit, min, max, NULL);

/// Create a 1-dimensional calibration curve that shares its axis with another curve/axis created via A2lCreateAxis
/// @param x_axis name of the axis object to share
#define A2lCreateCurveWithSharedAxis(name, xdim, comment, unit, min, max, x_axis)                                                                                                  \
    A2lCreateCurve_(#name, A2lGetArray1DElementTypeId(name), (uint8_t *)&(name)[0], xdim, comment, unit, min, max, x_axis);

/// Create a standalone axis, usable as the shared axis of one or more curves/maps
#define A2lCreateAxis(name, xdim, comment, unit, min, max) A2lCreateAxis_(#name, A2lGetArray1DElementTypeId(name), (uint8_t *)&(name)[0], xdim, comment, unit, min, max);

/// Create a 2-dimensional calibration map (no shared axes, tool creates default axes)
#define A2lCreateMap(name, xdim, ydim, comment, unit, min, max)                                                                                                                    \
    A2lCreateMap_(#name, A2lGetArray2DElementTypeId(name), (uint8_t *)&(name)[0][0], xdim, ydim, comment, unit, min, max, NULL, NULL);

/// Create a 2-dimensional calibration map that shares its axes with axis objects created via A2lCreateAxis
/// @param x_axis, y_axis names of the axis objects to share
#define A2lCreateMapWithSharedAxis(name, xdim, ydim, comment, unit, min, max, x_axis, y_axis)                                                                                      \
    A2lCreateMap_(#name, A2lGetArray2DElementTypeId(name), (uint8_t *)&(name)[0][0], xdim, ydim, comment, unit, min, max, x_axis, y_axis);

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// Create conversions
//
// A conversion is referenced from a measurement/parameter macro's unit_or_conversion parameter as the string "conv.<name>"

/// Create a linear (factor, offset) physical value conversion, referenced as unit_or_conversion string "conv.<name>"
#define A2lCreateLinearConversion(name, comment, unit, factor, offset) A2lCreateLinearConversion_(#name, comment, unit, factor, offset)

/// Create an enumeration conversion, referenced as unit_or_conversion string "conv.<name>"
/// @param description A2L COMPU_VTAB value list, e.g. "3 0 \"OFF\" 1 \"ON\" 2 \"STANDBY\"" (count, then value/label pairs)
#define A2lCreateEnumConversion(name, description) A2lCreateEnumConversion_(#name, description)

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// Create measurements on stack or in global memory
//
// @param name variable identifier; must be in scope, its type is detected automatically and its address is registered
// @param comment free-text description
// @param unit_or_conversion physical unit string, or a "conv.<name>" reference to a conversion (see above)
// @param min, max physical value limits shown to the XCP tool
// @param instance_name prefix prepended to the A2L symbol name, to distinguish measurements of the same variable name across multiple instances (e.g. threads, class instances)

/// Create a scalar measurement with no physical unit/conversion
#define A2lCreateMeasurement(name, comment) A2lCreateMeasurement_(NULL, #name, A2lGetTypeId(name), 1, &(name), NULL, 0.0, 0.0, comment);

/// Create a scalar measurement with a physical unit or conversion and value limits
#define A2lCreatePhysMeasurement(name, comment, unit_or_conversion, min, max)                                                                                                      \
    A2lCreateMeasurement_(NULL, #name, A2lGetTypeId(name), 1, &(name), unit_or_conversion, min, max, comment);

/// Create a 1-dimensional array measurement with no physical unit/conversion
#define A2lCreateMeasurementArray(name, comment)                                                                                                                                   \
    A2lCreateMeasurementArray_(NULL, #name, A2lGetArray1DElementTypeId(name), sizeof(name) / sizeof((name)[0]), 1, &(name)[0], NULL, 0.0, 0.0, comment);

/// Create a 2-dimensional array (matrix) measurement with no physical unit/conversion
#define A2lCreateMeasurementMatrix(name, comment)                                                                                                                                  \
    A2lCreateMeasurementArray_(NULL, #name, A2lGetArray2DElementTypeId(name), sizeof((name)[0]) / sizeof((name)[0][0]), sizeof(name) / sizeof((name)[0]), &(name)[0], NULL, 0.0,   \
                               0.0, comment);

/// Create a 1-dimensional array measurement with a physical unit or conversion and value limits
#define A2lCreatePhysMeasurementArray(name, comment, unit_or_conversion, min, max)                                                                                                 \
    A2lCreateMeasurementArray_(NULL, #name, A2lGetArray1DElementTypeId(name), sizeof(name) / sizeof((name)[0]), 1, &(name)[0], unit_or_conversion, min, max, comment);

/// Create a 2-dimensional array (matrix) measurement with a physical unit or conversion and value limits
#define A2lCreatePhysMeasurementMatrix(name, comment, unit_or_conversion, min, max)                                                                                                \
    A2lCreateMeasurementArray_(NULL, #name, A2lGetArray2DElementTypeId(name), sizeof((name)[0]) / sizeof((name)[0][0]), sizeof(name) / sizeof((name)[0]), &(name)[0],              \
                               unit_or_conversion, min, max, comment);

/// Create a scalar measurement, with instance_name used as a prefix for the A2L symbol name
#define A2lCreateMeasurementInstance(instance_name, name, comment) A2lCreateMeasurement_(instance_name, #name, A2lGetTypeId(name), 1, &(name), NULL, 0.0, 0.0, comment);

/// Create a scalar measurement with a physical unit or conversion, with instance_name used as a prefix for the A2L symbol name
#define A2lCreatePhysMeasurementInstance(instance_name, name, comment, unit_or_conversion, min, max)                                                                               \
    A2lCreateMeasurement_(instance_name, #name, A2lGetTypeId(name), 1, &(name), unit_or_conversion, min, max, comment);

/// Create a 1-dimensional array measurement, with instance_name used as a prefix for the A2L symbol name
#define A2lCreateMeasurementArrayInstance(instance_name, name, comment)                                                                                                            \
    A2lCreateMeasurementArray_(instance_name, #name, A2lGetArray1DElementTypeId(name), sizeof(name) / sizeof((name)[0]), 1, &(name)[0], NULL, 0.0, 0.0, comment);

/// Create a 2-dimensional array (matrix) measurement, with instance_name used as a prefix for the A2L symbol name
#define A2lCreateMeasurementMatrixInstance(instance_name, name, comment)                                                                                                           \
    A2lCreateMeasurementArray_(instance_name, #name, A2lGetArray2DElementTypeId(name), sizeof((name)[0]) / sizeof(((name)[0][0])), sizeof(name) / sizeof((name)[0]), &(name)[0],   \
                               NULL, 0.0, 0.0, comment);

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// Create instances from typedefs
//
// An instance is a variable of a struct type described with A2lTypedefBegin/A2lTypedefEnd (C) or A2lCreateTypedef (C++), the current
// addressing mode applies. Measurement instances in absolute, application or dynamic addressing mode are writable (READ_WRITE).
// @param name variable identifier (the A2L instance name), or a string where noted
// @param typeName typedef name as identifier, as given to A2lTypedefBegin
// @param dim number of array elements, 0 or 1 for a single instance
// @param comment free-text description

/// Create an instance of a typedef, generic form
/// @param name A2L instance name as string
/// @param ptr address of the instance
#define A2lCreateInstance(name, typeName, dim, ptr, comment) A2lCreateInstance_(name, #typeName, dim, ptr, comment);

/// Single instance of a typedef, the A2L instance name is the variable name
#define A2lCreateTypedefInstance(name, typeName, comment) A2lCreateInstance_(#name, #typeName, 0, &(name), comment);

/// Single instance of a typedef with an A2L instance name different from the variable name
/// @param name A2L instance name as string
/// @param instance the variable
#define A2lCreateTypedefNamedInstance(name, instance, typeName, comment) A2lCreateInstance_(name, #typeName, 0, &(instance), comment);

/// Array of typedef instances, name is the array variable, dim its number of elements (A2lCreateTypedefArray and A2lCreateTypedefInstanceArray are identical)
#define A2lCreateTypedefArray(name, typeName, dim, comment) A2lCreateInstance_(#name, #typeName, dim, &(name), comment);
#define A2lCreateTypedefInstanceArray(name, typeName, dim, comment) A2lCreateInstance_(#name, #typeName, dim, &(name), comment);

/// Typedef instance behind a pointer, name is the pointer variable, the A2L instance name is the pointer name
/// (A2lCreateTypedefReference and A2lCreateTypedefInstanceReference are identical)
#define A2lCreateTypedefReference(name, typeName, comment) A2lCreateInstance_(#name, #typeName, 0, name, comment);
#define A2lCreateTypedefInstanceReference(name, typeName, comment) A2lCreateInstance_(#name, #typeName, 0, name, comment);

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// Create typedefs and typedef components
//
// A typedef (A2L TYPEDEF_STRUCTURE) describes a struct type once, instances of it are created with the A2lCreateTypedefXxx macros above.
// The component macros must be used between A2lTypedefBegin and A2lTypedefEnd, they detect the type and the offset of the field automatically.
// A typedef is not bound to an addressing mode, it is created once per type, use a once pattern or A2lLock/A2lUnlock (see thread safety below).
// @param field_name struct member as identifier
// @param comment free-text description
// @param unit, unit_or_conversion physical unit string, or a "conv.<name>" reference to a conversion
// @param min, max physical value limits
// @param x_dim, y_dim number of array elements of a curve, map or axis field

/// Begin a typedef for the struct type 'typedef_name'
/// @param typedef_name struct type as identifier, used as A2L typedef name
/// @param type_instance pointer to an instance of the type, only used to compute the field offsets
/// @param ... comment, printf style format string and arguments
#define A2lTypedefBegin(typedef_name, type_instance, ...)                                                                                                                          \
    {                                                                                                                                                                              \
        const typedef_name *__A2lTypedefBegin_instance = type_instance;                                                                                                            \
        A2lTypedefBegin_(#typedef_name, (uint32_t)sizeof(typedef_name), __VA_ARGS__);

/// Component of a given type, for nested structs (another typedef) or explicitly typed fields
/// @param field_type_name name of another typedef, or of a predefined TYPEDEF_MEASUREMENT/TYPEDEF_CHARACTERISTIC (see A2lGetTypeName_M/_C), as identifier
/// @param field_dim number of array elements, 1 for a scalar
#define A2lTypedefComponent(field_name, field_type_name, field_dim)                                                                                                                \
    A2lTypedefComponent_(#field_name, #field_type_name, field_dim, ((uint8_t *)&(__A2lTypedefBegin_instance->field_name) - (uint8_t *)__A2lTypedefBegin_instance))

/// End a typedef started with A2lTypedefBegin
#define A2lTypedefEnd()                                                                                                                                                            \
    }                                                                                                                                                                              \
    A2lTypedefEnd_()

// Measurement components
// Implicitly create a TYPEDEF_MEASUREMENT

/// Scalar measurement field
#define A2lTypedefMeasurementComponent(field_name, comment)                                                                                                                        \
    do {                                                                                                                                                                           \
        A2lTypedefMeasurementComponent_(#field_name, A2lGetTypeId(__A2lTypedefBegin_instance->field_name), 1,                                                                      \
                                        ((uint8_t *)&(__A2lTypedefBegin_instance->field_name) - (uint8_t *)__A2lTypedefBegin_instance), comment, NULL, 0.0, 0.0);                  \
    } while (0)

/// 1D array measurement field, dimension detected from the type
#define A2lTypedefMeasurementArrayComponent(field_name, comment)                                                                                                                   \
    do {                                                                                                                                                                           \
        A2lTypedefMeasurementComponent_(#field_name, A2lGetArray1DElementTypeId(__A2lTypedefBegin_instance->field_name),                                                           \
                                        sizeof(__A2lTypedefBegin_instance->field_name) / sizeof(__A2lTypedefBegin_instance->field_name[0]),                                        \
                                        ((uint8_t *)&(__A2lTypedefBegin_instance->field_name) - (uint8_t *)__A2lTypedefBegin_instance), comment, NULL, 0.0, 0.0);                  \
    } while (0)

/// Scalar measurement field with a physical unit or conversion and limits
#define A2lTypedefPhysMeasurementComponent(field_name, comment, unit_or_conversion, min, max)                                                                                      \
    do {                                                                                                                                                                           \
        A2lTypedefMeasurementComponent_(#field_name, A2lGetTypeId(__A2lTypedefBegin_instance->field_name), 1,                                                                      \
                                        ((uint8_t *)&(__A2lTypedefBegin_instance->field_name) - (uint8_t *)__A2lTypedefBegin_instance), comment, unit_or_conversion, min, max);    \
    } while (0)

/// 1D array measurement field with a physical unit or conversion and limits, dimension detected from the type
#define A2lTypedefPhysMeasurementArrayComponent(field_name, comment, unit_or_conversion, min, max)                                                                                 \
    do {                                                                                                                                                                           \
        A2lTypedefMeasurementComponent_(#field_name, A2lGetArray1DElementTypeId(__A2lTypedefBegin_instance->field_name),                                                           \
                                        sizeof(__A2lTypedefBegin_instance->field_name) / sizeof(__A2lTypedefBegin_instance->field_name[0]),                                        \
                                        ((uint8_t *)&(__A2lTypedefBegin_instance->field_name) - (uint8_t *)__A2lTypedefBegin_instance), comment, unit_or_conversion, min, max);    \
    } while (0)

// Parameter components
// Implicitly create a TYPEDEF_CHARACTERISTIC

/// Scalar calibration parameter field
#define A2lTypedefParameterComponent(field_name, comment, unit, min, max)                                                                                                          \
    do {                                                                                                                                                                           \
        A2lTypedefParameterComponent_(#field_name, A2lGetTypeId(__A2lTypedefBegin_instance->field_name), 1, 1,                                                                     \
                                      ((uint8_t *)&(__A2lTypedefBegin_instance->field_name) - (uint8_t *)__A2lTypedefBegin_instance), comment, unit, min, max, NULL, NULL);        \
    } while (0)

/// 1D calibration curve field with x_dim elements (no shared axis, the tool creates a default axis)
#define A2lTypedefCurveComponent(field_name, x_dim, comment, unit, min, max)                                                                                                       \
    do {                                                                                                                                                                           \
        A2lTypedefParameterComponent_(#field_name, A2lGetArray1DElementTypeId(__A2lTypedefBegin_instance->field_name), x_dim, 1,                                                   \
                                      ((uint8_t *)&(__A2lTypedefBegin_instance->field_name) - (uint8_t *)__A2lTypedefBegin_instance), comment, unit, min, max, NULL, NULL);        \
    } while (0)

/// 1D calibration curve field, which shares the axis field x_axis (A2lTypedefAxisComponent) of the same typedef
/// @param x_axis name of the axis field as string
#define A2lTypedefCurveComponentWithSharedAxis(field_name, x_dim, comment, unit, min, max, x_axis)                                                                                 \
    do {                                                                                                                                                                           \
        A2lTypedefParameterComponent_(#field_name, A2lGetArray1DElementTypeId(__A2lTypedefBegin_instance->field_name), x_dim, 1,                                                   \
                                      ((uint8_t *)&(__A2lTypedefBegin_instance->field_name) - (uint8_t *)__A2lTypedefBegin_instance), comment, unit, min, max, x_axis, NULL);      \
    } while (0)

/// 2D calibration map field with x_dim * y_dim elements (no shared axes)
#define A2lTypedefMapComponent(field_name, x_dim, y_dim, comment, unit, min, max)                                                                                                  \
    do {                                                                                                                                                                           \
        A2lTypedefParameterComponent_(#field_name, A2lGetArray2DElementTypeId(__A2lTypedefBegin_instance->field_name), x_dim, y_dim,                                               \
                                      ((uint8_t *)&(__A2lTypedefBegin_instance->field_name) - (uint8_t *)__A2lTypedefBegin_instance), comment, unit, min, max, NULL, NULL);        \
    } while (0)

/// 2D calibration map field, which shares the axis fields x_axis and y_axis of the same typedef
/// @param x_axis, y_axis names of the axis fields as string
#define A2lTypedefMapComponentWithSharedAxis(field_name, x_dim, y_dim, comment, unit, min, max, x_axis, y_axis)                                                                    \
    do {                                                                                                                                                                           \
        A2lTypedefParameterComponent_(#field_name, A2lGetArray2DElementTypeId(__A2lTypedefBegin_instance->field_name), x_dim, y_dim,                                               \
                                      ((uint8_t *)&(__A2lTypedefBegin_instance->field_name) - (uint8_t *)__A2lTypedefBegin_instance), comment, unit, min, max, x_axis, y_axis);    \
    } while (0)

/// Axis field with x_dim elements, usable as shared axis of curve and map fields of the same typedef
#define A2lTypedefAxisComponent(field_name, x_dim, comment, unit, min, max)                                                                                                        \
    do {                                                                                                                                                                           \
        A2lTypedefParameterComponent_(#field_name, A2lGetArray1DElementTypeId(__A2lTypedefBegin_instance->field_name), x_dim, 0,                                                   \
                                      ((uint8_t *)&(__A2lTypedefBegin_instance->field_name) - (uint8_t *)__A2lTypedefBegin_instance), comment, unit, min, max, NULL, NULL);        \
    } while (0)

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// Thread safety
// Use these macros to protect blocks of macros and create once patterns in A2L generation code
// The states of the A2L generator (group, addressing mode, typedef begin/end) are not thread-safe

// Execute a block once
#define A2L_ONCE_TYPE uint64_t
#define A2L_ONCE_ATOMIC_TYPE atomic_uint_fast64_t

#ifndef __cplusplus

/// Execute the following block exactly once, thread-safe and wait-free across all threads: on concurrent first calls,
/// an atomic compare-and-swap picks exactly one caller to enter the block; every other (losing) caller skips it
/// immediately, without waiting for the winner to finish executing it. Use to guard a one-time A2L registration (e.g.
/// A2lCreateMeasurement calls) that may be reached from code executed by more than one event/function.
/// @param name suffix used to build a unique static flag variable (`__a2l_once_<name>_`) for this call site; may be
/// omitted (A2lOnce()) when the enclosing scope contains only one A2lOnce block, since the flag variable's name still
/// needs to be unique only within that scope
#define A2lOnce(name)                                                                                                                                                              \
    static A2L_ONCE_TYPE __a2l_once_##name##_ = 0;                                                                                                                                 \
    if (A2lOnce_(&__a2l_once_##name##_))
/// Execute the following block exactly once per calling thread (unlike A2lOnce, does not block other threads and each
/// thread runs the block independently on its first call).
/// @param name suffix used to build a unique thread-local flag variable (`__a2l_thread_once_<name>_`) for this call
/// site; may be omitted (A2lThreadOnce()) under the same uniqueness rule as A2lOnce's `name`
#define A2lThreadOnce(name)                                                                                                                                                        \
    static THREAD_LOCAL uint64_t __a2l_thread_once_##name##_ = 0;                                                                                                                  \
    if ((__a2l_thread_once_##name##_++) == 0)

#endif // !__cplusplus

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
/// Lock and unlock a block of A2L definitions (mutex), the A2L generator state (addressing mode, group, typedef) is not thread safe
void A2lLock(void);
void A2lUnlock(void);

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
/// Get A2L names for a type id as static const char* string, see the A2lGetTypeName macros above
const char *A2lGetA2lTypeName(tA2lTypeId type);
const char *A2lGetA2lTypeName_M(tA2lTypeId type);
const char *A2lGetA2lTypeName_C(tA2lTypeId type);
const char *A2lGetA2lRecordLayoutName(tA2lTypeId type);

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// Manually create groups
// If automatic group generation (A2lInit mode A2L_MODE_AUTO_GROUPS) is disabled, use these functions to create groups manually.
// A group contains either parameters or measurements, group names are global and get the project name prefix with A2L_MODE_SYMBOL_PREFIX.
// Only one group can be open at a time, creating a new group closes the open one.

/// Begin a group, the following A2lAddToGroup calls add to it
/// @param name Group name
/// @param comment free-text description
/// @param is_parameter_group true for a group of parameters (REF_CHARACTERISTIC), false for measurements (REF_MEASUREMENT)
/// @param is_root_group true for a top level group (ROOT)
void A2lBeginGroup(const char *name, const char *comment, bool is_parameter_group, bool is_root_group);
/// Add the parameter or measurement with the A2L symbol name 'name' to the open group
void A2lAddToGroup(const char *name);
/// Close the open group
void A2lEndGroup(void);

/// Create a root group of parameters in one call
/// @param name Group name
/// @param count Number of parameter names that follow, as const char* (variadic) or in the array pNames
void A2lCreateParameterGroup(const char *name, int count, ...);
void A2lCreateParameterGroupFromList(const char *name, const char *pNames[], int count);

/// Create a root group of measurements in one call
/// @param name Group name
/// @param count Number of measurement names that follow, as const char* (variadic) or in the array names
void A2lCreateMeasurementGroup(const char *name, int count, ...);
void A2lCreateMeasurementGroupFromList(const char *name, char *names[], uint32_t count);

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

/// Init runtime A2L generation
/// Must be called once after XcpInit() (and usually after XcpEthServerInit()), before any other A2L generation macro or function.
/// Project name and EPK are taken from XcpInit(), the A2L file name is built from them (A2lGetFilename()).
/// If an A2L file for this EPK already exists (A2L_MODE_WRITE_ONCE), A2L generation is skipped, the existing file is provided for upload
/// and all following A2L generation macros and functions do nothing.
/// While the A2L file is being generated, an XCP connect finalizes it (A2L_MODE_FINALIZE_ON_CONNECT) or is refused until A2lFinalize() is called.
/// This does not use the application connect callback (ApplXcpRegisterConnectCallback), it is called after the A2L check.
/// In passive mode (XcpInit with XCP_MODE_DEACTIVATE) nothing is done and true is returned.
/// @param addr IPv4 address of the XCP server for the A2L IF_DATA XCP (4 bytes), {0,0,0,0} = any, see OPTION_ENABLE_GET_LOCAL_ADDR
/// @param port Port of the XCP server for the A2L IF_DATA XCP
/// @param useTCP Transport protocol for the A2L IF_DATA XCP, true = TCP, false = UDP
/// @param mode Combination of the A2L_MODE_xxx flags:
///  A2L_MODE_WRITE_ALWAYS Generate the A2L file on every start, overwrite an existing file
///  A2L_MODE_WRITE_ONCE Generate the A2L file once per EPK and keep it, requires XcpInit with XCP_MODE_PERSISTENCE, the binary persistence file keeps
///   the event and calibration segment numbering stable and enables calibration segment persistence (freeze)
///  A2L_MODE_FINALIZE_ON_CONNECT Finalize the A2L file when an XCP client connects, otherwise call A2lFinalize()
///  A2L_MODE_AUTO_GROUPS Create groups per event (measurements) and per calibration segment (parameters), otherwise use A2lBeginGroup etc.
///  A2L_MODE_SYMBOL_PREFIX, A2L_MODE_WRITE_TEMPLATE, A2L_MODE_EVENT_CONVERSION, A2L_MODE_EMBED_AML_FILE see their definition
/// @return true on success, false on failure
bool A2lInit(const uint8_t *addr, uint16_t port, bool useTCP, uint8_t mode);

/// Finish A2L generation
/// Finalize the A2L file and write the binary persistence file (XCP_MODE_PERSISTENCE). Call it when all events, calibration segments,
/// measurements and parameters are registered, or use A2L_MODE_FINALIZE_ON_CONNECT.
/// After finalization the A2L file is provided for upload, all following A2L definitions are ignored and not visible to the XCP tool.
/// @return true on success, false if A2L generation is not active or the A2L file is already finalized
bool A2lFinalize(void);

/// Get the A2L file name, built from the project name and the EPK

const char *A2lGetFilename(void);

#ifdef OPTION_SHM_MODE // Get the A2L file name for an application with the given project name and EPK
/// Get the A2L file name for an application with the given project name
/// Buffer valid until the next call of this function
const char *A2lGetAppFilename(const char *project_name, const char *epk);
#endif

// --------------------------------------------------------------------------------------------
// Functions behind the A2L generation macros
// Use them directly when names, types or addresses are known only at runtime. The parameters correspond to the macros:
//   name, instance_name: A2L symbol name as string, instance_name is an optional prefix (NULL = none)
//   type, type_id: tA2lTypeId, see A2lGetTypeId
//   ptr: address of the variable, interpreted in the current addressing mode
//   x_dim, y_dim: array dimensions (1 = scalar), comment, unit or unit_or_conversion, min, max: as in the macros

// Set addressing mode by event name or calibration segment index
// Used by the macros with the identical name (one underscore)
void A2lSetSegmentAddrMode__i(tXcpCalSegIndex calseg_index, const uint8_t *calseg_instance);
void A2lSetSegmentAddrMode__s(const char *calseg_name, const uint8_t *calseg_instance);
void A2lSetAutoAddrMode__s(const char *event_name, const uint8_t *stack_frame, const uint8_t *base_addr);
void A2lSetAutoAddrMode__i(tXcpEventId event_id, const uint8_t *stack_frame, const uint8_t *base_addr);
void A2lSetRelativeAddrMode__s(const char *event_name, uint8_t i, const uint8_t *base_addr);
void A2lSetRelativeAddrMode__i(tXcpEventId event_id, uint8_t i, const uint8_t *base_addr);
void A2lSetStackAddrMode__s(const char *event_name, const uint8_t *stack_frame);
void A2lSetStackAddrMode__i(tXcpEventId event_id, const uint8_t *stack_frame);
void A2lSetAbsoluteAddrMode__s(const char *event_name);
void A2lSetAbsoluteAddrMode__i(tXcpEventId event_id);
void A2lSetApplicationAddrMode(void);

// Once pattern helper
bool A2lOnce_(uint64_t *once);

// Create parameters
void A2lCreateParameter_(const char *name, tA2lTypeId type, const void *ptr, const char *comment, const char *unit, double min, double max);
void A2lCreateMap_(const char *name, tA2lTypeId type, const void *ptr, uint32_t xdim, uint32_t ydim, const char *comment, const char *unit, double min, double max,
                   const char *x_axis, const char *y_axis);
void A2lCreateCurve_(const char *name, tA2lTypeId type, const void *ptr, uint32_t xdim, const char *comment, const char *unit, double min, double max, const char *x_axis);
void A2lCreateAxis_(const char *name, tA2lTypeId type, const void *ptr, uint32_t xdim, const char *comment, const char *unit, double min, double max);

// Create conversions
const char *A2lCreateLinearConversion_(const char *name, const char *comment, const char *unit, double factor, double offset);
const char *A2lCreateEnumConversion_(const char *name, const char *enum_description);

// Create measurements
void A2lCreateMeasurement_(const char *instance_name, const char *name, tA2lTypeId type, uint16_t dim, const void *ptr, const char *unit_or_conversion, double min, double max,
                           const char *comment);
void A2lCreateMeasurementArray_(const char *instance_name, const char *name, tA2lTypeId type, int x_dim, int y_dim, const void *ptr, const char *unit_or_conversion,
                                double phys_min, double phys_max, const char *comment);

// Create typedefs
void A2lTypedefBegin_(const char *name, uint32_t size, const char *format, ...);
void A2lTypedefEnd_(void);
void A2lTypedefComponent_(const char *name, const char *typedef_name, uint16_t x_dim, size_t offset);
void A2lTypedefMeasurementComponent_(const char *name, tA2lTypeId type_id, uint16_t x_dim, size_t offset, const char *comment, const char *unit_or_conversion, double min,
                                     double max);
void A2lTypedefParameterComponent_(const char *name, tA2lTypeId type_id, uint16_t x_dim, uint16_t y_dim, size_t offset, const char *comment, const char *unit, double min,
                                   double max, const char *x_axis, const char *y_axis);

// Create an instance of a typedef
void A2lCreateInstance_(const char *instance_name, const char *typeName, uint16_t x_dim, const void *ptr, const char *comment);

/// Set the input quantities (measurement names) for the X and Y axes of the following curves and maps, NULL = none
void A2lSetInputQuantity_x(const char *measurement_variable_name);
void A2lSetInputQuantity_y(const char *measurement_variable_name);

/// Enable/disable project name prefix for symbol names ("project.symbolname"), same as A2L_MODE_SYMBOL_PREFIX
/// Call after A2lInit, which sets the mode, affects the symbols created after the call, always enabled in SHM mode
void A2lSetSymbolPrefix(bool enable);

#ifdef __cplusplus
} // extern "C"
#endif
