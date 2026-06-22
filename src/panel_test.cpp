#include <Arduino.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

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

void setup() {
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
    setCpuFrequencyMhz(80);
    Serial.begin(115200);
    delay(3000);

    LOG("==== PANEL TEST v1 ====");
    LOG("Reset reason: %d", (int)esp_reset_reason());
    LOG("Free heap: %d", ESP.getFreeHeap());
    LOG("CPU freq: %d MHz", getCpuFrequencyMhz());

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

    LOG("Creating panel object");
    dma_display = new MatrixPanel_I2S_DMA(mxconfig);

    LOG("Calling begin()");
    dma_display->begin();

    LOG("setBrightness8(50)");
    dma_display->setBrightness8(50);

    LOG("clearScreen");
    dma_display->clearScreen();

    LOG("fillScreen RED");
    dma_display->fillScreen(dma_display->color565(255, 0, 0));

    LOG("Setup done, entering loop");
}

void loop() {
    static int step = 0;
    static unsigned long lastSwitch = 0;
    if (millis() - lastSwitch > 2000) {
        lastSwitch = millis();
        step = (step + 1) % 4;
        uint16_t c;
        const char *name;
        if (step == 0) { c = dma_display->color565(255, 0, 0); name = "RED"; }
        else if (step == 1) { c = dma_display->color565(0, 255, 0); name = "GREEN"; }
        else if (step == 2) { c = dma_display->color565(0, 0, 255); name = "BLUE"; }
        else { c = dma_display->color565(255, 255, 255); name = "WHITE"; }
        dma_display->fillScreen(c);
        LOG("Color: %s, heap: %d", name, ESP.getFreeHeap());
    }
    delay(10);
}
