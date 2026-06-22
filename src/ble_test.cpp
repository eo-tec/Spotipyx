#include <Arduino.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"

#define R1_PIN  4
#define G1_PIN  5
#define B1_PIN  6
#define R2_PIN  7
#define G2_PIN  15
#define B2_PIN  16
#define A_PIN   18
#define B_PIN   8
#define C_PIN   3
#define D_PIN   42
#define E_PIN   38
#define CLK_PIN 41
#define LAT_PIN 40
#define OE_PIN  2

#define PANEL_RES_X 64
#define PANEL_RES_Y 64

MatrixPanel_I2S_DMA *dma_display = nullptr;

#define LOG(fmt, ...) Serial.printf("[%lu] " fmt "\n", millis(), ##__VA_ARGS__); Serial.flush()

// Background color encodes the boot reset reason so a quick look at the
// panel tells us if the chip is in a reset loop and where it died.
static uint16_t bgColorForResetReason(int reason) {
    switch (reason) {
        case 1:  return 0x0000;        // POWERON  → black
        case 5:  return 0xFFE0;        // INT_WDT  → yellow
        case 6:  return 0xF800;        // TASK_WDT → red
        case 9:  return 0xF81F;        // BROWNOUT → magenta
        default: return 0x001F;        // other    → blue
    }
}

static uint16_t bgColor = 0;

static void showStep(const char *line1, const char *line2, uint16_t textColor) {
    if (!dma_display) return;
    dma_display->fillScreen(bgColor);
    dma_display->setTextColor(textColor);
    dma_display->setTextSize(1);
    dma_display->setCursor(2, 8);
    dma_display->print(line1);
    if (line2) {
        dma_display->setCursor(2, 24);
        dma_display->print(line2);
    }
}

void setup() {
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
    setCpuFrequencyMhz(80);
    Serial.begin(115200);
    delay(2000);

    int rr = (int)esp_reset_reason();
    bgColor = bgColorForResetReason(rr);

    LOG("==== BLE+PANEL TEST ====");
    LOG("Reset reason: %d", rr);
    LOG("Free heap: %d", ESP.getFreeHeap());

    // ---- Panel init (we already know this works) ----
    HUB75_I2S_CFG mxconfig(PANEL_RES_X, PANEL_RES_Y, 1);
    mxconfig.gpio.r1 = R1_PIN;
    mxconfig.gpio.g1 = G1_PIN;
    mxconfig.gpio.b1 = B1_PIN;
    mxconfig.gpio.r2 = R2_PIN;
    mxconfig.gpio.g2 = G2_PIN;
    mxconfig.gpio.b2 = B2_PIN;
    mxconfig.gpio.a = A_PIN;
    mxconfig.gpio.b = B_PIN;
    mxconfig.gpio.c = C_PIN;
    mxconfig.gpio.d = D_PIN;
    mxconfig.gpio.e = E_PIN;
    mxconfig.gpio.clk = CLK_PIN;
    mxconfig.gpio.lat = LAT_PIN;
    mxconfig.gpio.oe = OE_PIN;
    mxconfig.clkphase = false;
    mxconfig.i2sspeed = HUB75_I2S_CFG::HZ_8M;
    mxconfig.min_refresh_rate = 60;

    dma_display = new MatrixPanel_I2S_DMA(mxconfig);
    dma_display->begin();
    dma_display->setBrightness8(40);
    dma_display->clearScreen();

    char rrStr[16];
    snprintf(rrStr, sizeof(rrStr), "rst=%d", rr);
    showStep("PANEL OK", rrStr, 0xFFFF);
    LOG("Panel ready");
    delay(1500);

    // ---- BT controller init ----
    showStep("BT MEM", "release", 0xFFFF);
    esp_err_t ret = esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
    LOG("mem_release ret=%d", ret);
    delay(300);

    showStep("BT CFG", "build", 0xFFFF);
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    bt_cfg.ble_max_act = 1; // minimize controller resources for the inrush moment
    delay(200);

    showStep("BT INIT", "...", 0xFFFF);
    LOG("esp_bt_controller_init...");
    ret = esp_bt_controller_init(&bt_cfg);
    LOG("bt_controller_init ret=%d", ret);
    if (ret != 0) {
        showStep("BT INIT", "FAIL", 0xF800);
        while (true) delay(1000);
    }
    showStep("BT INIT", "OK", 0x07E0);
    delay(800);

    showStep("BT SLEEP", "enable", 0xFFFF);
    esp_bt_sleep_enable();
    delay(300);

    // ---- Pre-enable power-savings ----
    // Brief countdown so we can see the panel before it goes dark.
    showStep("BT EN", "prep...", 0xFFE0);
    delay(1500);

    // 1) Drop CPU to 10MHz — frees ~30-40mA for the radio inrush.
    LOG("Dropping CPU to 10MHz");
    Serial.flush();
    setCpuFrequencyMhz(10);

    // 2) Black-out the panel — LEDs off saves ~100-150mA from the 3V3 rail.
    if (dma_display) {
        dma_display->setBrightness8(0);
        dma_display->fillScreen(0);
    }

    // 3) Long settle so the 470µF cap reaches full charge.
    delay(2000);

    LOG("esp_bt_controller_enable(BLE)...");
    Serial.flush();
    ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);

    // ---- Survived (or didn't) — restore CPU and panel for feedback ----
    setCpuFrequencyMhz(80);
    if (dma_display) {
        dma_display->setBrightness8(40);
    }
    LOG("bt_controller_enable ret=%d", ret);

    if (ret != 0) {
        char buf[16];
        snprintf(buf, sizeof(buf), "ret=%d", ret);
        showStep("BT EN FAIL", buf, 0xF800);
        while (true) delay(1000);
    }

    // If we make it here the radio survived its inrush.
    showStep("BT EN", "OK!", 0x07E0);
    delay(2000);

    showStep("BLUE INIT", "...", 0xFFFF);
    ret = esp_bluedroid_init();
    LOG("bluedroid_init ret=%d", ret);
    delay(300);

    showStep("BLUE EN", "...", 0xFFFF);
    ret = esp_bluedroid_enable();
    LOG("bluedroid_enable ret=%d", ret);

    showStep("ALL OK", ":)", 0x07E0);
    LOG("==== ALL DONE, heap: %d ====", ESP.getFreeHeap());
}

void loop() {
    static unsigned long lastBlink = 0;
    static bool on = false;
    if (millis() - lastBlink > 1000) {
        lastBlink = millis();
        on = !on;
        // Tiny blinking dot in the corner so we know the loop is alive.
        if (dma_display) {
            dma_display->drawPixel(62, 62, on ? 0xFFFF : 0x0000);
        }
    }
    delay(10);
}
