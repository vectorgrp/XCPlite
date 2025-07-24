/// a2l.h
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

// Copyright(c) Vector Informatik GmbH.All rights reserved.
// Licensed under the MIT license.See LICENSE file in the project root for details.

#pragma once

#include <assert.h>  // for assert
#include <stdbool.h> // for bool
#include <stdint.h>  // for uintxx_t

#include "xcplib.h" // for tXcpEventId, tXcpCalSegIndex

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

static_assert(sizeof(char) == 1, "sizeof(char) must be 1 bytes for A2L types to work correctly");
static_assert(sizeof(short) == 2, "sizeof(short) must be 2 bytes for A2L types to work correctly");
static_assert(sizeof(long long) == 8, "sizeof(long long) must be 8 bytes for A2L types to work correctly");

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// Automatic, portable type detection macros for both C and C++

#ifdef __cplusplus
// C++ version using template meta-programming
namespace A2lTypeTraits {
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
} // namespace A2lTypeTraits

#define A2lGetTypeId(expr) A2lTypeTraits::GetTypeIdFromExpr(expr)

#else

// C version using _Generic for simple expressions and fallback for complex ones

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
        default: A2L_TYPE_UNDEFINED)

#endif

// Additional robust alternatives for complex type detection scenarios

// Helper macros for array element type detection (works with multi-dimensional arrays)
#ifdef __cplusplus
#define A2lGetArray1DElementTypeId(array) A2lTypeTraits::GetTypeIdFromExpr((array)[0])
#define A2lGetArray2DElementTypeId(array) A2lTypeTraits::GetTypeIdFromExpr((array)[0][0])

// Alternative macro using decltype (C++11) for maximum robustness
#if __cplusplus >= 201103L
#define A2lGetTypeIdDecltype(expr) A2lTypeTraits::GetTypeId<decltype(expr)>()
#else
#error "C++11 or later is required for decltype-based type detection"
#endif

#else
#define A2lGetArray1DElementTypeId(array) A2lGetTypeId((array)[0])
#define A2lGetArray2DElementTypeId(array) A2lGetTypeId((array)[0][0])
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Macros to generate type names as static const char* string
const char *A2lGetA2lTypeName(tA2lTypeId type);
const char *A2lGetA2lTypeName_M(tA2lTypeId type);
const char *A2lGetA2lTypeName_C(tA2lTypeId type);
const char *A2lGetRecordLayoutName_(tA2lTypeId type);

#define A2lGetTypeName(type) A2lGetA2lTypeName(A2lGetTypeId(type))
#define A2lGetTypeName1D(type) A2lGetA2lTypeName(A2lGetArray1DElementTypeId(type))
#define A2lGetTypeName2D(type) A2lGetA2lTypeName(A2lGetArray2DElementTypeId(type))

#define A2lGetTypeName_M(type) A2lGetA2lTypeName_M(A2lGetTypeId(type))
#define A2lGetTypeName1D_M(type) A2lGetA2lTypeName_M(A2lGetArray1DElementTypeId(type))
#define A2lGetTypeName2D_M(type) A2lGetA2lTypeName_M(A2lGetArray2DElementTypeId(type))

#define A2lGetTypeName_C(type) A2lGetA2lTypeName_C(A2lGetTypeId(type))
#define A2lGetTypeName1D_C(type) A2lGetA2lTypeName_C(A2lGetArray1DElementTypeId(type))
#define A2lGetTypeName2D_C(type) A2lGetA2lTypeName_C(A2lGetArray2DElementTypeId(type))

#define A2lGetRecordLayoutName(type) A2lGetRecordLayoutName_(A2lGetTypeId(type))
#define A2lGetRecordLayoutName1D(type) A2lGetRecordLayoutName_(A2lGetArray1DElementTypeId(type))
#define A2lGetRecordLayoutName2D(type) A2lGetRecordLayoutName_(A2lGetArray2DElementTypeId(type))

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// Addressing mode convenience macros

#ifndef get_stack_frame_pointer
#define get_stack_frame_pointer() (const uint8_t *)__builtin_frame_address(0)
#endif

// Set segment relative address mode
// Error if the segment index does not exist
#define A2lSetSegmentAddrMode(seg_index, seg_instance) A2lSetSegmentAddrMode__i(seg_index, (const uint8_t *)&seg_instance);

// Set addressing mode to relative for a given event 'event_name' and base address
// Error if the event does not exist
#define A2lSetRelativeAddrMode(event_name, base_addr) A2lSetRelativeAddrMode__s(#event_name, (const uint8_t *)base_addr);
#define A2lSetRelativeAddrMode_s(event_name, base_addr) A2lSetRelativeAddrMode__s(event_name, (const uint8_t *)base_addr);
#define A2lSetRelativeAddrMode_i(event_id, base_addr) A2lSetRelativeAddrMode__i(event_id, (const uint8_t *)base_addr);

// Set addressing mode to stack and event 'event_name'
// Error if the event does not exist
#define A2lSetStackAddrMode(event_name) A2lSetStackAddrMode__s(#event_name, get_stack_frame_pointer());
#define A2lSetStackAddrMode_s(event_name_string) A2lSetStackAddrMode__s(event_name_string, get_stack_frame_pointer());
#define A2lSetStackAddrMode_i(event_id) A2lSetStackAddrMode__i(event_id, get_stack_frame_pointer());

// Set addressing mode to absolute and event 'event_name'
// Error if the event does not exist
#define A2lSetAbsoluteAddrMode(event_name) A2lSetAbsoluteAddrMode__s(#event_name);
#define A2lSetAbsoluteAddrMode_s(event_name_string) A2lSetAbsoluteAddrMode__s(event_name_string);
#define A2lSetAbsoluteAddrMode_i(event_id) A2lSetAbsoluteAddrMode__i(event_id);

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// Create parameters
// Addressing mode ABS (unsafe), addressing mode CAL or addresing mode DYN with explicit sync event and base

#define A2lCreateParameter(name, comment, unit, min, max) A2lCreateParameter_(#name, A2lGetTypeId(name), A2lGetAddrExt_(), A2lGetAddr_((uint8_t *)&name), comment, unit, min, max);

#define A2lCreateCurve(name, xdim, comment, unit, min, max)                                                                                                                        \
    A2lCreateCurve_(#name, A2lGetArray1DElementTypeId(name), A2lGetAddrExt_(), A2lGetAddr_((uint8_t *)&name[0]), xdim, comment, unit, min, max, NULL);

#define A2lCreateCurveWithSharedAxis(name, xdim, comment, unit, min, max, x_axis)                                                                                                  \
    A2lCreateCurve_(#name, A2lGetArray1DElementTypeId(name), A2lGetAddrExt_(), A2lGetAddr_((uint8_t *)&name[0]), xdim, comment, unit, min, max, x_axis);

#define A2lCreateAxis(name, xdim, comment, unit, min, max)                                                                                                                         \
    A2lCreateAxis_(#name, A2lGetArray1DElementTypeId(name), A2lGetAddrExt_(), A2lGetAddr_((uint8_t *)&name[0]), xdim, comment, unit, min, max);

#define A2lCreateMap(name, xdim, ydim, comment, unit, min, max)                                                                                                                    \
    A2lCreateMap_(#name, A2lGetArray2DElementTypeId(name), A2lGetAddrExt_(), A2lGetAddr_((uint8_t *)&name[0][0]), xdim, ydim, comment, unit, min, max, NULL, NULL);

#define A2lCreateMapWithSharedAxis(name, xdim, ydim, comment, unit, min, max, x_axis, y_axis)                                                                                      \
    A2lCreateMap_(#name, A2lGetArray2DElementTypeId(name), A2lGetAddrExt_(), A2lGetAddr_((uint8_t *)&name[0][0]), xdim, ydim, comment, unit, min, max, x_axis, y_axis);

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// Create conversions

#define A2lCreateLinearConversion(name, comment, unit, factor, offset) const char *name = A2lCreateLinearConversion_(#name, comment, unit, factor, offset);

#define A2lCreateEnumConversion(name, description) const char *name = A2lCreateEnumConversion_(#name, description);

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// Create measurements on stack or in global memory

#define A2lCreateMeasurement(name, comment) A2lCreateMeasurement_(NULL, #name, A2lGetTypeId(name), A2lGetAddrExt_(), A2lGetAddr_((uint8_t *)&(name)), NULL, 0.0, 0.0, comment);

#define A2lCreatePhysMeasurement(name, comment, unit_or_conversion, min, max)                                                                                                      \
    A2lCreateMeasurement_(NULL, #name, A2lGetTypeId(name), A2lGetAddrExt_(), A2lGetAddr_((uint8_t *)&(name)), unit_or_conversion, min, max, comment);

#define A2lCreateMeasurementArray(name, comment)                                                                                                                                   \
    A2lCreateMeasurementArray_(NULL, #name, A2lGetArray1DElementTypeId(name), sizeof(name) / sizeof(name[0]), 1, A2lGetAddrExt_(), A2lGetAddr_(&name[0]), NULL, 0.0, 0.0, comment);

#define A2lCreateMeasurementMatrix(name, comment)                                                                                                                                  \
    A2lCreateMeasurementArray_(NULL, #name, A2lGetArray2DElementTypeId(name), sizeof(name[0]) / sizeof(name[0][0]), sizeof(name) / sizeof(name[0]), A2lGetAddrExt_(),              \
                               A2lGetAddr_(&name[0]), NULL, 0.0, 0.0, comment);

#define A2lCreatePhysMeasurementArray(name, comment, unit_or_conversion, min, max)                                                                                                 \
    A2lCreatePhysMeasurementArray_(NULL, #name, A2lGetArray1DElementTypeId(name), sizeof(name) / sizeof(name[0]), 1, A2lGetAddrExt_(), A2lGetAddr_(&name[0]), unit_or_conversion,  \
                                   min, max, comment);

#define A2lCreatePhysMeasurementMatrix(name, comment, unit_or_conversion, min, max)                                                                                                \
    A2lCreateMeasurementArray_(NULL, #name, A2lGetArray2DElementTypeId(name), sizeof(name[0]) / sizeof(name[0][0]), sizeof(name) / sizeof(name[0]), A2lGetAddrExt_(),              \
                               A2lGetAddr_(&name[0]), unit_or_conversion, min, max, comment);

// With instance name
#define A2lCreateMeasurementInstance(instance_name, name, comment, unit)                                                                                                           \
    A2lCreateMeasurement_(instance_name, #name, A2lGetTypeId(name), A2lGetAddrExt_(), A2lGetAddr_((uint8_t *)&(name)), unit, 0.0, 0.0, comment);

#define A2lCreatePhysMeasurementInstance(instance_name, name, comment, unit_or_conversion, min, max)                                                                               \
    A2lCreateMeasurement_(instance_name, #name, A2lGetTypeId(name), A2lGetAddrExt_(), A2lGetAddr_((uint8_t *)&(name)), unit_or_conversion, min, max, comment);

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// Create instances from typedefs

#define A2lCreateTypedefNamedInstance(name, instance, typeName, comment)                                                                                                           \
    A2lCreateTypedefMeasurementInstance_(name, #typeName, 0, A2lGetAddrExt_(), A2lGetAddr_((uint8_t *)&instance), comment);

#define A2lCreateTypedefInstance(name, typeName, comment) A2lCreateTypedefMeasurementInstance_(#name, #typeName, 0, A2lGetAddrExt_(), A2lGetAddr_((uint8_t *)&name), comment);

#define A2lCreateTypedefReference(name, typeName, comment) A2lCreateTypedefMeasurementInstance_(#name, #typeName, 0, A2lGetAddrExt_(), A2lGetAddr_((uint8_t *)name), comment);

#define A2lCreateTypedefArray(name, typeName, dim, comment) A2lCreateTypedefMeasurementInstance_(#name, #typeName, dim, A2lGetAddrExt_(), A2lGetAddr_((uint8_t *)&name), comment);

#define A2lCreateTypedefArrayReference(name, typeName, dim, comment)                                                                                                               \
    A2lCreateTypedefMeasurementInstance_(#name, #typeName, dim, A2lGetAddrExt_(), A2lGetAddr_((uint8_t *)name), comment);

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// Create typedefs and typedef components

#define A2lTypedefBegin(type_name, comment) A2lTypedefBegin_(#type_name, (uint32_t)sizeof(type_name), comment);

#define A2lTypedefComponent(field_name, field_type_name, field_dim, typedef_name)                                                                                                  \
    {                                                                                                                                                                              \
        typedef_name instance;                                                                                                                                                     \
        A2lTypedefComponent_(#field_name, #field_type_name, field_dim, ((uint8_t *)&(instance.field_name) - (uint8_t *)&instance));                                                \
    }

#define A2lTypedefEnd() A2lTypedefEnd_()

// Measurement components

#define A2lTypedefMeasurementComponent(field_name, typedef_name)                                                                                                                   \
    {                                                                                                                                                                              \
        typedef_name instance;                                                                                                                                                     \
        A2lTypedefComponent_(#field_name, A2lGetTypeName_M(instance.field_name), 1, ((uint8_t *)&(instance.field_name) - (uint8_t *)&instance));                                   \
    }

#define A2lTypedefPhysMeasurementComponent(field_name, typedef_name, comment, unit_or_conversion, min, max)                                                                        \
    {                                                                                                                                                                              \
        typedef_name instance;                                                                                                                                                     \
        A2lTypedefMeasurementComponent_(#field_name, A2lGetTypeName(instance.field_name), ((uint8_t *)&(instance.field_name) - (uint8_t *)&instance), comment, unit_or_conversion, \
                                        min, max);                                                                                                                                 \
    }

#define A2lTypedefMeasurementArrayComponent(field_name, typedef_name)                                                                                                              \
    {                                                                                                                                                                              \
        typedef_name instance;                                                                                                                                                     \
        A2lTypedefComponent_(#field_name, A2lGetTypeName1D_M(instance.field_name), sizeof(instance.field_name) / sizeof(instance.field_name[0]),                                   \
                             ((uint8_t *)&(instance.field_name[0]) - (uint8_t *)&instance));                                                                                       \
    }

// Parameter components

#define A2lTypedefParameterComponent(field_name, typeName, comment, unit, min, max)                                                                                                \
    {                                                                                                                                                                              \
        typeName instance;                                                                                                                                                         \
        A2lTypedefParameterComponent_(#field_name, A2lGetRecordLayoutName(instance.field_name), 1, 1, ((uint8_t *)&(instance.field_name) - (uint8_t *)&instance), comment, unit,   \
                                      min, max, NULL, NULL);                                                                                                                       \
    }

#define A2lTypedefCurveComponent(field_name, typeName, x_dim, comment, unit, min, max)                                                                                             \
    {                                                                                                                                                                              \
        typeName instance;                                                                                                                                                         \
        A2lTypedefParameterComponent_(#field_name, A2lGetRecordLayoutName1D(instance.field_name), x_dim, 1, ((uint8_t *)&(instance.field_name) - (uint8_t *)&instance), comment,   \
                                      unit, min, max, NULL, NULL);                                                                                                                 \
    }

#define A2lTypedefCurveComponentWithSharedAxis(field_name, typeName, x_dim, comment, unit, min, max, x_axis)                                                                       \
    {                                                                                                                                                                              \
        typeName instance;                                                                                                                                                         \
        A2lTypedefParameterComponent_(#field_name, A2lGetRecordLayoutName1D(instance.field_name), x_dim, 1, ((uint8_t *)&(instance.field_name) - (uint8_t *)&instance), comment,   \
                                      unit, min, max, x_axis, NULL);                                                                                                               \
    }

#define A2lTypedefMapComponent(field_name, typeName, x_dim, y_dim, comment, unit, min, max)                                                                                        \
    {                                                                                                                                                                              \
        typeName instance;                                                                                                                                                         \
        A2lTypedefParameterComponent_(#field_name, A2lGetRecordLayoutName2D(instance.field_name), x_dim, y_dim, ((uint8_t *)&(instance.field_name) - (uint8_t *)&instance),        \
                                      comment, unit, min, max, NULL, NULL);                                                                                                        \
    }

#define A2lTypedefMapComponentWithSharedAxis(field_name, typeName, x_dim, y_dim, comment, unit, min, max, x_axis, y_axis)                                                          \
    {                                                                                                                                                                              \
        typeName instance;                                                                                                                                                         \
        A2lTypedefParameterComponent_(#field_name, A2lGetRecordLayoutName2D(instance.field_name), x_dim, y_dim, ((uint8_t *)&(instance.field_name) - (uint8_t *)&instance),        \
                                      comment, unit, min, max, x_axis, y_axis);                                                                                                    \
    }

#define A2lTypedefAxisComponent(field_name, typeName, x_dim, comment, unit, min, max)                                                                                              \
    {                                                                                                                                                                              \
        typeName instance;                                                                                                                                                         \
        A2lTypedefParameterComponent_(#field_name, A2lGetRecordLayoutName1D(instance.field_name), x_dim, 0, ((uint8_t *)&(instance.field_name) - (uint8_t *)&instance), comment,   \
                                      unit, min, max, NULL, NULL);                                                                                                                 \
    }

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// Thread safety
// Use these macros to protect blocks of macros and create once patterns in A2L generation code
// The states of the A2L generator (group, addressing mode, typedef begin/end) are not thread-safe

// Execute a block once
// Global
#define A2lOnce(name)                                                                                                                                                              \
    static uint64_t __a2l_##name##_ = 0;                                                                                                                                           \
    if (A2lOnce_(&__a2l_##name##_))
// Per thread
#define A2lThreadOnce(name)                                                                                                                                                        \
    static THREAD_LOCAL uint64_t __a2l_##name##_ = 0;                                                                                                                              \
    if (A2lOnce_(&__a2l_##name##_))

// Lock and unlock a block (mutex)
void A2lLock(void);
void A2lUnlock(void);

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// Set addressing modes

void A2lRstAddrMode(void);
void A2lSetDynAddrMode(tXcpEventId event_id, const uint8_t *base);
void A2lSetRelAddrMode(tXcpEventId event_id, const uint8_t *base);
void A2lSetAbsAddrMode(tXcpEventId default_event_id);
void A2lSetSegAddrMode(tXcpCalSegIndex calseg_index, const uint8_t *calseg_instance_addr);

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// Manually create groups
// If automatic group generation (A2lInit parameter) is disabled, use these functions to create groups manually.

void A2lBeginGroup(const char *name, const char *comment, bool is_parameter_group);
void A2lAddToGroup(const char *name);
void A2lEndGroup(void);

void A2lCreateParameterGroup(const char *name, int count, ...);
void A2lCreateParameterGroupFromList(const char *name, const char *pNames[], int count);

void A2lCreateMeasurementGroup(const char *name, int count, ...);
void A2lCreateMeasurementGroupFromList(const char *name, char *names[], uint32_t count);

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

/// Init A2L generation
/// If the A2l file aready exists and matches the current EPK matches, if yes, load the binary persistence file
/// If not, prepare the A2L file and start the runtime generation process
/// @param a2l_projectname Name of the A2L project, used to build the A2L and BIN file name
/// @param a2l_version Version string of the A2L project, used to build the A2L and BIN file name, can be NULL to generate a version based on time and date
/// @param addr IP Address Used for IF_DATA XCP
/// @param port Port Used for IF_DATA XCP
/// @param useTCP Protocol Used for IF_DATA XCP
/// @param force_generation Force generation of the A2L file, even if it already exists and matches the EPK
/// @param finalize_on_connect Finalize the A2L file on XCP client connect, if false, the A2L file has to finalized manually
/// @param enable_auto_grouping Enable automatic grouping of parameters (per segment) and measurements (per event), if false, grouping must be done manually
/// @return true on success, false on failure
bool A2lInit(const char *a2l_projectname, const char *a2l_version, const uint8_t *addr, uint16_t port, bool useTCP, bool force_generation, bool finalize_on_connect,
             bool enable_auto_grouping);

/// Finish A2L generation
/// Finalize the A2L file, write the binary persistence file
bool A2lFinalize(void);

// --------------------------------------------------------------------------------------------
// Helper functions used in the by A2L generation macros

// Set addressing mode by event name or calibration segment index
// Used by the macros with the identical name (one underscore)
void A2lSetSegmentAddrMode__i(tXcpCalSegIndex calseg_index, const uint8_t *calseg_instance);
void A2lSetRelativeAddrMode__s(const char *event_name, const uint8_t *base_addr);
void A2lSetRelativeAddrMode__i(tXcpEventId event_id, const uint8_t *base_addr);
void A2lSetStackAddrMode__s(const char *event_name, const uint8_t *stack_frame);
void A2lSetStackAddrMode__i(tXcpEventId event_id, const uint8_t *stack_frame);
void A2lSetAbsoluteAddrMode__s(const char *event_name);
void A2lSetAbsoluteAddrMode__i(tXcpEventId event_id);

// Once pattern helper
bool A2lOnce_(uint64_t *once);

// Address encoding
uint32_t A2lGetAddr_(const void *addr);
uint8_t A2lGetAddrExt_(void);

// Create parameters
void A2lCreateParameter_(const char *name, tA2lTypeId type, uint8_t ext, uint32_t addr, const char *comment, const char *unit, double min, double max);
void A2lCreateMap_(const char *name, tA2lTypeId type, uint8_t ext, uint32_t addr, uint32_t xdim, uint32_t ydim, const char *comment, const char *unit, double min, double max,
                   const char *x_axis, const char *y_axis);
void A2lCreateCurve_(const char *name, tA2lTypeId type, uint8_t ext, uint32_t addr, uint32_t xdim, const char *comment, const char *unit, double min, double max,
                     const char *x_axis);
void A2lCreateAxis_(const char *name, tA2lTypeId type, uint8_t ext, uint32_t addr, uint32_t xdim, const char *comment, const char *unit, double min, double max);

// Create conversions
const char *A2lCreateLinearConversion_(const char *name, const char *comment, const char *unit, double factor, double offset);
const char *A2lCreateEnumConversion_(const char *name, const char *enum_description);

// Create measurements
void A2lCreateMeasurement_(const char *instance_name, const char *name, tA2lTypeId type, uint8_t ext, uint32_t addr, const char *unit_or_conversion, double min, double max,
                           const char *comment);
void A2lCreateMeasurementArray_(const char *instance_name, const char *name, tA2lTypeId type, int x_dim, int y_dim, uint8_t ext, uint32_t addr, const char *unit_or_conversion,
                                double phys_min, double phys_max, const char *comment);

// Create typedefs
void A2lTypedefBegin_(const char *name, uint32_t size, const char *comment);
void A2lTypedefEnd_(void);
void A2lTypedefComponent_(const char *name, const char *type_name, uint16_t x_dim, uint32_t offset);
void A2lTypedefMeasurementComponent_(const char *name, const char *type_name, uint32_t offset, const char *comment, const char *unit_or_conversion, double min, double max);
void A2lTypedefParameterComponent_(const char *name, const char *type_name, uint16_t x_dim, uint16_t y_dim, uint32_t offset, const char *comment, const char *unit, double min,
                                   double max, const char *x_axis, const char *y_axis);

// Create instances if typedefs
void A2lCreateTypedefMeasurementInstance_(const char *instance_name, const char *type_name, uint16_t x_dim, uint8_t ext, uint32_t addr, const char *comment);
void A2lCreateTypedefParameterInstance_(const char *instance_name, const char *type_name, uint8_t ext, uint32_t addr, const char *comment);

#ifdef __cplusplus
} // extern "C"
#endif
