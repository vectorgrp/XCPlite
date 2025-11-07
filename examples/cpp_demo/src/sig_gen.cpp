//-----------------------------------------------------------------------------------------------------
// Waveform signal generator class

/*
An instance of class SignalGenerator creates various waveforms, such as sine, square, arbitrary
Depending on calibration parameters ampl, phase, offset and period
*/

#include <cmath>   // for fmod, sin
#include <cstdint> // for uintxx_t
#include <thread>

#include "a2l.hpp"    // for xcplib A2l generation application programming interface
#include "xcplib.hpp" // for xcplib application programming interface

#include "sig_gen.hpp"

#include "lookup.hpp" // calibratble lookup table for arbitrary waveform generator
using namespace lookup_table;

namespace signal_generator {

constexpr double kPi = 3.14159265358979323846;
constexpr double k2Pi = (kPi * 2);

// Constructor - creates the signal generator with the given instance name and parameters
SignalGenerator::SignalGenerator(const char *instance_name, const SignalParametersT *params) : signal_parameters_(instance_name, params), instance_name_(instance_name) {

    // Global once A2l registration of SignalParametersT typedef
    if (A2lOnce()) {

        params->lookup.A2lRegisterTypedef(); // Register the lookup table typedef

        A2lTypedefBegin(SignalParametersT, params, "A2L typedef for SignalParametersT");
        A2lCreateEnumConversion(signal_type_enum, "5 0 \"SINE\" 1 \"SQUARE\" 2 \"TRIANGLE\" 3 \"SAWTOOTH\" 4 \"ARBITRARY\"");
        A2lTypedefParameterComponent(signal_type, SignalParametersT, "Signal type", "conv.signal_type_enum", 0, 4);
        A2lTypedefParameterComponent(ampl, SignalParametersT, "Amplitude", "Volt", 0, 100);
        A2lTypedefParameterComponent(phase, SignalParametersT, "Phase", "", 0, k2Pi);
        A2lTypedefParameterComponent(offset, SignalParametersT, "Offset", "Volt", -100, 100);
        A2lTypedefParameterComponent(period, SignalParametersT, "Period", "s", 0.01, 10.0);
        A2lTypedefParameterComponent(delay_us, SignalParametersT, "Delay time in us", "us", 0, 100000);
        A2lTypedefComponent(lookup, LookupTableT, 1);
        A2lTypedefEnd();
    }

    // Create an instance of the signal generators calibration segment
    signal_parameters_.CreateA2lTypedefInstance("SignalParametersT", "Signal parameters for the signal generator");

    // Start thread
    thread_ = new std::thread([this]() { Task(); });
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
void SignalGenerator::Task() {

    double time = 0;
    uint32_t delay_us = 1000;                                        // us
    double start_time = static_cast<double>(clockGetUs()) / 1000000; // time in s since start of the signal generator

    // Create a measurement event with individual name 'instance_name_' for each instance of SignalGenerator
    DaqCreateEvent_s(instance_name_);

    // A2L registration
    // Register the member variable 'value_' of each instance as measurements by using the instance name as a prefix
    // Register the local (stack) measurement variable 'time' for each instance
    A2lLock(); // Take care for thread safety when registering measurements and parameters in multiple threads
    A2lSetRelativeAddrMode_s(instance_name_, this);
    A2lCreatePhysMeasurementInstance(instance_name_, value_, "Signal generator output", "", -100, 100);
    A2lSetStackAddrMode_s(instance_name_);
    A2lCreatePhysMeasurementInstance(instance_name_, time, "Signal generator time", "s", 0, 3600);
    A2lUnlock();

    for (;;) {

        time = static_cast<double>(clockGetUs()) / 1000000 - start_time; // time in s since start of the signal generator

        // Calculate the waveform value based on the current time and signal parameters
        {
            auto p = signal_parameters_.lock();
            double normalized_time = fmod(time, p->period) / p->period; // Normalize time ([0.0..1.0[ to the period
            double v = 0;
            switch (p->signal_type) {
            case SignalTypeT::SQUARE:
                v = (normalized_time < 0.5) ? +1.0 : -1.0;
                break;
            case SignalTypeT::TRIANGLE:
                v = (normalized_time - 0.5);
                v = (normalized_time < 0.5) ? v : -v;
                v = (v + 0.25) * 4.0;
                break;
            case SignalTypeT::SAWTOOTH:
                v = (normalized_time - 0.5) * 2.0;
                break;
            case SignalTypeT::ARBITRARY: {
                // Find the index in the lookup table based on the time
                v = p->lookup.Lookup((float)normalized_time);
            } break;
            default:
                v = sin((normalized_time * k2Pi) + p->phase);
                break;
            }
            value_ = (p->ampl * v) + p->offset;
        }

        // XCP event by event name, event lookup is once and will be cached in a static thread local variable
        DaqEventRelative_s(instance_name_, this); // Trigger with this as dynamic addressing base to make member variables accessible

        // Sleep for delay_us microseconds
        // Be sure the lock is held as short as possible !
        // The calibration segment lock does not content against other threads, it is wait free !
        // But it delays or even starve XCP client tool calibration operations
        sleepUs(signal_parameters_.lock()->delay_us);
    }
}

} // namespace signal_generator
