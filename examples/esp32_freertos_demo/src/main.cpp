#include <Arduino.h>
#include <LovyanGFX.hpp>
#include <WiFi.h>

extern "C" {
#include "xcplib.h"
}

TaskHandle_t blinkTaskHandle = nullptr;
TaskHandle_t printTaskHandle = nullptr;

#define LED_BUILTIN 2

#if !defined(WIFI_SSID) || !defined(WIFI_PASSWORD)
#include "wlan.h"
#endif

#define XCP_PROJECT_NAME "esp32_freertos_demo"
#define XCP_PROJECT_VERSION "V100"
#define XCP_USE_TCP false
#define XCP_SERVER_PORT 5555
#define XCP_QUEUE_SIZE (1024 * 8)
#define XCP_LOG_LEVEL 5

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
      cfg.memory_width = 170;
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

static void displayLine(int32_t line, const char *text, uint16_t color = TFT_WHITE) {
  if (lcdMutex == nullptr || xSemaphoreTake(lcdMutex, pdMS_TO_TICKS(50)) != pdTRUE) {
    return;
  }

  const int32_t y = line * 24;
  lcd.fillRect(0, y, lcd.width(), 24, TFT_BLACK);
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
  displayLine(0, "XCPlite demo", TFT_CYAN);
  displayLine(1, "Booting...");
}

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

static const char *wifiDisconnectReasonName(uint8_t reason) {
  return WiFi.disconnectReasonName(static_cast<wifi_err_reason_t>(reason));
}

static void printBssid(const uint8_t *bssid) {
  Serial.printf("%02X:%02X:%02X:%02X:%02X:%02X",
                bssid[0],
                bssid[1],
                bssid[2],
                bssid[3],
                bssid[4],
                bssid[5]);
}

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
      Serial.printf("Found '%s': RSSI=%d dBm, channel=%d, encryption=%d (%s), BSSID=",
                    WiFi.SSID(i).c_str(),
                    rssi,
                    WiFi.channel(i),
                    authMode,
                    wifiAuthModeName(authMode));
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
  displayLine(1, "WiFi connecting...");
  const uint32_t startMs = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startMs < 30000) {
    Serial.print(".");
    vTaskDelay(pdMS_TO_TICKS(500));
  }
  Serial.println();

  if (WiFi.status() != WL_CONNECTED) {
    const wl_status_t status = WiFi.status();
    Serial.printf("WiFi connection failed, status=%d (%s)\n", status, wifiStatusName(status));
    displayLine(1, "WiFi failed", TFT_RED);
    return false;
  }

  Serial.printf("WiFi connected, IP address: %s\n", WiFi.localIP().toString().c_str());
  displayLine(1, WiFi.localIP().toString().c_str(), TFT_GREEN);
  return true;
}

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

  Serial.printf("XCP server started: UDP port %u\n", XCP_SERVER_PORT);
  displayLine(2, "XCP UDP :5555", TFT_GREEN);
  return true;
}

void blinkTask(void *parameter) {
  pinMode(LED_BUILTIN, OUTPUT);

  for (;;) {
    digitalWrite(LED_BUILTIN, HIGH);
    vTaskDelay(pdMS_TO_TICKS(500));

    digitalWrite(LED_BUILTIN, LOW);
    vTaskDelay(pdMS_TO_TICKS(500));

     Serial.printf("Blink task running on core %d\n", xPortGetCoreID());


  }
}

void printTask(void *parameter) {
  uint32_t counter = 0;
  char line[40];

  for (;;) {
    Serial.printf("Print task running on core %d\n", xPortGetCoreID());
    snprintf(line, sizeof(line), "Core %d tick %lu", xPortGetCoreID(), static_cast<unsigned long>(counter++));
    displayLine(3, line, TFT_YELLOW);
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  initDisplay();

  if (!connectWiFi()) {
    Serial.println("XCP server not started because WiFi is not connected.");
    displayLine(2, "XCP not started", TFT_RED);
  } else if (!startXcpServer()) {
    Serial.println("XCP server startup failed.");
    displayLine(2, "XCP failed", TFT_RED);
  }
  
  xTaskCreatePinnedToCore(
    blinkTask,
    "Blink Task",
    2048,
    nullptr,
    1,
    &blinkTaskHandle,
    1
  );

  xTaskCreatePinnedToCore(
    printTask,
    "Print Task",
    4096,
    nullptr,
    1,
    &printTaskHandle,
    0
  );
}

void loop() {
  vTaskDelay(pdMS_TO_TICKS(1000));
}
