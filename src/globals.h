#ifndef GLOBALS_H
#define GLOBALS_H

#include <string>
#include <Arduino.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <ArduinoJson.h>
#include <NTPClient.h>
#include <Preferences.h>
#include <PubSubClient.h>
#include <esp_task_wdt.h>
#include <NimBLEDevice.h>

// Macros de logging con timestamp
#define LOG(msg) Serial.printf("[%lu] %s\n", millis(), msg)
#define LOGF(fmt, ...) Serial.printf("[%lu] " fmt "\n", millis(), ##__VA_ARGS__)
#define LOGF_NL(fmt, ...) Serial.printf("[%lu] " fmt, millis(), ##__VA_ARGS__)

// MQTT Client ID prefix
#define MQTT_CLIENT_ID "frame-"

// Pines del panel
#define E_PIN 18

#define PANEL_RES_X 64
#define PANEL_RES_Y 64
#define PANEL_CHAIN 1

// Configuración de estabilidad y timeouts
#define WDT_TIMEOUT 30
#define MQTT_MAX_RETRIES 5
#define MQTT_RETRY_DELAY 3000
#define HTTP_TIMEOUT 10000
#define HTTP_TIMEOUT_DOWNLOAD 30000

// Drawing mode constants
const int MAX_DRAW_COMMANDS = 100;
const unsigned long DRAWING_TIMEOUT = 60000;
const unsigned long DRAWING_UPDATE_INTERVAL = 20;

// Drawing command struct
struct DrawCommand {
    int x;
    int y;
    uint16_t color;
    int size;
};

// Scroll state enum
enum ScrollState { SCROLL_PAUSED_START, SCROLL_SCROLLING, SCROLL_PAUSED_END, SCROLL_RETURNING };

// --- Extern declarations ---

// Core hardware
extern MatrixPanel_I2S_DMA *dma_display;
extern WiFiClient mqttClientWiFi;
extern PubSubClient mqttClient;
extern WiFiUDP ntpUDP;
extern NTPClient timeClient;
extern Preferences preferences;

// Colors
extern uint16_t myBLACK;
extern uint16_t myWHITE;
extern uint16_t myRED;
extern uint16_t myGREEN;
extern uint16_t myBLUE;
extern uint16_t color1, color2, color3, color4, color5;

// Settings
extern int brightness;
extern bool startupBrightnessRampDone;
extern int wifiBrightness;
extern int maxIndex;
extern int maxPhotos;
extern int currentVersion;
extern int frameId;
extern int photoIndex;
extern String mqttToken;
extern bool allowSpotify;

// Spotify timing
extern String songShowing;
extern unsigned long lastPhotoChange;
extern unsigned long secsPhotos;
extern unsigned long lastSpotifyCheck;
extern unsigned long timeToCheckSpotify;

// Schedule
extern bool scheduleEnabled;
extern int scheduleOnHour;
extern int scheduleOnMinute;
extern int scheduleOffHour;
extern int scheduleOffMinute;
extern int timezoneOffset;
extern bool screenOff;

// Clock overlay
extern bool clockEnabled;
extern unsigned long lastClockUpdate;

// Title scroll
extern String currentTitle;
extern String currentName;
extern int titleScrollOffset;
extern unsigned long lastTitleScrollTime;
extern unsigned long titleScrollSpeed;
extern unsigned long titleScrollPauseTime;
extern unsigned long titleScrollPauseStart;
extern ScrollState titleScrollState;
extern bool titleNeedsScroll;
extern int titleY;
extern int nameY;

// Photo loading
extern volatile bool isLoadingPhoto;
extern volatile int pendingNewPhotoId;

// Drawing mode
extern bool drawingMode;
extern uint16_t drawingBuffer[PANEL_RES_Y][PANEL_RES_X];
extern unsigned long lastDrawingActivity;
extern DrawCommand drawCommandBuffer[MAX_DRAW_COMMANDS];
extern int drawCommandCount;
extern unsigned long lastDrawingUpdate;
extern int dirtyMinX, dirtyMaxX, dirtyMinY, dirtyMaxY;

// BLE provisioning
extern NimBLEServer* pBLEServer;
extern NimBLECharacteristic* pWifiCredentialsChar;
extern NimBLECharacteristic* pResponseChar;
extern bool bleDeviceConnected;
extern bool bleCredentialsReceived;
extern String bleReceivedSSID;
extern String bleReceivedPassword;

// Display state
extern String loadingMsg;
extern uint16_t screenBuffer[PANEL_RES_Y][PANEL_RES_X];
extern int lastPercentage;

// Static buffers (avoid heap fragmentation)
extern uint8_t spotifyCoverBuffer[64 * 64 * 2];
extern char songIdBuffer[64];
extern char httpBuffer[512];
extern uint8_t photoBuffer[64 * 64 * 3];
extern char photoTitle[64];
extern char photoAuthor[64];

// MQTT response flags (request/response pattern)
extern volatile bool mqttResponseReceived;
extern volatile bool mqttResponseSuccess;
extern String mqttResponseType;
extern uint32_t mqttRequestId;
extern volatile uint32_t mqttResponseRequestId;
extern volatile int mqttRegisterFrameId;

// Loop variable
extern String songOnline;

#endif
