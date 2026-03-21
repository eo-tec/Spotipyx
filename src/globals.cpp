#include "globals.h"

// Core hardware
MatrixPanel_I2S_DMA *dma_display = nullptr;
WiFiClient mqttClientWiFi;
PubSubClient mqttClient(mqttClientWiFi);
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org");
Preferences preferences;

// Colors
uint16_t myBLACK = 0;
uint16_t myWHITE = 0xFFFF;
uint16_t myRED = 0xF800;
uint16_t myGREEN = 0x07E0;
uint16_t myBLUE = 0x001F;
uint16_t color1 = 0x3080;
uint16_t color2 = 0x56AA;
uint16_t color3 = 0xF6BD;
uint16_t color4 = 0xF680;
uint16_t color5 = 0xF746;

// Settings
int brightness = 10;
bool startupBrightnessRampDone = false;
int wifiBrightness = 0;
int maxIndex = 5;
int maxPhotos = 5;
int currentVersion = 0;
int frameId = 0;
int photoIndex = 0;
String mqttToken = "";
bool allowSpotify = true;

// Spotify timing
String songShowing = "";
unsigned long lastPhotoChange = -60000;
unsigned long secsPhotos = 30000;
unsigned long lastSpotifyCheck = 0;
unsigned long timeToCheckSpotify = 5000;

// Schedule
bool scheduleEnabled = false;
int scheduleOnHour = 8;
int scheduleOnMinute = 0;
int scheduleOffHour = 22;
int scheduleOffMinute = 0;
int timezoneOffset = 0;
bool screenOff = false;

// Clock overlay
bool clockEnabled = false;
unsigned long lastClockUpdate = 0;

// Title scroll
String currentTitle = "";
String currentName = "";
int titleScrollOffset = 0;
unsigned long lastTitleScrollTime = 0;
unsigned long titleScrollSpeed = 300;
unsigned long titleScrollPauseTime = 2000;
unsigned long titleScrollPauseStart = 0;
ScrollState titleScrollState = SCROLL_PAUSED_START;
bool titleNeedsScroll = false;
int titleY = 0;
int nameY = 0;

// Photo loading
volatile bool isLoadingPhoto = false;
volatile int pendingNewPhotoId = -1;

// Drawing mode
bool drawingMode = false;
uint16_t drawingBuffer[PANEL_RES_Y][PANEL_RES_X];
unsigned long lastDrawingActivity = 0;
DrawCommand drawCommandBuffer[MAX_DRAW_COMMANDS];
int drawCommandCount = 0;
unsigned long lastDrawingUpdate = 0;
int dirtyMinX = PANEL_RES_X;
int dirtyMaxX = -1;
int dirtyMinY = PANEL_RES_Y;
int dirtyMaxY = -1;

// Overlay bitmask
uint8_t overlayMask[64][8] = {};

void overlayMaskSet(int x, int y) {
    if (x >= 0 && x < 64 && y >= 0 && y < 64)
        overlayMask[y][x / 8] |= (1 << (x % 8));
}

bool overlayMaskGet(int x, int y) {
    return (overlayMask[y][x / 8] >> (x % 8)) & 1;
}

void overlayMaskClear() {
    memset(overlayMask, 0, sizeof(overlayMask));
}

// Animation playback
bool hasPsram = false;
uint8_t animFrameWidth = 32;      // default to 32x32 (no PSRAM)
uint16_t animFrameSize = ANIM_FRAME_SIZE_32;
uint8_t* animBuffer = nullptr;
uint8_t animFrameCount = 0;
uint8_t animFps = 10;
uint8_t animFramesReceived = 0;
bool animReady = false;
bool animPlaying = false;
int currentAnimationId = -1;
uint8_t animCurrentFrame = 0;
unsigned long animLastFrameTime = 0;
unsigned long animLoopCount = 0;
uint8_t animFrameStep = 1;
unsigned long animFrameInterval = 200;

// Waiting for owner mode
bool waitingForOwner = false;

// BLE provisioning
NimBLEServer* pBLEServer = nullptr;
NimBLECharacteristic* pWifiCredentialsChar = nullptr;
NimBLECharacteristic* pResponseChar = nullptr;
bool bleDeviceConnected = false;
bool bleCredentialsReceived = false;
String bleReceivedSSID = "";
String bleReceivedPassword = "";

// Display state
String loadingMsg = "";
uint16_t screenBuffer[PANEL_RES_Y][PANEL_RES_X];
int lastPercentage = 0;

// Static buffers
uint8_t spotifyCoverBuffer[64 * 64 * 2];
char songIdBuffer[64];
char httpBuffer[512];
uint8_t photoBuffer[64 * 64 * 3];
char photoTitle[64];
char photoAuthor[64];

// MQTT response flags
volatile bool mqttResponseReceived = false;
volatile bool mqttResponseSuccess = false;
String mqttResponseType = "";
uint32_t mqttRequestId = 0;
volatile uint32_t mqttResponseRequestId = 0;
volatile int mqttRegisterFrameId = 0;

// Loop variable
String songOnline = "";
