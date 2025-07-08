#include <cmath>
#include <iostream>
#include <string>
#include <thread>

#include "a2l.h"
#include "platform.h"
#include "xcplib.h"
#include "xcplib.hpp"

namespace signal_generator {

enum SignalTypeT : std::uint8_t {
    SINE = 0,     // Sine wave
    SQUARE = 1,   // Square wave
    TRIANGLE = 2, // Triangle wave
    SAWTOOTH = 3, // Sawtooth wave
    ARBITRARY = 4 // Arbitrary waveform from lookup table
};

// Typedefs with maps or curves with shared axis require CANape 24
#define CANAPE_24
// #define CANAPE_24_NESTED

// Lookup table for arbitrary waveforms
constexpr uint8_t kLookupTableSize = 11; // Size of the lookup table

// Lookup table struct
#ifdef CANAPE_24_NESTED
struct LookupTableT {
    float values[kLookupTableSize]; // Values
    float axis[kLookupTableSize];   // Axis
};
#endif

// Signal parameters struct
struct SignalParametersT {
    double ampl;   // Amplitude
    double phase;  // Phase shift in radians
    double offset; // Offset
    double period; // Period in seconds
    // Lookup table for arbitrary waveforms
#ifdef CANAPE_24
    float lookup_values[kLookupTableSize]; // Values
    float lookup_axis[kLookupTableSize];   // Axis
#endif
#ifdef CANAPE_24_NESTED
    LookupTableT lookup;
#endif
    uint32_t delay_us;   // Delay in microseconds for the task loop
    uint8_t signal_type; // Type of the signal (SignalTypeT)
};

class SignalGenerator {

  private:
    xcplib::CalSeg<SignalParametersT> signal_parameters_; // Wrapped signal parameters struct to enable XCP calibration access

    const char *instance_name_; // Instance name
    std::thread *thread_;       // Thread for the signal generator task

    double value_{0}; // Current value

    void Task();

    double Calculate(double time); // Calculate sine value based on time

  public:
    SignalGenerator(const char *instance_name, SignalParametersT params);
    ~SignalGenerator();

    [[nodiscard]] double GetValue() const { return value_; } // Getter for the current value
};

} // namespace signal_generator