// XCP FreeRTOS demo application


#include "main.h"
#include "FreeRTOS.h"
#include "cmsis_os2.h"
#include "lwip.h"
#include "spi.h"
#include "usart.h"
#include "gpio.h"
#include "assert.h"
#include <math.h>
#include <stdio.h>


#define OPTION_SERIAL_PRINTF
#define OPTION_CMSIS 


//----------------------------------------------------------------------------------------------------
// XCP

// XCPlite headers
#ifdef __cplusplus
auto params = parameters_calseg.lock();
#include "d:/git/XCPlite-RainerZ/inc/xcplib.hpp"   // for libxcplite application programming interface
#else
#include "d:/git/XCPlite-RainerZ/inc/xcplib.h"   // for libxcplite application programming interface
#endif

// XCPlite parameters
#define XCP_PROJECT_NAME "esp32_freertos_demo"
#define XCP_PROJECT_VERSION "V100"
#define XCP_USE_TCP false
#define XCP_SERVER_PORT 5555
#define XCP_QUEUE_SIZE (1024 * 8)
#define XCP_LOG_LEVEL 4 // 3 - Info, 4 - Print XCP commands, 5 - Debug


bool startXcpServer() {
    
    XcpSetLogLevel(XCP_LOG_LEVEL);
    XcpCreateEpk(XCP_PROJECT_VERSION);
    
    const uint8_t bindAny[4] = {0, 0, 0, 0};
    if (!XcpInit(XCP_PROJECT_NAME, XCP_PROJECT_VERSION, XCP_MODE_LOCAL)) {
        printf("XcpInit failed\n");
        return false;
    }

    if (!XcpEthServerInit(bindAny, XCP_SERVER_PORT, XCP_USE_TCP, XCP_QUEUE_SIZE)) {
        printf("XcpEthServerInit failed\n");
        return false;
    }

    return true;
}



//----------------------------------------------------------------------------------------------------
// Demo tasks

// Task settings

#define DEMO_TASK_CORE 1 // Pin both tasks to

#define FASTTASK_PRIORITY (configMAX_PRIORITIES - 1)
#define FASTTASK_STACKSIZE 4096
#define FASTTASK_PERIOD_MIN_MS  1
#define FASTTASK_PERIOD_MAX_MS 100

#define SLOWTASK_PRIORITY 3
#define SLOWTASK_STACKSIZE 4096
#define SLOWTASK_PERIOD_MIN_MS 1
#define SLOWTASK_PERIOD_MAX_MS 1000

// Task helper functions

#ifdef OPTION_CMSIS

osThreadId_t fastTaskHandle;
const osThreadAttr_t fastTaskHandle_attributes = {
  .name = "fastTask",
  .stack_size = FASTTASK_STACKSIZE,
  .priority = (osPriority_t) FASTTASK_PRIORITY,
};

osThreadId_t slowTaskHandle;
const osThreadAttr_t slowTaskHandle_attributes = {
  .name = "fastTask",
  .stack_size = SLOWTASK_STACKSIZE,
  .priority = (osPriority_t) SLOWTASK_PRIORITY,
};

#else

TaskHandle_t fastTaskHandle;
TaskHandle_t slowTaskHandle;

#endif


#ifdef OPTION_CMSIS
static bool createDemoTask(TaskFunction_t taskCode, const char *name, const uint32_t stackDepth, UBaseType_t priority, osThreadId_t *taskHandle) {
    if (priority > SLOWTASK_PRIORITY)
    *taskHandle = osThreadNew(taskCode, NULL, &fastTaskHandle_attributes);
    else
    *taskHandle = osThreadNew(taskCode, NULL, &slowTaskHandle_attributes);
    return *taskHandle!=NULL;
#else
static bool createDemoTask(TaskFunction_t taskCode, const char *name, const uint32_t stackDepth, UBaseType_t priority, TaskHandle_t *taskHandle) {
  #ifdef DEMO_TASK_CORE
    return (pdPASS==xTaskCreatePinnedToCore(taskCode, name, stackDepth, nullptr, priority, taskHandle, DEMO_TASK_CORE));
  #else
    return (pdPASS==xTaskCreate(taskCode, name, stackDepth, NULL, priority, taskHandle));
  #endif
#endif
}


//----------------------------------------------------------------------------------------------------
// Measurement variables



// Global measurement values
uint16_t global_counter = 0;

 // Sine signal on variable channel1
#define SLOWTASK_PHASE_STEP_RAD 0.1f
#define SINE_PERIOD_RAD 6.28318530717958647692f
float channel1 = 0.0f;


static uint32_t fastTaskOverruns = 0;
static uint32_t slowTaskOverruns = 0;



//----------------------------------------------------------------------------------------------------
// Calibration parameters


// Global calibration parameter constants
struct parameters {
    uint32_t fast_task_period_ms; // Period of measurement task 1 in milliseconds
    uint32_t slow_task_period_ms; // Period of measurement task 2 in milliseconds
    uint16_t counter_max;         // Counter wrap-around value for the global_counter incremented in fastTask
    float amplitude;              // Amplitude for the sine signal generator in slowTask
};

// Default calibration parameters (default/reference page)
// &parameters is the A2l file address of the calibration parameter segment 'parameters'
// Typename and variable name must be identical
const struct parameters parameters = {
    .fast_task_period_ms = 2,  // 2 ms = 500 Hz
    .slow_task_period_ms = 10, // 10 ms = 100 Hz
    .counter_max = 1000,
    .amplitude = 1.0f,
};

// Declare a calibration segment that wraps 'parameters' for thread-safe and consistent access.
// This creates:
//  - a linker-section 'xcp_cals' descriptor used by XcpInit() for registration
//  - an internal calibration segment index initialized by XcpInit()
//  - the typed C++ handle 'parameters_calseg' used by the tasks below
// The offline A2L generator currently assumes that the struct type name and default-parameter variable name are identical.
#ifdef __cplusplus
CalSegDeclRef(parameters, parameters_calseg);
#else
CalSegDecl(parameters);
#endif

// clamp_parameter calibration parameters to a given value range
#define clamp_parameter(x, y, min, max) do { if ((y) < (min)) (x) = (min); else if ((y) > (max)) (x) = (max); else (x) = (y); } while (0)



//----------------------------------------------------------------------------------------------------
// Tasks


// High priority fast task
static void fastTask(void *parameter) {
    (void)parameter;

    // Volatile keeps this local measurement variable visible in optimized builds,
    // The offline A2L generator can discover it in the ELF file and associate it to the functions DAQ event trigger.
    volatile uint16_t counter = 0;

#ifdef OPTION_SERIAL_PRINTF
    printf("fastTask started\n");
    printf("  frameaddr = %p\n", xcp_get_frame_addr());
    printf("  &counter = %p\n", &counter);
#endif

    // Create a DAQ event named 'fastTask'
    DaqCreateEvent(fastTask);

    TickType_t lastWakeTime = xTaskGetTickCount();
    for (;;) {

        uint32_t period_ms;

        // Flash IO PIN
        //HAL_GPIO_WritePin(FASTTASK_GPIO_Port, FASTTASK_Pin, GPIO_PIN_SET);


        // Lock the calibration segment 'parameters' for thread-safe and consistent access
        // There is no blocking mutex hold during the lock, only atomics used.
        {
#ifdef __cplusplus
            auto params = parameters_calseg.lock();
#else
            struct parameters *params = (struct parameters *)CalSegLock(parameters);
#endif

            // Save the task period parameter, don't delay during the lock to give XCP a chance to modify the parameters.
            clamp_parameter(period_ms, params->fast_task_period_ms, FASTTASK_PERIOD_MIN_MS, FASTTASK_PERIOD_MAX_MS);

            counter++;
            if (counter > params->counter_max) {
                counter = 0;
            }
            global_counter++;
            if (global_counter > params->counter_max) {
                global_counter = 0;
            }

#ifndef __cplusplus
            CalSegUnlock(parameters);
#endif

        }

        // Trigger the DAQ event 'fastTask'
        DaqTriggerEvent(fastTask);

        //HAL_GPIO_WritePin(FASTTASK_GPIO_Port, FASTTASK_Pin, GPIO_PIN_RESET);

        // Sleep until next wakeup time, check for overruns
        const BaseType_t delayed = xTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(period_ms));
        if (delayed == pdFALSE) {
            fastTaskOverruns++;
        }
    }
}



// Low priority slow task
static void slowTask(void *parameter) {
    (void)parameter;
        
    volatile uint16_t counter = 0;
    float phase = 0.0f;
    uint32_t slow_task_period_ms;
    uint32_t fast_task_period_ms;
    (void)fast_task_period_ms;
    
#ifdef OPTION_SERIAL_PRINTF
    printf("slowTask started\n");
    printf("  frameaddr = %p\n", xcp_get_frame_addr());
    printf("  &counter = %p\n", &counter);
#endif
    
    DaqCreateEvent(slowTask);
    
    TickType_t lastWakeTime = xTaskGetTickCount();
    for (;;) {

        // Flash orange LED
        HAL_GPIO_WritePin(LED2_Orange_GPIO_Port, LED2_Orange_Pin, GPIO_PIN_SET);
        //HAL_GPIO_WritePin(SLOWTASK_GPIO_Port, SLOWTASK_Pin, GPIO_PIN_SET);


        {
#ifdef __cplusplus
            auto params = parameters_calseg.lock();
#else
            struct parameters *params =  (struct parameters *)CalSegLock(parameters);
#endif

            clamp_parameter(slow_task_period_ms, params->slow_task_period_ms, SLOWTASK_PERIOD_MIN_MS, SLOWTASK_PERIOD_MAX_MS);
            fast_task_period_ms = params->fast_task_period_ms;
            
            counter++;
            if (counter > params->counter_max) {
                counter = 0;
            }

            channel1 = params->amplitude * sinf(phase);
            phase += SLOWTASK_PHASE_STEP_RAD;
            if (phase >= SINE_PERIOD_RAD) {
                phase -= SINE_PERIOD_RAD;
            }

#ifndef __cplusplus
            CalSegUnlock(parameters);
#endif

        }

        DaqTriggerEvent(slowTask);

#ifdef OPTION_SERIAL_PRINTF
        // printf("slowTask: counter = %u, period = %u ms, channel1 = %f\n", counter, slow_task_period_ms, channel1);
#endif

        //HAL_GPIO_WritePin(SLOWTASK_GPIO_Port, SLOWTASK_Pin, GPIO_PIN_RESET);
        //osDelay(20); // Sleep a bit to make sure the orange LED is visible, since the slow task may have a long period
        HAL_GPIO_WritePin(LED2_Orange_GPIO_Port, LED2_Orange_Pin, GPIO_PIN_RESET);
        
        const BaseType_t delayed = xTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(slow_task_period_ms));
        if (delayed == pdFALSE) {
            slowTaskOverruns++;
        }
    }
}



//----------------------------------------------------------------------------------------------------
// Init and start demo tasks


static bool startXcpDemoTasks() {

    printf("Start demo tasks\n");
    printf("&global_counter = %p\n", &global_counter);
    printf("&parameters = %p\n", &parameters);
    
    if (!createDemoTask(fastTask, "fastTask", FASTTASK_STACKSIZE, FASTTASK_PRIORITY, &fastTaskHandle)) {
        printf("Failed to create fastTask\n");
        return false;
    }

    if (!createDemoTask(slowTask, "slowTask", SLOWTASK_STACKSIZE, SLOWTASK_PRIORITY, &slowTaskHandle)) {
        printf("Failed to create slowTask\n");
        return false;
    }

    return true;
}



void xcp_demo_init(void) {

    printf("xcp_demo_init\r\n");
    printf("STM32H753ZI FreeRTOS XCP on ETH Demo\r\n");

    // Start XCP server
    if (!startXcpServer()) {
        printf("XCP server startup failed\r\n");
        return;
    }
 
    osDelay(200);

    // Check timestamp resolution and XCP clock
    uint64_t t1 = ApplXcpGetClock64();
    osDelay(10);       
    uint64_t t2 = ApplXcpGetClock64();
    printf("Clock resolution check: t1=%llu, t2=%llu, dt=%llu\n", t1, t2, t2 - t1);

    // Start the demo
    if (!startXcpDemoTasks()) {
        printf( "XCP demo start failed\r\n");
        return;
    }


}

