cmake_minimum_required(VERSION 3.10)

project(cmp_demo VERSION 1.0.0 LANGUAGES C)

# Standalone project, like examples/external_example: it is NOT built from the xcplite root
# CMakeLists, it consumes an INSTALLED xcplite. See README.md.
#
# xcplite must be installed from the "raw" configuration, which enables the raw Ethernet
# transport (OPTION_ENABLE_UDP_RAW):
#
#   cd <xcplite>
#   cmake -B build-raw -S . -DXCPLITE_CONFIGURATION=raw -DCMAKE_INSTALL_PREFIX=<install>
#   cmake --build build-raw --target install
#
# then point this project at it:
#
#   cmake -B build -S . -Dxcplite_DIR=<install>/lib/cmake/xcplite
#   cmake --build build

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

find_package(xcplite REQUIRED)
find_package(Threads REQUIRED)

# The application supplies the Ethernet HAL backend itself.
#
# socket_raw_hal_cmp.c defines all six eth_hal_* functions that libxcplite's socket_raw.c
# needs. Because libxcplite is a STATIC library, the linker only pulls an archive member to
# resolve an undefined symbol - and these are already defined here, so a built in backend
# is never pulled in and there is no symbol clash.
# This would NOT hold for a shared libxcplite, which would resolve its own symbols internally.
add_executable(cmp_demo
    src/main.c
    src/socket_raw_hal_cmp.c
    src/cmp_transport_udp.c
    src/cmp_rest.c
    src/cmp_discovery.c
    src/cmp.c
)

target_link_libraries(cmp_demo PRIVATE xcplite::xcplite Threads::Threads)

# OPTION_UDP_RAW_HAL_EXTERNAL tells the installed socket_raw_hal.h that the application
# supplies the backend. On Linux the library has a built in AF_PACKET backend which the
# static archive rule above already keeps out, so the option only documents the intent.
# On macOS and Windows there is no built in backend and the header refuses to compile
# without it - and the LIBRARY has to be built with it too, for example:
#   cmake -B build-raw -S . -DXCPLITE_CONFIGURATION=raw \
#         -DCMAKE_C_FLAGS="-DOPTION_UDP_RAW_HAL_EXTERNAL"
target_compile_definitions(cmp_demo PRIVATE OPTION_UDP_RAW_HAL_EXTERNAL)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(cmp_demo PRIVATE -Wall -Wextra -pedantic)
endif()

# Unit test for the CMP envelope codec. Links cmp.c only: no sockets, no libxcplite, so it
# runs anywhere and is the fastest way to check a change to the wire format. Its golden
# vectors come from the sample PCAPNG files of the ASAM CMP 1.1.0 specification.
add_executable(cmp_codec_test
    test/cmp_codec_test.c
    src/cmp.c
)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(cmp_codec_test PRIVATE -Wall -Wextra -pedantic)
endif()

enable_testing()
add_test(NAME cmp_codec_test COMMAND cmp_codec_test)

message(STATUS "Building cmp_demo")
message(STATUS "  xcplite version: ${xcplite_VERSION}")
message(STATUS "  Ethernet HAL   : supplied by this project (src/socket_raw_hal_cmp.c)")
message(STATUS "  CMP envelope   : ASAM CMP 1.1, Ethernet payload (0x08) over UDP")
