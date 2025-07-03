//-----------------------------------------------------------------------------------------------------
// Waveform generator class

/*
An instance of class SignalGenerator creates various waveforms, such as sine, square, arbitrary
Depending on calibration parameters ampl, phase, offset and period

*/

#include <cmath>
#include <iostream>
#include <string>
#include <thread>

#include "sig_gen.hpp"

namespace SignalGenerator {

// Constructor - creates the signal generator with the given instance name and parameters
SignalGenerator::SignalGenerator(const char *instance_name, SignalParametersT params) : signal_parameters_(instance_name, params), instance_name_(instance_name) {

    // Once add a typedef for SignalParametersT to the A2L file
    // All typedef registration macros use a once pattern, so there will be exactly one typedef in the A2L file
    A2lTypedefBegin(SignalParametersT, "A2L typedef for SignalParametersT");
    A2lTypedefParameterComponent(ampl, SignalParametersT, "Amplitude", "Volt", 0, 100);
    A2lTypedefParameterComponent(phase, SignalParametersT, "Phase", "", 0, k2Pi);
    A2lTypedefParameterComponent(offset, SignalParametersT, "Offset", "Volt", -100, 100);
    A2lTypedefParameterComponent(period, SignalParametersT, "Period", "s", 0.01, 10.0);
    A2lTypedefParameterComponent(delay_us, SignalParametersT, "Delay time in us", "us", 0, 100000);
    A2lTypedefEnd();

    // Add the calibration segment description as a typedef instance to the A2L file
    signal_parameters_.CreateA2lTypedefInstance("SignalParametersT", "Signal parameters for the signal generator");

    // Create an instance of the signal parameters member variable in the CalSeg<> wrapper for calibration access
    // The wrapper class 'CalSeg' instance 'signal_parameters' created a calibration memory segment in its constructor
    // A2lSetSegmentAddrMode(signal_parameters_.getIndex(), signal_parameters_);
    // A2lCreateTypedefNamedInstance(instance_name, signal_parameters_, SignalParametersT, "Signal parameters");

    // Start thread
    thread_ = new std::thread([this]() { task(); });
}

SignalGenerator::~SignalGenerator() {

    if (thread_ != nullptr) {
        if (thread_->joinable()) {
            thread_->join();
        }
        delete thread_;
    }
}

// Cyclic calculation function - runs in a separate thread
void SignalGenerator::task() {

    double time = 0;
    uint32_t delay_us = 1000;                                                // us
    double start_time = static_cast<double>(clockGet()) / CLOCK_TICKS_PER_S; // time in s since start of the signal generator

    // Create a measurement event for each instance of SignalGenerator
    // The event name is the instance name
    DaqCreateEvent_s(instance_name_);

    // A2L registration
    A2lLock(); // Take care for thread safety when registering measurements and parameters in multiple threads

    // Register the member variable 'value' of this instance as measurement
    A2lSetRelativeAddrMode_s(instance_name_, this);
    A2lCreatePhysMeasurementInstance(instance_name_, value, "Signal generator output", "", -100, 100);
    // Register the local (stack) measurement variable 'time' for measurement
    A2lSetStackAddrMode_s(instance_name_);
    A2lCreatePhysMeasurementInstance(instance_name_, time, "Signal generator time", "s", 0, 3600);

    A2lUnlock();

    for (;;) {

        time = static_cast<double>(clockGet()) / CLOCK_TICKS_PER_S - start_time; // time in s since start of the signal generator

        // Calculate a sine wave signal depending on the signal parameters
        {
            auto p = signal_parameters_.lock();
            value = p->offset + p->ampl * sin((time * k2Pi / p->period) + p->phase);
            delay_us = p->delay_us;
        }

        // XCP event by event name, event lookup is once and will be cached in a static thread local variable
        DaqEventRelative_s(instance_name_, this); // Trigger with this as dynamic addressing base to make member variables accessible

        sleepNs(delay_us * 1000);
    }
}

} // namespace SignalGenerator
