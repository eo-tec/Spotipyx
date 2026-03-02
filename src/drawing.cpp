#include "drawing.h"

void enterDrawingMode() {
    drawingMode = true;
    lastDrawingActivity = millis();

    LOG("Entering drawing mode");

    // Inicializar buffer de dibujo con canvas negro
    for (int y = 0; y < PANEL_RES_Y; y++) {
        for (int x = 0; x < PANEL_RES_X; x++) {
            drawingBuffer[y][x] = myBLACK;
        }
    }

    // Limpiar pantalla y mostrar canvas negro
    dma_display->clearScreen();
}

void exitDrawingMode() {
    drawingMode = false;

    LOG("Exiting drawing mode");

    // Volver al modo normal (mostrar fotos)
    dma_display->clearScreen();
    // La próxima iteración del loop mostrará una foto
}

void updateDrawingDisplay() {
    if (!drawingMode) return;

    for (int y = 0; y < PANEL_RES_Y; y++) {
        for (int x = 0; x < PANEL_RES_X; x++) {
            dma_display->drawPixel(x, y, drawingBuffer[y][x]);
        }
    }
}

void drawDrawingPixel(int x, int y, uint16_t color) {
    if (!drawingMode) {
        return;
    }

    // Validar coordenadas
    if (x < 0 || x >= PANEL_RES_X || y < 0 || y >= PANEL_RES_Y) {
        return;
    }

    // Solo actualizar el buffer, no dibujar directamente
    drawingBuffer[y][x] = color;

    // Actualizar timestamp de actividad
    lastDrawingActivity = millis();
}

void drawDrawingStroke(JsonArray points, uint16_t color) {
    if (!drawingMode) return;

    for (JsonVariant point : points) {
        int x = point["x"];
        int y = point["y"];
        drawDrawingPixel(x, y, color);
    }
}

void clearDrawingCanvas() {
    if (!drawingMode) return;

    // Limpiar buffer de comandos pendientes
    drawCommandCount = 0;

    // Limpiar buffer de dibujo
    for (int y = 0; y < PANEL_RES_Y; y++) {
        for (int x = 0; x < PANEL_RES_X; x++) {
            drawingBuffer[y][x] = myBLACK;
        }
    }

    // Limpiar pantalla
    dma_display->clearScreen();

    // Actualizar timestamp de actividad
    lastDrawingActivity = millis();
}

void checkDrawingTimeout() {
    if (drawingMode && (millis() - lastDrawingActivity > DRAWING_TIMEOUT)) {
        LOG("Drawing mode timeout - returning to photo mode");
        exitDrawingMode();
    }
}

void processDrawingBuffer() {
    if (drawCommandCount == 0) return;

    // Reiniciar dirty rectangle
    dirtyMinX = PANEL_RES_X;
    dirtyMaxX = -1;
    dirtyMinY = PANEL_RES_Y;
    dirtyMaxY = -1;

    // Procesar todos los comandos pendientes
    for (int i = 0; i < drawCommandCount; i++) {
        DrawCommand &cmd = drawCommandBuffer[i];

        // Dibujar píxeles según el tamaño del pincel
        int halfSize = (cmd.size - 1) / 2;
        for (int dy = -halfSize; dy <= halfSize + (cmd.size - 1) % 2; dy++) {
            for (int dx = -halfSize; dx <= halfSize + (cmd.size - 1) % 2; dx++) {
                int px = cmd.x + dx;
                int py = cmd.y + dy;

                if (px >= 0 && px < PANEL_RES_X && py >= 0 && py < PANEL_RES_Y) {
                    drawingBuffer[py][px] = cmd.color;

                    // Actualizar dirty rectangle
                    if (px < dirtyMinX) dirtyMinX = px;
                    if (px > dirtyMaxX) dirtyMaxX = px;
                    if (py < dirtyMinY) dirtyMinY = py;
                    if (py > dirtyMaxY) dirtyMaxY = py;
                }
            }
        }
    }

    // Solo actualizar la región modificada
    if (dirtyMaxX >= 0) {
        for (int y = dirtyMinY; y <= dirtyMaxY; y++) {
            for (int x = dirtyMinX; x <= dirtyMaxX; x++) {
                dma_display->drawPixel(x, y, drawingBuffer[y][x]);
            }
        }
    }

    // Limpiar el buffer
    drawCommandCount = 0;
    lastDrawingUpdate = millis();
}

void addDrawCommand(int x, int y, uint16_t color, int size) {
    if (drawCommandCount < MAX_DRAW_COMMANDS) {
        drawCommandBuffer[drawCommandCount].x = x;
        drawCommandBuffer[drawCommandCount].y = y;
        drawCommandBuffer[drawCommandCount].color = color;
        drawCommandBuffer[drawCommandCount].size = size;
        drawCommandCount++;
    }
}
