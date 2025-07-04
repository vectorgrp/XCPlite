#pragma once
/* A2L.h */
/* Copyright(c) Vector Informatik GmbH.All rights reserved.
   Licensed under the MIT license.See LICENSE file in the project root for details. */

#include <assert.h>  // for assert
#include <stdbool.h> // for bool
#include <stdint.h>  // for uintxx_t

#include "../xcplib.h" // for tXcpEventId, tXcpCalSegIndex
#include "platform.h"  // for A2lOnceType

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

// Basic A2L types
typedef int8_t tA2lTypeId; // A2L type ID, positive for unsigned types, negative for signed types
#define A2L_TYPE_UINT8 (tA2lTypeId)1
#define A2L_TYPE_UINT16 (tA2lTypeId)2
#define A2L_TYPE_UINT32 (tA2lTypeId)4
#define A2L_TYPE_UINT64 (tA2lTypeId)8
#define A2L_TYPE_INT8 (tA2lTypeId) - 1
#define A2L_TYPE_INT16 (tA2lTypeId) - 2
#define A2L_TYPE_INT32 (tA2lTypeId) - 4
#define A2L_TYPE_INT64 (tA2lTypeId) - 8
#define A2L_TYPE_FLOAT (tA2lTypeId) - 9
#define A2L_TYPE_DOUBLE (tA2lTypeId) - 10
#define A2L_TYPE_UNDEFINED (tA2lTypeId)0

static_assert(sizeof(char) == 1, "sizeof(char) must be 1 bytes for A2L types to work correctly");
static_assert(sizeof(short) == 2, "sizeof(short) must be 2 bytes for A2L types to work correctly");
static_assert(sizeof(long long) == 8, "sizeof(long long) must be 8 bytes for A2L types to work correctly");

// Portable type detection macros for both C and C++
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
    static const tA2lTypeId value = (tA2lTypeId)(-sizeof(int));
};
template <> struct TypeId<unsigned int> {
    static const tA2lTypeId value = (tA2lTypeId)sizeof(int);
};
template <> struct TypeId<signed long> {
    static const tA2lTypeId value = (tA2lTypeId)(-sizeof(long));
};
template <> struct TypeId<unsigned long> {
    static const tA2lTypeId value = (tA2lTypeId)sizeof(long);
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
#define A2lGetArrayElementTypeId(array) A2lTypeTraits::GetTypeIdFromExpr((array)[0])
#define A2lGetArray2DElementTypeId(array) A2lTypeTraits::GetTypeIdFromExpr((array)[0][0])

// Alternative macro using decltype (C++11) for maximum robustness
#if __cplusplus >= 201103L
#define A2lGetTypeIdDecltype(expr) A2lTypeTraits::GetTypeId<decltype(expr)>()
#else
#error "C++11 or later is required for decltype-based type detection"
#endif

#else
#define A2lGetArrayElementTypeId(array) A2lGetTypeId((array)[0])
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
#define A2lGetTypeName_M(type) A2lGetA2lTypeName_M(A2lGetTypeId(type))
#define A2lGetTypeName_C(type) A2lGetA2lTypeName_C(A2lGetTypeId(type))
#define A2lGetRecordLayoutName(type) A2lGetRecordLayoutName_(A2lGetTypeId(type))

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
extern MUTEX gA2lMutex;

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// Set mode (address generation and event) for all following A2lCreateXxxx macros and functions
// Not thread safe !!!!!

// Set addressing mode by event name or calibration segment index
// Used by the macros with the identical name (without underscore)
void A2lSetSegmentAddrMode__i(tXcpCalSegIndex calseg_index, const uint8_t *calseg_instance);
void A2lSetRelativeAddrMode__s(const char *event_name, const uint8_t *base_addr);
void A2lSetRelativeAddrMode__i(tXcpEventId event_id, const uint8_t *base_addr);
void A2lSetStackAddrMode__s(const char *event_name, const uint8_t *stack_frame);
void A2lSetStackAddrMode__i(tXcpEventId event_id, const uint8_t *stack_frame);
void A2lSetAbsoluteAddrMode__s(const char *event_name);

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// Addressing mode
// Can be used without runtime A2L file generation

#ifndef get_stack_frame_pointer
#define get_stack_frame_pointer() (const uint8_t *)__builtin_frame_address(0)
#endif

// static inline const uint8_t *get_stack_frame_pointer_(void) {
//  #if defined(__x86_64__) || defined(_M_X64)
//      void *fp;
//      __asm__ volatile("movq %%rbp, %0" : "=r"(fp));
//      return (uint8_t *)fp;
//  #elif defined(__i386__) || defined(_M_IX86)
//      void *fp;
//      __asm__ volatile("movl %%ebp, %0" : "=r"(fp));
//      return (uint8_t *)fp;
//  #elif defined(__aarch64__)
//      void *fp;
//      __asm__ volatile("mov %0, x29" : "=r"(fp));
//      return (uint8_t *)fp;
//  #elif defined(__arm__)
//      void *fp;
//      __asm__ volatile("mov %0, fp" : "=r"(fp));
//      return (uint8_t *)fp;
//  #else
//      return (uint8_t *)__builtin_frame_address(0);
//  #endif
//}

// Set segment relative address mode
// Error if the segment index does not exist
#define A2lSetSegmentAddrMode(seg_index, seg_instance) A2lSetSegmentAddrMode__i(seg_index, (const uint8_t *)&seg_instance);

// Set addressing mode to relative for a given event 'event_name' and base address
// Error if the event does not exist
// Use in combination with DaqEvent(event_name)
#define A2lSetRelativeAddrMode(event_name, base_addr) A2lSetRelativeAddrMode__s(#event_name, (const uint8_t *)base_addr);
#define A2lSetRelativeAddrMode_s(event_name, base_addr) A2lSetRelativeAddrMode__s(event_name, (const uint8_t *)base_addr);

// Once
#define A2lOnceSetRelativeAddrMode(event_name, base_addr)                                                                                                                          \
    {                                                                                                                                                                              \
        static A2lOnceType a2l_mode_dyn_##event_name##_ = false;                                                                                                                   \
        if (A2lOnce_(&a2l_mode_dyn_##event_name##_))                                                                                                                               \
            A2lSetRelativeAddrMode__s(#event_name, (const uint8_t *)base_addr);                                                                                                    \
    }
#define A2lOnceSetRelativeAddrMode_s(event_name_string, base_addr)                                                                                                                 \
    {                                                                                                                                                                              \
        static A2lOnceType a2l_mode_dyn__ = false;                                                                                                                                 \
        if (A2lOnce_(&a2l_mode_dyn__))                                                                                                                                             \
            A2lSetRelativeAddrMode__s(event_name_string, (const uint8_t *)base_addr);                                                                                              \
    }

// Set addressing mode to stack and event 'event_name'
// Error if the event does not exist
// Use in combination with DaqEvent(event_name)
#define A2lSetStackAddrMode(event_name) A2lSetStackAddrMode__s(#event_name, get_stack_frame_pointer());
#define A2lSetStackAddrMode_s(event_name_string) A2lSetStackAddrMode__s(event_name_string, get_stack_frame_pointer());
#define A2lSetStackAddrMode_i(event_id) A2lSetStackAddrMode__i(event_id, get_stack_frame_pointer());

// Once
#define A2lOnceSetStackAddrMode(event_name)                                                                                                                                        \
    {                                                                                                                                                                              \
        static A2lOnceType a2l_mode_rel_##event_name##_ = false;                                                                                                                   \
        if (A2lOnce_(&a2l_mode_rel_##event_name##_))                                                                                                                               \
            A2lSetStackAddrMode__s(#event_name, get_stack_frame_pointer());                                                                                                        \
    }
#define A2lOnceSetStackAddrMode_s(event_name_string)                                                                                                                               \
    {                                                                                                                                                                              \
        static A2lOnceType a2l_mode_rel__ = false;                                                                                                                                 \
        if (A2lOnce_(&a2l_mode_rel__))                                                                                                                                             \
            A2lSetStackAddrMode__s(event_name_string, get_stack_frame_pointer());                                                                                                  \
    }

// Set addressing mode to absolute and event 'event_name'
// Error if the event does not exist
// Use in combination with DaqEvent(event_name)
#define A2lSetAbsoluteAddrMode(event_name) A2lSetAbsoluteAddrMode__s(#event_name);
#define A2lSetAbsoluteAddrMode_s(event_name_string) A2lSetAbsoluteAddrMode__s(event_name_string);
// Once
#define A2lOnceSetAbsoluteAddrMode(event_name)                                                                                                                                     \
    {                                                                                                                                                                              \
        static A2lOnceType a2l_mode_abs_##event_name##_ = false;                                                                                                                   \
        if (A2lOnce_(&a2l_mode_abs_##event_name##_))                                                                                                                               \
            A2lSetAbsoluteAddrMode__s(#event_name);                                                                                                                                \
    }
#define A2lOnceSetAbsoluteAddrMode_s(event_name_string)                                                                                                                            \
    {                                                                                                                                                                              \
        static A2lOnceType a2l_mode_abs__ = false;                                                                                                                                 \
        if (A2lOnce_(&a2l_mode_abs__))                                                                                                                                             \
            A2lSetAbsoluteAddrMode__s(event_name_string);                                                                                                                          \
    }

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// Create parameters in a calibration segment or in global memory

#define A2lCreateParameter(instance_name, name, comment, unit, min, max)                                                                                                           \
    {                                                                                                                                                                              \
        static A2lOnceType a2l_par_##name##_ = false;                                                                                                                              \
        if (A2lOnce_(&a2l_par_##name##_))                                                                                                                                          \
            A2lCreateParameter_(#instance_name "." #name, A2lGetTypeId(instance_name.name), A2lGetAddrExt_(), A2lGetAddr_((uint8_t *)&instance_name.name), comment, unit, min,     \
                                max);                                                                                                                                              \
    }

#define A2lCreateCurve(instance_name, name, xdim, comment, unit, min, max)                                                                                                         \
    {                                                                                                                                                                              \
        static A2lOnceType a2l_par_##name##_ = false;                                                                                                                              \
        if (A2lOnce_(&a2l_par_##name##_))                                                                                                                                          \
            A2lCreateCurve_(#instance_name "." #name, A2lGetArrayElementTypeId(instance_name.name), A2lGetAddrExt_(), A2lGetAddr_((uint8_t *)&instance_name.name[0]), xdim,        \
                            comment, unit, min, max);                                                                                                                              \
    }

#define A2lCreateMap(instance_name, name, xdim, ydim, comment, unit, min, max)                                                                                                     \
    {                                                                                                                                                                              \
        static A2lOnceType a2l_par_##name##_ = false;                                                                                                                              \
        if (A2lOnce_(&a2l_par_##name##_))                                                                                                                                          \
            A2lCreateMap_(#instance_name "." #name, A2lGetArray2DElementTypeId(instance_name.name), A2lGetAddrExt_(), A2lGetAddr_((uint8_t *)&instance_name.name[0][0]), xdim,     \
                          ydim, comment, unit, min, max);                                                                                                                          \
    }

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// Create conversions

#define A2lCreateLinearConversion(name, comment, unit, factor, offset)                                                                                                             \
    static const char *name = "NO_COMPU_METHOD";                                                                                                                                   \
    {                                                                                                                                                                              \
        static A2lOnceType a2l_conv_##name##_ = false;                                                                                                                             \
        if (A2lOnce_(&a2l_conv_##name##_))                                                                                                                                         \
            name = A2lCreateLinearConversion_(#name, comment, unit, factor, offset);                                                                                               \
    }

#define A2lCreateEnumConversion(name, description)                                                                                                                                 \
    static const char *name = "NO_COMPU_METHOD";                                                                                                                                   \
    {                                                                                                                                                                              \
        static A2lOnceType a2l_conv_##name##_ = false;                                                                                                                             \
        if (A2lOnce_(&a2l_conv_##name##_))                                                                                                                                         \
            name = A2lCreateEnumConversion_(#name, description);                                                                                                                   \
    }

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// Create measurements on stack or in global memory
// Measurements are registered once, it is allowed to use the following macros in local scope which is run multiple times

// Once mode
#define A2lCreateMeasurement(name, comment, unit)                                                                                                                                  \
    {                                                                                                                                                                              \
        static A2lOnceType a2l_##name##_ = false;                                                                                                                                  \
        if (A2lOnce_(&a2l_##name##_))                                                                                                                                              \
            A2lCreateMeasurement_(NULL, #name, A2lGetTypeId(name), A2lGetAddrExt_(), A2lGetAddr_((uint8_t *)&(name)), unit, 0.0, 0.0, comment);                                    \
    }

#define A2lCreatePhysMeasurement(name, comment, unit_or_conversion, min, max)                                                                                                      \
    {                                                                                                                                                                              \
        static A2lOnceType a2l_##name##_ = false;                                                                                                                                  \
        if (A2lOnce_(&a2l_##name##_))                                                                                                                                              \
            A2lCreateMeasurement_(NULL, #name, A2lGetTypeId(name), A2lGetAddrExt_(), A2lGetAddr_((uint8_t *)&(name)), unit_or_conversion, min, max, comment);                      \
    }

#define A2lCreateMeasurementArray(name, comment, unit_or_conversion)                                                                                                               \
    {                                                                                                                                                                              \
        static A2lOnceType a2l_##name##_ = false;                                                                                                                                  \
        if (A2lOnce_(&a2l_##name##_))                                                                                                                                              \
            A2lCreateMeasurementArray_(NULL, #name, A2lGetTypeId(name[0]), sizeof(name) / sizeof(name[0]), 1, A2lGetAddrExt_(), A2lGetAddr_(&name[0]), unit_or_conversion,         \
                                       comment);                                                                                                                                   \
    }

#define A2lCreateMeasurementMatrix(name, comment, unit_or_conversion)                                                                                                              \
    {                                                                                                                                                                              \
        static A2lOnceType a2l_##name##_ = false;                                                                                                                                  \
        if (A2lOnce_(&a2l_##name##_))                                                                                                                                              \
            A2lCreateMeasurementArray_(NULL, #name, A2lGetTypeId(name[0][0]), sizeof(name[0]) / sizeof(name[0][0]), sizeof(name) / sizeof(name[0]), A2lGetAddrExt_(),              \
                                       A2lGetAddr_(&name[0]), unit_or_conversion, comment);                                                                                        \
    }

// With instance name
#define A2lCreateMeasurementInstance(instance_name, name, comment, unit)                                                                                                           \
    {                                                                                                                                                                              \
        A2lCreateMeasurement_(instance_name, #name, A2lGetTypeId(name), A2lGetAddrExt_(), A2lGetAddr_((uint8_t *)&(name)), unit, 0.0, 0.0, comment);                               \
    }

#define A2lCreatePhysMeasurementInstance(instance_name, name, comment, unit_or_conversion, min, max)                                                                               \
    {                                                                                                                                                                              \
        A2lCreateMeasurement_(instance_name, #name, A2lGetTypeId(name), A2lGetAddrExt_(), A2lGetAddr_((uint8_t *)&(name)), unit_or_conversion, min, max, comment);                 \
    }

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// Create instances from typedefs

#define A2lCreateTypedefNamedInstance(name, instance, typeName, comment)                                                                                                           \
    {                                                                                                                                                                              \
        A2lCreateTypedefMeasurementInstance_(name, #typeName, 0, A2lGetAddrExt_(), A2lGetAddr_((uint8_t *)&instance), comment);                                                    \
    }

#define A2lCreateTypedefInstance(name, typeName, comment)                                                                                                                          \
    {                                                                                                                                                                              \
        static A2lOnceType a2l_##name##_ = false;                                                                                                                                  \
        if (A2lOnce_(&a2l_##name##_)) {                                                                                                                                            \
            A2lCreateTypedefMeasurementInstance_(#name, #typeName, 0, A2lGetAddrExt_(), A2lGetAddr_((uint8_t *)&name), comment);                                                   \
        }                                                                                                                                                                          \
    }

#define A2lCreateTypedefReference(name, typeName, comment)                                                                                                                         \
    {                                                                                                                                                                              \
        static A2lOnceType a2l_##name##_ = false;                                                                                                                                  \
        if (A2lOnce_(&a2l_##name##_)) {                                                                                                                                            \
            A2lCreateTypedefMeasurementInstance_(#name, #typeName, 0, A2lGetAddrExt_(), A2lGetAddr_((uint8_t *)name), comment);                                                    \
        }                                                                                                                                                                          \
    }

#define A2lCreateTypedefArray(name, typeName, dim, comment)                                                                                                                        \
    {                                                                                                                                                                              \
        static A2lOnceType a2l_##name##_ = false;                                                                                                                                  \
        if (A2lOnce_(&a2l_##name##_)) {                                                                                                                                            \
            A2lCreateTypedefMeasurementInstance_(#name, #typeName, dim, A2lGetAddrExt_(), A2lGetAddr_((uint8_t *)&name), comment);                                                 \
        }                                                                                                                                                                          \
    }

#define A2lCreateTypedefArrayReference(name, typeName, dim, comment)                                                                                                               \
    {                                                                                                                                                                              \
        static A2lOnceType a2l_##name##_ = false;                                                                                                                                  \
        if (A2lOnce_(&a2l_##name##_)) {                                                                                                                                            \
            A2lCreateTypedefMeasurementInstance_(#name, #typeName, dim, A2lGetAddrExt_(), A2lGetAddr_((uint8_t *)name), comment);                                                  \
        }                                                                                                                                                                          \
    }

#define A2lTypedefBegin(type_name, comment)                                                                                                                                        \
    {                                                                                                                                                                              \
        static A2lOnceType a2l_##type_name##_ = false;                                                                                                                             \
        if (A2lOnce_(&a2l_##type_name##_)) {                                                                                                                                       \
            A2lLock();                                                                                                                                                             \
            A2lTypedefBegin_(#type_name, (uint32_t)sizeof(type_name), comment);                                                                                                    \
        }                                                                                                                                                                          \
    }

#define A2lTypedefMeasurementComponent(field_name, typedef_name)                                                                                                                   \
    {                                                                                                                                                                              \
        static A2lOnceType a2l_##name##_ = false;                                                                                                                                  \
        if (A2lOnce_(&a2l_##name##_)) {                                                                                                                                            \
            typedef_name instance;                                                                                                                                                 \
            A2lTypedefComponent_(#field_name, A2lGetTypeName_M(instance.field_name), 1, ((uint8_t *)&(instance.field_name) - (uint8_t *)&instance));                               \
        }                                                                                                                                                                          \
    }

#define A2lTypedefParameterComponent(field_name, typeName, comment, unit, min, max)                                                                                                \
    {                                                                                                                                                                              \
        static A2lOnceType a2l_##field_name##_ = false;                                                                                                                            \
        if (A2lOnce_(&a2l_##field_name##_)) {                                                                                                                                      \
            typeName instance;                                                                                                                                                     \
            A2lTypedefParameterComponent_(#field_name, A2lGetRecordLayoutName(instance.field_name), 1, 1, ((uint8_t *)&(instance.field_name) - (uint8_t *)&instance), comment,     \
                                          unit, min, max, NULL, NULL);                                                                                                             \
        }                                                                                                                                                                          \
    }

#define A2lTypedefCurveComponent(field_name, typeName, x_dim, comment, unit, min, max)                                                                                             \
    {                                                                                                                                                                              \
        static A2lOnceType a2l_##field_name##_ = false;                                                                                                                            \
        if (A2lOnce_(&a2l_##field_name##_)) {                                                                                                                                      \
            typeName instance;                                                                                                                                                     \
            A2lTypedefParameterComponent_(#field_name, A2lGetRecordLayoutName(instance.field_name[0]), x_dim, 1, ((uint8_t *)&(instance.field_name) - (uint8_t *)&instance),       \
                                          comment, unit, min, max, NULL, NULL);                                                                                                    \
        }                                                                                                                                                                          \
    }

#define A2lTypedefCurveComponentWithSharedAxis(field_name, typeName, x_dim, comment, unit, min, max, x_axis)                                                                       \
    {                                                                                                                                                                              \
        static A2lOnceType a2l_##field_name##_ = false;                                                                                                                            \
        if (A2lOnce_(&a2l_##field_name##_)) {                                                                                                                                      \
            typeName instance;                                                                                                                                                     \
            A2lTypedefParameterComponent_(#field_name, A2lGetRecordLayoutName(instance.field_name[0]), x_dim, 1, ((uint8_t *)&(instance.field_name) - (uint8_t *)&instance),       \
                                          comment, unit, min, max, x_axis, NULL);                                                                                                  \
        }                                                                                                                                                                          \
    }

#define A2lTypedefMapComponent(field_name, typeName, x_dim, y_dim, comment, unit, min, max)                                                                                        \
    {                                                                                                                                                                              \
        static A2lOnceType a2l_##field_name##_ = false;                                                                                                                            \
        if (A2lOnce_(&a2l_##field_name##_)) {                                                                                                                                      \
            typeName instance;                                                                                                                                                     \
            A2lTypedefParameterComponent_(#field_name, A2lGetRecordLayoutName(instance.field_name[0][0]), x_dim, y_dim,                                                            \
                                          ((uint8_t *)&(instance.field_name) - (uint8_t *)&instance), comment, unit, min, max, NULL, NULL);                                        \
        }                                                                                                                                                                          \
    }

#define A2lTypedefMapComponentWithSharedAxis(field_name, typeName, x_dim, y_dim, comment, unit, min, max, x_axis, y_axis)                                                          \
    {                                                                                                                                                                              \
        static A2lOnceType a2l_##field_name##_ = false;                                                                                                                            \
        if (A2lOnce_(&a2l_##field_name##_)) {                                                                                                                                      \
            typeName instance;                                                                                                                                                     \
            A2lTypedefParameterComponent_(#field_name, A2lGetRecordLayoutName(instance.field_name[0][0]), x_dim, y_dim,                                                            \
                                          ((uint8_t *)&(instance.field_name) - (uint8_t *)&instance), comment, unit, min, max, x_axis, y_axis);                                    \
        }                                                                                                                                                                          \
    }

#define A2lTypedefAxisComponent(field_name, typeName, x_dim, comment, unit, min, max)                                                                                              \
    {                                                                                                                                                                              \
        static A2lOnceType a2l_##field_name##_ = false;                                                                                                                            \
        if (A2lOnce_(&a2l_##field_name##_)) {                                                                                                                                      \
            typeName instance;                                                                                                                                                     \
            A2lTypedefParameterComponent_(#field_name, A2lGetRecordLayoutName(instance.field_name[0]), x_dim, 0, ((uint8_t *)&(instance.field_name) - (uint8_t *)&instance),       \
                                          comment, unit, min, max, NULL, NULL);                                                                                                    \
        }                                                                                                                                                                          \
    }

#define A2lTypedefMeasurementArrayComponent(field_name, typedef_name)                                                                                                              \
    {                                                                                                                                                                              \
        static A2lOnceType a2l_##field_name##_ = false;                                                                                                                            \
        if (A2lOnce_(&a2l_##field_name##_)) {                                                                                                                                      \
            typedef_name instance;                                                                                                                                                 \
            A2lTypedefComponent_(#field_name, A2lGetTypeName_M(instance.field_name[0]), sizeof(instance.field_name) / sizeof(instance.field_name[0]),                              \
                                 ((uint8_t *)&(instance.field_name[0]) - (uint8_t *)&instance));                                                                                   \
        }                                                                                                                                                                          \
    }

#define A2lTypedefParameterArrayComponent(field_name, typeName, comment, unit, min, max)                                                                                           \
    {                                                                                                                                                                              \
        static A2lOnceType a2l_##field_name##_ = false;                                                                                                                            \
        if (A2lOnce_(&a2l_##field_name##_)) {                                                                                                                                      \
            typeName instance;                                                                                                                                                     \
            A2lTypedefComponent_(#field_name, A2lGetTypeName_C(instance.field_name[0]), sizeof(instance.field_name) / sizeof(instance.field_name[0]),                              \
                                 ((uint8_t *)&(instance.field_name[0]) - (uint8_t *)&instance));                                                                                   \
        }                                                                                                                                                                          \
    }

#define A2lTypedefParameterMatrixComponent(field_name, typeName, comment, unit, min, max)                                                                                          \
    {                                                                                                                                                                              \
        static A2lOnceType a2l_##field_name##_ = false;                                                                                                                            \
        if (A2lOnce_(&a2l_##field_name##_)) {                                                                                                                                      \
            typeName instance;                                                                                                                                                     \
            A2lTypedefComponent_(#field_name, A2lGetTypeName_C(instance.field_name[0][0]), sizeof(instance.field_name) / sizeof(instance.field_name[0]),                           \
                                 ((uint8_t *)&(instance.field_name[0]) - (uint8_t *)&instance));                                                                                   \
        }                                                                                                                                                                          \
    }

#define A2lTypedefComponent(field_name, field_type_name, field_dim, typedef_name)                                                                                                  \
    {                                                                                                                                                                              \
        static A2lOnceType a2l_##field_name##_ = false;                                                                                                                            \
        if (A2lOnce_(&a2l_##field_name##_)) {                                                                                                                                      \
            typedef_name instance;                                                                                                                                                 \
            A2lTypedefComponent_(#field_name, #field_type_name, field_dim, ((uint8_t *)&(instance.field_name) - (uint8_t *)&instance));                                            \
        }                                                                                                                                                                          \
    }

#define A2lTypedefEnd()                                                                                                                                                            \
    {                                                                                                                                                                              \
        static A2lOnceType a2l_once = false;                                                                                                                                       \
        if (A2lOnce_(&a2l_once)) {                                                                                                                                                 \
            A2lTypedefEnd_();                                                                                                                                                      \
            A2lUnlock();                                                                                                                                                           \
        }                                                                                                                                                                          \
    }

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// Create groups

void A2lBeginGroup(const char *name, const char *comment, bool is_parameter_group);
void A2lAddToGroup(const char *name);
void A2lEndGroup(void);

void A2lCreateParameterGroup(const char *name, int count, ...);
void A2lCreateParameterGroupFromList(const char *name, const char *pNames[], int count);

void A2lCreateMeasurementGroup(const char *name, int count, ...);
void A2lCreateMeasurementGroupFromList(const char *name, char *names[], uint32_t count);

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

// Init A2L generation
bool A2lInit(const char *a2l_filename, const char *a2l_projectname, const uint8_t *addr, uint16_t port, bool useTCP, bool finalize_on_connect, bool auto_groups);

// Finish A2L generation
bool A2lFinalize(void);

// Lock and unlock for thread safety
void A2lLock(void);
void A2lUnlock(void);

// --------------------------------------------------------------------------------------------
// Helper functions used in the for A2L generation macros

typedef volatile bool A2lOnceType;
bool A2lOnce_(A2lOnceType *once);

void A2lSetAbsAddrMode(tXcpEventId default_event_id);
void A2lSetDynAddrMode(tXcpEventId event_id, const uint8_t *base);
void A2lSetSegAddrMode(tXcpCalSegIndex calseg_index, const uint8_t *calseg_instance_addr);
void A2lSetRelAddrMode(tXcpEventId event_id, const uint8_t *base);

uint32_t A2lGetAddr_(const void *addr);
uint8_t A2lGetAddrExt_(void);

// Create measurements
const char *A2lCreateLinearConversion_(const char *name, const char *comment, const char *unit, double factor, double offset);
const char *A2lCreateEnumConversion_(const char *name, const char *enum_description);

void A2lCreateMeasurement_(const char *instance_name, const char *name, tA2lTypeId type, uint8_t ext, uint32_t addr, const char *unit_or_conversion, double min, double max,
                           const char *comment);

void A2lCreateMeasurementArray_(const char *instance_name, const char *name, tA2lTypeId type, int x_dim, int y_dim, uint8_t ext, uint32_t addr, const char *unit_or_conversion,
                                const char *comment);

// Create typedefs
void A2lTypedefBegin_(const char *name, uint32_t size, const char *comment);
void A2lTypedefComponent_(const char *name, const char *type_name, uint16_t x_dim, uint32_t offset);
void A2lTypedefParameterComponent_(const char *name, const char *type_name, uint16_t x_dim, uint16_t y_dim, uint32_t offset, const char *comment, const char *unit, double min,
                                   double max, const char *x_axis, const char *y_axis);
void A2lTypedefEnd_(void);

// CrA2lCreateTypedefMeasurementInstance_s
void A2lCreateTypedefMeasurementInstance_(const char *instance_name, const char *type_name, uint16_t x_dim, uint8_t ext, uint32_t addr, const char *comment);
void A2lCreateTypedefParameterInstance_(const char *instance_name, const char *type_name, uint8_t ext, uint32_t addr, const char *comment);

// Create parameters
void A2lCreateParameter_(const char *name, tA2lTypeId type, uint8_t ext, uint32_t addr, const char *comment, const char *unit, double min, double max);
void A2lCreateMap_(const char *name, tA2lTypeId type, uint8_t ext, uint32_t addr, uint32_t xdim, uint32_t ydim, const char *comment, const char *unit, double min, double max);
void A2lCreateCurve_(const char *name, tA2lTypeId type, uint8_t ext, uint32_t addr, uint32_t xdim, const char *comment, const char *unit, double min, double max);

#ifdef __cplusplus
} // extern "C"
#endif
