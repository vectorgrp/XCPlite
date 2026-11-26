#pragma once

//-----------------------------------------------------------------------------------------------------
// Waveform signal generator class, instrumented with OpenTelemetry (opentelemetry-cpp) instead of XCP
//
// This is the same class as in ../src/sig_gen.hpp, without XCP.
// It is not built by the XCPlite build, it is meant for reading, see main.cpp for how to build and run it.
//
// How the two worlds compare:
//
//   XCPlite prototype (../src)                       OpenTelemetry (this file)
//   ---------------------------------------------    ------------------------------------------------------------
//   xcp::Context context_{"SigGen1"}                 no object: the instance is an ATTRIBUTE on spans and metrics
//   xcp::Scope scope(context_)                       trace::Scope scope(span) makes a SPAN current, not an instance
//   DaqSpan(calc)                                    tracer->StartSpan("calc") ... span->End()
//   DaqEventCtxVar(siggen, A2L_MEAS(value_))         a METRIC (gauge "siggen.output") with attribute siggen.instance
//   one XCP event per instance and code location     nothing is declared in advance, every span is a self-describing record
//-----------------------------------------------------------------------------------------------------

#include <atomic>  // for std::atomic
#include <cstdint> // for uintxx_t
#include <thread>  // for std::thread

#include "opentelemetry/context/context.h"       // for context::Context
#include "opentelemetry/metrics/observer_result.h" // for metrics::ObserverResult
#include "opentelemetry/nostd/shared_ptr.h"      // for nostd::shared_ptr
#include "opentelemetry/trace/span_context.h"    // for trace::SpanContext
#include "opentelemetry/trace/tracer.h"          // for trace::Tracer

namespace signal_generator {

enum SignalTypeT : uint8_t {
    SINE = 0,     // Sine wave
    SQUARE = 1,   // Square wave
    TRIANGLE = 2, // Triangle wave
    SAWTOOTH = 3  // Sawtooth wave
};

// Signal parameters struct
// In the XCP version this is a calibration segment (xcp::CalSeg), adjustable at runtime by the XCP tool.
// OpenTelemetry is an observability standard, it has no concept for changing parameters, so they are constant here.
struct SignalParametersT {
    double ampl;         // Amplitude
    double phase;        // Phase shift in radians
    double offset;       // Offset
    double period;       // Period in seconds
    uint32_t delay_us;   // Delay in microseconds for the task loop
    uint8_t signal_type; // Type of the signal (SignalTypeT)
};

class SignalGenerator {

  private:
    const SignalParametersT signal_parameters_; // Signal parameters

    const char *instance_name_;       // Instance name, becomes the attribute "siggen.instance"
    std::thread *thread_{nullptr};    // Thread for the signal generator task
    std::atomic<bool> running_{true}; // Stop flag for the worker thread

    double value_{0};           // Current value, written in the task thread only
    double normalized_time_{0}; // Current normalized time in [0.0..1.0[

    std::atomic<double> output_{0}; // Copy of value_ for thread safe access from other threads

    // The tracer is a named factory for spans. It has no id and holds no state of a trace.
    // The name identifies the instrumented component ("instrumentation scope") and is attached to every span it creates.
    opentelemetry::nostd::shared_ptr<opentelemetry::trace::Tracer> tracer_;

    // Captured in the constructor, in the thread which creates the instance:
    // - the whole context, to continue the creator's trace in the task thread (parent-child relation)
    // - the identity (trace id + span id) of the creator's span, to link to it from other traces (span link)
    opentelemetry::context::Context creator_context_;
    opentelemetry::trace::SpanContext creator_span_context_;

    void Task();

    // Callback of the observable gauge "siggen.output", called by the metrics SDK once per export interval
    static void ObserveOutput(opentelemetry::metrics::ObserverResult observer_result, void *state);

  public:
    SignalGenerator(const char *instance_name, const SignalParametersT &params);
    ~SignalGenerator();

    SignalGenerator(const SignalGenerator &) = delete;
    SignalGenerator &operator=(const SignalGenerator &) = delete;

    // Get the current value with instrumentation, may be called from any thread
    double Poll() const;
};

} // namespace signal_generator
