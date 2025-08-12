#pragma once
#define __A2L_HPP__

/*-----------------------------------------------------------------------------
| File:
|   a2l.hpp
|
| Description:
|   Public C++ header for A2L generation
|
| Copyright (c) Vector Informatik GmbH. All rights reserved.
| See LICENSE file in the project root for details.
|
 ----------------------------------------------------------------------------*/

#include "a2l.h"

#ifdef __cplusplus

#include <atomic>
#include <mutex>
#include <type_traits>

// For C++17 and later (which is required for XCPlite), use std::monostate from <variant>
#if __cplusplus >= 201703L
#include <variant>
using empty_type = std::monostate;
#else
#error "C++17 or later is required for XCPlite, please enable C++17 in your build system"
// For older compilers, provide a simple empty type
struct empty_type {};
#endif

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
    A2lOnceGuard<false, false, __LINE__> {}
#define A2lOnceLock()                                                                                                                                                              \
    A2lOnceGuard<true, false, __LINE__> {}
#define A2lOncePerThread()                                                                                                                                                         \
    A2lOnceGuard<false, true, __LINE__> {}
#define A2lOncePerThreadLock()                                                                                                                                                     \
    A2lOnceGuard<true, true, __LINE__> {}

// Usage examples (preferred syntax with explicit if):
// if (A2lOnce()) {
//     // This block executes exactly once globally across all threads
//     // Thread-safe with no additional mutex overhead during execution
// }
//
// if (A2lOnceLock()) {
//     // This block executes exactly once globally AND is mutex-protected during execution
//     // Useful when the once-block itself needs additional thread safety
// }
//
// if (A2lOncePerThread()) {
//     // This block executes exactly once per thread
//     // Each thread will execute this block once independently
// }
//
// if (A2lOncePerThreadLock()) {
//     // This block executes exactly once per thread AND is mutex-protected during execution
//     // Each thread executes once, with mutex protection during execution
// }

#endif // __cplusplus
