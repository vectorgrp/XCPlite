#pragma once

//-----------------------------------------------------------------------------------------------------
// Waveform signal generator class

/*
An instance of class SignalGenerator creates various waveforms, such as sine, square, arbitrary
Depending on calibration parameters ampl, phase, offset and period in struct SignalParametersT
*/

#include <atomic>  // for std::atomic
#include <cstdint> // for uintxx_t
#include <thread>  // for thread

#include "xcplib.hpp" // for xcp::CalSeg

#include "lookup.hpp" // for lookup_table::LookupTableT

//-----------------------------------------------------------------------------------------------

namespace signal_generator {

enum SignalTypeT : uint8_t {
    SINE = 0,     // Sine wave
    SQUARE = 1,   // Square wave
    TRIANGLE = 2, // Triangle wave
    SAWTOOTH = 3, // Sawtooth wave
    ARBITRARY = 4 // Arbitrary waveform from lookup table
};

// Signal parameters struct
struct SignalParametersT {
    double ampl;                              // Amplitude
    double phase;                             // Phase shift in radians
    double offset;                            // Offset
    double period;                            // Period in seconds
    lookup_table::LookupTableT lookup;        // Lookup table for arbitrary waveforms
    uint32_t delay_us;                        // Delay in microseconds for the task loop
    uint8_t /*enum SignalTypeT*/ signal_type; // Type of the signal (SignalTypeT)
};

class SignalGenerator {

  private:
    xcp::CalSeg<SignalParametersT> signal_parameters_; // Wrapped signal parameters struct to enable XCP calibration access

    const char *instance_name_;       // Instance name
    std::thread *thread_;             // Thread for the signal generator task
    std::atomic<bool> running_{true}; // Stop flag for the worker thread

    double value_{0};           // Current value, written and measured in the task thread only
    double normalized_time_{0}; // Current normalized time in [0.0..1.0[

    std::atomic<double> output_{0}; // Copy of value_ for thread safe access from other threads via GetValue()

    void Task();

    double Calculate(double time); // Calculate waveform value based on time

  public:
    SignalGenerator(const char *instance_name, const SignalParametersT *params);
    ~SignalGenerator();

    [[nodiscard]] double GetValue() const { return output_.load(std::memory_order_relaxed); } // Thread safe getter for the current value
};

} // namespace signal_generator