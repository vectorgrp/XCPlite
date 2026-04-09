#pragma once
#define __A2L_HPP__

/*-----------------------------------------------------------------------------
| File:
|   a2l.hpp - Public C++ API for A2L generation
|
| Description:
|   Public C++ header for A2L generation
|
| Copyright (c) Vector Informatik GmbH. All rights reserved.
| See LICENSE file in the project root for details.
|
 ----------------------------------------------------------------------------*/

// ============================================================================
// Helper macros for C++ one time and thread safe A2L registrations
// Mutex protection is needed in multi-threaded contexts, because the A2L registration macros are not thread-safe
// ============================================================================

/*
Usage examples:

if (A2lOnce()) {
    // Not Thread-safe !
    // This block executes exactly once globally across all threads
}

if (A2lOnceLock()) {
    // Thread-safe
    // This block executes exactly once globally AND is mutex-protected during execution
}

if (A2lOncePerThread()) {
    // Thread-safe
    // This block executes exactly once per thread AND is mutex-protected
}
*/

#include <a2l.h>

#ifdef __cplusplus

#include <mutex>
#include <thread>

namespace xcp {
namespace a2l {

struct empty_type {};

// RAII guard for execute-once pattern with optional mutex protection
// Location parameter ensures each call site gets its own once_flag
// Static members belong to the type, so each location is a different type which gets its own once_flag
template <bool WithMutex = false, bool PerThread = false, int Location = 0> class A2lOnceGuard {
  private:
    inline static std::once_flag once_flag_;
    inline static std::conditional_t<WithMutex, std::mutex, empty_type> mutex_;
    std::conditional_t<WithMutex, std::unique_lock<std::mutex>, empty_type> lock_;

  public:
    A2lOnceGuard() {
        if constexpr (WithMutex) {
            lock_ = std::unique_lock<std::mutex>(mutex_);
        }
    }

    explicit operator bool() const {
        bool execute = false;
        std::call_once(once_flag_, [&execute]() { execute = true; });
        return execute;
    }
};

// Specialization for per-thread execution
template <bool WithMutex, int Location> class A2lOnceGuard<WithMutex, true, Location> {
  private:
    inline static std::conditional_t<WithMutex, std::mutex, empty_type> mutex_;
    std::conditional_t<WithMutex, std::unique_lock<std::mutex>, empty_type> lock_;

  public:
    A2lOnceGuard() {
        if constexpr (WithMutex) {
            lock_ = std::unique_lock<std::mutex>(mutex_);
        }
    }

    explicit operator bool() const {
        static thread_local bool executed = false;
        if (!executed) {
            executed = true;
            return true;
        }
        return false;
    }
};

// Enhanced convenience macros for C++ RAII-style once execution
#define A2lOnce()                                                                                                                                                                  \
    xcp::a2l::A2lOnceGuard<false, false, __LINE__> {}
#define A2lOnceLock()                                                                                                                                                              \
    xcp::a2l::A2lOnceGuard<true, false, __LINE__> {}
#define A2lOncePerThread()                                                                                                                                                         \
    xcp::a2l::A2lOnceGuard<true, true, __LINE__> {}

// =============================================================================
// Variadic template to create typedefs
// Once execution and thread safety is handled inside the macro
// Usage example:
//   A2lCreateTypedef(ParametersT, "Typedef for ParametersT",
//                    A2L_PARAMETER_COMPONENT(min, "Minimum value", "miles/h", -0.0, 10.0),
//                    A2L_PARAMETER_COMPONENT(max, "Maximum value", "miles/h", -70.0, 80.0)
// );
// =============================================================================

// Helper lambda-based macros used with A2lCreateTypedef

// Parameters
#define A2L_PARAMETER_COMPONENT(field_name, comment, unit, min, max)                                                                                                               \
    [](auto type_ptr) {                                                                                                                                                            \
        using StructType = std::remove_pointer_t<decltype(type_ptr)>;                                                                                                              \
        using FieldType = decltype(StructType::field_name);                                                                                                                        \
        return xcp::a2l::A2lParameterComponentInfo<FieldType>(#field_name, (uint16_t)offsetof(StructType, field_name), 1, 1, comment, unit, min, max, NULL, NULL);                 \
    }

// Multi dimensional parameters (curve, map, axis), auto-detect array dimensions (automatic size detection from type)
// Note: when y_dim is set to 0, it is used to identify axis
#define A2L_CURVE_COMPONENT(field_name, comment, unit, min, max)                                                                                                                   \
    [](auto type_ptr) {                                                                                                                                                            \
        using StructType = std::remove_pointer_t<decltype(type_ptr)>;                                                                                                              \
        using FieldType = decltype(StructType::field_name);                                                                                                                        \
        using ElementType = std::remove_reference_t<decltype(std::declval<StructType>().field_name[0])>;                                                                           \
        constexpr size_t x_dim = std::extent_v<FieldType, 0>;                                                                                                                      \
        return xcp::a2l::A2lParameterComponentInfo<ElementType>(#field_name, (uint16_t)offsetof(StructType, field_name), x_dim, 1, comment, unit, min, max, NULL, NULL);           \
    }
#define A2L_CURVE_WITH_AXIS_COMPONENT(field_name, comment, unit, min, max, axis)                                                                                                   \
    [](auto type_ptr) {                                                                                                                                                            \
        using StructType = std::remove_pointer_t<decltype(type_ptr)>;                                                                                                              \
        using FieldType = decltype(StructType::field_name);                                                                                                                        \
        using ElementType = std::remove_reference_t<decltype(std::declval<StructType>().field_name[0])>;                                                                           \
        constexpr size_t x_dim = std::extent_v<FieldType, 0>;                                                                                                                      \
        return xcp::a2l::A2lParameterComponentInfo<ElementType>(#field_name, (uint16_t)offsetof(StructType, field_name), x_dim, 1, comment, unit, min, max, #axis, NULL);          \
    }
#define A2L_MAP_COMPONENT(field_name, comment, unit, min, max)                                                                                                                     \
    [](auto type_ptr) {                                                                                                                                                            \
        using StructType = std::remove_pointer_t<decltype(type_ptr)>;                                                                                                              \
        using FieldType = decltype(StructType::field_name);                                                                                                                        \
        using ElementType = std::remove_reference_t<decltype(std::declval<StructType>().field_name[0][0])>;                                                                        \
        constexpr size_t x_dim = std::extent_v<FieldType, 0>;                                                                                                                      \
        constexpr size_t y_dim = std::extent_v<FieldType, 1>;                                                                                                                      \
        return xcp::a2l::A2lParameterComponentInfo<ElementType>(#field_name, (uint16_t)offsetof(StructType, field_name), x_dim, y_dim, comment, unit, min, max, NULL, NULL);       \
    }
#define A2L_MAP_WITH_AXIS_COMPONENT(field_name, comment, unit, min, max, x_axis, y_axis)                                                                                           \
    [](auto type_ptr) {                                                                                                                                                            \
        using StructType = std::remove_pointer_t<decltype(type_ptr)>;                                                                                                              \
        using FieldType = decltype(StructType::field_name);                                                                                                                        \
        using ElementType = std::remove_reference_t<decltype(std::declval<StructType>().field_name[0][0])>;                                                                        \
        constexpr size_t x_dim = std::extent_v<FieldType, 0>;                                                                                                                      \
        constexpr size_t y_dim = std::extent_v<FieldType, 1>;                                                                                                                      \
        return xcp::a2l::A2lParameterComponentInfo<ElementType>(#field_name, (uint16_t)offsetof(StructType, field_name), x_dim, y_dim, comment, unit, min, max, #x_axis, #y_axis); \
    }
#define A2L_AXIS_COMPONENT(field_name, comment, unit, min, max)                                                                                                                    \
    [](auto type_ptr) {                                                                                                                                                            \
        using StructType = std::remove_pointer_t<decltype(type_ptr)>;                                                                                                              \
        using FieldType = decltype(StructType::field_name);                                                                                                                        \
        using ElementType = std::remove_reference_t<decltype(std::declval<StructType>().field_name[0])>;                                                                           \
        constexpr size_t x_dim = std::extent_v<FieldType, 0>;                                                                                                                      \
        return xcp::a2l::A2lParameterComponentInfo<ElementType>(#field_name, (uint16_t)offsetof(StructType, field_name), x_dim, 0, comment, unit, min, max, NULL, NULL);           \
    }

// Measurement
#define A2L_MEASUREMENT_COMPONENT(field_name, comment, unit)                                                                                                                       \
    [](auto type_ptr) {                                                                                                                                                            \
        using StructType = std::remove_pointer_t<decltype(type_ptr)>;                                                                                                              \
        using FieldType = decltype(StructType::field_name);                                                                                                                        \
        return xcp::a2l::A2lMeasurementComponentInfo<FieldType>(#field_name, (uint16_t)offsetof(StructType, field_name), 1, comment, unit);                                        \
    }

// Multi dimensional measurement
#define A2L_MEASUREMENT_ARRAY_COMPONENT(field_name, comment, unit)                                                                                                                 \
    [](auto type_ptr) {                                                                                                                                                            \
        using StructType = std::remove_pointer_t<decltype(type_ptr)>;                                                                                                              \
        using FieldType = decltype(StructType::field_name);                                                                                                                        \
        using ElementType = std::remove_reference_t<decltype(std::declval<StructType>().field_name[0])>;                                                                           \
        constexpr size_t x_dim = std::extent_v<FieldType, 0>;                                                                                                                      \
        return xcp::a2l::A2lMeasurementComponentInfo<ElementType>(#field_name, (uint16_t)offsetof(StructType, field_name), x_dim, comment, unit);                                  \
    }

// Typedef
#define A2L_TYPEDEF_COMPONENT(field_name, type_name, dim)                                                                                                                          \
    [](auto type_ptr) {                                                                                                                                                            \
        using StructType = std::remove_pointer_t<decltype(type_ptr)>;                                                                                                              \
        using FieldType = decltype(StructType::field_name);                                                                                                                        \
        return xcp::a2l::A2lTypedefComponentInfo<FieldType>(#field_name, type_name, (uint16_t)(dim), (uint16_t)offsetof(StructType, field_name));                                  \
    }

// Helper struct to hold typedef component information for parameter components
template <typename FieldType> struct A2lParameterComponentInfo {
    const char *name;
    const tA2lTypeId type_id;
    const size_t offset;
    const uint16_t x_dim;
    const uint16_t y_dim;
    const char *comment;
    const char *unit;
    double min;
    double max;
    const char *x_axis;
    const char *y_axis;

    // Constructor for 2 dimensional array parameter component with physical unit and limit (name, x_dim, y_dim, comment, unit, min, max)
    constexpr A2lParameterComponentInfo(const char *name, size_t offset, uint16_t x_dim, uint16_t y_dim, const char *comment, const char *unit, double min, double max,
                                        const char *x_axis, const char *y_axis)
        : name(name), type_id(GetTypeId<FieldType>()), offset(offset), x_dim(x_dim), y_dim(y_dim), comment(comment), unit(unit), min(min), max(max), x_axis(x_axis),
          y_axis(y_axis) {}
};

// Helper struct to hold typedef component information for measurement components
template <typename FieldType> struct A2lMeasurementComponentInfo {
    const char *name;
    const tA2lTypeId type_id;
    const size_t offset;
    const uint16_t dim;
    const char *comment;
    const char *unit;

    // Constructor for array of physical component (name, dim, comment, unit, min, max)
    constexpr A2lMeasurementComponentInfo(const char *name, size_t offset, uint16_t dim, const char *comment, const char *unit)
        : name(name), type_id(GetTypeId<FieldType>()), offset(offset), dim(dim), comment(comment), unit(unit) {}
};

// Helper struct to hold typedef component information for typedef components
template <typename FieldType> struct A2lTypedefComponentInfo {
    const char *name;
    const char *type_name;
    const uint16_t dim;
    const size_t offset;

    // Constructor for array of typedef component
    constexpr A2lTypedefComponentInfo(const char *name, const char *type_name, uint16_t dim, size_t offset) : name(name), type_name(type_name), dim(dim), offset(offset) {}
};

// =============================================================================

// Helper template function to register a parameter component
template <typename T> void A2lCreateTypedefComponentTemplate(const A2lParameterComponentInfo<T> &info) {
    A2lTypedefParameterComponent_(info.name, info.type_id, info.x_dim, info.y_dim, info.offset, info.comment, info.unit, info.min, info.max, info.x_axis, info.y_axis);
}
// Helper template function to register a measurement component
template <typename T> void A2lCreateTypedefComponentTemplate(const A2lMeasurementComponentInfo<T> &info) {
    A2lTypedefMeasurementComponent_(info.name, info.type_id, info.dim, info.offset, info.comment, info.unit, 0.0, 0.0);
}
// Helper template function to register a typedef component
template <typename T> void A2lCreateTypedefComponentTemplate(const A2lTypedefComponentInfo<T> &info) { A2lTypedefComponent_(info.name, info.type_name, info.dim, info.offset); }

// Main macro to create a typedef and its fields
#define A2lCreateTypedef(type_name, comment, ...) xcp::a2l::A2lCreateTypedefTemplate<type_name>(#type_name, sizeof(type_name), comment, __VA_ARGS__);

// Template function for typedef creation
template <typename TypeName, typename... ComponentBuilders>
void A2lCreateTypedefTemplate(const char *type_name, size_t type_size, const char *comment, ComponentBuilders &&...builders) {
    if (XcpIsActivated()) {
        static std::once_flag once_flag;
        std::call_once(once_flag, [&]() {
            A2lLock();
            A2lTypedefBegin_(type_name, type_size, comment);
            (A2lCreateTypedefComponentTemplate(builders((TypeName *)nullptr)), ...);
            A2lTypedefEnd_();
            A2lUnlock();
        });
    }
}

} // namespace a2l
} // namespace xcp

#endif // __cplusplus
