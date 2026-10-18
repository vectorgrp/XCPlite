
/*
 * XCPlite FreeRTOS-Plus-TCP Demo
 *
 * Runs the XCPlite measurement and calibration example on Linux using
 * the FreeRTOS POSIX port and native FreeRTOS-Plus-TCP UDP sockets.
 *
 * Network modes:
 *   - Loopback: internal FreeRTOS-Plus-TCP communication for integration tests.
 *   - Linux:    external network interface for connecting an XCP client.
 *
 * See README.md for build instructions and network configuration.
 */

#include "FreeRTOS.h"
#include "FreeRTOS_IP.h"
#include "FreeRTOS_Routing.h"
#include "task.h"

#include <arpa/inet.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/random.h>

#if defined(DEMO_LOOPBACK_TEST)
extern void demoApplication(void);
#else
#include "xcp_demo.h"
#endif

#if defined(DEMO_LOOPBACK_TEST) || (defined(DEMO_NETWORK_LOOPBACK) && DEMO_NETWORK_LOOPBACK)
#define DEMO_USE_LOOPBACK 1
#else
#define DEMO_USE_LOOPBACK 0
#endif

#define DEMO_NETWORK_TIMEOUT_MS 10000U

#if DEMO_USE_LOOPBACK
extern NetworkInterface_t *pxLoopback_FillInterfaceDescriptor(BaseType_t index, NetworkInterface_t *interface);
#else
extern NetworkInterface_t *pxLinux_FillInterfaceDescriptor(BaseType_t index, NetworkInterface_t *interface);
#endif

/*
 * Start the XCP application only after FreeRTOS-Plus-TCP is operational.
 * Socket operations must run in an application task, not the IP task.
 */
static void applicationTask(void *arg) {
    (void)arg;

    TickType_t start = xTaskGetTickCount();

    while (FreeRTOS_IsNetworkUp() == pdFALSE) {
        if ((xTaskGetTickCount() - start) >= pdMS_TO_TICKS(DEMO_NETWORK_TIMEOUT_MS)) {
            fprintf(stderr, "Network startup timed out. Check interface configuration and permissions.\n");
            exit(EXIT_FAILURE);
        }

        vTaskDelay(1);
    }

#if defined(DEMO_LOOPBACK_TEST)
    demoApplication();
#else
    if (!xcp_demo_init()) {
        fprintf(stderr, "Failed to initialize XCPlite demo.\n");
        exit(EXIT_FAILURE);
    }

    printf("XCPlite ready: XCP on UDP/IPv4\n");
#endif

    vTaskDelete(NULL);
}

int main(int argc, char *argv[]) {
    /* FreeRTOS-Plus-TCP retains these descriptors for the lifetime of the stack. */
    static NetworkInterface_t interface;
    static NetworkEndPoint_t endpoint;

    const uint8_t dns[4] = {0, 0, 0, 0};
    const uint8_t mac[6] = {0x02, 0, 0, 0, 0, 1};

    uint8_t ip[4];
    uint8_t mask[4];
    uint8_t gateway[4];

    setvbuf(stdout, NULL, _IONBF, 0);

    printf("XCPlite FreeRTOS-Plus-TCP Demo\n");
    printf("-------------------------\n");

#if DEMO_USE_LOOPBACK
    (void)argc;
    (void)argv;

    const uint8_t loopback_ip[4] = {127, 0, 0, 1};
    const uint8_t loopback_mask[4] = {255, 0, 0, 0};
    const uint8_t loopback_gateway[4] = {0, 0, 0, 0};

    memcpy(ip, loopback_ip, sizeof(ip));
    memcpy(mask, loopback_mask, sizeof(mask));
    memcpy(gateway, loopback_gateway, sizeof(gateway));

    printf("Interface : FreeRTOS-Plus-TCP internal loopback\n");
    printf("Note      : External XCP clients cannot connect.\n");

    if (pxLoopback_FillInterfaceDescriptor(0, &interface) == NULL) {
        fprintf(stderr, "Failed to initialize loopback interface.\n");
        return EXIT_FAILURE;
    }
#else
    /* Use a free LAN IP address, distinct from the Linux host address. */
    if (argc < 2 || argc > 4 || inet_pton(AF_INET, argv[1], ip) != 1 || inet_pton(AF_INET, argc > 2 ? argv[2] : "255.255.255.0", mask) != 1 ||
        inet_pton(AF_INET, argc > 3 ? argv[3] : "0.0.0.0", gateway) != 1 || ip[0] == 0 || ip[0] == 127 || ip[0] >= 224) {

        fprintf(stderr, "Usage: %s IP_ADDRESS [NETMASK [GATEWAY]]\n", argv[0]);
        fprintf(stderr, "Use an unused LAN IP address, distinct from the Linux host.\n");
        return EXIT_FAILURE;
    }

    printf("Interface : FreeRTOS-Plus-TCP Linux network driver\n");
    printf("IP        : %s\n", argv[1]);
    printf("MAC       : 02:00:00:00:00:01\n");

    if (pxLinux_FillInterfaceDescriptor(0, &interface) == NULL) {
        fprintf(stderr, "Failed to initialize Linux network interface.\n");
        return EXIT_FAILURE;
    }
#endif

    /* Configure the IPv4 endpoint and initialize the network stack. */
    FreeRTOS_FillEndPoint(&interface, &endpoint, ip, mask, gateway, dns, mac);

    if (FreeRTOS_IPInit_Multi() != pdPASS) {
        fprintf(stderr, "FreeRTOS-Plus-TCP initialization failed.\n");
        return EXIT_FAILURE;
    }

    /* The application task waits for networking before starting XCPlite. */
    if (xTaskCreate(applicationTask, "application", configMINIMAL_STACK_SIZE * 2U, NULL, tskIDLE_PRIORITY + 1U, NULL) != pdPASS) {
        fprintf(stderr, "Failed to create application task.\n");
        return EXIT_FAILURE;
    }

    vTaskStartScheduler();

    fprintf(stderr, "FreeRTOS scheduler exited unexpectedly.\n");
    return EXIT_FAILURE;
}

/* FreeRTOS-Plus-TCP random-number hook, backed by the Linux OS. */
BaseType_t xApplicationGetRandomNumber(uint32_t *number) {
    if (number == NULL) {
        return pdFALSE;
    }

    uint8_t *dst = (uint8_t *)number;
    size_t remaining = sizeof(*number);

    while (remaining > 0U) {
        ssize_t n = getrandom(dst, remaining, 0);

        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n <= 0) {
            return pdFALSE;
        }

        dst += (size_t)n;
        remaining -= (size_t)n;
    }

    return pdTRUE;
}
