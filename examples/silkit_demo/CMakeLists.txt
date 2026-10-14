# CMakeLists.txt for the SilKit demo example

cmake_minimum_required(VERSION 3.14)

project(silkit_demo VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

# ---------------------------------------------------------------------------
# SIL Kit and xcplite are both built from source via CMake FetchContent.
# No installed SIL Kit or xcplite is needed. CMake clones the repositories at configure time
# into build/_deps/<name>-src, adds them as subdirectories and builds them together with the demo.
#
# Requires: CMake >= 3.14, git, network access on the first configure.
#
# For local development against checked-out source trees, bypass the git clone with
#   cmake -B build -S . -DFETCHCONTENT_SOURCE_DIR_XCPLITE=../..
#   cmake -B build -S . -DFETCHCONTENT_SOURCE_DIR_SILKIT=<path>/sil-kit
# (a local sil-kit checkout needs its submodules: git submodule update --init --recursive)
# ---------------------------------------------------------------------------

# ---------------------------------------------------------------------------
# SIL Kit
#
# SIL Kit has no dedicated FetchContent documentation, but its build tree defines the alias target
# SilKit::SilKit, the same name the installed package exports, so it can be consumed with add_subdirectory.
# Its build options are option()s, which honor normal variables from the parent project (policy CMP0077,
# sil-kit requires CMake >= 3.13). The third party dependencies asio, fmt and spdlog are git submodules.
# The submodules googletest (SILKIT_BUILD_TESTS) and oatpp (SILKIT_BUILD_DASHBOARD) are not needed and not cloned.
#
# The utilities (sil-kit-registry, sil-kit-system-controller, sil-kit-monitor) are needed to run the demo.
# SIL Kit places its library and the utilities in build/<CONFIG>/, e.g. build/Debug/sil-kit-registry.
# ---------------------------------------------------------------------------
set(SILKIT_GIT_REPOSITORY "https://github.com/vectorgrp/sil-kit" CACHE STRING "SIL Kit git repository")
set(SILKIT_GIT_TAG        "v5.0.7"                               CACHE STRING "SIL Kit git tag, branch or commit")

FetchContent_Declare(silkit
    GIT_REPOSITORY ${SILKIT_GIT_REPOSITORY}
    GIT_TAG        ${SILKIT_GIT_TAG}
    GIT_SHALLOW    TRUE
    GIT_SUBMODULES ThirdParty/asio ThirdParty/fmt ThirdParty/spdlog
)

set(SILKIT_BUILD_DEMOS     OFF)
set(SILKIT_BUILD_TESTS     OFF)
set(SILKIT_BUILD_DOCS      OFF)
set(SILKIT_BUILD_DASHBOARD OFF)
set(SILKIT_BUILD_UTILITIES ON)

# ---------------------------------------------------------------------------
# xcplite
#
# silkit_demo requires the 'shm' configuration (shared-memory multi-application mode).
# Use "default" to compile the participants with individual XCP servers.
# The xcplite build options must be set before FetchContent_MakeAvailable(), see examples/fetchcontent_example.
# XCPLITE_BUILD_TOOLS adds the shm tools, shmtool is used by the demo scripts.
# ---------------------------------------------------------------------------
# Release:
# Needs a release later than V2.2.2. The demo creates and triggers events in different scopes (name lookup),
# which does not compile with V2.2.2, where section registration (OPTION_SECTION_REGISTRATION) is always on.
#set(XCPLITE_GIT_REPOSITORY "https://github.com/vectorgrp/XCPlite" CACHE STRING "xcplite git repository")
#set(XCPLITE_GIT_TAG        "V2.2.3"                               CACHE STRING "xcplite git tag, branch or commit")
# Dev:
set(XCPLITE_GIT_REPOSITORY "https://github.com/RainerZ/XCPlite.git" CACHE STRING "xcplite git repository")
set(XCPLITE_GIT_TAG        "dev-2.3.2"                              CACHE STRING "xcplite git tag, branch or commit")

FetchContent_Declare(xcplite
    GIT_REPOSITORY ${XCPLITE_GIT_REPOSITORY}
    GIT_TAG        ${XCPLITE_GIT_TAG}
    GIT_SHALLOW    TRUE
)

set(XCPLITE_CONFIGURATION  "shm")
set(XCPLITE_BUILD_EXAMPLES OFF)
set(XCPLITE_BUILD_TESTS    OFF)
set(XCPLITE_BUILD_TOOLS    ON)

FetchContent_MakeAvailable(silkit xcplite)

# Place shmtool next to the demo executables in build/
# xcpdaemon is not needed, the demo has its own XCP server participant (cmake --build build --target xcpdaemon)
if(TARGET shmtool)
    set_target_properties(shmtool PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
endif()
if(TARGET xcpdaemon)
    set_target_properties(xcpdaemon PROPERTIES EXCLUDE_FROM_ALL TRUE)
endif()

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
# XCP server
# Optional: Use XCP_MODE_SHM_AUTO to automatically select one participant to create the XCP server locally
# ---------------------------------------------------------------------------
add_executable(SilKitXcpServer src/XcpServer.cpp)

target_include_directories(SilKitXcpServer PRIVATE ${SILKIT_DEMO_INCLUDE_DIR})

target_link_libraries(SilKitXcpServer
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

message(STATUS "Building silkit_demo")
message(STATUS "  SIL Kit source: ${silkit_SOURCE_DIR} (${SILKIT_GIT_TAG})")
message(STATUS "  xcplite source: ${xcplite_SOURCE_DIR}")
message(STATUS "  xcplite configuration: ${XCPLITE_CONFIGURATION}")
message(STATUS "  build type: ${CMAKE_BUILD_TYPE}")
