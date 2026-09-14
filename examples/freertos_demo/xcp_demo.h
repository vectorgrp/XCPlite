// XCP FreeRTOS demo application header

#include <stdbool.h>
#include <stdint.h>

// Options
// #define OPTION_DISPLAY
// #define OPTION_CMSIS
// #define OPTION_IO
// #define OPTION_ANALOG

// #define DEMO_TASK_CORE 1 // If defined, pin both tasks to this core

#define FASTTASK_PRIORITY (configMAX_PRIORITIES - 1)
#define FASTTASK_STACKSIZE 4096


#define SLOWTASK_PRIORITY 3
#define SLOWTASK_STACKSIZE 4096

// Initialize XCP demo (start XCP server and demo tasks)
bool xcp_demo_init(void);

#ifdef OPTION_ANALOG
// Read a platform-provided single-ended analog channel. Returns NAN when the
// converter is unavailable or the channel is invalid.
float readAnalogChannel(uint8_t channel);

// Raw analog value exposed as an XCP measurement.
extern float pressure_sensor_voltage;
#endif

#ifdef OPTION_DISPLAY
void displayUpdate(uint32_t slowTaskPeriodMs, uint16_t slowCounter, uint32_t fastTaskPeriodMs, uint16_t fastCounter);
#endif

#ifdef OPTION_IO
void setPin1(void);
void rstPin1(void);
void setPin2(void);
void rstPin2(void);
void toggleOrangeLed(void);
void toggleRedLed(void);
void toggleGreenLed(void);
#endif
