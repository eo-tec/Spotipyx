#ifndef DISPLAY_H
#define DISPLAY_H

#include "globals.h"

void wait(int ms);
void showLoadingMsg(String msg);
void drawPixelWithBuffer(int x, int y, uint16_t color);
void rgb565ToRgb(uint16_t rgb565, uint8_t &r, uint8_t &g, uint8_t &b);
void fadeOut();
void fadeIn();
void pushUpAnimation(int y, JsonArray &data);
void drawLogo();
void showPercetage(int percentage);
void showUpdateMessage();
void showCheckMessage();

#endif
