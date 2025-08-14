#pragma once

//-----------------------------------------------------------------------------------------------------
// Waveform signal generator class

/*
An instance of class SignalGenerator creates various waveforms, such as sine, square, arbitrary
Depending on calibration parameters ampl, phase, offset and period
*/


#include <cstdint> // for uintxx_t
#include <thread>  // for thread

#include "xcplib.hpp" // for xcplib::CalSeg

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
    double ampl;                       // Amplitude
    double phase;                      // Phase shift in radians
    double offset;                     // Offset
    double period;                     // Period in seconds
    lookup_table::LookupTableT lookup; // Lookup table for arbitrary waveforms
    uint32_t delay_us;                 // Delay in microseconds for the task loop
    uint8_t signal_type;               // Type of the signal (SignalTypeT)
};

class SignalGenerator {

  private:
    xcplib::CalSeg<SignalParametersT> signal_parameters_; // Wrapped signal parameters struct to enable XCP calibration access

    const char *instance_name_; // Instance name
    std::thread *thread_;       // Thread for the signal generator task

    double value_{0}; // Current value

    void Task();

    double Calculate(double time); // Calculate waveform value based on time

  public:
    SignalGenerator(const char *instance_name, SignalParametersT params);
    ~SignalGenerator();

    [[nodiscard]] double GetValue() const { return value_; } // Getter for the current value
};

} // namespace signal_generator