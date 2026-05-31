#include <Arduino.h>
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
#define XCP_LOG_LEVEL 4

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
    Serial.printf("WiFi disconnected, reason=%u\n", info.wifi_sta_disconnected.reason);
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
      Serial.printf("Found '%s': RSSI=%d dBm, channel=%d, encryption=%d, BSSID=",
                    WiFi.SSID(i).c_str(),
                    rssi,
                    WiFi.channel(i),
                    WiFi.encryptionType(i));
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

  Serial.printf("WiFi connected, IP address: %s\n", WiFi.localIP().toString().c_str());
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
  for (;;) {
    Serial.printf("Print task running on core %d\n", xPortGetCoreID());
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  if (!connectWiFi()) {
    Serial.println("XCP server not started because WiFi is not connected.");
  } else if (!startXcpServer()) {
    Serial.println("XCP server startup failed.");
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
