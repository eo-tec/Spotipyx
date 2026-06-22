#include <Arduino.h>
#include <WiFi.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

#define LOG(fmt, ...) Serial.printf("[%lu] " fmt "\n", millis(), ##__VA_ARGS__); Serial.flush()

void setup() {
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
    Serial.begin(115200);
    delay(3000);

    LOG("==== WIFI TEST ====");
    LOG("Reset reason: %d", (int)esp_reset_reason());
    LOG("Free heap: %d", ESP.getFreeHeap());

    LOG("WiFi.mode(WIFI_STA)");
    WiFi.mode(WIFI_STA);
    LOG("mode set");

    LOG("WiFi.disconnect()");
    WiFi.disconnect();
    LOG("disconnected");

    delay(100);

    LOG("Scanning networks...");
    int n = WiFi.scanNetworks();
    LOG("scan found %d networks", n);

    for (int i = 0; i < n && i < 5; i++) {
        LOG("  %d: %s (rssi %d)", i, WiFi.SSID(i).c_str(), WiFi.RSSI(i));
    }

    LOG("==== WIFI SETUP COMPLETE ====");
}

void loop() {
    static unsigned long lastLog = 0;
    if (millis() - lastLog > 2000) {
        lastLog = millis();
        LOG("alive, heap: %d", ESP.getFreeHeap());
    }
    delay(10);
}
