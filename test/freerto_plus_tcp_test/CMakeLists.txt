# ---------------------------------------------------------------------------
# Test executable
# ---------------------------------------------------------------------------

set(_demo "${CMAKE_CURRENT_SOURCE_DIR}/../../examples/freertos_demo/freertos_plus_tcp_emu_demo")

add_executable(freerto_plus_tcp_test
    "${_demo}/src/main.c"
    src/main.c
    src/faults.c
)

target_compile_definitions(freerto_plus_tcp_test PRIVATE
    DEMO_LOOPBACK_TEST=1
)

target_link_libraries(freerto_plus_tcp_test PRIVATE
    xcplite
)

# DAQ tests use 32-bit absolute addresses for static data.
target_link_options(freerto_plus_tcp_test PRIVATE -no-pie)

target_compile_options(freerto_plus_tcp_test PRIVATE
    -Wall
    -Wextra
    -Wno-format-zero-length
)

# ---------------------------------------------------------------------------
# Fault injection
# ---------------------------------------------------------------------------

# Linker wrappers inject failures while forwarding normal calls to the real implementations.
foreach(_symbol
    queueRelease
    FreeRTOS_socket
    FreeRTOS_bind
    FreeRTOS_setsockopt
    FreeRTOS_sendto
    FreeRTOS_recvfrom
    FreeRTOS_closesocket
)
    target_link_options(freerto_plus_tcp_test PRIVATE "LINKER:--wrap=${_symbol}")
endforeach()

# ---------------------------------------------------------------------------
# CTest registration
# ---------------------------------------------------------------------------

add_test(NAME freerto_plus_tcp_test
    COMMAND freerto_plus_tcp_test
)

set_tests_properties(freerto_plus_tcp_test PROPERTIES
    LABELS "integration;freertos"
    TIMEOUT 30
)
