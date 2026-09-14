
// ESP32 FreeRTOS Demo
// See README.md in the demo folder for details

#include <Arduino.h>
#include <math.h>

#ifdef OPTION_ANALOG
#include <Adafruit_ADS1X15.h>
#include <Wire.h>
#endif

#ifdef OPTION_DISPLAY
#include <LovyanGFX.hpp>
#endif

#include <WiFi.h>

#include "xcplib.hpp"

#include "xcp_demo.hpp"

#ifdef OPTION_ANALOG
static bool ads1115Present = false;
#endif

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

int32_t displayLineCount() { return (lcd.height() / DISPLAY_LINE_HEIGHT); }

void displayLine(int32_t line, const char *text, uint16_t color = TFT_WHITE) {
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



//----------------------------------------------------------------------------------------------------
// Status display for the XCP demo

void displayUpdate(uint32_t slowTaskPeriodMs, uint16_t slowCounter, uint32_t fastTaskPeriodMs, uint16_t fastCounter) {
    char line[40];

    if (XcpIsDaqRunning()) {
        snprintf(line, sizeof(line), "XCP DAQ running");
    } else if (XcpIsConnected()) {
        snprintf(line, sizeof(line), "XCP Connected");
    } else if (XcpIsStarted()) {
        snprintf(line, sizeof(line), "WiFi.IP %s", WiFi.localIP().toString().c_str());
    } else {
        snprintf(line, sizeof(line), "XCP Offline");
    }
    displayLine(displayLineCount() - 7, line, TFT_WHITE);

#ifdef OPTION_ANALOG
    if (!ads1115Present) {
        snprintf(line, sizeof(line), "ADS1115: not found");
    } else if (isnan(pressure_sensor_voltage)) {
        snprintf(line, sizeof(line), "ADS1115: found");
    } else {
        snprintf(line, sizeof(line), "ADS1115: %.3f V", pressure_sensor_voltage);
    }
    displayLine(displayLineCount() - 5, line, ads1115Present ? TFT_CYAN : TFT_RED);
#endif
    
    snprintf(line, sizeof(line), "slowTask: %ums %u", slowTaskPeriodMs, slowCounter);
    displayLine(displayLineCount() - 4, line, TFT_YELLOW);
    
    snprintf(line, sizeof(line), "fastTask: %ums %u", fastTaskPeriodMs, fastCounter);
    displayLine(displayLineCount() - 3, line, TFT_RED);
    
    size_t rxStackSize = 0,txStackSize = 0;
    XcpEthServerDebugInfo(&rxStackSize,&txStackSize);
    snprintf(line, sizeof(line), "Stack: rx=%u, tx=%u", (uint16_t)rxStackSize, (uint16_t)txStackSize);
    displayLine(displayLineCount() - 2, line, TFT_BLUE);
    
    snprintf(line, sizeof(line), "XCP clock %" PRIu64 "", ApplXcpGetClock64());
    displayLine(displayLineCount() - 1, line, TFT_GREEN);
}

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
// IO

#ifdef OPTION_IO

#define PIN1 2
#define PIN2 3

void setPin1(void) {
    digitalWrite(PIN1, HIGH);
}

void rstPin1(void) {
    digitalWrite(PIN1, LOW);
}

void setPin2(void) {
    digitalWrite(PIN2, HIGH);
}

void rstPin2(void) {
    digitalWrite(PIN2, LOW);
}

void initIO( void ) {
    pinMode(PIN1, OUTPUT);
    rstPin1();
    pinMode(PIN2, OUTPUT);
    rstPin2();
}

#endif

//----------------------------------------------------------------------------------------------------
// Analog input

#ifdef OPTION_ANALOG

static constexpr uint8_t ADS1115_I2C_ADDRESS = 0x48;
static Adafruit_ADS1115 ads1115;

static void initAnalogConverter() {
    Wire.begin(SDA, SCL);

    ads1115Present = ads1115.begin(ADS1115_I2C_ADDRESS, &Wire);
    if (!ads1115Present) {
        Serial.printf("ADS1115 not found at I2C address 0x%02X\n", ADS1115_I2C_ADDRESS);
        return;
    }

    // With a 3.3 V supply, gain 1 covers every valid single-ended input.
    ads1115.setGain(GAIN_ONE);
    // The slow task defaults to 500 Hz, so use the ADS1115's fastest rate.
    ads1115.setDataRate(RATE_ADS1115_860SPS);
    Serial.printf("ADS1115 found at I2C address 0x%02X (SDA=%u, SCL=%u)\n", ADS1115_I2C_ADDRESS, SDA, SCL);
}

float readAnalogChannel(uint8_t channel) {
    if (!ads1115Present || channel > 3) {
        return NAN;
    }

    return ads1115.computeVolts(ads1115.readADC_SingleEnded(channel));
}

#endif

//----------------------------------------------------------------------------------------------------
// Main (Arduino style)

// Init
void setup() {

    Serial.begin(115200);
    delay(500);

#ifdef OPTION_DISPLAY
    initDisplay();
#endif

#ifdef OPTION_IO
    initIO();
#endif

#ifdef OPTION_ANALOG
    initAnalogConverter();
#endif

    // Connect to WLAN
    if (!connectWiFi()) {
        Serial.println("XCP server not started because WiFi is not connected");
    }

    // Start XCP demo
    else if (!xcp_demo_init()) {
        Serial.println("XCP demo init failed");
#ifdef OPTION_DISPLAY
        displayLine(3, "XCP demo init failed", TFT_RED);
#endif
    }

}

// Background task
void loop() { 
    
    vTaskDelay(pdMS_TO_TICKS(1000)); 
}
