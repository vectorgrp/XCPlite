// thread_context_demo, OpenTelemetry version
//
// The same demo as ../src, instrumented with opentelemetry-cpp instead of XCP.
// It is for reading and comparing, it is not built by the XCPlite build and has not been compiled.
//
// What it produces:
//   Traces   one trace "startup" (with a child span "task.start" from each generator thread),
//            one trace per generator cycle: siggen.cycle -> calc -> clip,
//            one trace per main loop iteration: mainloop -> poll (SigGen1), poll (SigGen2)
//   Metrics  gauge "siggen.output" and counter "siggen.clipped", each with one time series per siggen.instance
//
// Build:
//   Needs opentelemetry-cpp built with the OTLP HTTP exporter (CMake option OTELCPP_WITH_OTLP_HTTP=ON),
//   which needs protobuf, libcurl and nlohmann-json. Then, in this directory:
//     cmake -B build -S . && cmake --build build
//
// Run:
//   The demo sends OTLP over HTTP to localhost:4318. Something must listen there, for example the all-in-one
//   container grafana/otel-lgtm (OpenTelemetry Collector + Tempo for traces + Prometheus for metrics + Grafana):
//     docker run --rm -p 3000:3000 -p 4317:4317 -p 4318:4318 grafana/otel-lgtm
//     ./build/thread_context_demo_otel
//   then open http://localhost:3000 (Grafana), Explore, data source Tempo for traces or Prometheus for metrics.
//   For traces only, Jaeger is enough:
//     docker run --rm -p 16686:16686 -p 4318:4318 jaegertracing/jaeger
//   then open http://localhost:16686

#include <atomic>   // for std::atomic
#include <chrono>   // for std::chrono
#include <csignal>  // for signal(), SIGINT, SIGTERM
#include <iostream> // for std::cout
#include <memory>   // for std::shared_ptr, std::unique_ptr
#include <thread>   // for std::this_thread
#include <utility>  // for std::move

// OpenTelemetry API: what instrumented code uses
#include "opentelemetry/metrics/provider.h" // for metrics::Provider
#include "opentelemetry/trace/provider.h"   // for trace::Provider

// OpenTelemetry SDK and exporters: only the application's main uses these, to decide where the data goes
#include "opentelemetry/exporters/otlp/otlp_http_exporter_factory.h"
#include "opentelemetry/exporters/otlp/otlp_http_exporter_options.h"
#include "opentelemetry/exporters/otlp/otlp_http_metric_exporter_factory.h"
#include "opentelemetry/exporters/otlp/otlp_http_metric_exporter_options.h"
#include "opentelemetry/sdk/metrics/export/periodic_exporting_metric_reader_factory.h"
#include "opentelemetry/sdk/metrics/export/periodic_exporting_metric_reader_options.h"
#include "opentelemetry/sdk/metrics/meter_context.h"
#include "opentelemetry/sdk/metrics/meter_context_factory.h"
#include "opentelemetry/sdk/metrics/meter_provider.h"
#include "opentelemetry/sdk/metrics/meter_provider_factory.h"
#include "opentelemetry/sdk/metrics/view/view_registry_factory.h"
#include "opentelemetry/sdk/resource/resource.h"
#include "opentelemetry/sdk/trace/batch_span_processor_factory.h"
#include "opentelemetry/sdk/trace/batch_span_processor_options.h"
#include "opentelemetry/sdk/trace/samplers/parent_factory.h"
#include "opentelemetry/sdk/trace/samplers/trace_id_ratio_factory.h"
#include "opentelemetry/sdk/trace/tracer_provider.h"
#include "opentelemetry/sdk/trace/tracer_provider_factory.h"

#include "sig_gen.hpp" // for signal_generator::SignalGenerator

namespace trace_api = opentelemetry::trace;
namespace trace_sdk = opentelemetry::sdk::trace;
namespace metrics_api = opentelemetry::metrics;
namespace metrics_sdk = opentelemetry::sdk::metrics;
namespace resource_sdk = opentelemetry::sdk::resource;
namespace otlp = opentelemetry::exporter::otlp;

//-----------------------------------------------------------------------------------------------------
// Telemetry parameters

constexpr const char *kServiceName = "thread_context_demo";
constexpr const char *kServiceVersion = "0.9";
constexpr const char *kOtlpTracesUrl = "http://localhost:4318/v1/traces";
constexpr const char *kOtlpMetricsUrl = "http://localhost:4318/v1/metrics";

// Fraction of the traces which is recorded and exported, 1.0 = all
// Every generator cycle is a trace of its own. Recording all of them is fine for this demo with its slow cycle,
// a real cyclic task would use a small fraction, for example 0.01.
constexpr double kTraceSampleRatio = 1.0;

//-----------------------------------------------------------------------------------------------------
// Demo signal generator default parameters

constexpr double kPi = 3.14159265358979323846;

// Signal handler for clean shutdown
static std::atomic<bool> running{true};
static void sig_handler(int) { running = false; }

//-----------------------------------------------------------------------------------------------------
// Telemetry setup
// The provider objects own the pipeline: instrumented code -> processor/reader -> exporter -> network.
// They are installed as global providers, instrumented code gets its tracer and meter from there.

static std::shared_ptr<trace_sdk::TracerProvider> gTracerProvider;
static std::shared_ptr<metrics_sdk::MeterProvider> gMeterProvider;

static void InitTelemetry() {

    // The resource describes the process which produces the telemetry, it is attached to all spans and metrics.
    // The backend shows service.name as the name of the application.
    auto resource = resource_sdk::Resource::Create({{"service.name", kServiceName}, {"service.version", kServiceVersion}});

    // Traces
    {
        // Exporter: sends finished spans in the OTLP protocol over HTTP
        otlp::OtlpHttpExporterOptions exporter_options;
        exporter_options.url = kOtlpTracesUrl;
        auto exporter = otlp::OtlpHttpExporterFactory::Create(exporter_options);

        // Processor: collects finished spans in a queue and exports them in batches from a background thread.
        // Ending a span does not block on the network. Spans are dropped when the queue is full.
        trace_sdk::BatchSpanProcessorOptions processor_options;
        processor_options.max_queue_size = 8192;
        processor_options.schedule_delay_millis = std::chrono::milliseconds(1000);
        processor_options.max_export_batch_size = 512;
        auto processor = trace_sdk::BatchSpanProcessorFactory::Create(std::move(exporter), processor_options);

        // Sampler: decides for every new trace (root span) whether it is recorded. The decision is made once at the
        // root and is inherited by all child spans (ParentBased), so a trace is either complete or absent.
        std::shared_ptr<trace_sdk::Sampler> root_sampler = trace_sdk::TraceIdRatioBasedSamplerFactory::Create(kTraceSampleRatio);
        auto sampler = trace_sdk::ParentBasedSamplerFactory::Create(root_sampler);

        gTracerProvider = trace_sdk::TracerProviderFactory::Create(std::move(processor), resource, std::move(sampler));
        std::shared_ptr<trace_api::TracerProvider> api_provider = gTracerProvider;
        trace_api::Provider::SetTracerProvider(api_provider);
    }

    // Metrics
    {
        otlp::OtlpHttpMetricExporterOptions exporter_options;
        exporter_options.url = kOtlpMetricsUrl;
        auto exporter = otlp::OtlpHttpMetricExporterFactory::Create(exporter_options);

        // Reader: collects all instruments once per interval and exports the result.
        // This interval is the time resolution of the metrics.
        metrics_sdk::PeriodicExportingMetricReaderOptions reader_options;
        reader_options.export_interval_millis = std::chrono::milliseconds(1000);
        reader_options.export_timeout_millis = std::chrono::milliseconds(500);
        auto reader = metrics_sdk::PeriodicExportingMetricReaderFactory::Create(std::move(exporter), reader_options);

        auto context = metrics_sdk::MeterContextFactory::Create(metrics_sdk::ViewRegistryFactory::Create(), resource);
        context->AddMetricReader(std::move(reader));

        gMeterProvider = metrics_sdk::MeterProviderFactory::Create(std::move(context));
        std::shared_ptr<metrics_api::MeterProvider> api_provider = gMeterProvider;
        metrics_api::Provider::SetMeterProvider(api_provider);
    }
}

static void CleanupTelemetry() {

    // Export what is still in the queues
    if (gTracerProvider) {
        gTracerProvider->ForceFlush();
    }
    if (gMeterProvider) {
        gMeterProvider->ForceFlush();
    }

    // Remove the global providers, instrumented code falls back to the no-op implementation
    gTracerProvider.reset();
    gMeterProvider.reset();
    trace_api::Provider::SetTracerProvider(std::shared_ptr<trace_api::TracerProvider>());
    metrics_api::Provider::SetMeterProvider(std::shared_ptr<metrics_api::MeterProvider>());
}

//-----------------------------------------------------------------------------------------------------

int main() {

    std::cout << "\nthread_context_demo, OpenTelemetry version\n" << std::endl;
    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    // Install the SDK, must be done before the first tracer or meter is requested
    InitTelemetry();

    // The tracer of the application code in this file
    auto tracer = trace_api::Provider::GetTracerProvider()->GetTracer("thread_context_demo.main", kServiceVersion);

    {
        std::unique_ptr<signal_generator::SignalGenerator> signal_generator_1;
        std::unique_ptr<signal_generator::SignalGenerator> signal_generator_2;

        {
            // Root span "startup": the first span, it generates the trace id of the startup trace
            auto startup_span = tracer->StartSpan("startup");
            // Make it the current span of the main thread until the end of this block,
            // the constructors of the signal generators capture it
            auto startup_scope = trace_api::Tracer::WithActiveSpan(startup_span);

            const signal_generator::SignalParametersT kSignalParameters1 = {
                .ampl = 12.5,
                .phase = 0.0,
                .offset = 0.0,
                .period = 1.0,                                          // s
                .delay_us = 10000,                                      // us
                .signal_type = signal_generator::SignalTypeT::TRIANGLE, // Type of the signal
            };
            signal_generator_1 = std::make_unique<signal_generator::SignalGenerator>("SigGen1", kSignalParameters1);

            const signal_generator::SignalParametersT kSignalParameters2 = {
                .ampl = 80.0,
                .phase = kPi / 2,
                .offset = 0.0,
                .period = 10.0,                                     // s
                .delay_us = 10000,                                  // us
                .signal_type = signal_generator::SignalTypeT::SINE, // Type of the signal
            };
            signal_generator_2 = std::make_unique<signal_generator::SignalGenerator>("SigGen2", kSignalParameters2);

            std::this_thread::sleep_for(std::chrono::milliseconds(100)); // Wait a bit to let the signal generator threads initialize

            // End the startup span. The child spans "task.start" from the generator threads have already ended.
            // This is not required, a child span may outlive its parent.
            startup_span->End();
        } // The scope ends here, the main thread has no current span again

        // Main loop
        std::cout << "Starting main loop..." << std::endl;
        while (running) {

            {
                // One trace per iteration of the main loop
                auto span = tracer->StartSpan("mainloop");
                auto scope = trace_api::Tracer::WithActiveSpan(span);

                // Poll the current values of signal generator 1+2
                // Each call creates a child span "poll" with the attribute siggen.instance
                double channel1 = signal_generator_1->Poll();
                double channel2 = signal_generator_2->Poll();

                span->SetAttribute("channel1", channel1);
                span->SetAttribute("channel2", channel2);
                span->End();
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(100));

        } // while (running)

    } // Destroy signal generator instances before shutting down the telemetry

    CleanupTelemetry();
    return 0;
}
