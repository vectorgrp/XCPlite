//-----------------------------------------------------------------------------------------------------
// Waveform signal generator class, instrumented with OpenTelemetry (opentelemetry-cpp)
// See sig_gen.hpp for the comparison with the XCP version
//
// Only the OpenTelemetry API is used here (namespaces opentelemetry::trace, ::metrics, ::context).
// The API does nothing until an SDK is installed, which is done once in main.cpp. This split is intended:
// a library is instrumented against the API only, the application decides if and where the data goes.
//-----------------------------------------------------------------------------------------------------

#include <chrono>   // for std::chrono
#include <cmath>    // for fmod, sin
#include <cstdint>  // for uintxx_t
#include <iostream> // for std::cout
#include <map>      // for std::map
#include <string>   // for std::string
#include <thread>   // for std::thread
#include <utility>  // for std::pair
#include <vector>   // for std::vector

#include "opentelemetry/context/runtime_context.h" // for context::RuntimeContext
#include "opentelemetry/metrics/meter.h"           // for metrics::Meter
#include "opentelemetry/metrics/provider.h"        // for metrics::Provider
#include "opentelemetry/nostd/variant.h"           // for nostd::holds_alternative, nostd::get
#include "opentelemetry/trace/provider.h"          // for trace::Provider
#include "opentelemetry/trace/scope.h"             // for trace::Scope
#include "opentelemetry/trace/span_startoptions.h" // for trace::StartSpanOptions

#include "sig_gen.hpp"

namespace trace_api = opentelemetry::trace;
namespace metrics_api = opentelemetry::metrics;
namespace context_api = opentelemetry::context;
namespace nostd = opentelemetry::nostd;

namespace signal_generator {

constexpr double kPi = 3.14159265358979323846;
constexpr double k2Pi = (kPi * 2);
constexpr double kClipLimit = 60.0; // Output limit in Volt

// Name and version of this instrumented component, the "instrumentation scope"
constexpr const char *kScopeName = "signal_generator";
constexpr const char *kScopeVersion = "1.0.0";

// Attribute key which identifies the instance
// This is the OpenTelemetry answer to the multiple instance problem: not a separate signal per instance,
// but one signal name with an attribute. The backend groups and filters by the attribute.
constexpr const char *kAttrInstance = "siggen.instance";

//-----------------------------------------------------------------------------------------------------
// Metric instruments
// An instrument is created once and shared by all instances, the instance is given as an attribute with every value.
// Metrics are the idiomatic way to record a continuous signal like the generator output. Spans are for operations
// with a begin and an end.

namespace {

struct Instruments {
    // Counter: a value that only increases, here the number of clipped output values
    nostd::unique_ptr<metrics_api::Counter<uint64_t>> clipped_count;
    // Observable gauge: a current value, read by a callback when the SDK exports (once per export interval).
    // The value is sampled at the export rate (1 s in main.cpp), not at the rate of the task loop.
    nostd::shared_ptr<metrics_api::ObservableInstrument> output;
};

Instruments &instruments() {
    static Instruments instance = []() {
        auto meter = metrics_api::Provider::GetMeterProvider()->GetMeter(kScopeName, kScopeVersion);
        Instruments i;
        i.clipped_count = meter->CreateUInt64Counter("siggen.clipped", "Number of clipped output values", "{value}");
        i.output = meter->CreateDoubleObservableGauge("siggen.output", "Signal generator output", "V");
        return i;
    }();
    return instance;
}

} // namespace

//-----------------------------------------------------------------------------------------------------
// Shared helper function, called by all signal generator instances
//
// It does not know for which instance it is working, and it does not need to: StartSpan takes the current span of
// this thread as the parent. The clip span becomes a child of the calc span, which is a child of the cycle span,
// and the cycle span carries the attribute siggen.instance. The backend shows the whole tree.
//
// This is what "ambient context" means: the parent relation is found in thread-local storage, it is not passed
// as a function argument.
// If the helper itself needed the instance name, the idiomatic way would be Baggage (key-value pairs which travel
// with the context), or simply a function argument.
// Returns true in *was_clipped, if the value was limited
static double Clip(double input, bool *was_clipped) {

    // A library function gets its tracer from the global provider, it is cheap and returns the same tracer for the same name
    auto tracer = trace_api::Provider::GetTracerProvider()->GetTracer(kScopeName, kScopeVersion);

    auto span = tracer->StartSpan("clip");
    auto scope = trace_api::Tracer::WithActiveSpan(span);

    double clipped = input;
    if (clipped > kClipLimit)
        clipped = kClipLimit;
    if (clipped < -kClipLimit)
        clipped = -kClipLimit;
    *was_clipped = (clipped != input);

    // Attributes describe the span as a whole
    span->SetAttribute("clip.input", input);
    span->SetAttribute("clip.output", clipped);

    // An event is a timestamped annotation inside a span
    if (*was_clipped) {
        span->AddEvent("clipped", {{"clip.limit", kClipLimit}});
    }

    // A span must be ended explicitly. The scope only makes the span the current span of this thread,
    // its destructor restores the previous current span, it does not end the span.
    span->End();
    return clipped;
}

//-----------------------------------------------------------------------------------------------------

// Constructor - creates the signal generator with the given instance name and parameters
// Runs in the thread which creates the instance (the main thread), inside the "startup" span of main.cpp
SignalGenerator::SignalGenerator(const char *instance_name, const SignalParametersT &params)
    : signal_parameters_(params), instance_name_(instance_name),
      // The tracer of this component
      tracer_(trace_api::Provider::GetTracerProvider()->GetTracer(kScopeName, kScopeVersion)),
      // Rule 1 for threads: capture the context in the parent thread.
      // A new thread does not inherit the current context, it starts with an empty one.
      creator_context_(context_api::RuntimeContext::GetCurrent()),
      // The identity of the current span (trace id, span id, flags), used for span links
      creator_span_context_(trace_api::Tracer::GetCurrentSpan()->GetContext()) {

    // Register the callback of the output gauge for this instance, the state pointer is given back to the callback
    instruments().output->AddCallback(ObserveOutput, this);

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
    instruments().output->RemoveCallback(ObserveOutput, this);
}

// Callback of the observable gauge, called by a thread of the metrics SDK
// One instrument "siggen.output", one time series per value of the attribute siggen.instance
void SignalGenerator::ObserveOutput(metrics_api::ObserverResult observer_result, void *state) {
    const auto *self = static_cast<const SignalGenerator *>(state);
    // The observer result is a variant, depending on the value type of the instrument (int64_t or double)
    if (nostd::holds_alternative<nostd::shared_ptr<metrics_api::ObserverResultT<double>>>(observer_result)) {
        nostd::get<nostd::shared_ptr<metrics_api::ObserverResultT<double>>>(observer_result)
            ->Observe(self->output_.load(std::memory_order_relaxed), {{kAttrInstance, self->instance_name_}});
    }
}

// Get the current value, called from other threads (the main loop)
// No context handling is needed here: the caller has a current span ("mainloop" in main.cpp), the poll span becomes
// its child. The instance is an attribute of the span.
double SignalGenerator::Poll() const {
    auto span = tracer_->StartSpan("poll", {{kAttrInstance, instance_name_}});
    auto scope = trace_api::Tracer::WithActiveSpan(span);

    double polled = output_.load(std::memory_order_relaxed);

    span->SetAttribute("siggen.polled", polled);
    span->End();
    return polled;
}

// Cyclic calculation function - runs in a separate thread
void SignalGenerator::Task() {

    // Rule 2 for threads: attach the captured context in the new thread, or set the parent explicitly.
    // Rule 3: detach when the work ends.
    //
    // Here the context of the creator is attached for the start-up of the task only, so the span "task.start" becomes
    // a child of the span "startup" in main.cpp and belongs to the same trace, although it runs in another thread.
    {
        // Attach returns a token, the context is detached when the token goes out of scope.
        // A token is bound to the thread which created it.
        auto token = context_api::RuntimeContext::Attach(creator_context_);

        auto span = tracer_->StartSpan("task.start", {{kAttrInstance, instance_name_}});
        // ... one-time initialisation of the task would be here ...
        span->End();

        // The same without attaching, with an explicit parent:
        //   trace_api::StartSpanOptions options;
        //   options.parent = creator_span_context_;
        //   auto span = tracer_->StartSpan("task.start", {{kAttrInstance, instance_name_}}, options);
    }
    // From here on this thread has no current span again

    // The cyclic work is not part of the creator's trace. A trace describes one operation from begin to end, a trace
    // which lasts as long as the process would never be complete and could not be displayed.
    // So every cycle is a trace of its own (a new root span), with a span link back to the span which created the
    // instance. A link relates spans of different traces without a parent-child relation.
    const std::map<std::string, std::string> cycle_attributes = {{kAttrInstance, instance_name_}};
    const std::vector<std::pair<trace_api::SpanContext, std::map<std::string, std::string>>> cycle_links = {{creator_span_context_, {}}};

    const auto start_time = std::chrono::steady_clock::now();

    while (running_) {

        const double time = std::chrono::duration<double>(std::chrono::steady_clock::now() - start_time).count(); // time in s since start of the signal generator

        {
            // Root span of this cycle: there is no current span on this thread, so a new trace id is generated.
            // Whether the trace is recorded at all is decided here by the sampler, see main.cpp.
            auto cycle_span = tracer_->StartSpan("siggen.cycle", cycle_attributes, cycle_links);
            auto cycle_scope = trace_api::Tracer::WithActiveSpan(cycle_span);

            // Calculate the waveform value based on the current time and signal parameters
            {
                // Child span: the parent is the current span of this thread, the cycle span
                auto calc_span = tracer_->StartSpan("calc");
                auto calc_scope = trace_api::Tracer::WithActiveSpan(calc_span);

                const SignalParametersT *p = &signal_parameters_;
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

                bool was_clipped = false;
                value_ = Clip((p->ampl * v) + p->offset, &was_clipped); // The clip span becomes a child of the calc span
                output_.store(value_, std::memory_order_relaxed);       // Publish for Poll() and for the output gauge

                // Metric: count the clipped values of this instance
                if (was_clipped) {
                    instruments().clipped_count->Add(1, {{kAttrInstance, instance_name_}});
                }

                calc_span->End();
            }

            // The value as an attribute of the cycle span: one value per recorded trace.
            // The continuous signal is the gauge "siggen.output", see ObserveOutput.
            cycle_span->SetAttribute("siggen.value", value_);
            cycle_span->End();
        }

        // Sleep for delay_us microseconds
        std::this_thread::sleep_for(std::chrono::microseconds(signal_parameters_.delay_us));
    }
}

} // namespace signal_generator
