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

// Pines del panel HUB75
#ifdef HW_V2
    // Hardware v2 (ESP32-S3-WROOM-1)
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
#else
    // Hardware v1 (ESP32 clásico) - usa defaults de la librería
    #define E_PIN 18
#endif

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
extern int otaPendingVersion; // versión OTA a prueba (sin validar); 0 = ninguna
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

// Overlay bitmask: marks pixels occupied by title/author/clock (64x64 = 512 bytes)
extern uint8_t overlayMask[64][8]; // 64 rows × 64 bits (8 bytes per row)
void overlayMaskSet(int x, int y);
bool overlayMaskGet(int x, int y);
void overlayMaskClear();

// Animation playback
#define MAX_ANIM_FRAMES 60
#define ANIM_FRAME_SIZE_64 (64 * 64 * 2) // 8192 bytes RGB565
#define ANIM_FRAME_SIZE_32 (32 * 32 * 2) // 2048 bytes RGB565
extern bool hasPsram;
extern uint8_t animFrameWidth;   // 64 or 32 depending on PSRAM
extern uint16_t animFrameSize;   // bytes per frame (8192 or 2048)
extern uint8_t* animBuffer; // allocated dynamically when needed
extern uint8_t animFrameCount;
extern uint8_t animFps;
extern uint8_t animFramesReceived;
extern bool animReady;
extern bool animPlaying;
extern int currentAnimationId;
extern uint8_t animCurrentFrame;
extern unsigned long animLastFrameTime;
extern unsigned long animLoopCount;
extern uint8_t animFrameStep;        // skip N backend frames to cover full duration
extern unsigned long animFrameInterval; // ms between frames (replaces 1000/fps when step > 1)
extern uint64_t animFramesBitmap;    // bit i set = slot i already stored (tolerates out-of-order arrival)
extern unsigned long animDownloadStartTime; // millis() when first batch of requests was sent
extern uint8_t animRetryCount;       // how many timeout retries we've issued for current animation

// Loading spinner: shown in place of the title while an animation is downloading
extern bool animSpinnerActive;       // true while the spinner replaces the title
extern unsigned long lastSpinnerUpdate; // millis() of last spinner frame advance
extern uint8_t spinnerFrame;         // current spinner glyph index

// Waiting for owner mode (BLE re-entry when no owner)
extern bool waitingForOwner;

// BLE provisioning
extern NimBLEServer* pBLEServer;
extern NimBLECharacteristic* pWifiCredentialsChar;
extern NimBLECharacteristic* pResponseChar;
extern NimBLECharacteristic* pNetworksChar;
extern bool bleNetworkScanRequested;
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
