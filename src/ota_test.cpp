// Test standalone de la pantalla de actualizacion OTA (env: test-ota).
// Anima el progreso 0->100% en bucle sin WiFi/MQTT, replicando el dibujo
// de showPercetage() en display.cpp. Init del panel copiado de panel_test.cpp.
#include <Arduino.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include <Adafruit_GFX.h>
#include <Fonts/Picopixel.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "logo.h"

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
static const uint16_t myWHITE = 0xFFFF;
static const uint16_t myBLACK = 0x0000;
static int lastPercentage = -1;

#define LOG(fmt, ...) Serial.printf("[%lu] " fmt "\n", millis(), ##__VA_ARGS__); Serial.flush()

#define OTA_TEXT_TOP 44
static GFXcanvas1 otaTextCanvas(PANEL_RES_X, 16);

static void renderOtaTextLine(const String &text, int baseline)
{
    int16_t x1, y1;
    uint16_t w, h;
    otaTextCanvas.getTextBounds(text, 0, baseline, &x1, &y1, &w, &h);
    otaTextCanvas.setCursor((PANEL_RES_X - w) / 2 - (x1 - 0), baseline);
    otaTextCanvas.print(text);
}

// Composicion en una sola pasada por pixel (texto como mascara) para no
// sobrepintar: repintar fondo y luego texto encima parpadea.
static void showOtaScreen(int percentage)
{
    percentage = constrain(percentage, 0, 100);
    if (lastPercentage == percentage)
        return;
    lastPercentage = percentage;

    otaTextCanvas.fillScreen(0);
    otaTextCanvas.setFont(&Picopixel);
    otaTextCanvas.setTextColor(1);
    renderOtaTextLine("Updating", 6);
    renderOtaTextLine(String(percentage) + "%", 14);

    int waterline = PANEL_RES_Y - (percentage * PANEL_RES_Y) / 100;
    uint8_t *textBuf = otaTextCanvas.getBuffer();
    const int rowBytes = (PANEL_RES_X + 7) / 8;

    for (int y = 0; y < PANEL_RES_Y; y++)
    {
        int ty = y - OTA_TEXT_TOP;
        for (int x = 0; x < PANEL_RES_X; x++)
        {
            bool isText = ty >= 0 && ty < 16 &&
                          (textBuf[ty * rowBytes + x / 8] & (0x80 >> (x & 7)));
            uint16_t color;
            if (isText)
            {
                color = y < waterline ? myWHITE : myBLACK;
            }
            else
            {
                int idx = (y * 64 + x) * 3;
                uint8_t g = pgm_read_byte(&LOGO_DATA[idx]);
                uint8_t r = pgm_read_byte(&LOGO_DATA[idx + 1]);
                uint8_t b = pgm_read_byte(&LOGO_DATA[idx + 2]);
                uint8_t mx = max(r, max(g, b));
                uint8_t mn = min(r, min(g, b));
                if (y < waterline && (mx - mn) < 40)
                {
                    r = 255 - r;
                    g = 255 - g;
                    b = 255 - b;
                }
                color = dma_display->color565(r, g, b);
            }
            dma_display->drawPixel(x, y, color);
        }
    }
}

void setup() {
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
    setCpuFrequencyMhz(80);
    Serial.begin(115200);
    delay(3000);

    LOG("==== OTA SCREEN TEST ====");
    LOG("Free heap: %d", ESP.getFreeHeap());

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
    // Igual que el firmware real en v2 (main.cpp): 20 MHz / 120 Hz. Con la
    // config de panel_test (8 MHz / 60) aparecian pixeles fantasma rojos
    // en las ultimas columnas sobre fondo negro.
    mxconfig.i2sspeed = HUB75_I2S_CFG::HZ_20M;
    mxconfig.min_refresh_rate = 120;

    dma_display = new MatrixPanel_I2S_DMA(mxconfig);
    dma_display->begin();
    // Ghosting: el borde blanco de la linea de agua filtra carga residual a la
    // fila negra de encima (pixeles rojos tenues en las ultimas columnas).
    // Alargar el blanking del latch lo elimina.
    dma_display->setLatBlanking(2);
    dma_display->setBrightness8(50);
    dma_display->clearScreen();

    LOG("Setup done, animating OTA screen");
}

void loop() {
    static int percent = 0;
    static unsigned long lastStep = 0;

    if (millis() - lastStep > (percent >= 100 ? 3000 : 150)) {
        lastStep = millis();
        if (percent >= 100) {
            percent = 0;
            lastPercentage = -1;
            LOG("Restarting animation, heap: %d", ESP.getFreeHeap());
        } else {
            percent++;
        }
        showOtaScreen(percent);
    }
    delay(10);
}
