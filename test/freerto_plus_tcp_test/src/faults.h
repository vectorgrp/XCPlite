#pragma once

// Linker-wrapper controls shared by the test task and socket calls.
#include "sockets.h"
#include <limits.h>
#include <stdatomic.h>

#define TEST_XCP_PORT 5555U

// Use real socket calls, or repeat an injected send result until cleared.
#define FAULT_PASSTHROUGH INT32_MIN
#define FAULT_REPEAT_FOREVER UINT_MAX

// Startup failures are consumed once; close failures use a retry count.
extern bool fail_open;
extern bool fail_bind;
extern bool fail_timeout;
extern unsigned close_failures_remaining;

// Track sockets until their close requests are accepted by the IP task.
extern atomic_uint owned_sockets;
extern _Atomic(Socket_t) server_socket;

// Send injection affects only fault_socket; send_calls counts all send attempts.
extern _Atomic(Socket_t) fault_socket;
extern atomic_int_least32_t send_result;
extern atomic_uint send_failures_remaining;
extern atomic_uint send_calls;

// The next receive returns recv_result, then resumes normal stack calls.
extern int32_t recv_result;

// Verify that the TX worker retains its queued datagram throughout retries.
extern atomic_uint queue_releases;
extern atomic_bool check_queue_ownership;
