// Generic C++ RAII wrapper for calibration parameter segments
// Provides automatic lock/unlock management using a guard pattern

#pragma once

#include "xcplib.h"

namespace xcplib {

/// Generic RAII wrapper for structs with calibration parameters
/// Template parameter T must be the calibration parameter struct type
template <typename T> class CalSeg {
  private:
    tXcpCalSegIndex segment_index_;

  public:
    /// Constructor - creates the calibration segment struct wrapper
    /// @param name Name of the calibration segment
    /// @param default_params Default parameter values (reference page)
    CalSeg(const char *name, const T &default_params) {
        segment_index_ = XcpCreateCalSeg(name, &default_params, sizeof(T));
        if (segment_index_ == XCP_UNDEFINED_CALSEG) {
            assert(false);
        }
    }

    /// Get the segment index (for direct XCP API calls if needed)
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
};

/// Convenience function to create a calibration segment wrapper
/// Usage: auto calseg = xcp::CreateCalSeg("Parameters", default_parameters);
template <typename T> CalSeg<T> CreateCalSeg(const char *name, const T &default_params) { return CalSeg<T>(name, default_params); }

} // namespace xcplib
