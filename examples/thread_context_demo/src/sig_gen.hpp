#pragma once

//-----------------------------------------------------------------------------------------------------
// Waveform signal generator class

/*
An instance of class SignalGenerator creates various waveforms, such as sine, square, triangle, sawtooth
Depending on calibration parameters ampl, phase, offset and period in struct SignalParametersT
*/

#include <atomic>   // for std::atomic
#include <cstdint>  // for uintxx_t
#include <optional> // for std::optional
#include <thread>   // for thread

#include "xcplib.hpp" // for xcp::CalSeg, xcp::Context

#include "spsc_queue.hpp" // for SpscQueue

//-----------------------------------------------------------------------------------------------

// Sleeps in the spans which simulate work, for a nicer view in CANape or Perfetto
// Comment out for a performance test
#define OPTION_SIMULATE_WORK

inline void SimulateWork(uint32_t us) {
#ifdef OPTION_SIMULATE_WORK
    sleepUs(us); // from xcplib
#else
    (void)us;
#endif
}

//-----------------------------------------------------------------------------------------------

namespace signal_generator {

enum SignalTypeT : uint8_t {
    SINE = 0,     // Sine wave
    SQUARE = 1,   // Square wave
    TRIANGLE = 2, // Triangle wave
    SAWTOOTH = 3  // Sawtooth wave
};

// Signal parameters struct
struct SignalParametersT {
    double ampl;                              // Amplitude (in Volt)
    double phase;                             // Phase shift in radians (in Rad)
    double offset;                            // Offset (in Volt)
    double period;                            // Period of the signal (in Sec)
    uint32_t delay_us;                        // Task loop delay (Hz) raw in microseconds, physical value defines the sampling rate (1000000/delay_us in Hz) of the signal
    uint8_t /*enum SignalTypeT*/ signal_type; // Type of the signal (in enum SignalTypeT)
};

// @@@@ TODO: handle enums as integer types automatically

// Output sample
struct SampleT {
    double value;     // Signal value (in Volt)
    uint64_t time_us; // Time of calculation, clockGetMonotonicUs() (in us)
    uint32_t seq;     // Sample sequence number, starts with 0, samples dropped before this one: seq - previous seq - 1
};

class SignalGenerator {

  private:
    xcp::CalSeg<SignalParametersT> signal_parameters_; // Wrapped signal parameters struct to enable XCP calibration access

    xcp::Context context_; // XCP context of this instance, makes the instance known to context-aware instrumentation

    const char *instance_name_;        // Instance name
    std::thread *thread_;              // Thread for the signal generator task
    std::atomic<bool> running_{true};  // Stop flag for the worker thread
    std::atomic<bool> started_{false}; // Set by the worker thread after its first cycle

    double value_{0};           // Current value, written and measured in the task thread only
    double normalized_time_{0}; // Current normalized time in [0.0..1.0[

    SpscQueue<SampleT> queue_; // Output samples, written by the task thread, read by Poll()
    uint32_t seq_{0};          // Sequence number of the next sample, written in the task thread only
    uint32_t overflows_{0};    // Number of dropped samples, written in the task thread only

    void Task();

  public:
    // queue_size 1: Poll() returns the latest sample, a new sample overwrites an unread one
    // queue_size > 1: Poll() returns the samples in order, a new sample is dropped when the queue is full
    SignalGenerator(const char *instance_name, const SignalParametersT *params, size_t queue_size);
    ~SignalGenerator();

    // Get the next sample from the output queue, empty if there is no new one
    // May be called from any thread, but only from one thread at a time (single consumer)
    std::optional<SampleT> Poll();
};

} // namespace signal_generator