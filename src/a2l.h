#pragma once
/* A2L.h */
/* Copyright(c) Vector Informatik GmbH.All rights reserved.
   Licensed under the MIT license.See LICENSE file in the project root for details. */

#include <assert.h>  // for assert
#include <stdbool.h> // for bool
#include <stdint.h>  // for uintxx_t

#include "../xcplib.h" // for tXcpEventId, tXcpCalSegIndex
#include "platform.h"  // for atomic_bool

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

// Macro to generate type
// A2L type
#define A2lGetTypeId(type)                                                                                                                                                         \
    _Generic((type),                                                                                                                                                               \
        signed char: A2L_TYPE_INT8,                                                                                                                                                \
        unsigned char: A2L_TYPE_UINT8,                                                                                                                                             \
        bool: A2L_TYPE_UINT8,                                                                                                                                                      \
        signed short: A2L_TYPE_INT16,                                                                                                                                              \
        unsigned short: A2L_TYPE_UINT16,                                                                                                                                           \
        signed int: (tA2lTypeId)(-sizeof(int)),                                                                                                                                    \
        unsigned int: (tA2lTypeId)sizeof(int),                                                                                                                                     \
        signed long: (tA2lTypeId)(-sizeof(long)),                                                                                                                                  \
        unsigned long: (tA2lTypeId)sizeof(long),                                                                                                                                   \
        signed long long: A2L_TYPE_INT64,                                                                                                                                          \
        unsigned long long: A2L_TYPE_UINT64,                                                                                                                                       \
        float: A2L_TYPE_FLOAT,                                                                                                                                                     \
        double: A2L_TYPE_DOUBLE,                                                                                                                                                   \
        default: A2L_TYPE_UNDEFINED)

// Macros to generate type names as static char* string
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
void A2lSetSegmentAddrMode_(tXcpCalSegIndex calseg_index, const uint8_t *calseg_instance); // Calibration segment relative addressing mode
void A2lSetRelativeAddrMode_(const char *event_name, const uint8_t *stack_frame_pointer);
void A2lSetAbsoluteAddrMode_(const char *event_name);

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// Stack frame relative addressing mode
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
#define A2lSetSegmentAddrMode(seg_index, seg_instance) A2lSetSegmentAddrMode_(seg_index, (const uint8_t *)&seg_instance);

// Set addressing mode to relative for a given event 'name' and base address
// Error if the event does not exist
// Use in combination with DaqEvent(name)
#define A2lSetRelativeAddrMode(name, base_addr)                                                                                                                                    \
    {                                                                                                                                                                              \
        static atomic_bool a2l_mode_rel_##name##_ = false;                                                                                                                         \
        if (A2lOnce_(&a2l_mode_rel_##name##_))                                                                                                                                     \
            A2lSetRelativeAddrMode_(#name, (const uint8_t *)base_addr);                                                                                                            \
    }

// Set addressing mode to stack and event 'name'
// Error if the event does not exist
// Use in combination with DaqEvent(name)
#define A2lSetStackAddrMode(name) A2lSetRelativeAddrMode(name, get_stack_frame_pointer());

// Set addressing mode to absolute and event 'name'
// Error if the event does not exist
// Use in combination with DaqEvent(name)
#define A2lSetAbsoluteAddrMode(name)                                                                                                                                               \
    {                                                                                                                                                                              \
        static atomic_bool a2l_mode_abs_##name##_ = false;                                                                                                                         \
        if (A2lOnce_(&a2l_mode_abs_##name##_))                                                                                                                                     \
            A2lSetAbsoluteAddrMode_(#name);                                                                                                                                        \
    }

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// Create parameters in a calibration segment or in global memory

#define A2lCreateParameter(instance_name, name, comment, unit, min, max)                                                                                                           \
    {                                                                                                                                                                              \
        static atomic_bool a2l_par_##name##_ = false;                                                                                                                              \
        if (A2lOnce_(&a2l_par_##name##_))                                                                                                                                          \
            A2lCreateParameter_(#instance_name "." #name, A2lGetTypeId(instance_name.name), A2lGetAddrExt_(), A2lGetAddr_((uint8_t *)&instance_name.name), comment, unit, min,     \
                                max);                                                                                                                                              \
    }

#define A2lCreateCurve(instance_name, name, xdim, comment, unit, min, max)                                                                                                         \
    {                                                                                                                                                                              \
        static atomic_bool a2l_par_##name##_ = false;                                                                                                                              \
        if (A2lOnce_(&a2l_par_##name##_))                                                                                                                                          \
            A2lCreateCurve_(#instance_name "." #name, A2lGetTypeId(instance_name.name[0]), A2lGetAddrExt_(), A2lGetAddr_((uint8_t *)&instance_name.name[0]), xdim, comment, unit,  \
                            min, max);                                                                                                                                             \
    }

#define A2lCreateMap(instance_name, name, xdim, ydim, comment, unit, min, max)                                                                                                     \
    {                                                                                                                                                                              \
        static atomic_bool a2l_par_##name##_ = false;                                                                                                                              \
        if (A2lOnce_(&a2l_par_##name##_))                                                                                                                                          \
            A2lCreateMap_(#instance_name "." #name, A2lGetTypeId(instance_name.name[0][0]), A2lGetAddrExt_(), A2lGetAddr_((uint8_t *)&instance_name.name[0][0]), xdim, ydim,       \
                          comment, unit, min, max);                                                                                                                                \
    }

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// Create conversions

#define A2lCreateLinearConversion(name, comment, unit, factor, offset) A2lCreateLinearConversion_(#name, comment, unit, factor, offset);

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// Create measurements on stack or in global memory
// Measurements are registered once, it is allowed to use the following macros in local scope which is run multiple times

#define A2lCreateMeasurement(name, comment, unit)                                                                                                                                  \
    {                                                                                                                                                                              \
        static atomic_bool a2l_##name##_ = false;                                                                                                                                  \
        if (A2lOnce_(&a2l_##name##_))                                                                                                                                              \
            A2lCreateMeasurement_(NULL, #name, A2lGetTypeId(name), A2lGetAddrExt_(), A2lGetAddr_((uint8_t *)&(name)), unit, 0.0, 0.0, comment);                                    \
    }

#define A2lCreatePhysMeasurement(name, comment, unit_or_conversion, min, max)                                                                                                      \
    {                                                                                                                                                                              \
        static atomic_bool a2l_##name##_ = false;                                                                                                                                  \
        if (A2lOnce_(&a2l_##name##_))                                                                                                                                              \
            A2lCreateMeasurement_(NULL, #name, A2lGetTypeId(name), A2lGetAddrExt_(), A2lGetAddr_((uint8_t *)&(name)), unit_or_conversion, min, max, comment);                      \
    }

// Thread safe
// Create thread local measurement instance, combine with XcpCreateEventInstance() and DaqEventInstance()
#define A2lCreateMeasurementInstance(instance_name, event, name, comment, unit_or_conversion)                                                                                      \
    {                                                                                                                                                                              \
        mutexLock(&gA2lMutex);                                                                                                                                                     \
        A2lSetDynAddrMode_(event, (const uint8_t *)&event);                                                                                                                        \
        A2lCreateMeasurement_(instance_name, #name, A2lGetTypeId(name), A2lGetAddrExt_(), A2lGetAddr_((uint8_t *)&(name)), unit_or_conversion, 0.0, 0.0, comment);                 \
        mutexUnlock(&gA2lMutex);                                                                                                                                                   \
    }

#define A2lCreateMeasurementArray(name, comment, unit_or_conversion)                                                                                                               \
    {                                                                                                                                                                              \
        static atomic_bool a2l_##name##_ = false;                                                                                                                                  \
        if (A2lOnce_(&a2l_##name##_))                                                                                                                                              \
            A2lCreateMeasurementArray_(NULL, #name, A2lGetTypeId(name[0]), sizeof(name) / sizeof(name[0]), 1, A2lGetAddrExt_(), A2lGetAddr_(&name[0]), unit_or_conversion,         \
                                       comment);                                                                                                                                   \
    }

#define A2lCreateMeasurementMatrix(name, comment, unit_or_conversion)                                                                                                              \
    {                                                                                                                                                                              \
        static atomic_bool a2l_##name##_ = false;                                                                                                                                  \
        if (A2lOnce_(&a2l_##name##_))                                                                                                                                              \
            A2lCreateMeasurementArray_(NULL, #name, A2lGetTypeId(name[0][0]), sizeof(name[0]) / sizeof(name[0][0]), sizeof(name) / sizeof(name[0]), A2lGetAddrExt_(),              \
                                       A2lGetAddr_(&name[0]), unit_or_conversion, comment);                                                                                        \
    }

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// Create typedefs and typedef components

#define A2lCreateTypedefInstance(name, typeName, comment)                                                                                                                          \
    {                                                                                                                                                                              \
        static atomic_bool a2l_##name##_ = false;                                                                                                                                  \
        if (A2lOnce_(&a2l_##name##_)) {                                                                                                                                            \
            A2lCreateTypedefInstance_(#name, #typeName, 0, A2lGetAddrExt_(), A2lGetAddr_((uint8_t *)&name), comment);                                                              \
        }                                                                                                                                                                          \
    }

#define A2lCreateTypedefReference(name, typeName, comment)                                                                                                                         \
    {                                                                                                                                                                              \
        static atomic_bool a2l_##name##_ = false;                                                                                                                                  \
        if (A2lOnce_(&a2l_##name##_)) {                                                                                                                                            \
            A2lCreateTypedefInstance_(#name, #typeName, 0, A2lGetAddrExt_(), A2lGetAddr_((uint8_t *)name), comment);                                                               \
        }                                                                                                                                                                          \
    }

#define A2lCreateTypedefArray(name, typeName, dim, comment)                                                                                                                        \
    {                                                                                                                                                                              \
        static atomic_bool a2l_##name##_ = false;                                                                                                                                  \
        if (A2lOnce_(&a2l_##name##_)) {                                                                                                                                            \
            A2lCreateTypedefInstance_(#name, #typeName, dim, A2lGetAddrExt_(), A2lGetAddr_((uint8_t *)&name), comment);                                                            \
        }                                                                                                                                                                          \
    }

#define A2lCreateTypedefArrayReference(name, typeName, dim, comment)                                                                                                               \
    {                                                                                                                                                                              \
        static atomic_bool a2l_##name##_ = false;                                                                                                                                  \
        if (A2lOnce_(&a2l_##name##_)) {                                                                                                                                            \
            A2lCreateTypedefInstance_(#name, #typeName, dim, A2lGetAddrExt_(), A2lGetAddr_((uint8_t *)name), comment);                                                             \
        }                                                                                                                                                                          \
    }

#define A2lTypedefBegin(type_name, comment)                                                                                                                                        \
    {                                                                                                                                                                              \
        static atomic_bool a2l_##type_name##_ = false;                                                                                                                             \
        if (A2lOnce_(&a2l_##type_name##_)) {                                                                                                                                       \
            A2lTypedefBegin_(#type_name, (uint32_t)sizeof(type_name), comment);                                                                                                    \
        }                                                                                                                                                                          \
    }

#define A2lTypedefMeasurementComponent(field_name, typedef_name)                                                                                                                   \
    {                                                                                                                                                                              \
        static atomic_bool a2l_##name##_ = false;                                                                                                                                  \
        if (A2lOnce_(&a2l_##name##_)) {                                                                                                                                            \
            typedef_name instance;                                                                                                                                                 \
            A2lTypedefComponent_(#field_name, A2lGetTypeName_M(instance.field_name), 1, ((uint8_t *)&(instance.field_name) - (uint8_t *)&instance));                               \
        }                                                                                                                                                                          \
    }

#define A2lTypedefParameterComponent(field_name, typeName, comment, unit, min, max)                                                                                                \
    {                                                                                                                                                                              \
        static atomic_bool a2l_##field_name##_ = false;                                                                                                                            \
        if (A2lOnce_(&a2l_##field_name##_)) {                                                                                                                                      \
            typeName instance;                                                                                                                                                     \
            A2lTypedefParameterComponent_(#field_name, A2lGetRecordLayoutName(instance.field_name), 1, 1, ((uint8_t *)&(instance.field_name) - (uint8_t *)&instance), comment,     \
                                          unit, min, max, NULL, NULL);                                                                                                             \
        }                                                                                                                                                                          \
    }

#define A2lTypedefCurveComponent(field_name, typeName, x_dim, comment, unit, min, max)                                                                                             \
    {                                                                                                                                                                              \
        static atomic_bool a2l_##field_name##_ = false;                                                                                                                            \
        if (A2lOnce_(&a2l_##field_name##_)) {                                                                                                                                      \
            typeName instance;                                                                                                                                                     \
            A2lTypedefParameterComponent_(#field_name, A2lGetRecordLayoutName(instance.field_name[0]), x_dim, 1, ((uint8_t *)&(instance.field_name) - (uint8_t *)&instance),       \
                                          comment, unit, min, max, NULL, NULL);                                                                                                    \
        }                                                                                                                                                                          \
    }

#define A2lTypedefCurveComponentWithSharedAxis(field_name, typeName, x_dim, comment, unit, min, max, x_axis)                                                                       \
    {                                                                                                                                                                              \
        static atomic_bool a2l_##field_name##_ = false;                                                                                                                            \
        if (A2lOnce_(&a2l_##field_name##_)) {                                                                                                                                      \
            typeName instance;                                                                                                                                                     \
            A2lTypedefParameterComponent_(#field_name, A2lGetRecordLayoutName(instance.field_name[0]), x_dim, 1, ((uint8_t *)&(instance.field_name) - (uint8_t *)&instance),       \
                                          comment, unit, min, max, x_axis, NULL);                                                                                                  \
        }                                                                                                                                                                          \
    }

#define A2lTypedefMapComponent(field_name, typeName, x_dim, y_dim, comment, unit, min, max)                                                                                        \
    {                                                                                                                                                                              \
        static atomic_bool a2l_##field_name##_ = false;                                                                                                                            \
        if (A2lOnce_(&a2l_##field_name##_)) {                                                                                                                                      \
            typeName instance;                                                                                                                                                     \
            A2lTypedefParameterComponent_(#field_name, A2lGetRecordLayoutName(instance.field_name[0][0]), x_dim, y_dim,                                                            \
                                          ((uint8_t *)&(instance.field_name) - (uint8_t *)&instance), comment, unit, min, max, NULL, NULL);                                        \
        }                                                                                                                                                                          \
    }

#define A2lTypedefMapComponentWithSharedAxis(field_name, typeName, x_dim, y_dim, comment, unit, min, max, x_axis, y_axis)                                                          \
    {                                                                                                                                                                              \
        static atomic_bool a2l_##field_name##_ = false;                                                                                                                            \
        if (A2lOnce_(&a2l_##field_name##_)) {                                                                                                                                      \
            typeName instance;                                                                                                                                                     \
            A2lTypedefParameterComponent_(#field_name, A2lGetRecordLayoutName(instance.field_name[0][0]), x_dim, y_dim,                                                            \
                                          ((uint8_t *)&(instance.field_name) - (uint8_t *)&instance), comment, unit, min, max, x_axis, y_axis);                                    \
        }                                                                                                                                                                          \
    }

#define A2lTypedefAxisComponent(field_name, typeName, x_dim, comment, unit, min, max)                                                                                              \
    {                                                                                                                                                                              \
        static atomic_bool a2l_##field_name##_ = false;                                                                                                                            \
        if (A2lOnce_(&a2l_##field_name##_)) {                                                                                                                                      \
            typeName instance;                                                                                                                                                     \
            A2lTypedefParameterComponent_(#field_name, A2lGetRecordLayoutName(instance.field_name[0]), x_dim, 0, ((uint8_t *)&(instance.field_name) - (uint8_t *)&instance),       \
                                          comment, unit, min, max, NULL, NULL);                                                                                                    \
        }                                                                                                                                                                          \
    }

#define A2lTypedefMeasurementArrayComponent(field_name, typedef_name)                                                                                                              \
    {                                                                                                                                                                              \
        static atomic_bool a2l_##field_name##_ = false;                                                                                                                            \
        if (A2lOnce_(&a2l_##field_name##_)) {                                                                                                                                      \
            typedef_name instance;                                                                                                                                                 \
            A2lTypedefComponent_(#field_name, A2lGetTypeName_M(instance.field_name[0]), sizeof(instance.field_name) / sizeof(instance.field_name[0]),                              \
                                 ((uint8_t *)&(instance.field_name[0]) - (uint8_t *)&instance));                                                                                   \
        }                                                                                                                                                                          \
    }

#define A2lTypedefParameterArrayComponent(field_name, typeName, comment, unit, min, max)                                                                                           \
    {                                                                                                                                                                              \
        static atomic_bool a2l_##field_name##_ = false;                                                                                                                            \
        if (A2lOnce_(&a2l_##field_name##_)) {                                                                                                                                      \
            typeName instance;                                                                                                                                                     \
            A2lTypedefComponent_(#field_name, A2lGetTypeName_C(instance.field_name[0]), sizeof(instance.field_name) / sizeof(instance.field_name[0]),                              \
                                 ((uint8_t *)&(instance.field_name[0]) - (uint8_t *)&instance));                                                                                   \
        }                                                                                                                                                                          \
    }

#define A2lTypedefParameterMatrixComponent(field_name, typeName, comment, unit, min, max)                                                                                          \
    {                                                                                                                                                                              \
        static atomic_bool a2l_##field_name##_ = false;                                                                                                                            \
        if (A2lOnce_(&a2l_##field_name##_)) {                                                                                                                                      \
            typeName instance;                                                                                                                                                     \
            A2lTypedefComponent_(#field_name, A2lGetTypeName_C(instance.field_name[0][0]), sizeof(instance.field_name) / sizeof(instance.field_name[0]),                           \
                                 ((uint8_t *)&(instance.field_name[0]) - (uint8_t *)&instance));                                                                                   \
        }                                                                                                                                                                          \
    }

#define A2lTypedefComponent(field_name, field_type_name, field_dim, typedef_name)                                                                                                  \
    {                                                                                                                                                                              \
        static atomic_bool a2l_##field_name##_ = false;                                                                                                                            \
        if (A2lOnce_(&a2l_##field_name##_)) {                                                                                                                                      \
            typedef_name instance;                                                                                                                                                 \
            A2lTypedefComponent_(#field_name, #field_type_name, field_dim, ((uint8_t *)&(instance.field_name) - (uint8_t *)&instance));                                            \
        }                                                                                                                                                                          \
    }

#define A2lTypedefEnd()                                                                                                                                                            \
    {                                                                                                                                                                              \
        static atomic_bool a2l_once = false;                                                                                                                                       \
        if (A2lOnce_(&a2l_once)) {                                                                                                                                                 \
            A2lTypedefEnd_();                                                                                                                                                      \
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

// --------------------------------------------------------------------------------------------
// Helper functions used in the for A2L generation macros

bool A2lOnce_(atomic_bool *once);

void A2lSetAbsAddrMode_(tXcpEventId default_event_id);
void A2lSetDynAddrMode_(tXcpEventId event_id, const uint8_t *base);
void A2lSetSegAddrMode_(tXcpCalSegIndex calseg_index, const uint8_t *calseg_instance_addr);
void A2lSetRelAddrMode_(tXcpEventId event_id, const uint8_t *base);

uint32_t A2lGetAddr_(const void *addr);
uint8_t A2lGetAddrExt_(void);

// Create measurements
const char *A2lCreateLinearConversion_(const char *name, const char *comment, const char *unit, double factor, double offset);

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
void A2lCreateTypedefInstance_(const char *instance_name, const char *type_name, uint16_t x_dim, uint8_t ext, uint32_t addr, const char *comment);

// Create parameters
void A2lCreateParameter_(const char *name, tA2lTypeId type, uint8_t ext, uint32_t addr, const char *comment, const char *unit, double min, double max);
void A2lCreateMap_(const char *name, tA2lTypeId type, uint8_t ext, uint32_t addr, uint32_t xdim, uint32_t ydim, const char *comment, const char *unit, double min, double max);
void A2lCreateCurve_(const char *name, tA2lTypeId type, uint8_t ext, uint32_t addr, uint32_t xdim, const char *comment, const char *unit, double min, double max);
