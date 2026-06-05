
// ESP32 FreeRTOS Demo
// See README.md in the demo folder for details

#include <Arduino.h>
#include <math.h>
#ifdef OPTION_DISPLAY
#include <LovyanGFX.hpp>
#endif
#include <WiFi.h>

#include "xcplib.hpp"

//----------------------------------------------------------------------------------------------------
// Display

#ifdef OPTION_DISPLAY

class Display : public lgfx::LGFX_Device {
    lgfx::Bus_Parallel8 _bus;
    lgfx::Panel_ST7789 _panel;
    lgfx::Light_PWM _light;

  public:
    Display() {
        {
            auto cfg = _bus.config();
            cfg.freq_write = 20000000;
            cfg.pin_wr = LCD_WR;
            cfg.pin_rd = LCD_RD;
            cfg.pin_rs = LCD_DC;
            cfg.pin_d0 = LCD_D0;
            cfg.pin_d1 = LCD_D1;
            cfg.pin_d2 = LCD_D2;
            cfg.pin_d3 = LCD_D3;
            cfg.pin_d4 = LCD_D4;
            cfg.pin_d5 = LCD_D5;
            cfg.pin_d6 = LCD_D6;
            cfg.pin_d7 = LCD_D7;
            _bus.config(cfg);
            _panel.setBus(&_bus);
        }

        {
            auto cfg = _panel.config();
            cfg.pin_cs = LCD_CS;
            cfg.pin_rst = LCD_RES;
            cfg.pin_busy = -1;
            cfg.memory_width = 240;
            cfg.memory_height = 320;
            cfg.panel_width = 170;
            cfg.panel_height = 320;
            cfg.offset_x = 35;
            cfg.offset_y = 0;
            cfg.offset_rotation = 0;
            cfg.invert = true;
            cfg.rgb_order = false;
            _panel.config(cfg);
        }

        {
            auto cfg = _light.config();
            cfg.pin_bl = LCD_BL;
            cfg.invert = false;
            cfg.freq = 44100;
            cfg.pwm_channel = 7;
            _light.config(cfg);
            _panel.setLight(&_light);
        }

        setPanel(&_panel);
    }
};

static Display lcd;
static SemaphoreHandle_t lcdMutex = nullptr;
static constexpr int32_t DISPLAY_LINE_HEIGHT = 24;

#define displayLineCount() (lcd.height() / DISPLAY_LINE_HEIGHT)

static void displayLine(int32_t line, const char *text, uint16_t color = TFT_WHITE) {
    if (lcdMutex == nullptr || xSemaphoreTake(lcdMutex, pdMS_TO_TICKS(50)) != pdTRUE) {
        return;
    }
    const int32_t y = line * DISPLAY_LINE_HEIGHT;
    lcd.fillRect(0, y, lcd.width(), DISPLAY_LINE_HEIGHT, TFT_BLACK);
    lcd.setCursor(0, y + 4);
    lcd.setTextColor(color, TFT_BLACK);
    lcd.print(text);
    xSemaphoreGive(lcdMutex);
}

static void initDisplay() {
    pinMode(LCD_POWER_ON, OUTPUT);
    digitalWrite(LCD_POWER_ON, HIGH);
    lcdMutex = xSemaphoreCreateMutex();
    lcd.init();
    lcd.setRotation(1);
    lcd.setBrightness(180);
    lcd.fillScreen(TFT_BLACK);
    lcd.setTextSize(2);
    lcd.setTextWrap(false);
}

#else

static constexpr uint16_t TFT_BLACK = 0;
static constexpr uint16_t TFT_BLUE = 0;
static constexpr uint16_t TFT_RED = 0;
static constexpr uint16_t TFT_GREEN = 0;
static constexpr uint16_t TFT_CYAN = 0;
static constexpr uint16_t TFT_YELLOW = 0;
static constexpr uint16_t TFT_WHITE = 0;

#define displayLineCount() 0

static void displayLine(int32_t line, const char *text, uint16_t color = TFT_WHITE) {
    (void)line;
    (void)text;
    (void)color;
}

static void initDisplay() {}

#endif

//----------------------------------------------------------------------------------------------------
// WiFi

#if !defined(WIFI_SSID) || !defined(WIFI_PASSWORD)
#include "wlan.h"
#endif

struct WiFiTarget {
    bool found;
    int32_t rssi;
    int32_t channel;
    uint8_t bssid[6];
};

static const char *wifiStatusName(wl_status_t status) {
    switch (status) {
    case WL_IDLE_STATUS:
        return "WL_IDLE_STATUS";
    case WL_NO_SSID_AVAIL:
        return "WL_NO_SSID_AVAIL";
    case WL_SCAN_COMPLETED:
        return "WL_SCAN_COMPLETED";
    case WL_CONNECTED:
        return "WL_CONNECTED";
    case WL_CONNECT_FAILED:
        return "WL_CONNECT_FAILED";
    case WL_CONNECTION_LOST:
        return "WL_CONNECTION_LOST";
    case WL_DISCONNECTED:
        return "WL_DISCONNECTED";
    default:
        return "UNKNOWN";
    }
}

static const char *wifiAuthModeName(wifi_auth_mode_t authMode) {
    switch (authMode) {
    case WIFI_AUTH_OPEN:
        return "OPEN";
    case WIFI_AUTH_WEP:
        return "WEP";
    case WIFI_AUTH_WPA_PSK:
        return "WPA_PSK";
    case WIFI_AUTH_WPA2_PSK:
        return "WPA2_PSK";
    case WIFI_AUTH_WPA_WPA2_PSK:
        return "WPA_WPA2_PSK";
    case WIFI_AUTH_WPA2_ENTERPRISE:
        return "WPA2_ENTERPRISE";
    case WIFI_AUTH_WPA3_PSK:
        return "WPA3_PSK";
    case WIFI_AUTH_WPA2_WPA3_PSK:
        return "WPA2_WPA3_PSK";
    default:
        return "UNKNOWN";
    }
}

static const char *wifiDisconnectReasonName(uint8_t reason) { return WiFi.disconnectReasonName(static_cast<wifi_err_reason_t>(reason)); }

static void printBssid(const uint8_t *bssid) { Serial.printf("%02X:%02X:%02X:%02X:%02X:%02X", bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5]); }

static void onWiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
    if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
        const uint8_t reason = info.wifi_sta_disconnected.reason;
        Serial.printf("WiFi disconnected, reason=%u (%s)\n", reason, wifiDisconnectReasonName(reason));
    }
}

static WiFiTarget scanForConfiguredSsid() {
    WiFiTarget target = {};
    target.rssi = -1000;
    target.channel = 0;

    Serial.printf("Scanning WLANs for '%s'...\n", WIFI_SSID);
    const int networkCount = WiFi.scanNetworks();
    if (networkCount < 0) {
        Serial.printf("WiFi scan failed: %d\n", networkCount);
        return target;
    }

    for (int i = 0; i < networkCount; i++) {
        if (WiFi.SSID(i) == WIFI_SSID) {
            const int32_t rssi = WiFi.RSSI(i);
            const wifi_auth_mode_t authMode = static_cast<wifi_auth_mode_t>(WiFi.encryptionType(i));
            Serial.printf("Found '%s': RSSI=%d dBm, channel=%d, encryption=%d (%s), BSSID=", WiFi.SSID(i).c_str(), rssi, WiFi.channel(i), authMode, wifiAuthModeName(authMode));
            printBssid(WiFi.BSSID(i));
            Serial.println();

            if (!target.found || rssi > target.rssi) {
                target.found = true;
                target.rssi = rssi;
                target.channel = WiFi.channel(i);
                memcpy(target.bssid, WiFi.BSSID(i), sizeof(target.bssid));
            }
        }
    }

    if (!target.found) {
        Serial.printf("SSID '%s' was not found. Check that it is a 2.4 GHz WLAN and in range.\n", WIFI_SSID);
    } else {
        Serial.printf("Using strongest AP: RSSI=%d dBm, channel=%d, BSSID=", target.rssi, target.channel);
        printBssid(target.bssid);
        Serial.println();
    }
    return target;
}

static bool connectWiFi() {
    WiFi.onEvent(onWiFiEvent);
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.disconnect(true, true);
    delay(250);

    const WiFiTarget target = scanForConfiguredSsid();

    if (target.found) {
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD, target.channel, target.bssid);
    } else {
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    }

    Serial.printf("Connecting to WLAN '%s'", WIFI_SSID);
    const uint32_t startMs = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startMs < 30000) {
        Serial.print(".");
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    Serial.println();

    if (WiFi.status() != WL_CONNECTED) {
        const wl_status_t status = WiFi.status();
        Serial.printf("WiFi connection failed, status=%d (%s)\n", status, wifiStatusName(status));
        return false;
    }

    return true;
}

//----------------------------------------------------------------------------------------------------
// XCP

#define XCP_PROJECT_NAME "esp32_freertos_demo"
#define XCP_PROJECT_VERSION "V100"
#define XCP_USE_TCP false
#define XCP_SERVER_PORT 5555
#define XCP_QUEUE_SIZE (1024 * 8)
#define XCP_LOG_LEVEL 4 // 3 - Info, 4 - Print XCP commands, 5 - Debug

static bool startXcpServer() {
    const uint8_t bindAny[4] = {0, 0, 0, 0};

    XcpSetLogLevel(XCP_LOG_LEVEL);
    XcpCreateEpk(XCP_PROJECT_VERSION);

    if (!XcpInit(XCP_PROJECT_NAME, XCP_PROJECT_VERSION, XCP_MODE_LOCAL)) {
        Serial.println("XcpInit failed");
        return false;
    }

    if (!XcpEthServerInit(bindAny, XCP_SERVER_PORT, XCP_USE_TCP, XCP_QUEUE_SIZE)) {
        Serial.println("XcpEthServerInit failed");
        return false;
    }

    return true;
}

//----------------------------------------------------------------------------------------------------
// Demo RTOS tasks

// LilyGO T-Display-S3 scope hookup:
// channel 1 probe tip -> IO2/GPIO2 header pin, probe ground -> any board GND pin.
// channel 2 probe tip -> IO1/GPIO1 header pin, probe ground -> any board GND pin.
#define FASTTASK_SCOPE_PIN 2
#define SLOWTASK_SCOPE_PIN 1

#define DEMO_TASK_CORE 1

#define FASTTASK_PRIORITY (configMAX_PRIORITIES - 1)
#define SLOWTASK_PRIORITY 3

#if configTICK_RATE_HZ < 1000
#error "fastTask needs configTICK_RATE_HZ >= 1000 for a 1 ms FreeRTOS tick period"
#endif

static constexpr uint32_t FASTTASK_PERIOD_MIN_MS = 1;
static constexpr uint32_t FASTTASK_PERIOD_MAX_MS = 100;
static constexpr uint32_t SLOWTASK_PERIOD_MIN_MS = 1;
static constexpr uint32_t SLOWTASK_PERIOD_MAX_MS = 1000;
static constexpr float SLOWTASK_PHASE_STEP_RAD = 0.1f;
static constexpr float SINE_PERIOD_RAD = 6.28318530717958647692f;

// Check a uint32_t value range to make calibration safe
static uint32_t clamp(uint32_t x, uint32_t min, uint32_t max) {
    if (x < min) {
        return min;
    }
    if (x > max) {
        return max;
    }
    return x;
}

// Global measurement values
uint16_t global_counter = 0;
uint32_t fastTaskOverruns = 0;
uint32_t slowTaskOverruns = 0;

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
CalSegDeclRef(parameters, parameters_calseg);

TaskHandle_t fastTaskHandle = nullptr;
TaskHandle_t slowTaskHandle = nullptr;

// High priority fast task
void fastTask(void *parameter) {

    // Volatile keeps this local measurement variable visible in optimized builds,
    // The offline A2L generator can discover it in the ELF file and associate it to the functions DAQ event trigger
    volatile uint16_t counter = 0;

#ifdef OPTION_SERIAL_PRINTF
    Serial.printf("fastTask started\n");
    Serial.printf("  priority = %u\n", static_cast<unsigned>(uxTaskPriorityGet(nullptr)));
    Serial.printf("  frameaddr = %p\n", xcp_get_frame_addr());
    Serial.printf("  &counter = %p\n", &counter);
#endif

    // Create a DAQ event named 'fastTask'
    DaqCreateEvent(fastTask);

    // Initialize IO pin
    pinMode(FASTTASK_SCOPE_PIN, OUTPUT);
    digitalWrite(FASTTASK_SCOPE_PIN, LOW);

    TickType_t lastWakeTime = xTaskGetTickCount();
    for (;;) {

        // Toggle an IO pin to observe cycle time jitter and runtime jitter
        digitalWrite(FASTTASK_SCOPE_PIN, HIGH);

        uint32_t period_ms;

        // Lock the calibration segment 'parameters' for thread-safe and consistent access
        // There is no blocking mutex hold during the lock, only atomics used
        {
            auto params = parameters_calseg.lock();

            // Save the task period parameter, don't delay during the lock to give XCP a chance to modify the parameters
            period_ms = clamp(params->fast_task_period_ms, FASTTASK_PERIOD_MIN_MS, FASTTASK_PERIOD_MAX_MS);

            counter++;
            if (counter > params->counter_max) {
                counter = 0;
            }
            global_counter++;
            if (global_counter > params->counter_max) {
                global_counter = 0;
            }
        }

        // Trigger the DAQ event 'fastTask'
        DaqTriggerEvent(fastTask);

        // Toggle IO pin
        digitalWrite(FASTTASK_SCOPE_PIN, LOW);

        // Sleep until next wakeup time, check for overruns
        const BaseType_t delayed = xTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(period_ms));
        if (delayed == pdFALSE) {
            fastTaskOverruns++;
        }
    }
}

// Low priority slow task
void slowTask(void *parameter) {

    volatile uint16_t counter = 0;
    volatile float sineValue = 0.0f;
    float phase = 0.0f;
    TickType_t lastWakeTime = xTaskGetTickCount();

#ifdef OPTION_SERIAL_PRINTF
    Serial.printf("slowTask started\n");
    Serial.printf("  frameaddr = %p\n", xcp_get_frame_addr());
    Serial.printf("  &counter = %p\n", &counter);
#endif

    DaqCreateEvent(slowTask);
    pinMode(SLOWTASK_SCOPE_PIN, OUTPUT);
    digitalWrite(SLOWTASK_SCOPE_PIN, LOW);

    for (;;) {

        digitalWrite(SLOWTASK_SCOPE_PIN, HIGH);

        uint32_t slow_task_period_ms;
        uint32_t fast_task_period_ms;

        {
            auto params = parameters_calseg.lock();
            slow_task_period_ms = clamp(params->slow_task_period_ms, SLOWTASK_PERIOD_MIN_MS, SLOWTASK_PERIOD_MAX_MS);
            fast_task_period_ms = clamp(params->fast_task_period_ms, FASTTASK_PERIOD_MIN_MS, FASTTASK_PERIOD_MAX_MS);

            counter++;
            if (counter > params->counter_max) {
                counter = 0;
            }

            sineValue = params->amplitude * sinf(phase);
            phase += SLOWTASK_PHASE_STEP_RAD;
            if (phase >= SINE_PERIOD_RAD) {
                phase -= SINE_PERIOD_RAD;
            }
        }

        DaqTriggerEvent(slowTask);

#ifdef OPTION_SERIAL_PRINTF
        Serial.printf("slowTask: core %d - %u, period = %u ms, sine = %.3f\n", xPortGetCoreID(), counter, static_cast<unsigned>(slow_task_period_ms),
                      static_cast<double>(sineValue));
#endif

// Display
#ifdef OPTION_DISPLAY
        {

            char line[40];

            if (XcpIsDaqRunning()) {
                snprintf(line, sizeof(line), "XCP DAQ running");
            } else if (XcpIsConnected()) {
                snprintf(line, sizeof(line), "XCP Connected");
            } else if (XcpIsStarted()) {
                snprintf(line, sizeof(line), "IP %s", WiFi.localIP().toString().c_str());
            } else {
                snprintf(line, sizeof(line), "XCP Offline");
            }
            displayLine(displayLineCount() - 7, line, TFT_WHITE);
            snprintf(line, sizeof(line), "slowTask: %ums %u", slow_task_period_ms, counter);
            displayLine(displayLineCount() - 4, line, TFT_YELLOW);
            snprintf(line, sizeof(line), "fastTask: %ums %u", fast_task_period_ms, global_counter);
            displayLine(displayLineCount() - 3, line, TFT_RED);
            snprintf(line, sizeof(line), "XCP clock %" PRIu64 "", ApplXcpGetClock64());
            displayLine(displayLineCount() - 1, line, TFT_GREEN);
        }
#endif

        digitalWrite(SLOWTASK_SCOPE_PIN, LOW);

        const BaseType_t delayed = xTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(slow_task_period_ms));
        if (delayed == pdFALSE) {
            slowTaskOverruns++;
        }
    }
}

//----------------------------------------------------------------------------------------------------
// Main (Arduino style)

// Init
void setup() {

    Serial.begin(115200);
    delay(500);

#ifdef OPTION_DISPLAY
    initDisplay();
#endif

    // Connect to WLAN
    if (!connectWiFi()) {
        Serial.println("XCP server not started because WiFi is not connected.");
    }

    // Start XCP server
    else if (!startXcpServer()) {
        Serial.println("XCP server startup failed.");
    }

    Serial.printf("&global_counter = %p\n", &global_counter);
    Serial.printf("&parameters = %p\n", &parameters);

    // Create 2 demo tasks on DEMO_TASK_CORE

    BaseType_t taskCreated = xTaskCreatePinnedToCore(fastTask, "fastTask",
                                                     2048, // stack
                                                     nullptr, FASTTASK_PRIORITY, &fastTaskHandle, DEMO_TASK_CORE);
    if (taskCreated != pdPASS) {
        Serial.println("Failed to create fastTask");
        displayLine(3, "fastTask failed", TFT_RED);
        return;
    }

    taskCreated = xTaskCreatePinnedToCore(slowTask, "slowTask",
                                          4096, // stack
                                          nullptr, SLOWTASK_PRIORITY, &slowTaskHandle, DEMO_TASK_CORE);
    if (taskCreated != pdPASS) {
        Serial.println("Failed to create slowTask");
        displayLine(3, "slowTask failed", TFT_RED);
        return;
    }
}

// Background task
void loop() { vTaskDelay(pdMS_TO_TICKS(1000)); }
