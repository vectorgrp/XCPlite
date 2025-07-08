//-----------------------------------------------------------------------------------------------------
// Waveform signal generator class

/*
An instance of class SignalGenerator creates various waveforms, such as sine, square, arbitrary
Depending on calibration parameters ampl, phase, offset and period
*/

#include <cmath>
#include <iostream>
#include <string>
#include <thread>

#include "sig_gen.hpp"

namespace signal_generator {

constexpr double kPi = 3.14159265358979323846;
constexpr double k2Pi = (kPi * 2);

// Constructor - creates the signal generator with the given instance name and parameters
SignalGenerator::SignalGenerator(const char *instance_name, SignalParametersT params) : signal_parameters_(instance_name, params), instance_name_(instance_name) {

// A2l registration of LookupTableT typedef
#ifdef CANAPE_24_NESTED // Shared axis in typedefs requires CANape 24
    A2lOnce(LookupTableT) {
        A2lTypedefBegin(LookupTableT, "A2L typedef for LookupTableT");
        A2lTypedefCurveComponentWithSharedAxis(values, LookupTableT, kLookupTableSize, "Lookup table with shared axis", "", -1.0, 1.0, "axis");
        A2lTypedefAxisComponent(axis, LookupTableT, kLookupTableSize, "Axis for lookup table in", "", -0.0, 1.0);
        A2lTypedefEnd();
    }
#endif

    // A2l registration of SignalParametersT typedef
    A2lOnce(SignalParametersT) {
        A2lTypedefBegin(SignalParametersT, "A2L typedef for SignalParametersT");
        A2lCreateEnumConversion(signal_type_enum, "5 0 \"SINE\" 1 \"SQUARE\" 2 \"TRIANGLE\" 3 \"SAWTOOTH\" 4 \"ARBITRARY\"");
        A2lTypedefParameterComponent(signal_type, SignalParametersT, "Signal type", signal_type_enum, 0, 4);
        A2lTypedefParameterComponent(ampl, SignalParametersT, "Amplitude", "Volt", 0, 100);
        A2lTypedefParameterComponent(phase, SignalParametersT, "Phase", "", 0, k2Pi);
        A2lTypedefParameterComponent(offset, SignalParametersT, "Offset", "Volt", -100, 100);
        A2lTypedefParameterComponent(period, SignalParametersT, "Period", "s", 0.01, 10.0);
        A2lTypedefParameterComponent(delay_us, SignalParametersT, "Delay time in us", "us", 0, 100000);
#ifdef CANAPE_24 // Shared axis in typedefs requires CANape 24
        A2lTypedefCurveComponentWithSharedAxis(lookup_values, SignalParametersT, kLookupTableSize, "Lookup table with shared axis lookup_axis", "", -1.0, 1.0, "lookup_axis");
        A2lTypedefAxisComponent(lookup_axis, SignalParametersT, kLookupTableSize, "Axis for lookup table in", "", -0.0, 1.0);
#endif
#ifdef CANAPE_24_NESTED // Shared axis in typedefs requires CANape 24
        A2lTypedefComponent(lookup, LookupTableT, 1, SignalParametersT);
#endif
        A2lTypedefEnd();
    }

    // Create an instance of the signal generatores calibration segment
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
    uint32_t delay_us = 1000;                                                // us
    double start_time = static_cast<double>(clockGet()) / CLOCK_TICKS_PER_S; // time in s since start of the signal generator

    // Create a measurement event for each instance of SignalGenerator
    // The event name is the instance name
    DaqCreateEvent_s(instance_name_);

    // A2L registration
    // Register the member variables 'value' and 'time' of each instance as measurement
    // Register the local (stack) measurement variable 'time' for measurement
    A2lLock(); // Take care for thread safety when registering measurements and parameters in multiple threads
    A2lSetRelativeAddrMode_s(instance_name_, this);
    A2lCreatePhysMeasurementInstance(instance_name_, value_, "Signal generator output", "", -100, 100);
    A2lSetStackAddrMode_s(instance_name_);
    A2lCreatePhysMeasurementInstance(instance_name_, time, "Signal generator time", "s", 0, 3600);
    A2lUnlock();

    for (;;) {

        time = static_cast<double>(clockGet()) / CLOCK_TICKS_PER_S - start_time; // time in s since start of the signal generator

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
#if defined(CANAPE_24) || defined(CANAPE_24_NESTED) // Shared axis in typedefs requires CANape 24
            case SignalTypeT::ARBITRARY: {
                // Find the index in the lookup table based on the time
#if defined(CANAPE_24_NESTED)
                const float *values = p->lookup.values; // Use the lookup table values
                const float *axis = p->lookup.axis;     // Use the lookup table axis
#else
                const float *values = p->lookup_values; // Use the lookup table values
                const float *axis = p->lookup_axis;     // Use the lookup table axis
#endif
                v = values[kLookupTableSize - 1];
                for (uint8_t i = 0; i < kLookupTableSize - 1; i++) {
                    if (normalized_time < axis[i + 1]) {
                        // Linear interpolation between the two points
                        double t1 = axis[i];
                        double t2 = axis[i + 1];
                        double v1 = values[i];
                        double v2 = values[i + 1];
                        v = v1 + (v2 - v1) * (normalized_time - t1) / (t2 - t1);
                        break;
                    }
                };
            } break;
#endif
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
        sleepNs(signal_parameters_.lock()->delay_us * 1000);
    }
}

} // namespace signal_generator
