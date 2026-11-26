# thread_context_demo, OpenTelemetry version
#
# Standalone project, not built from the XCPlite root CMakeLists.txt and not dependent on xcplite.
# Requires an installed opentelemetry-cpp, built with the OTLP HTTP exporter (OTELCPP_WITH_OTLP_HTTP=ON).
#
#   cmake -B build -S . [-DCMAKE_PREFIX_PATH=<opentelemetry-cpp install prefix>]
#   cmake --build build
#   ./build/thread_context_demo_otel

cmake_minimum_required(VERSION 3.16)
project(thread_context_demo_otel LANGUAGES CXX)

# C++20 for designated initializers in main.cpp
# The C++ standard must be compatible with the one opentelemetry-cpp was built with
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(opentelemetry-cpp CONFIG REQUIRED)
find_package(Threads REQUIRED)

add_executable(thread_context_demo_otel src/main.cpp src/sig_gen.cpp)

# The exporter targets pull in the SDK and the API
target_link_libraries(thread_context_demo_otel PRIVATE
    opentelemetry-cpp::otlp_http_exporter
    opentelemetry-cpp::otlp_http_metric_exporter
    Threads::Threads)
