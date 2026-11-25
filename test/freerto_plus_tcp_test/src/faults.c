// GNU linker wrappers intercept selected calls; other calls use the real stack.
#include "faults.h"
#include "../../support/check.h"
#include "FreeRTOS_IP.h"
#include "queue.h"

bool fail_open;
bool fail_bind;
bool fail_timeout;
unsigned close_failures_remaining;

atomic_uint owned_sockets;
_Atomic(Socket_t) server_socket;

_Atomic(Socket_t) fault_socket;
atomic_int_least32_t send_result = FAULT_PASSTHROUGH;
atomic_uint send_failures_remaining;
atomic_uint send_calls;
int32_t recv_result = FAULT_PASSTHROUGH;

atomic_uint queue_releases;
atomic_bool check_queue_ownership;
static const void *retry_buffer;
static size_t retry_size;

// Socket lifecycle: inject startup failures and track accepted close requests.
Socket_t __real_FreeRTOS_socket(BaseType_t, BaseType_t, BaseType_t);
Socket_t __wrap_FreeRTOS_socket(BaseType_t domain, BaseType_t type, BaseType_t protocol) {
    if (fail_open) {
        fail_open = false;
        return FREERTOS_INVALID_SOCKET;
    }
    Socket_t socket = __real_FreeRTOS_socket(domain, type, protocol);
    if (xSocketValid(socket) == pdTRUE) {
        atomic_fetch_add(&owned_sockets, 1);
    }
    return socket;
}

BaseType_t __real_FreeRTOS_bind(Socket_t, const struct freertos_sockaddr *, socklen_t);
BaseType_t __wrap_FreeRTOS_bind(Socket_t socket, const struct freertos_sockaddr *address, socklen_t size) {
    if (fail_bind) {
        fail_bind = false;
        return -pdFREERTOS_ERRNO_EADDRINUSE;
    }
    BaseType_t result = __real_FreeRTOS_bind(socket, address, size);
    if (result == 0 && address != NULL && address->sin_port == FreeRTOS_htons(TEST_XCP_PORT)) {
        atomic_store(&server_socket, socket);
    }
    return result;
}

BaseType_t __real_FreeRTOS_setsockopt(Socket_t, int32_t, int32_t, const void *, size_t);
BaseType_t __wrap_FreeRTOS_setsockopt(Socket_t socket, int32_t level, int32_t option, const void *value, size_t size) {
    if (fail_timeout && option == FREERTOS_SO_RCVTIMEO) {
        fail_timeout = false;
        return -pdFREERTOS_ERRNO_EINVAL;
    }
    return __real_FreeRTOS_setsockopt(socket, level, option, value, size);
}

// Send failures are restricted to one socket so the loopback client stays usable.
int32_t __real_FreeRTOS_sendto(Socket_t, const void *, size_t, BaseType_t, const struct freertos_sockaddr *, socklen_t);
int32_t __wrap_FreeRTOS_sendto(Socket_t socket, const void *buffer, size_t size, BaseType_t flags, const struct freertos_sockaddr *destination, socklen_t length) {
    atomic_fetch_add(&send_calls, 1);
    if (socket == atomic_load(&fault_socket)) {
        if (atomic_load(&check_queue_ownership)) {
            // Retries must retain the same queue buffer, length, and payload.
            CHECK(atomic_load(&queue_releases) == 0, "queue released before send completed");
            if (retry_buffer == NULL) {
                retry_buffer = buffer;
                retry_size = size;
            }
            CHECK(buffer == retry_buffer && size == retry_size, "retry changed the queued datagram");
            CHECK(size == 12);
            for (size_t i = 4; i < size; i++) {
                CHECK(((const uint8_t *)buffer)[i] == 0x42);
            }
        }
        int32_t result = atomic_load(&send_result);
        if (result != FAULT_PASSTHROUGH) {
            unsigned remaining = atomic_load(&send_failures_remaining);
            if (remaining != FAULT_REPEAT_FOREVER && atomic_fetch_sub(&send_failures_remaining, 1) == 1) {
                atomic_store(&send_result, FAULT_PASSTHROUGH);
            }
            return result;
        }
    }
    return __real_FreeRTOS_sendto(socket, buffer, size, flags, destination, length);
}

// Receive results are injected once, including timeout and interruption errors.
int32_t __real_FreeRTOS_recvfrom(ConstSocket_t, void *, size_t, BaseType_t, struct freertos_sockaddr *, socklen_t *);
int32_t __wrap_FreeRTOS_recvfrom(ConstSocket_t socket, void *buffer, size_t size, BaseType_t flags, struct freertos_sockaddr *source, socklen_t *length) {
    if (recv_result != FAULT_PASSTHROUGH) {
        int32_t result = recv_result;
        recv_result = FAULT_PASSTHROUGH;
        return result;
    }
    return __real_FreeRTOS_recvfrom(socket, buffer, size, flags, source, length);
}

BaseType_t __real_FreeRTOS_closesocket(Socket_t);
BaseType_t __wrap_FreeRTOS_closesocket(Socket_t socket) {
    if (close_failures_remaining != 0) {
        --close_failures_remaining;
        return -1;
    }
    BaseType_t result = __real_FreeRTOS_closesocket(socket);
    if (result == 1) {
        CHECK(atomic_fetch_sub(&owned_sockets, 1) != 0);
    }
    return result;
}

// Count releases after forwarding to the actual XCPlite queue implementation.
void __real_queueRelease(tQueueHandle, const tQueueBuffer *);
void __wrap_queueRelease(tQueueHandle queue, const tQueueBuffer *buffer) {
    __real_queueRelease(queue, buffer);
    atomic_fetch_add(&queue_releases, 1);
}
