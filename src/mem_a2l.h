#pragma once
#define __A2L_H__

/*----------------------------------------------------------------------------
| File:
|   mem_a2l.h
|
| Description:
|   Public header for A2L generation
|
| Copyright (c) Vector Informatik GmbH. All rights reserved.
| See LICENSE file in the project root for details.
|
 ----------------------------------------------------------------------------*/

#include "platform.h" // for atomic_uint_fast8_t

// Test disable
// #undef OPTION_A2L_DISABLE
// #undef OPTION_A2L_WRITER
// #undef OPTION_A2L_CREATOR
// #define OPTION_A2L_DISABLE
// #define OPTION_A2L_WRITER
// #define OPTION_A2L_CREATOR

// There are 3 different options to configure runtime A2L generation:
// 1. OPTION_A2L_WRITER: Enables the A2L writer, needs a file system, generates the A2L files during runtime and provides it for upload by XCP client tools.
// 2. OPTION_A2L_CREATOR: Enables the A2L creator, does not need a file system, generates a A2L object list in memory which may be accessed by the XCP client.
// 3. OPTION_A2L_DISABLE: Disables all A2L generation macros completely with no side effects for the user code. A2l generation functions return immediately.

// Default is OPTION_A2L_WRITER
// Enables the a2l macros to generate A2L by using the file system and provide the A2L file for upload by XCP client tools
#if !defined(OPTION_A2L_WRITER) && !defined(OPTION_A2L_CREATOR) && !defined(OPTION_A2L_DISABLE)
#define OPTION_A2L_WRITER
#else
#if defined(OPTION_A2L_CREATOR) && defined(OPTION_A2L_DISABLE)
#error "OPTION_A2L_CREATOR and OPTION_A2L_DISABLE defined. This is not allowed."
#endif
#if defined(OPTION_A2L_WRITER) && defined(OPTION_A2L_DISABLE)
#error "OPTION_A2L_WRITER and OPTION_A2L_DISABLE defined. This is not allowed."
#endif
#if defined(OPTION_A2L_CREATOR) && defined(OPTION_A2L_WRITER)
#error "OPTION_A2L_CREATOR and OPTION_A2L_WRITER defined. This is not allowed."
#endif
#endif

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

#include "xcplib.h" // for tXcpEventId, tXcpCalSegIndex

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// A2L generation modes

#define A2L_MODE_IN_MEMORY 0x00           // Create A2l information in memory (flag A2L_MODE_WRITE_ONCE and A2L_MODE_WRITE_ALWAYS not set)
#define A2L_MODE_WRITE_ALWAYS 0x01        // Always write A2L file, overwrite existing file
#define A2L_MODE_WRITE_ONCE 0x02          // Write A2L file only once, do not overwrite existing file, use the binary persistence file to keep the A2L file valid
#define A2L_MODE_FINALIZE_ON_CONNECT 0x04 // Finalize A2L file on XCP connect
#define A2L_MODE_AUTO_GROUPS 0x08         // Automatically create groups for measurements and parameters

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

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// Binary A2l representation

#ifdef __cplusplus
extern "C" {
#endif

#define A2L_MAX_OBJECTS 256 // Maximum number of objects in the binary A2L description

#define A2L_OBJECT_TYPE_CONVERSION 0x01  // Object type tag for conversions
#define A2L_OBJECT_TYPE_PARAMETER 0x02   // Object type tag for parameters
#define A2L_OBJECT_TYPE_MEASUREMENT 0x03 // Object type tag for measurements

// Object (16 bytes)
#pragma pack(push, 1)
typedef struct {
    uint8_t tag;
    uint8_t addr_ext;
    uint16_t event_id;
    uint32_t addr;
    void *metadata; // Pointer to metadata structure a2l_parameter_t, a2l_conversion_t, a2l_measurement_t, etc.
}
#ifdef __APPLE__
__attribute__((aligned(8))) // Force 8-byte alignment on Apple platforms to avoid linker issues
#endif
a2l_object_t;
#pragma pack(pop)

// Header (16 bytes) + object list (count * 16 bytes    )
#pragma pack(push, 1)
typedef struct {
    uint32_t version; // 0x00 - Major - Minor - Patch
    uint32_t count;
    a2l_object_t objects[A2L_MAX_OBJECTS]; // Flexible array member for objects
}
#ifdef __APPLE__
__attribute__((aligned(8))) // Force 8-byte alignment on Apple platforms to avoid linker issues
#endif
a2l_object_list_t;
#pragma pack(pop)

// Conversion meta data structure definition
#pragma pack(push, 1)
typedef struct {
    const char *name_;
    const char *comment_;
    const char *unit_;
    const char *description_;
    double factor_;
    double offset_;
    uint8_t is_linear_; // 1 for linear, 0 for enum
}
#ifdef __APPLE__
__attribute__((aligned(8))) // Force 8-byte alignment on Apple platforms to avoid linker issues
#endif
a2l_conversion_t;
#pragma pack(pop)

// Parameter type meta data structure definition
#pragma pack(push, 1)
typedef struct {
    const char *name_;
    const char *comment_;
    const char *unit_;
    const char *x_axis_;
    const char *y_axis_;
    double min_;
    double max_;
    uint16_t x_dim_;
    uint16_t y_dim_;
    tA2lTypeId type_id_;
}
#ifdef __APPLE__
__attribute__((aligned(8))) // Force 8-byte alignment on Apple platforms to avoid linker issues
#endif
a2l_parameter_t;
#pragma pack(pop)

// Measurement type meta data structure definition
#pragma pack(push, 1)
typedef struct {
    const char *name_;
    const char *comment_;
    const char *unit_;
    double min_;
    double max_;
    uint16_t x_dim_;
    uint16_t y_dim_;
    tA2lTypeId type_id_;
}
#ifdef __APPLE__
__attribute__((aligned(8))) // Force 8-byte alignment on Apple platforms to avoid linker issues
#endif
a2l_measurement_t;
#pragma pack(pop)

bool A2lCreateObject(uint8_t tag, uint8_t addr_ext, uint16_t event_id, uint32_t addr, const void *metadata);
const a2l_object_list_t *A2lGetHeader(void);
void A2lPrintObjectList(void);

#ifdef __cplusplus
} // extern "C"
#endif

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// Reflection
// Portable basic type detection macros for both C and C++

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

// Helper macros for array element type detection (works with multi-dimensional arrays)
#ifdef __cplusplus
#define A2lGetArray1DElementTypeId(array) A2lTypeTraits::GetTypeIdFromExpr((array)[0])
#define A2lGetArray2DElementTypeId(array) A2lTypeTraits::GetTypeIdFromExpr((array)[0][0])

// Check availability of decltype (C++11)
#ifdef __cpp_decltype
// #if __cplusplus >= 199711
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

#ifndef OPTION_A2L_WRITER
// When OPTION_A2L_WRITER is not defined, replace functions with empty macros for user code
// but keep declarations available for library compilation
#ifndef A2L_INTERNAL_COMPILATION
#define A2lGetA2lTypeName(type) ""
#define A2lGetA2lTypeName_M(type) ""
#define A2lGetA2lTypeName_C(type) ""
#define A2lGetRecordLayoutName_(type) ""
#endif
#endif

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

#undef get_stack_frame_pointer
#ifndef get_stack_frame_pointer
#if defined(__GNUC__) || defined(__clang__)
#define get_stack_frame_pointer() (const uint8_t *)__builtin_frame_address(0)
#elif defined(_MSC_VER)
#define get_stack_frame_pointer() (const uint8_t *)_AddressOfReturnAddress()
#else
#error "get_stack_frame_pointer is not defined for this compiler. Please implement it."
#endif
#endif

#if defined(OPTION_A2L_CREATOR) || defined(OPTION_A2L_WRITER)

// Set segment relative address mode
// Error if the segment index or name does not exist
#define A2lSetSegmentAddrMode(seg_index, seg_instance) A2lSetSegmentAddrMode__i(seg_index, (const uint8_t *)&seg_instance);
#define A2lSetSegmentAddrMode_s(seg_name, seg_instance) A2lSetSegmentAddrMode__s(seg_name, (const uint8_t *)&seg_instance);

// Set addressing mode to relative for a given event 'event_name' and base address
// Error if the event id or name does not exist
#define A2lSetRelativeAddrMode(event_name, base_addr) A2lSetRelativeAddrMode__s(#event_name, (const uint8_t *)base_addr);
#define A2lSetRelativeAddrMode_s(event_name, base_addr) A2lSetRelativeAddrMode__s(event_name, (const uint8_t *)base_addr);
#define A2lSetRelativeAddrMode_i(event_id, base_addr) A2lSetRelativeAddrMode__i(event_id, (const uint8_t *)base_addr);

// Set addressing mode to stack and event 'event_name'
// Error if the event id or name does not exist
#define A2lSetStackAddrMode(event_name) A2lSetStackAddrMode__s(#event_name, get_stack_frame_pointer());
#define A2lSetStackAddrMode_s(event_name_string) A2lSetStackAddrMode__s(event_name_string, get_stack_frame_pointer());
#define A2lSetStackAddrMode_i(event_id) A2lSetStackAddrMode__i(event_id, get_stack_frame_pointer());

// Set addressing mode to absolute and event 'event_name'
// Error if the event id or name does not exist
#define A2lSetAbsoluteAddrMode(event_name) A2lSetAbsoluteAddrMode__s(#event_name);
#define A2lSetAbsoluteAddrMode_s(event_name_string) A2lSetAbsoluteAddrMode__s(event_name_string);
#define A2lSetAbsoluteAddrMode_i(event_id) A2lSetAbsoluteAddrMode__i(event_id);

#else

#define A2lSetSegmentAddrMode(seg_index, seg_instance)
#define A2lSetSegmentAddrMode_s(seg_index, seg_instance)
#define A2lSetRelativeAddrMode(event_name, base_addr)
#define A2lSetRelativeAddrMode_s(event_name, base_addr)
#define A2lSetRelativeAddrMode_i(event_id, base_addr)
#define A2lSetStackAddrMode(event_name)
#define A2lSetStackAddrMode_s(event_name_string)
#define A2lSetStackAddrMode_i(event_id)
#define A2lSetAbsoluteAddrMode(event_name)
#define A2lSetAbsoluteAddrMode_s(event_name_string)
#define A2lSetAbsoluteAddrMode_i(event_id)

#endif

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// Create parameters in calibration parameter segments or in global memory

#ifdef OPTION_A2L_CREATOR

#define A2lCreateParameter(name, comment, unit, min, max)                                                                                                                          \
    {                                                                                                                                                                              \
        static const a2l_parameter_t a2l_parameter_ = {.name_ = #name,                                                                                                             \
                                                       .comment_ = comment,                                                                                                        \
                                                       .unit_ = unit,                                                                                                              \
                                                       .x_axis_ = NULL,                                                                                                            \
                                                       .y_axis_ = NULL,                                                                                                            \
                                                       .min_ = min,                                                                                                                \
                                                       .max_ = max,                                                                                                                \
                                                       .x_dim_ = 1,                                                                                                                \
                                                       .y_dim_ = 1,                                                                                                                \
                                                       .type_id_ = A2lGetTypeId(name)};                                                                                            \
        A2lCreateObject(A2L_OBJECT_TYPE_PARAMETER, A2lGetAddrExt_(), 0, A2lGetAddr_((uint8_t *)&name), &a2l_parameter_);                                                           \
    }

#define A2lCreateCurve(name, xdim, comment, unit, min, max)                                                                                                                        \
    {                                                                                                                                                                              \
        static const a2l_parameter_t a2l_parameter_ = {.name_ = #name,                                                                                                             \
                                                       .comment_ = comment,                                                                                                        \
                                                       .unit_ = unit,                                                                                                              \
                                                       .x_axis_ = NULL,                                                                                                            \
                                                       .y_axis_ = NULL,                                                                                                            \
                                                       .min_ = min,                                                                                                                \
                                                       .max_ = max,                                                                                                                \
                                                       .x_dim_ = xdim,                                                                                                             \
                                                       .y_dim_ = 1,                                                                                                                \
                                                       .type_id_ = A2lGetArray1DElementTypeId(name)};                                                                              \
        A2lCreateObject(A2L_OBJECT_TYPE_PARAMETER, A2lGetAddrExt_(), 0, A2lGetAddr_((uint8_t *)&name), &a2l_parameter_);                                                           \
    }

#define A2lCreateCurveWithSharedAxis(name, xdim, comment, unit, min, max, x_axis)                                                                                                  \
    {                                                                                                                                                                              \
        static const a2l_parameter_t a2l_parameter_ = {.name_ = #name,                                                                                                             \
                                                       .comment_ = comment,                                                                                                        \
                                                       .unit_ = unit,                                                                                                              \
                                                       .x_axis_ = x_axis,                                                                                                          \
                                                       .y_axis_ = NULL,                                                                                                            \
                                                       .min_ = min,                                                                                                                \
                                                       .max_ = max,                                                                                                                \
                                                       .x_dim_ = xdim,                                                                                                             \
                                                       .y_dim_ = 1,                                                                                                                \
                                                       .type_id_ = A2lGetArray1DElementTypeId(name)};                                                                              \
        A2lCreateObject(A2L_OBJECT_TYPE_PARAMETER, A2lGetAddrExt_(), 0, A2lGetAddr_((uint8_t *)&name[0]), &a2l_parameter_);                                                        \
    }

#define A2lCreateAxis(name, xdim, comment, unit, min, max)                                                                                                                         \
    {                                                                                                                                                                              \
        static const a2l_parameter_t a2l_parameter_ = {.name_ = #name,                                                                                                             \
                                                       .comment_ = comment,                                                                                                        \
                                                       .unit_ = unit,                                                                                                              \
                                                       .x_axis_ = NULL,                                                                                                            \
                                                       .y_axis_ = NULL,                                                                                                            \
                                                       .min_ = min,                                                                                                                \
                                                       .max_ = max,                                                                                                                \
                                                       .x_dim_ = xdim,                                                                                                             \
                                                       .y_dim_ = 0,                                                                                                                \
                                                       .type_id_ = A2lGetArray1DElementTypeId(name)};                                                                              \
        A2lCreateObject(A2L_OBJECT_TYPE_PARAMETER, A2lGetAddrExt_(), 0, A2lGetAddr_((uint8_t *)&name[0]), &a2l_parameter_);                                                        \
    }

#define A2lCreateMap(name, xdim, ydim, comment, unit, min, max)                                                                                                                    \
    {                                                                                                                                                                              \
        static const a2l_parameter_t a2l_parameter_ = {.name_ = #name,                                                                                                             \
                                                       .comment_ = comment,                                                                                                        \
                                                       .unit_ = unit,                                                                                                              \
                                                       .x_axis_ = NULL,                                                                                                            \
                                                       .y_axis_ = NULL,                                                                                                            \
                                                       .min_ = min,                                                                                                                \
                                                       .max_ = max,                                                                                                                \
                                                       .x_dim_ = xdim,                                                                                                             \
                                                       .y_dim_ = ydim,                                                                                                             \
                                                       .type_id_ = A2lGetArray2DElementTypeId(name)};                                                                              \
        A2lCreateObject(A2L_OBJECT_TYPE_PARAMETER, A2lGetAddrExt_(), 0, A2lGetAddr_((uint8_t *)&name[0][0]), &a2l_parameter_);                                                     \
    }

#define A2lCreateMapWithSharedAxis(name, xdim, ydim, comment, unit, min, max, x_axis, y_axis)                                                                                      \
    {                                                                                                                                                                              \
        static const a2l_parameter_t a2l_parameter_ = {.name_ = #name,                                                                                                             \
                                                       .comment_ = comment,                                                                                                        \
                                                       .unit_ = unit,                                                                                                              \
                                                       .x_axis_ = x_axis,                                                                                                          \
                                                       .y_axis_ = y_axis,                                                                                                          \
                                                       .min_ = min,                                                                                                                \
                                                       .max_ = max,                                                                                                                \
                                                       .x_dim_ = xdim,                                                                                                             \
                                                       .y_dim_ = ydim,                                                                                                             \
                                                       .type_id_ = A2lGetArray2DElementTypeId(name)};                                                                              \
        A2lCreateObject(A2L_OBJECT_TYPE_PARAMETER, A2lGetAddrExt_(), 0, A2lGetAddr_((uint8_t *)&name[0][0]), &a2l_parameter_);                                                     \
    }

#endif

#ifdef OPTION_A2L_WRITER

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

#endif

#if !defined(OPTION_A2L_CREATOR) && !defined(OPTION_A2L_WRITER)

#define A2lCreateParameter(name, comment, unit, min, max)
#define A2lCreateCurve(name, xdim, comment, unit, min, max)
#define A2lCreateCurveWithSharedAxis(name, xdim, comment, unit, min, max, x_axis)
#define A2lCreateAxis(name, xdim, comment, unit, min, max)
#define A2lCreateMap(name, xdim, ydim, comment, unit, min, max)
#define A2lCreateMapWithSharedAxis(name, xdim, ydim, comment, unit, min, max, x_axis, y_axis)

#endif

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// Create conversions

#ifdef OPTION_A2L_CREATOR

#define A2lCreateLinearConversion(name, comment, unit, factor, offset)                                                                                                             \
    {                                                                                                                                                                              \
        static const a2l_conversion_t a2l_conversion_##name = {                                                                                                                    \
            .name_ = #name, .comment_ = comment, .unit_ = unit, .description_ = NULL, .factor_ = factor, .offset_ = offset, .is_linear_ = 1};                                      \
        A2lCreateObject(A2L_OBJECT_TYPE_CONVERSION, 0, 0, 0, &a2l_conversion_##name);                                                                                              \
    }

#define A2lCreateEnumConversion(name, description)                                                                                                                                 \
    {                                                                                                                                                                              \
        static const a2l_conversion_t a2l_conversion_##name = {                                                                                                                    \
            .name_ = #name, .comment_ = NULL, .unit_ = NULL, .description_ = description, .factor_ = 0.0, .offset_ = 0.0, .is_linear_ = 0};                                        \
        A2lCreateObject(A2L_OBJECT_TYPE_CONVERSION, 0, 0, 0, &a2l_conversion_##name);                                                                                              \
    }

#endif

#ifdef OPTION_A2L_WRITER

#define A2lCreateLinearConversion(name, comment, unit, factor, offset) A2lCreateLinearConversion_(#name, comment, unit, factor, offset)

#define A2lCreateEnumConversion(name, description) A2lCreateEnumConversion_(#name, description)

#endif

#if !defined(OPTION_A2L_CREATOR) && !defined(OPTION_A2L_WRITER)

#define A2lCreateLinearConversion(name, comment, unit, factor, offset)
#define A2lCreateEnumConversion(name, description)

#endif

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// Create measurements on stack or in global memory

#ifdef OPTION_A2L_CREATOR

#define A2lCreateMeasurement(name, comment)                                                                                                                                        \
    {                                                                                                                                                                              \
        static const a2l_measurement_t a2l_measurement_##name = {                                                                                                                  \
            .name_ = #name, .comment_ = comment, .unit_ = NULL, .min_ = 0.0, .max_ = 0.0, .x_dim_ = 1, .y_dim_ = 1, .type_id_ = A2lGetTypeId(name)};                               \
        A2lCreateObject(A2L_OBJECT_TYPE_MEASUREMENT, A2lGetAddrExt_(), 0, A2lGetAddr_((uint8_t *)&name), &a2l_measurement_##name);                                                 \
    }

#define A2lCreatePhysMeasurement(name, comment, unit_or_conversion, min, max)                                                                                                      \
    {                                                                                                                                                                              \
        static const a2l_measurement_t a2l_measurement_##name = {                                                                                                                  \
            .name_ = #name, .comment_ = comment, .unit_ = unit_or_conversion, .min_ = min, .max_ = max, .x_dim_ = 1, .y_dim_ = 1, .type_id_ = A2lGetTypeId(name)};                 \
        A2lCreateObject(A2L_OBJECT_TYPE_MEASUREMENT, A2lGetAddrExt_(), 0, A2lGetAddr_((uint8_t *)&name), &a2l_measurement_##name);                                                 \
    }

#define A2lCreateMeasurementArray(name, comment)                                                                                                                                   \
    {                                                                                                                                                                              \
        static const a2l_measurement_t a2l_measurement_##name = {.name_ = #name,                                                                                                   \
                                                                 .comment_ = comment,                                                                                              \
                                                                 .unit_ = NULL,                                                                                                    \
                                                                 .min_ = 0.0,                                                                                                      \
                                                                 .max_ = 0.0,                                                                                                      \
                                                                 .x_dim_ = sizeof(name) / sizeof(name[0]),                                                                         \
                                                                 .y_dim_ = 1,                                                                                                      \
                                                                 .type_id_ = A2lGetArray1DElementTypeId(name)};                                                                    \
        A2lCreateObject(A2L_OBJECT_TYPE_MEASUREMENT, A2lGetAddrExt_(), 0, A2lGetAddr_((uint8_t *)&name[0]), &a2l_measurement_##name);                                              \
    }

#define A2lCreateMeasurementMatrix(name, comment)                                                                                                                                  \
    {                                                                                                                                                                              \
        static const a2l_measurement_t a2l_measurement_##name = {.name_ = #name,                                                                                                   \
                                                                 .comment_ = comment,                                                                                              \
                                                                 .unit_ = NULL,                                                                                                    \
                                                                 .min_ = 0.0,                                                                                                      \
                                                                 .max_ = 0.0,                                                                                                      \
                                                                 .x_dim_ = sizeof(name[0]) / sizeof(name[0][0]),                                                                   \
                                                                 .y_dim_ = sizeof(name) / sizeof(name[0]),                                                                         \
                                                                 .type_id_ = A2lGetArray2DElementTypeId(name)};                                                                    \
        A2lCreateObject(A2L_OBJECT_TYPE_MEASUREMENT, A2lGetAddrExt_(), 0, A2lGetAddr_((uint8_t *)&name[0][0]), &a2l_measurement_##name);                                           \
    }

#define A2lCreatePhysMeasurementArray(name, comment, unit_or_conversion, min, max)                                                                                                 \
    {                                                                                                                                                                              \
        static const a2l_measurement_t a2l_measurement_##name = {.name_ = #name,                                                                                                   \
                                                                 .comment_ = comment,                                                                                              \
                                                                 .unit_ = unit_or_conversion,                                                                                      \
                                                                 .min_ = min,                                                                                                      \
                                                                 .max_ = max,                                                                                                      \
                                                                 .x_dim_ = sizeof(name) / sizeof(name[0]),                                                                         \
                                                                 .y_dim_ = 1,                                                                                                      \
                                                                 .type_id_ = A2lGetArray1DElementTypeId(name)};                                                                    \
        A2lCreateObject(A2L_OBJECT_TYPE_MEASUREMENT, A2lGetAddrExt_(), 0, A2lGetAddr_((uint8_t *)&name[0]), &a2l_measurement_##name);                                              \
    }

#define A2lCreatePhysMeasurementMatrix(name, comment, unit_or_conversion, min, max)                                                                                                \
    {                                                                                                                                                                              \
        static const a2l_measurement_t a2l_measurement_##name = {.name_ = #name,                                                                                                   \
                                                                 .comment_ = comment,                                                                                              \
                                                                 .unit_ = unit_or_conversion,                                                                                      \
                                                                 .min_ = min,                                                                                                      \
                                                                 .max_ = max,                                                                                                      \
                                                                 .x_dim_ = sizeof(name[0]) / sizeof(name[0][0]),                                                                   \
                                                                 .y_dim_ = sizeof(name) / sizeof(name[0]),                                                                         \
                                                                 .type_id_ = A2lGetArray2DElementTypeId(name)};                                                                    \
        A2lCreateObject(A2L_OBJECT_TYPE_MEASUREMENT, A2lGetAddrExt_(), 0, A2lGetAddr_((uint8_t *)&name[0][0]), &a2l_measurement_##name);                                           \
    }

// With instance name
#define A2lCreateMeasurementInstance(instance_name, name, comment)                                                                                                                 \
    {                                                                                                                                                                              \
        static const a2l_measurement_t a2l_measurement_##name = {                                                                                                                  \
            .name_ = #instance_name "." #name, .comment_ = comment, .unit_ = NULL, .min_ = 0.0, .max_ = 0.0, .x_dim_ = 1, .y_dim_ = 1, .type_id_ = A2lGetTypeId(name)};            \
        A2lCreateObject(A2L_OBJECT_TYPE_MEASUREMENT, A2lGetAddrExt_(), 0, A2lGetAddr_((uint8_t *)&name), &a2l_measurement_##name);                                                 \
    }

#define A2lCreatePhysMeasurementInstance(instance_name, name, comment, unit_or_conversion, min, max)                                                                               \
    {                                                                                                                                                                              \
        static const a2l_measurement_t a2l_measurement_##name = {.name_ = #instance_name "." #name,                                                                                \
                                                                 .comment_ = comment,                                                                                              \
                                                                 .unit_ = unit_or_conversion,                                                                                      \
                                                                 .min_ = min,                                                                                                      \
                                                                 .max_ = max,                                                                                                      \
                                                                 .x_dim_ = 1,                                                                                                      \
                                                                 .y_dim_ = 1,                                                                                                      \
                                                                 .type_id_ = A2lGetTypeId(name)};                                                                                  \
        A2lCreateObject(A2L_OBJECT_TYPE_MEASUREMENT, A2lGetAddrExt_(), 0, A2lGetAddr_((uint8_t *)&name), &a2l_measurement_##name);                                                 \
    }

#endif

#ifdef OPTION_A2L_WRITER

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
#define A2lCreateMeasurementInstance(instance_name, name, comment)                                                                                                                 \
    A2lCreateMeasurement_(instance_name, #name, A2lGetTypeId(name), A2lGetAddrExt_(), A2lGetAddr_((uint8_t *)&(name)), NULL, 0.0, 0.0, comment);

#define A2lCreatePhysMeasurementInstance(instance_name, name, comment, unit_or_conversion, min, max)                                                                               \
    A2lCreateMeasurement_(instance_name, #name, A2lGetTypeId(name), A2lGetAddrExt_(), A2lGetAddr_((uint8_t *)&(name)), unit_or_conversion, min, max, comment);

#endif

#if !defined(OPTION_A2L_CREATOR) && !defined(OPTION_A2L_WRITER)

#define A2lCreateMeasurement(name, comment)
#define A2lCreatePhysMeasurement(name, comment, unit_or_conversion, min, max)
#define A2lCreateMeasurementArray(name, comment)
#define A2lCreateMeasurementMatrix(name, comment)
#define A2lCreatePhysMeasurementArray(name, comment, unit_or_conversion, min, max)
#define A2lCreatePhysMeasurementMatrix(name, comment, unit_or_conversion, min, max)
#define A2lCreateMeasurementInstance(instance_name, name, comment)
#define A2lCreatePhysMeasurementInstance(instance_name, name, comment, unit_or_conversion, min, max)

#endif

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// Create instances from typedefs

#ifdef OPTION_A2L_WRITER

// Single instance of typedef
// A2L instance name and symbol name are the same
#define A2lCreateTypedefInstance(name, typeName, comment) A2lCreateTypedefMeasurementInstance_(#name, #typeName, 0, A2lGetAddrExt_(), A2lGetAddr_((uint8_t *)&name), comment);

// Single instance of typedef
// A2L instance name and symbol name are different
#define A2lCreateTypedefNamedInstance(name, instance, typeName, comment)                                                                                                           \
    A2lCreateTypedefMeasurementInstance_(name, #typeName, 0, A2lGetAddrExt_(), A2lGetAddr_((uint8_t *)&instance), comment);

// Array of typedef instances
#define A2lCreateTypedefArray(name, typeName, dim, comment) A2lCreateTypedefMeasurementInstance_(#name, #typeName, dim, A2lGetAddrExt_(), A2lGetAddr_((uint8_t *)&name), comment);

// Pointer to typedef instance
#define A2lCreateTypedefReference(name, typeName, comment) A2lCreateTypedefMeasurementInstance_(#name, #typeName, 0, A2lGetAddrExt_(), A2lGetAddr_((uint8_t *)name), comment);

// Pointer to array of typedef instances
#define A2lCreateTypedefArrayReference(name, typeName, dim, comment)                                                                                                               \
    A2lCreateTypedefMeasurementInstance_(#name, #typeName, dim, A2lGetAddrExt_(), A2lGetAddr_((uint8_t *)name), comment);

#else

#define A2lCreateTypedefInstance(name, typeName, comment)
#define A2lCreateTypedefNamedInstance(name, instance, typeName, comment)
#define A2lCreateTypedefArray(name, typeName, dim, comment)
#define A2lCreateTypedefReference(name, typeName, comment)
#define A2lCreateTypedefArrayReference(name, typeName, dim, comment)

#endif

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// Create typedefs and typedef components

#ifdef OPTION_A2L_WRITER

#define A2lTypedefBegin(type_name, comment) A2lTypedefBegin_(#type_name, (uint32_t)sizeof(type_name), comment);

#define A2lTypedefComponent(field_name, field_type_name, field_dim, typedef_name)                                                                                                  \
    A2lTypedefComponent_(#field_name, #field_type_name, field_dim, offsetof(typedef_name, field_name));

#define A2lTypedefEnd() A2lTypedefEnd_()

#define _offsetof(i, f) ((uint32_t)((uint8_t *)&i.f - (uint8_t *)&i))

// Measurement components

#define A2lTypedefMeasurementComponent(field_name, typedef_name)                                                                                                                   \
    {                                                                                                                                                                              \
        typedef_name instance;                                                                                                                                                     \
        A2lTypedefComponent_(#field_name, A2lGetTypeName_M(instance.field_name), 1, _offsetof(instance, field_name));                                                              \
    }

#define A2lTypedefPhysMeasurementComponent(field_name, typedef_name, comment, unit_or_conversion, min, max)                                                                        \
    {                                                                                                                                                                              \
        typedef_name instance;                                                                                                                                                     \
        A2lTypedefMeasurementComponent_(#field_name, A2lGetTypeName(instance.field_name), _offsetof(instance, field_name), comment, unit_or_conversion, min, max);                 \
    }

#define A2lTypedefMeasurementArrayComponent(field_name, typedef_name)                                                                                                              \
    {                                                                                                                                                                              \
        typedef_name instance;                                                                                                                                                     \
        A2lTypedefComponent_(#field_name, A2lGetTypeName1D_M(instance.field_name), sizeof(instance.field_name) / sizeof(instance.field_name[0]), _offsetof(instance, field_name)); \
    }

// Parameter components

#define A2lTypedefParameterComponent(field_name, typedef_name, comment, unit, min, max)                                                                                            \
    {                                                                                                                                                                              \
        typedef_name instance;                                                                                                                                                     \
        A2lTypedefParameterComponent_(#field_name, A2lGetRecordLayoutName(instance.field_name), 1, 1, _offsetof(instance, field_name), comment, unit, min, max, NULL, NULL);       \
    }

#define A2lTypedefCurveComponent(field_name, typedef_name, x_dim, comment, unit, min, max)                                                                                         \
    {                                                                                                                                                                              \
        typedef_name instance;                                                                                                                                                     \
        A2lTypedefParameterComponent_(#field_name, A2lGetRecordLayoutName1D(instance.field_name), x_dim, 1, _offsetof(instance, field_name), comment, unit, min, max, NULL, NULL); \
    }

#define A2lTypedefCurveComponentWithSharedAxis(field_name, typedef_name, x_dim, comment, unit, min, max, x_axis)                                                                   \
    {                                                                                                                                                                              \
        typedef_name instance;                                                                                                                                                     \
        A2lTypedefParameterComponent_(#field_name, A2lGetRecordLayoutName1D(instance.field_name), x_dim, 1, _offsetof(instance, field_name), comment, unit, min, max, x_axis,      \
                                      NULL);                                                                                                                                       \
    }

#define A2lTypedefMapComponent(field_name, typedef_name, x_dim, y_dim, comment, unit, min, max)                                                                                    \
    {                                                                                                                                                                              \
        typedef_name instance;                                                                                                                                                     \
        A2lTypedefParameterComponent_(#field_name, A2lGetRecordLayoutName2D(instance.field_name), x_dim, y_dim, _offsetof(instance, field_name), comment, unit, min, max, NULL,    \
                                      NULL);                                                                                                                                       \
    }

#define A2lTypedefMapComponentWithSharedAxis(field_name, typedef_name, x_dim, y_dim, comment, unit, min, max, x_axis, y_axis)                                                      \
    {                                                                                                                                                                              \
        typedef_name instance;                                                                                                                                                     \
        A2lTypedefParameterComponent_(#field_name, A2lGetRecordLayoutName2D(instance.field_name), x_dim, y_dim, _offsetof(instance, field_name), comment, unit, min, max, x_axis,  \
                                      y_axis);                                                                                                                                     \
    }

#define A2lTypedefAxisComponent(field_name, typedef_name, x_dim, comment, unit, min, max)                                                                                          \
    {                                                                                                                                                                              \
        typedef_name instance;                                                                                                                                                     \
        A2lTypedefParameterComponent_(#field_name, A2lGetRecordLayoutName1D(instance.field_name), x_dim, 0, _offsetof(instance, field_name), comment, unit, min, max, NULL, NULL); \
    }

#else

#define A2lTypedefBegin(type_name, comment)
#define A2lTypedefComponent(field_name, field_type_name, field_dim, typedef_name)
#define A2lTypedefEnd()
#define _offsetof(i, f) ((uint32_t)((uint8_t *)&i.f - (uint8_t *)&i))
#define A2lTypedefMeasurementComponent(field_name, typedef_name)
#define A2lTypedefPhysMeasurementComponent(field_name, typedef_name, comment, unit_or_conversion, min, max)
#define A2lTypedefMeasurementArrayComponent(field_name, typedef_name)
#define A2lTypedefParameterComponent(field_name, typedef_name, comment, unit, min, max)
#define A2lTypedefCurveComponent(field_name, typedef_name, x_dim, comment, unit, min, max)
#define A2lTypedefCurveComponentWithSharedAxis(field_name, typedef_name, x_dim, comment, unit, min, max, x_axis)
#define A2lTypedefMapComponent(field_name, typedef_name, x_dim, y_dim, comment, unit, min, max)
#define A2lTypedefMapComponentWithSharedAxis(field_name, typedef_name, x_dim, y_dim, comment, unit, min, max, x_axis, y_axis)
#define A2lTypedefAxisComponent(field_name, typedef_name, x_dim, comment, unit, min, max)

#endif

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

#ifndef OPTION_A2L_WRITER
#ifndef A2L_INTERNAL_COMPILATION
#define A2lRstAddrMode()
#define A2lSetDynAddrMode(event_id, base)
#define A2lSetRelAddrMode(event_id, base)
#define A2lSetAbsAddrMode(default_event_id)
#define A2lSetSegAddrMode(calseg_index, calseg_instance_addr)
#endif
#endif

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

#ifndef OPTION_A2L_WRITER
#ifndef A2L_INTERNAL_COMPILATION
#define A2lBeginGroup(name, comment, is_parameter_group)
#define A2lAddToGroup(name)
#define A2lEndGroup()
#define A2lCreateParameterGroup(name, count, ...)
#define A2lCreateParameterGroupFromList(name, pNames, count)
#define A2lCreateMeasurementGroup(name, count, ...)
#define A2lCreateMeasurementGroupFromList(name, names, count)
#endif
#endif

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

/// Init A2L generation
/// If the A2l file aready exists and matches the current EPK matches, if yes, load the binary persistence file
/// If not, prepare the A2L file and start the runtime generation process
/// @param a2l_projectname Name of the A2L project, used to build the A2L and BIN file name
/// @param a2l_version Version string of the A2L project, used to build the A2L and BIN file name, can be NULL to generate a version based on time and date
/// @param addr IP Address Used for IF_DATA XCP
/// @param port Port Used for IF_DATA XCP
/// @param useTCP Protocol Used for IF_DATA XCP
/// @param mode
///  A2L_MODE_FORCE_GENERATION Force generation of the A2L file, even if it already exists and matches the EPK
///  A2L_MODE_FINALIZE_ON_CONNECT Finalize the A2L file on XCP client connect, if false, the A2L file has to finalized manually
///  A2L_MODE_AUTO_GROUPS Enable automatic grouping of parameters (per segment) and measurements (per event), if false, grouping must be done manually
/// @return true on success, false on failure
bool A2lInit(const char *a2l_projectname, const char *a2l_version, const uint8_t *addr, uint16_t port, bool useTCP, uint8_t mode);

/// Finish A2L generation
/// Finalize the A2L file, write the binary persistence file
bool A2lFinalize(void);

// --------------------------------------------------------------------------------------------
// Helper functions used in the by A2L generation macros

// Set addressing mode by event name or calibration segment index
// Used by the macros with the identical name (one underscore)
void A2lSetSegmentAddrMode__i(tXcpCalSegIndex calseg_index, const uint8_t *calseg_instance);
void A2lSetSegmentAddrMode__s(const char *calseg_name, const uint8_t *calseg_instance);
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
void A2lCreateMeasurementArray_(const char *instance_name, const char *name, tA2lTypeId type, uint16_t x_dim, uint16_t y_dim, uint8_t ext, uint32_t addr,
                                const char *unit_or_conversion, double phys_min, double phys_max, const char *comment);

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
