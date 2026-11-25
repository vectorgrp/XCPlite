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

// Clip the input value to the limits clip_min and clip_max of the calibration parameters of this instance
// The context-aware instrumentation gets the instance from the current context of the thread, which is the context of this instance
double SignalGenerator::Clip(double input) {

    // Create and begin span 'clip'
    DaqSpan(clip);

    double output = input;
    bool clipped = false;
    {
        // Nested lock, the caller holds a lock of the same calibration segment
        // A nested lock may return a newer page than the outer lock (docs/CAL_RCU.md), the limits may be newer than the other parameters
        auto p = signal_parameters_.lock();
        if (output > p->clip_max) {
            output = p->clip_max;
            clipped = true;
        }
        if (output < p->clip_min) {
            output = p->clip_min;
            clipped = true;
        }
    }
    SimulateWork(1000); // 1ms

    // Trigger a context aware measurement event 'clip'
    DaqEventCtxVar(clip, // context='SigGen1' or 'SigGen2', span='clip'
                   A2L_MEAS(clipped, "Clip function clipped input"));
    return output;
}

// Constructor - creates the signal generator with the given instance name and parameters
SignalGenerator::SignalGenerator(const char *instance_name, const SignalParametersT *params, size_t queue_size)
    : signal_parameters_(instance_name, params), context_(instance_name), instance_name_(instance_name), queue_(queue_size) {

    // Once registration of the A2L TYPEDEF for SignalParametersT
    if (A2lOnce()) {
        A2lTypedefBegin(SignalParametersT, params, "A2L typedef for SignalParametersT");
        A2lCreateEnumConversion(signal_type_enum, "4 0 \"SINE\" 1 \"SQUARE\" 2 \"TRIANGLE\" 3 \"SAWTOOTH\"");
        A2lTypedefParameterComponent(signal_type, "Signal type", "conv.signal_type_enum", 0, 3);
        A2lTypedefParameterComponent(ampl, "Amplitude", "Volt", 0, 100);
        A2lTypedefParameterComponent(phase, "Phase", "", 0, k2Pi);
        A2lTypedefParameterComponent(offset, "Offset", "Volt", -100, 100);
        A2lTypedefParameterComponent(period, "Period", "s", 0.01, 10.0);
        A2lTypedefParameterComponent(clip_min, "Lower output limit", "Volt", -100, 100);
        A2lTypedefParameterComponent(clip_max, "Upper output limit", "Volt", -100, 100);
        A2lTypedefParameterComponent(sample_time_us, "Delay time in us", "us", 0, 100000);
        A2lTypedefEnd();
    }

    // Create an instance of this signal generators calibration segment
    signal_parameters_.CreateA2lTypedefInstance("SignalParametersT", "Signal parameters for the signal generator");

    // Create the worker thread, it idles until Start()
    thread_ = new std::thread([this]() { Task(); });
}

SignalGenerator::~SignalGenerator() {
    Stop();
    running_ = false;
    if (thread_ != nullptr) {
        if (thread_->joinable()) {
            thread_->join();
        }
        delete thread_;
    }
}

// Start the worker thread cycles, called from the consumer thread
void SignalGenerator::Start() {
    if (run_.load(std::memory_order_relaxed))
        return; // Already running

    // The worker thread is idle and does not push (Stop() waited for it), clear the queue and reset the counters
    // The writes are published to the worker thread by the release store of run_
    queue_.Clear();
    seq_ = 0;
    overflows_ = 0;
    started_.store(false, std::memory_order_relaxed);

    std::cout << "Starting task: " << instance_name_ << std::endl;
    run_.store(true, std::memory_order_release);

    // Wait until the task has executed its first cycle
    // The events and measurements of an instance are created when its instrumentation is executed for the first time,
    // waiting here makes the creation order and so the event ids and the A2L file independent of thread start-up timing
    while (!started_.load(std::memory_order_acquire)) {
        sleepUs(1);
    }
}

// Stop the worker thread cycles, called from the consumer thread
void SignalGenerator::Stop() {
    if (!run_.load(std::memory_order_relaxed))
        return; // Already stopped
    run_.store(false, std::memory_order_relaxed);

    // Wait until the worker thread has finished its current cycle and seen the stop request
    // The acquire load makes its writes of seq_ and overflows_ visible here
    while (active_.load(std::memory_order_acquire)) {
        sleepUs(100);
    }
    std::cout << "Stopped task: " << instance_name_ << ", samples: " << seq_ << ", dropped: " << overflows_ << std::endl;
}

// Get the next sample from the output queue, called from another thread (the main loop)
// Single consumer: only one thread at a time may poll an instance
std::optional<SampleT> SignalGenerator::Poll() {

    // Optional feature:
    // Attach the signal generator instance context, possible with OPTION_DAQ_SPAN_THREAD in xcplib_cfg.h (disabled by default)
    // If not done, the trace will not contain the information which instance was polled, there must be an active  span in the context of the caller as the current main loop does
    // xcp::Scope scope(context_);

    // Create and begin span 'poll'
    DaqSpan(poll);

    // Read a sample from queue
    std::optional<SampleT> sample = queue_.Pop();
    if (!sample) {
        return std::nullopt; // No new sample
    }
    double polled = sample->value;

    SimulateWork(1000); // 1ms

    // Link the span 'poll' to the producer span of the instance which calculated the sample, in the context of the caller
    xcp::LinkSpan(sample->link);

    // Trigger a context aware measurement event 'poll'
    DaqEventCtxVar(poll, // context=<CallerContext>, span='poll'
                   A2L_MEAS_PHYS(polled, "Value polled by another thread", "Volt", -100.0, +100.0));

    return sample;
} // end of span 'poll'

// Cyclic calculation function - runs in a separate thread
void SignalGenerator::Task() {

    // Attach the context propagated to this instance to this thread
    // The context was created in the thread which constructed the instance, a new thread does not inherit a context
    xcp::Scope scope(context_);

    double time = 0;
    double start_time = 0; // Time base of the waveform in s, set at each start

    while (running_) {

        // Stopped: acknowledge the stop request, idle until Start()
        // The acquire load of run_ makes the queue clear and the counter reset of Start() visible here
        if (!run_.load(std::memory_order_acquire)) {
            if (active_.load(std::memory_order_relaxed)) {
                active_.store(false, std::memory_order_release); // Publish seq_ and overflows_ to Stop()
            }
            sleepUs(100);
            continue;
        }
        if (!active_.load(std::memory_order_relaxed)) {
            start_time = static_cast<double>(clockGetMonotonicUs()) / 1000000; // Restart the waveform
            active_.store(true, std::memory_order_relaxed);
        }

        {
            // Create span 'task'
            DaqSpan(task);

            time = static_cast<double>(clockGetMonotonicUs()) / 1000000 - start_time; // time in s since start of the signal generator

            // Calculate the waveform value based on the current time and signal parameters
            {
                // Create and start span 'calc'
                DaqSpan(calc);

                // Calculate value_
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
                SimulateWork(2000); // 2ms
                value_ = Clip((p->ampl * v) + p->offset);
            } // End of span 'calc'

            // Publish the sample for Poll() called from another thread, count dropped samples
            // Context propagation: The sample carries the span context of the current span 'task', Poll() links its span 'poll' to it
            uint64_t time_us = clockGetMonotonicUs();
            if (!queue_.Push(SampleT{value_, time_us, seq_++, xcp::CaptureSpanLink()})) {
                overflows_++;
            }
            uint32_t queue_level = queue_.Count();

            // Measurement
            // Trigger a context aware measurement event 'siggen'
            // Each instance gets its own event instance siggen_<n> and its own measurements <instance_name>.value_, ...
            // The default limits of 64 bit types are +-1e12, the monotonic clock may exceed them (1e12 us are 11.6 days)
            DaqEventCtxVar(siggen, // context='SigGen1' or 'SigGen2', span='task'                                                                                    //
                           A2L_MEAS_PHYS(value_, "Signal generator output", "Volt", -100.0, +100.0),                 //
                           A2L_MEAS_PHYS(time_us, "Time stamp of the sample, clockGetMonotonicUs()", "us", 0, 1e18), //
                           A2L_MEAS(queue_level, "Number of unread samples in the output queue after the push"),     //
                           A2L_MEAS(overflows_, "Number of samples dropped because the output queue was full")       //
            );
        } // end of span 'task'

        // First cycle after Start() done, all instrumentation of the task has been executed once
        if (!started_.load(std::memory_order_relaxed)) {
            started_.store(true, std::memory_order_release);
        }

        // Sleep for sample_time_us microseconds
        sleepUs(signal_parameters_.lock()->sample_time_us);
    }
}

} // namespace signal_generator
