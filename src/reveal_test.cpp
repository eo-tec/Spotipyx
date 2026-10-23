// Test standalone de las animaciones de foto nueva (env: test-reveal, solo v2).
// Sin WiFi ni MQTT: cada 10 s "llega" una foto de prueba con el siguiente estilo de
// lib/photo_reveal, igual que lo pinta displayPhotoFromCenter() en photos.cpp. Por
// serie saca cuánto tarda el ESP32 en componer cada fotograma de cada estilo.
// Init del panel copiado de main.cpp (v2: 20 MHz / 120 Hz, rotación 135).
#include <Arduino.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include <PhotoReveal.h>
#include <new>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "reveal_test_photos.h"

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

#define LOG(fmt, ...) Serial.printf("[%lu] " fmt "\n", millis(), ##__VA_ARGS__)

static const uint32_t PHOTO_EVERY_MS = 10000;
static const uint32_t FRAME_MS = 33;  // como NEW_PHOTO_FRAME_MS en photos.cpp

// Los estilos que se están valorando, en el orden en que salen.
static const reveal::Style STYLES[] = {
    reveal::Style::POLAROID, reveal::Style::LETTER, reveal::Style::POSTCARD,
    reveal::Style::FILM,     reveal::Style::PRINT,  reveal::Style::NEWPIC,
};
static const int STYLE_COUNT = sizeof(STYLES) / sizeof(STYLES[0]);

struct TestPhoto {
    const uint8_t *rgb;
    const char *sender;
};
static const TestPhoto PHOTOS[] = {
    {PHOTO_FARO, "Lucia"},
    {PHOTO_AMIGOS, "Maximiliano"},
    {PHOTO_ABUELA, "mama"},
    {PHOTO_ABUELO, "Alejandro"},
};
static const int PHOTO_COUNT = sizeof(PHOTOS) / sizeof(PHOTOS[0]);

MatrixPanel_I2S_DMA *dma_display = nullptr;

class RgbSource : public reveal::Source {
 public:
    const uint8_t *rgb = nullptr;
    void get(int x, int y, uint8_t &r, uint8_t &g, uint8_t &b) const override {
        const uint8_t *p = rgb + (y * 64 + x) * 3;
        r = p[0];
        g = p[1];
        b = p[2];
    }
};

static reveal::Canvas *frame = nullptr;
static reveal::Canvas *screen = nullptr;  // lo que hay en pantalla (el "from")

// El panel trae los canales rotados (como en photos.cpp): rojo real por el pin verde,
// verde real por el azul y azul real por el rojo.
static void push(const reveal::Canvas &c) {
    for (int y = 0; y < PANEL_RES_Y; y++)
        for (int x = 0; x < PANEL_RES_X; x++)
            dma_display->drawPixelRGB888(x, y, c.px[y][x][2], c.px[y][x][0], c.px[y][x][1]);
}

static void playReveal(reveal::Style style, const TestPhoto &photo) {
    RgbSource to;
    to.rgb = photo.rgb;
    uint32_t duration = reveal::durationMs(style);
    uint32_t frames = 0, renderTotal = 0, renderMax = 0;
    unsigned long start = millis();
    for (;;) {
        uint32_t t = millis() - start;
        unsigned long before = micros();
        reveal::render(style, *screen, to, photo.sender, t, *frame);
        uint32_t renderUs = micros() - before;
        push(*frame);
        frames++;
        renderTotal += renderUs;
        if (renderUs > renderMax) renderMax = renderUs;
        if (t >= duration) break;
        uint32_t spent = millis() - start - t;
        if (spent < FRAME_MS) delay(FRAME_MS - spent);
    }
    uint32_t elapsed = millis() - start;
    LOG("%-9s de %-12s %u ms, %u fotogramas (%.1f fps), componer: media %.1f ms, max %.1f ms",
        reveal::styleName(style), photo.sender, (unsigned)elapsed, (unsigned)frames,
        frames * 1000.0f / elapsed, renderTotal / 1000.0f / frames, renderMax / 1000.0f);
    *screen = *frame;  // ya es la foto tal cual
}

void setup() {
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);  // igual que main.cpp (PSU v2 justa)
    Serial.begin(115200);
    delay(2000);
    LOG("==== REVEAL TEST ====");

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
    mxconfig.i2sspeed = HUB75_I2S_CFG::HZ_20M;
    mxconfig.min_refresh_rate = 120;
    dma_display = new MatrixPanel_I2S_DMA(mxconfig);
    dma_display->begin();
    dma_display->setBrightness8(90);
    dma_display->clearScreen();
    dma_display->setRotation(135);

    frame = new (std::nothrow) reveal::Canvas;
    screen = new (std::nothrow) reveal::Canvas;
    if (!frame || !screen) {
        LOG("Sin memoria para los lienzos");
        return;
    }
    // Arranca con la última foto en pantalla, como si ya estuviera puesta.
    RgbSource first;
    first.rgb = PHOTOS[PHOTO_COUNT - 1].rgb;
    screen->copyFrom(first);
    push(*screen);
    LOG("Listo. Heap libre: %d. Una foto cada %u s", ESP.getFreeHeap(), (unsigned)(PHOTO_EVERY_MS / 1000));
}

void loop() {
    static int index = 0;
    static unsigned long lastPhoto = 0;
    if (!frame || !screen) {
        delay(1000);
        return;
    }
    if (lastPhoto == 0 || millis() - lastPhoto >= PHOTO_EVERY_MS) {
        lastPhoto = millis();
        playReveal(STYLES[index % STYLE_COUNT], PHOTOS[index % PHOTO_COUNT]);
        index++;
    }
    delay(20);
}
