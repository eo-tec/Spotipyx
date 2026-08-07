#include "display.h"
#include "net_task.h"
#include <ArduinoOTA.h>
#include <Adafruit_GFX.h>
#include <Fonts/Picopixel.h>
#include <logo.h>

void wait(int ms)
{
    // Fuera del loopTask (p.ej. mqttReconnect desde la tarea de red, core 0)
    // solo dormir: esp_task_wdt_reset() y ArduinoOTA.handle() NO son seguros
    // en concurrencia con el core 1. Crash real cazado por telemetria
    // (2026-08-07): LoadProhibited en find_task_in_twdt_list con la lista del
    // task WDT corrupta, loop()->wait()->esp_task_wdt_reset().
    if (xPortGetCoreID() != 1) {
        vTaskDelay(pdMS_TO_TICKS(ms));
        return;
    }

    unsigned long startTime = millis();
    while (millis() - startTime < ms)
    {
        esp_task_wdt_reset();
        if (!netTaskRunning) mqttClient.loop(); // con tarea de red, el bombeo va en core 0
        ArduinoOTA.handle();
        yield();
    }
}

void showLoadingMsg(String msg)
{
    dma_display->setTextSize(1);
    if (loadingMsg != msg)
    {
        loadingMsg = msg;
        dma_display->fillRect(0, 48, PANEL_RES_X, 16, myWHITE);
    }
    dma_display->setTextColor(myBLACK);
    dma_display->setFont(&Picopixel);

    // Calcular la posición para centrar el texto
    int16_t x1, y1;
    uint16_t w, h;
    dma_display->getTextBounds(msg, 0, 50, &x1, &y1, &w, &h);
    int16_t x = (PANEL_RES_X - w) / 2;

    // Dibujar el texto centrado
    dma_display->setCursor(x, 55);
    dma_display->print(msg);
}

void drawPixelWithBuffer(int x, int y, uint16_t color) {
    dma_display->drawPixel(x, y, color);
    screenBuffer[y][x] = color;
}

void rgb565ToRgb(uint16_t rgb565, uint8_t &r, uint8_t &g, uint8_t &b) {
    r = ((rgb565 >> 11) & 0x1F) << 3;
    g = ((rgb565 >> 5) & 0x3F) << 2;
    b = (rgb565 & 0x1F) << 3;
}

void fadeOut()
{
    const int steps = 20;
    for (int step = 0; step <= steps; step++)
    {
        for (int y = 0; y < PANEL_RES_Y; y++)
        {
            for (int x = 0; x < PANEL_RES_X; x++)
            {
                uint16_t color = screenBuffer[y][x];
                uint8_t r = ((color >> 11) & 0x1F) << 3;
                uint8_t g = ((color >> 5) & 0x3F) << 2;
                uint8_t b = (color & 0x1F) << 3;

                r = r * (steps - step) / steps;
                g = g * (steps - step) / steps;
                b = b * (steps - step) / steps;

                dma_display->drawPixel(x, y, dma_display->color565(r, g, b));
            }
        }
        delay(30);
    }
}

void fadeIn()
{
    const int steps = 20;
    for (int step = 0; step <= steps; step++)
    {
        for (int y = 0; y < PANEL_RES_Y; y++)
        {
            for (int x = 0; x < PANEL_RES_X; x++)
            {
                uint16_t color = screenBuffer[y][x];
                uint8_t r = ((color >> 11) & 0x1F) << 3;
                uint8_t g = ((color >> 5) & 0x3F) << 2;
                uint8_t b = (color & 0x1F) << 3;

                r = r * step / steps;
                g = g * step / steps;
                b = b * step / steps;

                dma_display->drawPixel(x, y, dma_display->color565(r, g, b));
            }
        }
        delay(30);
    }
}

void pushUpAnimation(int y, JsonArray &data)
{
    unsigned long startTime = millis();
    for (int i = 0; i <= y; i++)
    {
        JsonArray row = data[PANEL_RES_Y - y + i - 1];
        for (int j = 0; j < PANEL_RES_X; j++)
        {
            uint16_t color = row[j];
            drawPixelWithBuffer(j, i, color);
        }
    }
    unsigned long endTime = millis();
    unsigned long elapsedTime = endTime - startTime;
    if (elapsedTime < 3)
    {
        delay(3 - elapsedTime);
    }
}

void drawLogo()
{
    for (int y = 0; y < 64; y++)
    {
        for (int x = 0; x < 64; x++)
        {
            int idx = (y * 64 + x) * 3;
            uint8_t g = pgm_read_byte(&LOGO_DATA[idx]);
            uint8_t r = pgm_read_byte(&LOGO_DATA[idx + 1]);
            uint8_t b = pgm_read_byte(&LOGO_DATA[idx + 2]);
            drawPixelWithBuffer(x, y, dma_display->color565(r, g, b));
        }
    }
}

// Texto en Picopixel volcado pixel a pixel: cada pixel elige blanco o negro
// segun quede por encima (zona invertida, fondo negro) o por debajo (fondo
// blanco) de la linea de agua, de modo que el texto puede quedar partido.
static void drawOtaTextLine(const String &text, int top, int waterline)
{
    static GFXcanvas1 canvas(PANEL_RES_X, 8);
    canvas.fillScreen(0);
    canvas.setFont(&Picopixel);
    canvas.setTextColor(1);

    int16_t x1, y1;
    uint16_t w, h;
    canvas.getTextBounds(text, 0, 6, &x1, &y1, &w, &h);
    canvas.setCursor((PANEL_RES_X - w) / 2 - (x1 - 0), 6);
    canvas.print(text);

    uint8_t *buf = canvas.getBuffer();
    const int rowBytes = (PANEL_RES_X + 7) / 8;
    for (int cy = 0; cy < 8; cy++)
    {
        for (int cx = 0; cx < PANEL_RES_X; cx++)
        {
            if (buf[cy * rowBytes + cx / 8] & (0x80 >> (cx & 7)))
            {
                int ty = top + cy;
                drawPixelWithBuffer(cx, ty, ty < waterline ? myWHITE : myBLACK);
            }
        }
    }
}

// Pantalla de OTA: el logo de arranque invertido (fondo negro, letras
// blancas) que se va "llenando" de blanco de abajo arriba con el progreso.
// Solo se invierten los pixeles grises; el punto verde se mantiene.
void showPercetage(int percentage)
{
    percentage = constrain(percentage, 0, 100);
    if (lastPercentage == percentage)
        return;
    lastPercentage = percentage;

    int waterline = PANEL_RES_Y - (percentage * PANEL_RES_Y) / 100;

    for (int y = 0; y < PANEL_RES_Y; y++)
    {
        for (int x = 0; x < PANEL_RES_X; x++)
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
            drawPixelWithBuffer(x, y, dma_display->color565(r, g, b));
        }
    }

    drawOtaTextLine("actualizando", 44, waterline);
    drawOtaTextLine(String(percentage) + "%", 52, waterline);
}

void showUpdateMessage()
{
    lastPercentage = -1;
    showPercetage(0);
    LOG("Se muestra pantalla de actualizacion");
}

void showCheckMessage()
{
    showLoadingMsg("Checking updates");
}
