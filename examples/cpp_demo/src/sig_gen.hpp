#include <cmath>
#include <iostream>
#include <string>
#include <thread>

#include "a2l.h"
#include "platform.h"
#include "xcplib.h"
#include "xcplib.hpp"

namespace signal_generator {

constexpr double kPi = 3.14159265358979323846;
constexpr double k2Pi = (kPi * 2);

// Signal parameters struct
struct SignalParametersT {
    double ampl;       // Amplitude of the sine wave
    double phase;      // Phase shift in radians
    double offset;     // Offset of the sine wave
    double period;     // Period of the sine wave in seconds
    uint32_t delay_us; // Delay in microseconds for the main loop
};

class SignalGenerator {

  private:
    xcplib::CalSeg<SignalParametersT> signal_parameters_; // Wrapper template class for the signal parameters struct to enable XCP calibration access
    const char *instance_name_;                           // Instance name
    std::thread *thread_;                                 // Thread for the signal generator task
    double value_{0};                                     // Current value, accessible via XCP measurement

    void Task();

  public:
    [[nodiscard]] double GetValue() const { return value_; } // Getter for the current value
    SignalGenerator(const char *instance_name, SignalParametersT params);
    ~SignalGenerator();
};

} // namespace signal_generator