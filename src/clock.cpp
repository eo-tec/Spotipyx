#include "clock.h"
#include <Fonts/FreeSans12pt7b.h>
#include <Fonts/Picopixel.h>

void showTime()
{
    dma_display->clearScreen();
    timeClient.update();
    // Añadir offset local para mostrar (UTC+2 para España)
    int localHours = (timeClient.getHours() + 2) % 24;
    String currentTime = String(localHours < 10 ? "0" : "") + String(localHours) + ":" +
                         String(timeClient.getMinutes() < 10 ? "0" : "") + String(timeClient.getMinutes());
    dma_display->setTextSize(1);
    dma_display->setTextColor(myWHITE);
    dma_display->setFont(&FreeSans12pt7b);
    dma_display->setCursor(3, 38);
    dma_display->print(currentTime);
    LOGF("Hora: %s", currentTime.c_str());
}

void showDevOverlay() {
#ifdef DEV_MODE
    dma_display->setFont(&Picopixel);
    dma_display->setTextSize(1);

    int16_t x1, y1;
    uint16_t w, h;
    dma_display->getTextBounds("Dev", 0, 0, &x1, &y1, &w, &h);

    int xPos = 2;       // 2px margen izquierdo
    int yPos = h;        // texto 1px más arriba

    dma_display->fillRect(0, 0, w + 4, h + 2, myBLACK);
    dma_display->setTextColor(dma_display->color565(255, 50, 50)); // Rojo
    dma_display->setCursor(xPos, yPos);
    dma_display->print("Dev");
#endif
}

void hideClockOverlay() {
    // Restaurar píxeles originales de la foto en la zona del reloj
    // Usamos un área generosa en la esquina superior derecha
    int rectX = PANEL_RES_X - 30;
    int rectW = 30;
    int rectH = 8;
    for (int y = 0; y < rectH; y++) {
        for (int x = rectX; x < PANEL_RES_X; x++) {
            dma_display->drawPixel(x, y, screenBuffer[y][x]);
        }
    }
}

void showClockOverlay() {
    if (!clockEnabled) return;

    timeClient.update();
    int utcHours = timeClient.getHours();
    int utcMinutes = timeClient.getMinutes();
    int utcTotalMinutes = utcHours * 60 + utcMinutes;
    int localTotalMinutes = (utcTotalMinutes + timezoneOffset + 24 * 60) % (24 * 60);
    int localHour = localTotalMinutes / 60;
    int localMinute = localTotalMinutes % 60;

    String timeStr = String(localHour < 10 ? "0" : "") + String(localHour) + ":" +
                     String(localMinute < 10 ? "0" : "") + String(localMinute);

    dma_display->setFont(&Picopixel);
    dma_display->setTextSize(1);

    // Medir texto para posicionar a la derecha
    int16_t x1, y1;
    uint16_t w, h;
    dma_display->getTextBounds(timeStr, 0, 0, &x1, &y1, &w, &h);

    int xPos = PANEL_RES_X - w - 2;  // 2px margen derecho
    int yPos = h;                      // texto 1px más arriba

    // Fondo negro para legibilidad
    dma_display->fillRect(xPos - 1, 0, w + 3, h + 2, myBLACK);
    dma_display->setTextColor(myWHITE);
    dma_display->setCursor(xPos, yPos);
    dma_display->print(timeStr);
}
