# CMakeLists.txt for the SilKit demo example

cmake_minimum_required(VERSION 3.13)

project(silkit_demo VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---------------------------------------------------------------------------
# Find SilKit
#
# Point CMake to your SilKit installation via one of:
#   cmake -DSilKit_DIR=<path>/SilKit -B build
#   cmake -DCMAKE_PREFIX_PATH=<path-containing-SilKit-dir> -B build
#
# When building against the sil-kit git repo build tree use:
#   cmake -DSilKit_DIR=<repo>/_build/debug/SilKit -B build
# ---------------------------------------------------------------------------
find_package(SilKit REQUIRED)

# ---------------------------------------------------------------------------
# Find xcplite
#
# Point CMake to your xcplite installation via one of:
#   cmake -Dxcplite_DIR=<path>/lib/cmake/xcplite -B build
#   cmake -DCMAKE_PREFIX_PATH=<path-to-xcplite-install-root> -B build
# ---------------------------------------------------------------------------
find_package(xcplite REQUIRED)

# ---------------------------------------------------------------------------
# Helper include directory (bundled from the SilKit demo tree)
# ---------------------------------------------------------------------------
set(SILKIT_DEMO_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include)

# ---------------------------------------------------------------------------
# Publisher demo
# ---------------------------------------------------------------------------
add_executable(SilKitDemoPublisher src/PublisherDemo.cpp)

target_include_directories(SilKitDemoPublisher PRIVATE ${SILKIT_DEMO_INCLUDE_DIR})

target_link_libraries(SilKitDemoPublisher
    PRIVATE
        SilKit::SilKit
        xcplite::xcplite
)

# ---------------------------------------------------------------------------
# Subscriber demo
# ---------------------------------------------------------------------------
add_executable(SilKitDemoSubscriber src/SubscriberDemo.cpp)

target_include_directories(SilKitDemoSubscriber PRIVATE ${SILKIT_DEMO_INCLUDE_DIR})

target_link_libraries(SilKitDemoSubscriber
    PRIVATE
        SilKit::SilKit
        xcplite::xcplite
)

# ---------------------------------------------------------------------------
# Compiler warnings
# ---------------------------------------------------------------------------
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    target_compile_options(SilKitDemoPublisher  PRIVATE -Wall -Wextra)
    target_compile_options(SilKitDemoSubscriber PRIVATE -Wall -Wextra)
elseif(MSVC)
    target_compile_options(SilKitDemoPublisher  PRIVATE /W3)
    target_compile_options(SilKitDemoSubscriber PRIVATE /W3)
endif()

message(STATUS "silkit_demo: SilKit version  : ${SilKit_VERSION}")
message(STATUS "silkit_demo: xcplite version : ${xcplite_VERSION}")
