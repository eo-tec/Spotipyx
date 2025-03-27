#ifndef PIXIE_H
#define PIXIE_H

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include <Preferences.h>
#include <NTPClient.h>
#include <ArduinoJson.h>
#include <pics.h>

class Pixie
{
public:
    Pixie(const char *serverUrl, bool devMode);
    String getCover();
    String getSongId();
    String getPhoto(int photoIndex, int pixieId);
    int registerPixie(const String &macAddress);
    static void fadeOut(MatrixPanel_I2S_DMA *dma_display, int brightness);
    static void fadeIn(MatrixPanel_I2S_DMA *dma_display, int brightness);
    static void showUpdateMessage(MatrixPanel_I2S_DMA *dma_display);
    static void showCheckMessage(MatrixPanel_I2S_DMA *dma_display);
    void checkForUpdates(MatrixPanel_I2S_DMA *dma_display, Preferences &preferences, int currentVersion);
    static void showWiFiIcon(MatrixPanel_I2S_DMA *dma_display, int n, uint16_t myWHITE, uint16_t myBLACK);
    static void showPercetage(MatrixPanel_I2S_DMA *dma_display, int percentage, uint16_t myWHITE);
    static void showTime(MatrixPanel_I2S_DMA *dma_display, NTPClient &timeClient);
    static void pushUpAnimation(MatrixPanel_I2S_DMA *dma_display, int y, JsonArray &data);
    void showPhoto(MatrixPanel_I2S_DMA *dma_display, int photoIndex, int pixieId, uint16_t myWHITE, uint16_t myBLACK);

private:
    const char *serverUrl;
    bool devMode;
    WiFiClient *getWiFiClient();
};

#endif // PIXIE_H
