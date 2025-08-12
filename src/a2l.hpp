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
#include <mutex>
#include <type_traits>

// For C++17 and later, use std::monostate from <variant>
// For older compilers, provide a simple empty type
#if __cplusplus >= 201703L
#include <variant>
using empty_type = std::monostate;
#else
struct empty_type {};
#endif

// RAII guard for execute-once pattern with optional mutex protection
template <bool WithMutex = false, bool PerThread = false> class A2lOnceGuard {
  private:
    inline static std::once_flag once_flag_;
    inline static std::conditional_t<WithMutex, std::mutex, empty_type> mutex_;
    std::conditional_t<WithMutex, std::unique_lock<std::mutex>, empty_type> lock_;
    bool should_execute_;

  public:
    A2lOnceGuard() : should_execute_(false) {
        if constexpr (WithMutex) {
            lock_ = std::unique_lock<std::mutex>(mutex_);
        }
        std::call_once(once_flag_, [this]() { should_execute_ = true; });
    }

    explicit operator bool() const { return should_execute_; }
};

// Specialization for per-thread execution (cannot use thread_local inline static)
template <bool WithMutex> class A2lOnceGuard<WithMutex, true> {
  private:
    static thread_local std::once_flag once_flag_;
    inline static std::conditional_t<WithMutex, std::mutex, empty_type> mutex_;
    std::conditional_t<WithMutex, std::unique_lock<std::mutex>, empty_type> lock_;
    bool should_execute_;

  public:
    A2lOnceGuard() : should_execute_(false) {
        if constexpr (WithMutex) {
            lock_ = std::unique_lock<std::mutex>(mutex_);
        }
        std::call_once(once_flag_, [this]() { should_execute_ = true; });
    }

    explicit operator bool() const { return should_execute_; }
};

// Enhanced convenience macros for C++ RAII-style once execution
#define A2lOnce()                                                                                                                                                                  \
    A2lOnceGuard<false, false> {}
#define A2lOnceLock()                                                                                                                                                              \
    A2lOnceGuard<true, false> {}
#define A2lOncePerThread()                                                                                                                                                         \
    A2lOnceGuard<false, true> {}
#define A2lOncePerThreadLock()                                                                                                                                                     \
    A2lOnceGuard<true, true> {}

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
//
// Alternative syntax (also supported):
// A2lOnce() && []() {
//     // Initialization code here
//     return true;
// }();

#endif // __cplusplus
