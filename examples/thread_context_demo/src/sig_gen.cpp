//-----------------------------------------------------------------------------------------------------
// Waveform signal generator class

/*
An instance of class SignalGenerator creates various waveforms, such as sine, square, arbitrary
Depending on calibration parameters ampl, phase, offset and period
*/

#include <cmath>    // for fmod, sin
#include <cstdint>  // for uintxx_t
#include <iostream> // for std::cout
#include <thread>

// Include XCPlite/libxcplite C++ headers
#include "a2l.hpp"    // for A2l generation application programming interface
#include "xcplib.hpp" // for application programming interface

#include "sig_gen.hpp"

namespace signal_generator {

constexpr double kPi = 3.14159265358979323846;
constexpr double k2Pi = (kPi * 2);

// Constructor - creates the signal generator with the given instance name and parameters
SignalGenerator::SignalGenerator(const char *instance_name, const SignalParametersT *params) : signal_parameters_(instance_name, params), instance_name_(instance_name) {

    // Once registration of the A2L TYPEDEF for SignalParametersT
    if (A2lOnce()) {
        A2lTypedefBegin(SignalParametersT, params, "A2L typedef for SignalParametersT");
        A2lCreateEnumConversion(signal_type_enum, "4 0 \"SINE\" 1 \"SQUARE\" 2 \"TRIANGLE\" 3 \"SAWTOOTH\"");
        A2lTypedefParameterComponent(signal_type, "Signal type", "conv.signal_type_enum", 0, 3);
        A2lTypedefParameterComponent(ampl, "Amplitude", "Volt", 0, 100);
        A2lTypedefParameterComponent(phase, "Phase", "", 0, k2Pi);
        A2lTypedefParameterComponent(offset, "Offset", "Volt", -100, 100);
        A2lTypedefParameterComponent(period, "Period", "s", 0.01, 10.0);
        A2lTypedefParameterComponent(delay_us, "Delay time in us", "us", 0, 100000);
        A2lTypedefEnd();
    }

    // Create an instance of the signal generators calibration segment
    signal_parameters_.CreateA2lTypedefInstance("SignalParametersT", "Signal parameters for the signal generator");

    // Start thread
    std::cout << "Starting task: " << instance_name_ << std::endl;

    thread_ = new std::thread([this]() { Task(); });
}

SignalGenerator::~SignalGenerator() {
    running_ = false;
    if (thread_ != nullptr) {
        if (thread_->joinable()) {
            thread_->join();
        }
        delete thread_;
    }
}

// Cyclic calculation function - runs in a separate thread
void SignalGenerator::Task() {

    double time = 0;
    double start_time = static_cast<double>(clockGetMonotonicUs()) / 1000000; // time in s since start of the signal generator

    while (running_) {

        time = static_cast<double>(clockGetMonotonicUs()) / 1000000 - start_time; // time in s since start of the signal generator

        // Calculate the waveform value based on the current time and signal parameters
        {
            auto p = signal_parameters_.lock();
            normalized_time_ = fmod(time, p->period) / p->period; // Normalize time ([0.0..1.0[ to the period
            double v = 0;
            switch (p->signal_type) {
            case SignalTypeT::SQUARE:
                v = (normalized_time_ < 0.5) ? +1.0 : -1.0;
                break;
            case SignalTypeT::TRIANGLE:
                v = (normalized_time_ - 0.5);
                v = (normalized_time_ < 0.5) ? v : -v;
                v = (v + 0.25) * 4.0;
                break;
            case SignalTypeT::SAWTOOTH:
                v = (normalized_time_ - 0.5) * 2.0;
                break;
            default:
                v = sin((normalized_time_ * k2Pi) + p->phase);
                break;
            }
            value_ = (p->ampl * v) + p->offset;
            output_.store(value_, std::memory_order_relaxed); // Publish for GetValue() called from other threads
        }

        // XCP event
        DaqEventVar(siggen,                                                                          //
                    A2L_MEAS_PHYS(value_, "Signal generator output (all instances)", "Volt", -100.0, +100.0) //
        );

        // Sleep for delay_us microseconds
        sleepUs(signal_parameters_.lock()->delay_us);
    }
}

} // namespace signal_generator
