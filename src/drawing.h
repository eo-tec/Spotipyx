#ifndef DRAWING_H
#define DRAWING_H

#include "globals.h"

void enterDrawingMode();
void exitDrawingMode();
void updateDrawingDisplay();
void drawDrawingPixel(int x, int y, uint16_t color);
void drawDrawingStroke(JsonArray points, uint16_t color);
void clearDrawingCanvas();
void checkDrawingTimeout();
void processDrawingBuffer();
void addDrawCommand(int x, int y, uint16_t color, int size);

#endif
