#include <string>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <Fonts/FreeSans12pt7b.h>
#include <Fonts/Picopixel.h>
#include <ArduinoOTA.h>
#include <logo.h>        // Logo personalizado 64x64 RGB
#include <Preferences.h> // Para guardar las credenciales en memoria
#include <PubSubClient.h>
#include <esp_task_wdt.h>
#include <NimBLEDevice.h>
#include "ble_config.h"
#include "config.h"

// Macros de logging con timestamp
#define LOG(msg) Serial.printf("[%lu] %s\n", millis(), msg)
#define LOGF(fmt, ...) Serial.printf("[%lu] " fmt "\n", millis(), ##__VA_ARGS__)
#define LOGF_NL(fmt, ...) Serial.printf("[%lu] " fmt, millis(), ##__VA_ARGS__)

// MQTT Client ID prefix
#define MQTT_CLIENT_ID "frame-"

WiFiClient mqttClientWiFi;
PubSubClient mqttClient(mqttClientWiFi);

// Pines del panel
#define E_PIN 18

#define PANEL_RES_X 64
#define PANEL_RES_Y 64
#define PANEL_CHAIN 1

MatrixPanel_I2S_DMA *dma_display = nullptr;

uint16_t myBLACK = 0;
uint16_t myWHITE = 0xFFFF;
uint16_t myRED = 0xF800;
uint16_t myGREEN = 0x07E0;
uint16_t myBLUE = 0x001F;

// Colores para la animación de nueva foto
uint16_t color1 = 0x3080; // 3080B6
uint16_t color2 = 0x56AA; // 56AA92
uint16_t color3 = 0xF6BD; // F6BD19
uint16_t color4 = 0xF680; // F6801A
uint16_t color5 = 0xF746; // F74633

int brightness = 10;
bool startupBrightnessRampDone = false;  // Bloquea setBrightness hasta que la rampa se complete
int wifiBrightness = 0;
int maxIndex = 5;
int maxPhotos = 5;
int currentVersion = 0;
int frameId = 0;
int photoIndex = 0;

const bool DEV = false;
const char *serverUrl = DEV ? DEV_SERVER_URL : API_SERVER_URL;

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org");

String songShowing = "";
unsigned long lastPhotoChange = -60000;
unsigned long secsPhotos = 30000; // 30 segundos por defecto
unsigned long lastSpotifyCheck = 0;
unsigned long timeToCheckSpotify = 5000; // 5 segundos

// Variables para horario de encendido/apagado
bool scheduleEnabled = false;
int scheduleOnHour = 8;
int scheduleOnMinute = 0;
int scheduleOffHour = 22;
int scheduleOffMinute = 0;
int timezoneOffset = 0;  // Offset en minutos desde UTC (recibido del servidor)
bool screenOff = false;

// Variables para el reloj overlay
bool clockEnabled = false;
unsigned long lastClockUpdate = 0;

// Variables para el scroll del título
String currentTitle = "";
String currentName = "";
int titleScrollOffset = 0;
unsigned long lastTitleScrollTime = 0;
unsigned long titleScrollSpeed = 300; // Velocidad del scroll en ms
unsigned long titleScrollPauseTime = 2000; // Pausa al inicio y al final
unsigned long titleScrollPauseStart = 0;
enum ScrollState { SCROLL_PAUSED_START, SCROLL_SCROLLING, SCROLL_PAUSED_END, SCROLL_RETURNING };
ScrollState titleScrollState = SCROLL_PAUSED_START;
bool titleNeedsScroll = false;
int titleY = 0;
int nameY = 0;

// Flag para evitar condiciones de carrera en descarga de fotos
volatile bool isLoadingPhoto = false;
volatile int pendingNewPhotoId = -1;  // ID de foto nueva pendiente (-1 = ninguna)

// Variables para el modo de dibujo
bool drawingMode = false;
uint16_t drawingBuffer[PANEL_RES_Y][PANEL_RES_X]; // Buffer separado para dibujo
unsigned long lastDrawingActivity = 0;
const unsigned long DRAWING_TIMEOUT = 60000; // 1 minuto sin actividad

// Buffer de comandos de dibujo
struct DrawCommand {
    int x;
    int y;
    uint16_t color;
    int size;
};
const int MAX_DRAW_COMMANDS = 100;
DrawCommand drawCommandBuffer[MAX_DRAW_COMMANDS];
int drawCommandCount = 0;
unsigned long lastDrawingUpdate = 0;
const unsigned long DRAWING_UPDATE_INTERVAL = 20; // Actualizar cada 20ms (50 FPS)

// Configuración de estabilidad y timeouts
#define WDT_TIMEOUT 30              // Watchdog timeout en segundos
#define MQTT_MAX_RETRIES 5          // Máximo intentos de conexión MQTT
#define MQTT_RETRY_DELAY 3000       // Delay entre reintentos MQTT (ms)
#define HTTP_TIMEOUT 10000          // Timeout para llamadas HTTP API (ms)
#define HTTP_TIMEOUT_DOWNLOAD 30000 // Timeout para descargas (ms)

// Dirty rectangle tracking
int dirtyMinX = PANEL_RES_X;
int dirtyMaxX = -1;
int dirtyMinY = PANEL_RES_Y;
int dirtyMaxY = -1;

// Declaraciones globales para las credenciales y el servidor
Preferences preferences;
bool allowSpotify = true;

// Variables BLE para provisioning
NimBLEServer* pBLEServer = nullptr;
NimBLECharacteristic* pWifiCredentialsChar = nullptr;
NimBLECharacteristic* pResponseChar = nullptr;
bool bleDeviceConnected = false;
bool bleCredentialsReceived = false;
String bleReceivedSSID = "";
String bleReceivedPassword = "";

void wait(int ms)
{
    unsigned long startTime = millis();
    while (millis() - startTime < ms)
    {
        esp_task_wdt_reset();
        mqttClient.loop();
        ArduinoOTA.handle();
        yield();
    }
}

String loadingMsg = "";
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

// Buffer para la pantalla (ya existente)
uint16_t screenBuffer[PANEL_RES_Y][PANEL_RES_X]; // Buffer to store the current screen content

void drawPixelWithBuffer(int x, int y, uint16_t color) {
    dma_display->drawPixel(x, y, color);
    screenBuffer[y][x] = color;
}

// ===== FUNCIONES DE DIBUJO =====

// Función para entrar en modo dibujo
void enterDrawingMode() {
    drawingMode = true;
    lastDrawingActivity = millis();
    
    if (!DEV) {
        LOG("Entering drawing mode");
    }
    
    // Inicializar buffer de dibujo con canvas negro
    for (int y = 0; y < PANEL_RES_Y; y++) {
        for (int x = 0; x < PANEL_RES_X; x++) {
            drawingBuffer[y][x] = myBLACK;
        }
    }
    
    // Limpiar pantalla y mostrar canvas negro
    dma_display->clearScreen();
}

// Función para salir del modo dibujo
void exitDrawingMode() {
    drawingMode = false;
    
    if (!DEV) {
        LOG("Exiting drawing mode");
    }
    
    // Volver al modo normal (mostrar fotos)
    dma_display->clearScreen();
    // La próxima iteración del loop mostrará una foto
}

// Función para actualizar la pantalla con el buffer de dibujo
void updateDrawingDisplay() {
    if (!drawingMode) return;
    
    for (int y = 0; y < PANEL_RES_Y; y++) {
        for (int x = 0; x < PANEL_RES_X; x++) {
            dma_display->drawPixel(x, y, drawingBuffer[y][x]);
        }
    }
}

// Función para dibujar un píxel en modo dibujo
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

// Función para dibujar múltiples píxeles (stroke)
void drawDrawingStroke(JsonArray points, uint16_t color) {
    if (!drawingMode) return;
    
    for (JsonVariant point : points) {
        int x = point["x"];
        int y = point["y"];
        drawDrawingPixel(x, y, color);
    }
}

// Función para limpiar el canvas de dibujo
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

// Función para verificar timeout del modo dibujo
void checkDrawingTimeout() {
    if (drawingMode && (millis() - lastDrawingActivity > DRAWING_TIMEOUT)) {
        if (!DEV) {
            LOG("Drawing mode timeout - returning to photo mode");
        }
        exitDrawingMode();
    }
}

// Función para convertir RGB565 a componentes RGB
void rgb565ToRgb(uint16_t rgb565, uint8_t &r, uint8_t &g, uint8_t &b) {
    r = ((rgb565 >> 11) & 0x1F) << 3;
    g = ((rgb565 >> 5) & 0x3F) << 2;
    b = (rgb565 & 0x1F) << 3;
}

// Función para procesar el buffer de comandos de dibujo
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

// Función para agregar comando al buffer
void addDrawCommand(int x, int y, uint16_t color, int size) {
    if (drawCommandCount < MAX_DRAW_COMMANDS) {
        drawCommandBuffer[drawCommandCount].x = x;
        drawCommandBuffer[drawCommandCount].y = y;
        drawCommandBuffer[drawCommandCount].color = color;
        drawCommandBuffer[drawCommandCount].size = size;
        drawCommandCount++;
    }
}

// ===== FIN FUNCIONES DE DIBUJO =====

// Función para mostrar el porcentaje durante la actualización OTA
int lastPercentage = 0;
void showPercetage(int percentage)
{
    dma_display->setTextSize(1);
    dma_display->setTextColor(myWHITE);
    dma_display->setFont(&FreeSans12pt7b);
    if (lastPercentage != percentage)
    {
        dma_display->fillRect(0, 0, PANEL_RES_X, 40, myBLACK);
        dma_display->setCursor(8, 38); // Centrar en el panel
        dma_display->print((percentage < 10 ? "0" : "") + String(min(percentage, 99)) + "%");
        lastPercentage = percentage;
    }
    if (percentage < 2)
    {
        dma_display->setFont();
        dma_display->setCursor(8, 50); // Centrar en el panel
        dma_display->print("Updating");
    }
}

// Función para mostrar la hora en el panel (ya existente)
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
    dma_display->setCursor(3, 38); // Centrar en el panel
    dma_display->print(currentTime);
    LOGF("Hora: %s", currentTime.c_str());
}



// Función para animación "push up" (ya existente)
void pushUpAnimation(int y, JsonArray &data)
{
    unsigned long startTime = millis();
    for (int i = 0; i <= y; i++) // Incluye la última fila
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

// Buffers estáticos para Spotify (evita fragmentación de memoria)
static uint8_t spotifyCoverBuffer[64 * 64 * 2];  // 8192 bytes para imagen
static char songIdBuffer[64];
static char httpBuffer[512];

// Buffer estático para fotos (evita fragmentación de memoria)
static uint8_t photoBuffer[64 * 64 * 3];  // 12288 bytes para imagen RGB888

// Variables para metadata de foto (MQTT)
static char photoTitle[64];
static char photoAuthor[64];

// Flags de respuesta MQTT (para patrón request/response)
volatile bool mqttResponseReceived = false;
volatile bool mqttResponseSuccess = false;
String mqttResponseType = "";

// Request ID para filtrar respuestas duplicadas/stale
uint32_t mqttRequestId = 0;
volatile uint32_t mqttResponseRequestId = 0;

// Variables para registro via MQTT
volatile int mqttRegisterFrameId = 0;

// Forward declarations
bool waitForMqttResponse(const char* expectedType, unsigned long timeout = 10000);
void processPendingPhoto();
void onReceiveNewPic(int id);
void mqttCallback(char *topic, byte *payload, unsigned int length);
bool processBLECredentialsNonBlocking();

// Forward declaration
void showClockOverlay();

// Función para descargar y mostrar la portada (via MQTT)
void fetchAndDrawCover()
{
    lastPhotoChange = millis();

    LOG("[Spotify] Fetching cover via MQTT...");
    esp_task_wdt_reset();  // Reset watchdog antes de operación larga

    // Publicar request via MQTT
    String topic = String("frame/") + String(frameId) + "/request/cover";
    String payload = "{\"songId\":\"" + songShowing + "\"}";
    if (!mqttClient.publish(topic.c_str(), payload.c_str())) {
        LOG("[Spotify] Error publicando request MQTT");
        showTime();
        LOG("[Spotify] Done");
        return;
    }

    // Esperar respuesta
    if (waitForMqttResponse("cover", 15000)) {
        esp_task_wdt_reset();  // Reset watchdog después de recibir

        LOG("[Spotify] Animation start");
        // Mostrar la imagen con animación push up
        for (int y = 0; y < 64; y++)
        {
            // Mover todas las líneas existentes hacia arriba
            for (int moveY = 0; moveY < 63; moveY++)
            {
                for (int x = 0; x < 64; x++)
                {
                    uint16_t color = screenBuffer[moveY + 1][x];
                    drawPixelWithBuffer(x, moveY, color);
                    screenBuffer[moveY][x] = color;
                }
            }

            // Dibujar la nueva línea en la parte inferior (línea 63)
            for (int x = 0; x < 64; x++)
            {
                int bufferIdx = (y * 64 + x) * 2;
                uint16_t color = (spotifyCoverBuffer[bufferIdx] << 8) | spotifyCoverBuffer[bufferIdx + 1];
                drawPixelWithBuffer(x, 63, color);
                screenBuffer[63][x] = color;
            }

            wait(15);  // Usar wait() en lugar de delay() para alimentar el watchdog
        }
        LOG("[Spotify] Animation done");

        // Limpiar el texto del título de la foto anterior
        titleNeedsScroll = false;
        currentTitle = "";
        currentName = "";
        loadingMsg = "";
        showClockOverlay();
    }
    else
    {
        LOG("[Spotify] Error recibiendo cover via MQTT");
        showTime();
    }

    LOG("[Spotify] Done");
}

// Función para obtener el ID de la canción en reproducción (via MQTT)
String fetchSongId()
{
    songIdBuffer[0] = '\0';  // Limpiar buffer

    // Publicar request via MQTT
    String topic = String("frame/") + String(frameId) + "/request/song";
    if (!mqttClient.publish(topic.c_str(), "{}")) {
        LOG("[Spotify:fetchSongId] Error publicando request MQTT");
        return "";
    }

    // Esperar respuesta
    if (waitForMqttResponse("song", 5000)) {
        // Solo log si es una canción diferente
        if (songIdBuffer[0] != '\0' && songShowing != String(songIdBuffer)) {
            LOGF("Nueva canción: %s", songIdBuffer);
        }
        return String(songIdBuffer);
    }

    return "";
}

// Funciones de fade in/out (ya existentes)
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

// Función para solicitar configuración via MQTT
void requestConfig()
{
    LOG("[Config] Solicitando configuración via MQTT...");

    // Publicar request via MQTT
    String topic = String("frame/") + String(frameId) + "/request/config";
    if (!mqttClient.publish(topic.c_str(), "{}")) {
        LOG("[Config] Error publicando request MQTT");
        return;
    }

    // Esperar respuesta
    if (waitForMqttResponse("config", 10000)) {
        LOG("[Config] Configuración aplicada correctamente");
    } else {
        LOG("[Config] Error recibiendo configuración via MQTT");
    }
}

// Función para actualizar el scroll del título
void updatePhotoInfo() {
    if (!titleNeedsScroll || currentTitle.length() == 0) {
        return;
    }
    
    unsigned long currentTime = millis();
    
    switch (titleScrollState) {
        case SCROLL_PAUSED_START:
            if (currentTime - titleScrollPauseStart >= titleScrollPauseTime) {
                titleScrollState = SCROLL_SCROLLING;
                lastTitleScrollTime = currentTime;
            }
            break;
            
        case SCROLL_SCROLLING:
            if (currentTime - lastTitleScrollTime >= titleScrollSpeed) {
                // Incrementar el offset para quitar un carácter más del inicio
                titleScrollOffset++;
                
                // Obtener el substring del título
                String scrolledTitle = currentTitle.substring(titleScrollOffset);
                
                // Medir el ancho del texto actual
                dma_display->setFont(&Picopixel);
                dma_display->setTextSize(1);
                int16_t x1, y1;
                uint16_t w, h;
                dma_display->getTextBounds(scrolledTitle, 1, titleY, &x1, &y1, &w, &h);
                
                // Limpiar el área del título
                dma_display->fillRect(0, y1 - 1, PANEL_RES_X, h + 2, myBLACK);
                
                // Dibujar el título con scroll
                dma_display->setTextColor(myWHITE);
                dma_display->setCursor(1, titleY);
                dma_display->print(scrolledTitle);
                
                // Si el título ahora cabe completamente, pausar
                if (w <= PANEL_RES_X - 2) {
                    titleScrollState = SCROLL_PAUSED_END;
                    titleScrollPauseStart = currentTime;
                }
                
                lastTitleScrollTime = currentTime;
            }
            break;
            
        case SCROLL_PAUSED_END:
            if (currentTime - titleScrollPauseStart >= titleScrollPauseTime) {
                // Cambiar a estado de retorno
                titleScrollState = SCROLL_RETURNING;
                lastTitleScrollTime = currentTime;
            }
            break;
            
        case SCROLL_RETURNING:
            if (currentTime - lastTitleScrollTime >= titleScrollSpeed) {
                // Decrementar el offset para añadir letras al principio
                if (titleScrollOffset > 0) {
                    titleScrollOffset--;
                    
                    // Obtener el substring del título
                    String scrolledTitle = currentTitle.substring(titleScrollOffset);
                    
                    // Medir el ancho del texto actual
                    dma_display->setFont(&Picopixel);
                    dma_display->setTextSize(1);
                    int16_t x1, y1;
                    uint16_t w, h;
                    dma_display->getTextBounds(scrolledTitle, 1, titleY, &x1, &y1, &w, &h);
                    
                    // Limpiar el área del título
                    dma_display->fillRect(0, y1 - 1, PANEL_RES_X, h + 2, myBLACK);
                    
                    // Dibujar el título con scroll
                    dma_display->setTextColor(myWHITE);
                    dma_display->setCursor(1, titleY);
                    dma_display->print(scrolledTitle);
                    
                    lastTitleScrollTime = currentTime;
                } else {
                    // Hemos vuelto al principio, cambiar a pausa inicial
                    titleScrollState = SCROLL_PAUSED_START;
                    titleScrollPauseStart = currentTime;
                }
            }
            break;
    }
}

void showPhotoInfo(String title, String name)
{
    dma_display->setFont(&Picopixel);
    dma_display->setTextSize(1);
    dma_display->setTextColor(myWHITE);
    
    // Calcular las dimensiones reales de ambos textos primero
    int16_t titleX1, titleY1, nameX1, nameY1;
    uint16_t titleW = 0, titleH = 0, nameW = 0, nameH = 0;

    dma_display->setTextWrap(false);
    
    if (title.length() > 0) {
        dma_display->getTextBounds(title, 0, 0, &titleX1, &titleY1, &titleW, &titleH);
    }
    
    if (name.length() > 0) {
        dma_display->getTextBounds(name, 0, 0, &nameX1, &nameY1, &nameW, &nameH);
    }
    LOGF("titleW: %d title: %s", titleW, title.c_str());
    
    // Guardar el título y nombre actuales
    currentTitle = title;
    currentName = name;
    
    // Margen entre textos cuando están en la misma línea
    const int horizontalMargin = 4;
    
    // Decidir el layout basado en los anchos reales
    bool sameLine = false;
    if (title.length() > 0 && name.length() > 0) {
        // Verificar si ambos textos caben en la misma línea con margen
        sameLine = (titleW + nameW + horizontalMargin) <= PANEL_RES_X;
    } else {
        // Si solo hay uno de los dos textos, siempre va en una línea
        sameLine = true;
    }
    
    if (sameLine) {
        // Ambos textos en la misma línea (inferior)
        titleY = PANEL_RES_Y - 2;
        nameY = PANEL_RES_Y - 2;
        
        if (title.length() > 0) {
            // Verificar si el título necesita scroll
            if (titleW > PANEL_RES_X - 2) {
                // Activar scroll
                titleNeedsScroll = true;
                titleScrollOffset = 0;
                titleScrollState = SCROLL_PAUSED_START;
                titleScrollPauseStart = millis();
                
                // Dibujar el título completo inicialmente
                dma_display->getTextBounds(title, 1, titleY, &titleX1, &titleY1, &titleW, &titleH);
                dma_display->fillRect(0, titleY1 - 1, PANEL_RES_X, titleH + 2, myBLACK);
                dma_display->setCursor(1, titleY);
                dma_display->print(title);
            } else {
                // El título cabe, dibujarlo normalmente
                titleNeedsScroll = false;
                dma_display->getTextBounds(title, 1, titleY, &titleX1, &titleY1, &titleW, &titleH);
                dma_display->fillRect(0, titleY1 - 1, titleW + 3, titleH + 2, myBLACK);
                dma_display->setCursor(1, titleY);
                dma_display->print(title);
            }
        }
        
        if (name.length() > 0) {
            // Calcular posición X para alinear a la derecha
            int nameX = PANEL_RES_X - nameW;
            dma_display->getTextBounds(name, nameX, nameY, &nameX1, &nameY1, &nameW, &nameH);
            dma_display->fillRect(nameX - 1, nameY1 - 1, nameW + 2, nameH + 2, myBLACK);
            dma_display->setCursor(nameX, nameY);
            dma_display->print(name);
        }
    } else {
        // Textos en líneas diferentes
        // Calcular las posiciones Y para evitar solapamiento
        int bottomY = PANEL_RES_Y - 2;
        int topY = bottomY - titleH - 3; // 3 píxeles de separación entre líneas
        
        // Asegurar que el texto superior no esté demasiado arriba
        if (topY < PANEL_RES_Y - 12) {
            topY = PANEL_RES_Y - 12;
        }
        
        titleY = bottomY;
        nameY = topY;
        
        // Título en la línea inferior
        if (title.length() > 0) {
            // Verificar si el título necesita scroll
            if (titleW > PANEL_RES_X - 2) {
                // Activar scroll
                titleNeedsScroll = true;
                titleScrollOffset = 0;
                titleScrollState = SCROLL_PAUSED_START;
                titleScrollPauseStart = millis();
                
                // Dibujar el título completo inicialmente
                dma_display->getTextBounds(title, 1, titleY, &titleX1, &titleY1, &titleW, &titleH);
                dma_display->fillRect(0, titleY1 - 1, PANEL_RES_X, titleH + 2, myBLACK);
                dma_display->setCursor(1, titleY);
                dma_display->print(title);
            } else {
                // El título cabe, dibujarlo normalmente
                titleNeedsScroll = false;
                dma_display->getTextBounds(title, 1, titleY, &titleX1, &titleY1, &titleW, &titleH);
                dma_display->fillRect(0, titleY1 - 1, titleW + 3, titleH + 2, myBLACK);
                dma_display->setCursor(1, titleY);
                dma_display->print(title);
            }
        }
        
        // Nombre en la línea superior
        if (name.length() > 0) {
            dma_display->getTextBounds(name, 1, nameY, &nameX1, &nameY1, &nameW, &nameH);
            dma_display->fillRect(0, nameY1 - 1, nameW + 3, nameH + 2, myBLACK);
            dma_display->setCursor(1, nameY);
            dma_display->print(name);
        }
    }
}

// Función para mostrar el reloj overlay en la esquina superior derecha
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
    int yPos = h + 1;                  // 1px margen superior

    // Fondo negro para legibilidad
    dma_display->fillRect(xPos - 1, 0, w + 3, h + 2, myBLACK);
    dma_display->setTextColor(myWHITE);
    dma_display->setCursor(xPos, yPos);
    dma_display->print(timeStr);
}

// Función auxiliar para mostrar foto desde photoBuffer (ya descargada via MQTT)
void displayPhotoWithFade()
{
    // Solo hacer fadeOut DESPUÉS de confirmar que la imagen está completa
    fadeOut();
    dma_display->clearScreen();

    // Resetear el estado del scroll del título anterior
    titleNeedsScroll = false;

    // Copiar al screenBuffer desde el buffer estático
    for (int y = 0; y < 64; y++)
    {
        for (int x = 0; x < 64; x++)
        {
            int idx = (y * 64 + x) * 3;
            uint8_t g = photoBuffer[idx];
            uint8_t b = photoBuffer[idx + 1];
            uint8_t r = photoBuffer[idx + 2];
            screenBuffer[y][x] = dma_display->color565(r, g, b);
        }
    }

    fadeIn();
    showPhotoInfo(String(photoTitle), String(photoAuthor));
    showClockOverlay();
}

// Función para mostrar foto por índice (via MQTT)
void showPhoto(int index)
{
    // Evitar condiciones de carrera
    if (isLoadingPhoto) {
        LOG("Ya hay una foto cargándose, ignorando petición");
        return;
    }
    isLoadingPhoto = true;

    LOGF("[Photo] Heap libre: %d bytes", ESP.getFreeHeap());
    LOGF("[Photo] Solicitando foto index=%d via MQTT", index);

    // Generar request ID único para filtrar respuestas stale
    mqttRequestId++;

    // Publicar request via MQTT
    String topic = String("frame/") + String(frameId) + "/request/photo";
    String payload = "{\"index\":" + String(index) + ",\"reqId\":" + String(mqttRequestId) + "}";

    if (!mqttClient.publish(topic.c_str(), payload.c_str())) {
        LOG("[Photo] Error publicando request MQTT");
        isLoadingPhoto = false;
        processPendingPhoto();
        return;
    }

    // Esperar respuesta
    if (waitForMqttResponse("photo", 15000)) {
        esp_task_wdt_reset();
        LOGF("[Photo] Foto recibida via MQTT: %s by %s", photoTitle, photoAuthor);
        displayPhotoWithFade();
    } else {
        LOG("[Photo] Error recibiendo foto via MQTT");
    }

    isLoadingPhoto = false;
    processPendingPhoto();
}

// Función para mostrar foto por ID (via MQTT)
void showPhotoById(int id)
{
    // Evitar condiciones de carrera
    if (isLoadingPhoto) {
        LOG("Ya hay una foto cargándose, ignorando petición");
        return;
    }
    isLoadingPhoto = true;

    LOGF("[Photo] Heap libre: %d bytes", ESP.getFreeHeap());
    LOGF("[Photo] Solicitando foto id=%d via MQTT", id);

    // Publicar request via MQTT
    String topic = String("frame/") + String(frameId) + "/request/photo";
    String payload = "{\"id\":" + String(id) + "}";

    if (!mqttClient.publish(topic.c_str(), payload.c_str())) {
        LOG("[Photo] Error publicando request MQTT");
        isLoadingPhoto = false;
        processPendingPhoto();
        return;
    }

    // Esperar respuesta
    if (waitForMqttResponse("photo", 15000)) {
        esp_task_wdt_reset();
        LOGF("[Photo] Foto recibida via MQTT: %s by %s", photoTitle, photoAuthor);
        displayPhotoWithFade();
    } else {
        LOG("[Photo] Error recibiendo foto via MQTT");
    }

    isLoadingPhoto = false;
    processPendingPhoto();
}

// Función auxiliar para mostrar foto desde photoBuffer con animación desde el centro
void displayPhotoFromCenter()
{
    // Resetear el estado del scroll del título anterior
    titleNeedsScroll = false;

    const int width = 64;
    const int height = 64;
    int centerX = 32;
    int centerY = 32;

    // 🟥 ANIMACIÓN DE CUADRADOS DE COLORES
    uint16_t colors[] = {color1, color2, color3, color4, color5};
    int numColors = 5;

    for (int colorIndex = 0; colorIndex < numColors; colorIndex++)
    {
        for (int size = 2; size <= max(PANEL_RES_X, PANEL_RES_Y); size += 2)
        {
            // Dibujar el cuadrado actual
            for (int y = centerY - size / 2; y <= centerY + size / 2; y++)
            {
                for (int x = centerX - size / 2; x <= centerX + size / 2; x++)
                {
                    if (x >= 0 && x < PANEL_RES_X && y >= 0 && y < PANEL_RES_Y)
                    {
                        drawPixelWithBuffer(x, y, colors[colorIndex]);
                    }
                }
            }
            delay(5);
        }
    }

    // 🖼️ Mostrar imagen desde el centro
    for (int radius = 0; radius <= max(width, height); radius++)
    {
        for (int y = centerY - radius; y <= centerY + radius; y++)
        {
            for (int x = centerX - radius; x <= centerX + radius; x++)
            {
                if (x >= 0 && x < width && y >= 0 && y < height)
                {
                    if (abs(x - centerX) == radius || abs(y - centerY) == radius)
                    {
                        int index = (y * width + x) * 3;
                        uint8_t r = photoBuffer[index + 2];
                        uint8_t g = photoBuffer[index + 0];
                        uint8_t b = photoBuffer[index + 1];
                        uint16_t color = dma_display->color565(r, g, b);
                        drawPixelWithBuffer(x, y, color);
                    }
                }
            }
        }
        delay(5);
    }
    showPhotoInfo(String(photoTitle), String(photoAuthor));
}

// Función para mostrar foto por ID con animación desde centro (via MQTT)
void showPhotoFromCenterById(int id)
{
    // Evitar condiciones de carrera
    if (isLoadingPhoto) {
        LOG("Ya hay una foto cargándose, ignorando petición");
        return;
    }
    isLoadingPhoto = true;

    LOGF("[PhotoCenter] Heap libre: %d bytes", ESP.getFreeHeap());
    LOGF("[PhotoCenter] Solicitando foto id=%d via MQTT", id);

    // Publicar request via MQTT
    String topic = String("frame/") + String(frameId) + "/request/photo";
    String payload = "{\"id\":" + String(id) + "}";

    if (!mqttClient.publish(topic.c_str(), payload.c_str())) {
        LOG("[PhotoCenter] Error publicando request MQTT");
        isLoadingPhoto = false;
        processPendingPhoto();
        return;
    }

    // Esperar respuesta
    if (waitForMqttResponse("photo", 15000)) {
        esp_task_wdt_reset();
        LOGF("[PhotoCenter] Foto recibida via MQTT: %s by %s", photoTitle, photoAuthor);
        displayPhotoFromCenter();
    } else {
        LOG("[PhotoCenter] Error recibiendo foto via MQTT");
    }

    songShowing = "";
    isLoadingPhoto = false;
    processPendingPhoto();
}

void showPhotoIndex(int index)
{
    LOGF("[Photo] Solicitando foto index=%d", index);
    showPhoto(index);
}

void showPhotoId(int id)
{
    LOGF("[Photo] Solicitando foto id=%d", id);
    showPhotoById(id);
}

// Función para procesar foto pendiente (si hay alguna)
void processPendingPhoto() {
    if (pendingNewPhotoId >= 0) {
        int id = pendingNewPhotoId;
        pendingNewPhotoId = -1;  // Limpiar antes de procesar
        LOGF("[Photo] Procesando foto pendiente id=%d", id);
        onReceiveNewPic(id);
    }
}

// Función para la animación de nueva foto
void onReceiveNewPic(int id)
{
    // Si ya hay una foto cargándose, guardar como pendiente
    if (isLoadingPhoto) {
        LOGF("[Photo] Foto %d guardada como pendiente (hay otra cargando)", id);
        pendingNewPhotoId = id;
        return;
    }

    photoIndex = 1;

    // Mostrar la foto con animación desde el centro via MQTT
    showPhotoFromCenterById(id);
    lastPhotoChange = millis();
    photoIndex = 0;
    songShowing = "";
}

void showUpdateMessage()
{
    dma_display->clearScreen();
    LOG("Se limpia pantalla");
    dma_display->setFont();
    dma_display->setCursor(8, 50); // Centrar en el panel
    dma_display->print("Updating");
    LOG("Se muestra mensaje de actualizacion");
}

void showCheckMessage()
{
    showLoadingMsg("Checking updates");
}

void checkForUpdates()
{
    // En modo DEV, no comprobar actualizaciones
    if (DEV) {
        LOG("Modo DEV activo - saltando comprobación de actualizaciones");
        return;
    }

    // Asegurar que el tiempo esté sincronizado antes de la actualización
    timeClient.update();
    time_t now = timeClient.getEpochTime();
    LOGF("Current time (UTC): %lu", now);
    LOGF("Current time (formatted): %s", timeClient.getFormattedTime().c_str());

    // Configurar el tiempo del sistema para URLs firmadas
    struct timeval tv;
    tv.tv_sec = now;
    tv.tv_usec = 0;
    settimeofday(&tv, NULL);

    LOG("[OTA] Solicitando información de actualización via MQTT");

    // Publicar request via MQTT
    String topic = String("frame/") + String(frameId) + "/request/ota";
    if (!mqttClient.publish(topic.c_str(), "{}")) {
        LOG("[OTA] Error publicando request MQTT");
        return;
    }

    // Esperar respuesta
    if (!waitForMqttResponse("ota", 10000)) {
        LOG("[OTA] Error recibiendo respuesta de versión via MQTT");
        return;
    }

    // Parsear la respuesta JSON (almacenada en httpBuffer por handleOtaResponse)
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, httpBuffer);
    if (error) {
        LOGF("[OTA] Error parseando JSON de versión: %s", error.c_str());
        return;
    }

    int latestVersion = doc["version"];
    String updateUrl = doc["url"].as<String>();
    LOGF("Latest version: %d", latestVersion);
    LOGF("Update URL: %s", updateUrl.c_str());

    if (latestVersion > currentVersion)
    {
        LOGF("New version available: %d", latestVersion);
        showUpdateMessage();
        LOG("Downloading update...");

        // La descarga del firmware sigue usando HTTP (necesario para streaming)
        WiFiClient *updateClient;
        if (updateUrl.startsWith("https://")) {
            WiFiClientSecure *secureClient = new WiFiClientSecure();
            secureClient->setInsecure();
            updateClient = secureClient;
        } else {
            updateClient = new WiFiClient();
        }

        HTTPClient updateHttp;
        updateHttp.begin(*updateClient, updateUrl);
        updateHttp.setTimeout(30000);
        int updateHttpCode = updateHttp.GET();

        if (updateHttpCode == 200)
        {
            WiFiClient *updateStream = updateHttp.getStreamPtr();
            size_t contentLength = updateHttp.getSize();
            Update.onProgress([](size_t written, size_t total)
                              {
                int percent = (written * 100) / total;
                showPercetage(percent);
                LOGF("Progreso: %d%% (%d/%d bytes)", percent, written, total); });

            if (Update.begin(contentLength))
            {
                size_t written = Update.writeStream(*updateStream);
                if (written == contentLength)
                {
                    LOG("[OTA] Firmware escrito correctamente");
                    if (Update.end())
                    {
                        LOG("[OTA] Actualización completada exitosamente");
                        preferences.putInt("currentVersion", latestVersion);
                        ESP.restart();
                    }
                    else
                    {
                        LOGF("[OTA] Error al finalizar Update: %s", Update.errorString());
                    }
                }
                else
                {
                    LOGF("[OTA] Error de escritura: solo %d/%d bytes escritos", written, contentLength);
                }
            }
            else
            {
                LOG("[OTA] Error: espacio insuficiente para actualización");
            }
        }
        else
        {
            LOGF("[OTA] Error HTTP al descargar firmware: %d", updateHttpCode);
            if (updateHttpCode == 404) {
                LOG("[OTA] Archivo no encontrado o URL expirada");
            }
            String response = updateHttp.getString();
            LOGF("[OTA] Respuesta del servidor: %s", response.c_str());
        }
        updateHttp.end();
        delete updateClient;
    }
    else
    {
        LOG("No new updates available.");
    }
}

// Registro via MQTT (reemplaza HTTP para evitar problemas SSL)
bool registerFrameViaMQTT()
{
    String macAddress = WiFi.macAddress();
    LOGF("[MQTT:register] Registrando frame con MAC: %s", macAddress.c_str());

    // Client ID temporal basado en MAC
    String tempClientId = "frame-mac-" + macAddress;
    tempClientId.replace(":", "");  // Quitar : del MAC para el client ID

    // Configurar MQTT
    mqttClient.setServer(MQTT_BROKER_URL, MQTT_BROKER_PORT);
    mqttClient.setCallback(mqttCallback);
    mqttClient.setKeepAlive(60);

    // Conectar con client ID temporal
    LOGF("[MQTT:register] Conectando con client ID: %s", tempClientId.c_str());
    if (!mqttClient.connect(tempClientId.c_str(), MQTT_BROKER_USERNAME, MQTT_BROKER_PASSWORD)) {
        LOGF("[MQTT:register] Error conectando: %d", mqttClient.state());
        return false;
    }
    LOG("[MQTT:register] Conectado a MQTT");

    // Suscribirse al topic de respuesta
    String responseTopic = "frame/mac/" + macAddress + "/response/register";
    LOGF("[MQTT:register] Suscribiendo a: %s", responseTopic.c_str());
    if (!mqttClient.subscribe(responseTopic.c_str())) {
        LOG("[MQTT:register] Error suscribiendo");
        mqttClient.disconnect();
        return false;
    }

    // Publicar request de registro
    String requestTopic = "frame/mac/" + macAddress + "/request/register";
    LOGF("[MQTT:register] Publicando en: %s", requestTopic.c_str());
    if (!mqttClient.publish(requestTopic.c_str(), "{}")) {
        LOG("[MQTT:register] Error publicando request");
        mqttClient.disconnect();
        return false;
    }

    // Esperar respuesta
    if (waitForMqttResponse("register", 10000)) {
        if (mqttRegisterFrameId > 0) {
            frameId = mqttRegisterFrameId;
            preferences.putInt("frameId", frameId);
            LOGF("[MQTT:register] Frame registrado: ID=%d", frameId);

            // Desconectar para reconectar luego con el client ID correcto
            mqttClient.disconnect();
            return true;
        }
    }

    LOG("[MQTT:register] Timeout o error en registro");
    mqttClient.disconnect();
    return false;
}

// Función para reiniciar todas las preferencias a valores de fábrica
void testInit()
{
    preferences.begin("wifi", false);
    preferences.clear(); // Borra todas las preferencias
    preferences.end();
    preferences.begin("wifi", false);
    preferences.putInt("currentVersion", 0);
    preferences.putInt("frameId", 0);
    preferences.putInt("brightness", 50);
    preferences.putInt("maxPhotos", 5);
    preferences.putUInt("secsPhotos", 30000);
    preferences.putString("ssid", "");
    preferences.putString("password", "");
    preferences.putBool("allowSpotify", false);
    LOG("Preferencias reiniciadas a valores de fábrica");
}

// Función para esperar respuesta MQTT con timeout
bool waitForMqttResponse(const char* expectedType, unsigned long timeout) {
    mqttResponseReceived = false;
    mqttResponseSuccess = false;
    mqttResponseType = "";
    unsigned long start = millis();

    while ((millis() - start) < timeout) {
        esp_task_wdt_reset();
        mqttClient.loop();
        yield();
        delay(10);

        // Solo salir si recibimos la respuesta del tipo esperado Y fue exitosa
        if (mqttResponseReceived && mqttResponseType == expectedType) {
            if (mqttResponseSuccess) {
                return true;
            }
            // Respuesta stale (reqId incorrecto) - seguir esperando la buena
            LOGF("[MQTT] Respuesta stale descartada, seguimos esperando '%s'", expectedType);
            mqttResponseReceived = false;
            mqttResponseType = "";
            continue;
        }

        // Si recibimos respuesta de otro tipo (tardía), ignorarla y seguir esperando
        if (mqttResponseReceived && mqttResponseType != expectedType) {
            LOGF("[MQTT] Ignorando respuesta tardía tipo '%s' (esperando '%s')",
                 mqttResponseType.c_str(), expectedType);
            mqttResponseReceived = false;
            mqttResponseType = "";
        }
    }

    LOGF("[MQTT] Timeout esperando respuesta %s", expectedType);
    return false;
}

// Handlers para respuestas MQTT (patrón request/response)
void handleSongResponse(byte* payload, unsigned int length) {
    songIdBuffer[0] = '\0';  // Limpiar buffer

    if (length > 0 && length < sizeof(httpBuffer)) {
        memcpy(httpBuffer, payload, length);
        httpBuffer[length] = '\0';

        // Parsear para extraer el id
        char *idStart = strstr(httpBuffer, "\"id\":\"");
        if (idStart) {
            idStart += 6;
            char *idEnd = strchr(idStart, '"');
            if (idEnd && (idEnd - idStart) < 63) {
                int idLen = idEnd - idStart;
                strncpy(songIdBuffer, idStart, idLen);
                songIdBuffer[idLen] = '\0';
            }
        }
    }

    mqttResponseReceived = true;
    mqttResponseSuccess = true;
    mqttResponseType = "song";
}

void handleCoverResponse(byte* payload, unsigned int length) {
    if (length == 8192) {
        memcpy(spotifyCoverBuffer, payload, length);
        mqttResponseSuccess = true;
        LOG("[MQTT] Cover recibido correctamente");
    } else {
        LOGF("[MQTT] Cover con tamaño incorrecto: %d (esperado 8192)", length);
        mqttResponseSuccess = false;
    }
    mqttResponseReceived = true;
    mqttResponseType = "cover";
}

void handlePhotoResponse(byte* payload, unsigned int length) {
    photoTitle[0] = '\0';
    photoAuthor[0] = '\0';

    // Formato: {"title":"x","author":"y","reqId":N}\n[12288 bytes binarios]
    // Buscar el newline que separa JSON de binario
    int jsonEnd = -1;
    for (unsigned int i = 0; i < min(length, 256u); i++) {
        if (payload[i] == '\n') {
            jsonEnd = i;
            break;
        }
    }

    if (jsonEnd > 0 && (length - jsonEnd - 1) >= 12288) {
        // Parsear JSON metadata
        char jsonBuf[256];
        memcpy(jsonBuf, payload, jsonEnd);
        jsonBuf[jsonEnd] = '\0';

        JsonDocument doc;
        if (deserializeJson(doc, jsonBuf) == DeserializationError::Ok) {
            // Filtrar respuestas stale: ignorar si reqId no coincide
            if (doc.containsKey("reqId")) {
                uint32_t respReqId = doc["reqId"];
                if (respReqId != mqttRequestId) {
                    LOGF("[MQTT] Ignorando foto stale (reqId=%d, esperado=%d): %s",
                         respReqId, mqttRequestId, (const char*)(doc["title"] | "?"));
                    mqttResponseReceived = true;
                    mqttResponseType = "photo";
                    mqttResponseSuccess = false;
                    return;
                }
            }

            strncpy(photoTitle, doc["title"] | "", sizeof(photoTitle) - 1);
            strncpy(photoAuthor, doc["author"] | "", sizeof(photoAuthor) - 1);
            photoTitle[sizeof(photoTitle) - 1] = '\0';
            photoAuthor[sizeof(photoAuthor) - 1] = '\0';
        }

        // Copiar datos binarios
        memcpy(photoBuffer, payload + jsonEnd + 1, 12288);
        mqttResponseSuccess = true;
        LOGF("[MQTT] Foto recibida (reqId=%d): %s by %s", mqttRequestId, photoTitle, photoAuthor);
    } else {
        LOGF("[MQTT] Foto con formato incorrecto: length=%d, jsonEnd=%d", length, jsonEnd);
        mqttResponseSuccess = false;
    }
    mqttResponseReceived = true;
    mqttResponseType = "photo";
}

void handleOtaResponse(byte* payload, unsigned int length) {
    if (length > 0 && length < sizeof(httpBuffer) - 1) {
        memcpy(httpBuffer, payload, length);
        httpBuffer[length] = '\0';
        mqttResponseSuccess = true;
        LOG("[MQTT] Respuesta OTA recibida");
    } else {
        mqttResponseSuccess = false;
    }
    mqttResponseReceived = true;
    mqttResponseType = "ota";
}

void handleConfigResponse(byte* payload, unsigned int length) {
    if (length > 0 && length < sizeof(httpBuffer) - 1) {
        memcpy(httpBuffer, payload, length);
        httpBuffer[length] = '\0';

        JsonDocument doc;
        if (deserializeJson(doc, httpBuffer) == DeserializationError::Ok) {
            if (doc.containsKey("brightness")) {
                brightness = doc["brightness"];
                if (startupBrightnessRampDone) {
                    dma_display->setBrightness(max(brightness, 10));
                }
                preferences.putInt("brightness", brightness);
                LOGF("[MQTT] Config brightness: %d", brightness);
            }
            if (doc.containsKey("pictures_on_queue")) {
                maxPhotos = doc["pictures_on_queue"];
                preferences.putInt("maxPhotos", maxPhotos);
                LOGF("[MQTT] Config max photos: %d", maxPhotos);
            }
            if (doc.containsKey("spotify_enabled")) {
                allowSpotify = doc["spotify_enabled"];
                preferences.putBool("allowSpotify", allowSpotify);
                LOGF("[MQTT] Config spotify: %s", allowSpotify ? "true" : "false");
            }
            if (doc.containsKey("secs_between_photos")) {
                int secsBetweenPhotos = doc["secs_between_photos"];
                secsPhotos = secsBetweenPhotos * 1000;
                preferences.putUInt("secsPhotos", secsPhotos);
                LOGF("[MQTT] Config secs between photos: %d", secsBetweenPhotos);
            }
            if (doc.containsKey("schedule_enabled")) {
                scheduleEnabled = doc["schedule_enabled"];
                preferences.putBool("scheduleEnabled", scheduleEnabled);
                LOGF("[MQTT] Config schedule enabled: %s", scheduleEnabled ? "true" : "false");
            }
            if (doc.containsKey("schedule_on_hour")) {
                scheduleOnHour = doc["schedule_on_hour"];
                preferences.putInt("scheduleOnHour", scheduleOnHour);
                LOGF("[MQTT] Config schedule on hour: %d", scheduleOnHour);
            }
            if (doc.containsKey("schedule_on_minute")) {
                scheduleOnMinute = doc["schedule_on_minute"];
                preferences.putInt("scheduleOnMinute", scheduleOnMinute);
                LOGF("[MQTT] Config schedule on minute: %d", scheduleOnMinute);
            }
            if (doc.containsKey("schedule_off_hour")) {
                scheduleOffHour = doc["schedule_off_hour"];
                preferences.putInt("scheduleOffHour", scheduleOffHour);
                LOGF("[MQTT] Config schedule off hour: %d", scheduleOffHour);
            }
            if (doc.containsKey("schedule_off_minute")) {
                scheduleOffMinute = doc["schedule_off_minute"];
                preferences.putInt("scheduleOffMinute", scheduleOffMinute);
                LOGF("[MQTT] Config schedule off minute: %d", scheduleOffMinute);
            }
            if (doc.containsKey("timezone_offset")) {
                timezoneOffset = doc["timezone_offset"];
                preferences.putInt("timezoneOffset", timezoneOffset);
                LOGF("[MQTT] Config timezone offset: %d", timezoneOffset);
            }
            if (doc.containsKey("clock_enabled")) {
                clockEnabled = doc["clock_enabled"];
                preferences.putBool("clockEnabled", clockEnabled);
                LOGF("[MQTT] Config clock enabled: %s", clockEnabled ? "true" : "false");
            }
            mqttResponseSuccess = true;
            LOG("[MQTT] Configuración recibida correctamente");
        } else {
            LOG("[MQTT] Error parseando config JSON");
            mqttResponseSuccess = false;
        }
    } else {
        mqttResponseSuccess = false;
    }
    mqttResponseReceived = true;
    mqttResponseType = "config";
}

// Handler para respuesta de registro via MQTT
void handleRegisterResponse(byte* payload, unsigned int length) {
    mqttRegisterFrameId = 0;

    if (length > 0 && length < sizeof(httpBuffer)) {
        memcpy(httpBuffer, payload, length);
        httpBuffer[length] = '\0';

        LOGF("[MQTT:register] Respuesta recibida: %s", httpBuffer);

        // TODO: Backend still sends "pixieId" - update to "frameId" when backend is migrated
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, httpBuffer);
        if (!error) {
            mqttRegisterFrameId = doc["pixieId"] | 0;
            LOGF("[MQTT:register] frameId=%d", mqttRegisterFrameId);
            mqttResponseSuccess = (mqttRegisterFrameId > 0);
        } else {
            LOG("[MQTT:register] Error parseando JSON");
            mqttResponseSuccess = false;
        }
    } else {
        mqttResponseSuccess = false;
    }
    mqttResponseReceived = true;
    mqttResponseType = "register";
}

// Función para manejar los mensajes MQTT recibidos
void mqttCallback(char *topic, byte *payload, unsigned int length)
{
    String topicStr = String(topic);

    // Manejar respuestas del patrón request/response
    if (topicStr.indexOf("/response/") != -1) {
        if (topicStr.endsWith("/response/song")) {
            handleSongResponse(payload, length);
        }
        else if (topicStr.endsWith("/response/cover")) {
            handleCoverResponse(payload, length);
        }
        else if (topicStr.endsWith("/response/photo")) {
            handlePhotoResponse(payload, length);
        }
        else if (topicStr.endsWith("/response/ota")) {
            handleOtaResponse(payload, length);
        }
        else if (topicStr.endsWith("/response/config")) {
            handleConfigResponse(payload, length);
        }
        else if (topicStr.endsWith("/response/register")) {
            handleRegisterResponse(payload, length);
        }
        return;  // No procesar como comando normal
    }

    // Crear un buffer para el mensaje (solo para comandos, no respuestas binarias)
    char message[length + 1];
    memcpy(message, payload, length);
    message[length] = '\0';

    // Imprimir el mensaje
    LOGF("Mensaje recibido en el topic: %s", topic);
    LOGF("Mensaje: %s", message);

    // Crear un documento JSON
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, message);

    if (!error)
    {
        const char *action = doc["action"];
        if (action)
        {
            if (strcmp(action, "update_photo") == 0)
            {
                LOG("Se recibio una nueva foto por MQTT");
                int id = doc["id"];
                onReceiveNewPic(id);
            }
            else if (strcmp(action, "update_info") == 0)
            {
                LOG("[MQTT] Recibida actualización de configuración");

                // Leer configuración directamente del payload MQTT
                if (doc.containsKey("brightness")) {
                    brightness = doc["brightness"];
                    if (startupBrightnessRampDone) {
                        dma_display->setBrightness(max(brightness, 10));
                    }
                    preferences.putInt("brightness", brightness);
                    LOGF("[MQTT] Brightness: %d", brightness);
                }
                if (doc.containsKey("pictures_on_queue")) {
                    maxPhotos = doc["pictures_on_queue"];
                    preferences.putInt("maxPhotos", maxPhotos);
                    LOGF("[MQTT] Max photos: %d", maxPhotos);
                }
                if (doc.containsKey("spotify_enabled")) {
                    allowSpotify = doc["spotify_enabled"];
                    preferences.putBool("allowSpotify", allowSpotify);
                    LOGF("[MQTT] Spotify enabled: %s", allowSpotify ? "true" : "false");
                }
                if (doc.containsKey("secs_between_photos")) {
                    int secsBetweenPhotos = doc["secs_between_photos"];
                    secsPhotos = secsBetweenPhotos * 1000;
                    preferences.putUInt("secsPhotos", secsPhotos);
                    LOGF("[MQTT] Secs between photos: %d", secsBetweenPhotos);
                }
                if (doc.containsKey("schedule_enabled")) {
                    scheduleEnabled = doc["schedule_enabled"];
                    preferences.putBool("scheduleEnabled", scheduleEnabled);
                    LOGF("[MQTT] Schedule enabled: %s", scheduleEnabled ? "true" : "false");
                }
                if (doc.containsKey("schedule_on_hour")) {
                    scheduleOnHour = doc["schedule_on_hour"];
                    preferences.putInt("scheduleOnHour", scheduleOnHour);
                    LOGF("[MQTT] Schedule on hour: %d", scheduleOnHour);
                }
                if (doc.containsKey("schedule_on_minute")) {
                    scheduleOnMinute = doc["schedule_on_minute"];
                    preferences.putInt("scheduleOnMinute", scheduleOnMinute);
                    LOGF("[MQTT] Schedule on minute: %d", scheduleOnMinute);
                }
                if (doc.containsKey("schedule_off_hour")) {
                    scheduleOffHour = doc["schedule_off_hour"];
                    preferences.putInt("scheduleOffHour", scheduleOffHour);
                    LOGF("[MQTT] Schedule off hour: %d", scheduleOffHour);
                }
                if (doc.containsKey("schedule_off_minute")) {
                    scheduleOffMinute = doc["schedule_off_minute"];
                    preferences.putInt("scheduleOffMinute", scheduleOffMinute);
                    LOGF("[MQTT] Schedule off minute: %d", scheduleOffMinute);
                }
                if (doc.containsKey("timezone_offset")) {
                    timezoneOffset = doc["timezone_offset"];
                    preferences.putInt("timezoneOffset", timezoneOffset);
                    LOGF("[MQTT] Timezone offset: %d", timezoneOffset);
                }
                if (doc.containsKey("clock_enabled")) {
                    clockEnabled = doc["clock_enabled"];
                    preferences.putBool("clockEnabled", clockEnabled);
                    LOGF("[MQTT] Clock enabled: %s", clockEnabled ? "true" : "false");
                }
            }
            else if (strcmp(action, "update_bin") == 0)
            {
                if (DEV) {
                    LOG("Comando de actualización recibido por MQTT - ignorado en modo DEV");
                } else {
                    LOG("Se recibio una actualizacion de binario por MQTT");
                    checkForUpdates();
                }
            }
            else if (strcmp(action, "factory_reset") == 0)
            {
                LOG("Factory reset recibido via MQTT");
                dma_display->clearScreen();
                showLoadingMsg("Factory Reset...");
                delay(2000);
                preferences.clear();
                ESP.restart();
            }
            else if (strcmp(action, "reset_env_vars") == 0)
            {
                LOG("Reset de variables de entorno recibido via MQTT");
                dma_display->clearScreen();
                showLoadingMsg("Resetting Env Vars...");
                delay(1000);
                
                // Resetear variables de entorno sin reiniciar el dispositivo
                preferences.begin("wifi", false);
                preferences.clear();
                preferences.end();
                
                // Reinicializar con valores por defecto
                preferences.begin("wifi", false);
                preferences.putInt("currentVersion", 0);
                preferences.putInt("frameId", 0);
                preferences.putInt("brightness", 50);
                preferences.putInt("maxPhotos", 5);
                preferences.putUInt("secsPhotos", 30000);
                preferences.putString("ssid", "");
                preferences.putString("password", "");
                preferences.putBool("allowSpotify", false);
                preferences.end();

                // Resetear variables globales
                brightness = 50;
                maxPhotos = 5;
                secsPhotos = 30000;
                allowSpotify = false;
                currentVersion = 0;
                frameId = 0;
                scheduleEnabled = false;
                scheduleOnHour = 8;
                scheduleOnMinute = 0;
                scheduleOffHour = 22;
                scheduleOffMinute = 0;
                timezoneOffset = 0;
                screenOff = false;

                showLoadingMsg("Env Vars Reset!");
                delay(2000);
                dma_display->clearScreen();
            }
            // ===== HANDLERS DE DIBUJO =====
            else if (strcmp(action, "enter_draw_mode") == 0)
            {
                LOG("Entrando en modo dibujo via MQTT");
                enterDrawingMode();
            }
            else if (strcmp(action, "exit_draw_mode") == 0)
            {
                LOG("Saliendo del modo dibujo via MQTT");
                exitDrawingMode();
            }
            else if (strcmp(action, "draw_pixel") == 0)
            {
                // Auto-entrar en modo dibujo si no está activo
                if (!drawingMode) {
                    LOG("Auto-activando modo dibujo para draw_pixel");
                    enterDrawingMode();
                }
                
                int x = doc["x"];
                int y = doc["y"];
                const char* colorHex = doc["color"];
                int size = doc["size"] | 1; // Default to 1 if not provided
                
                // Convert hex color to RGB565 (with BGR correction)
                uint16_t color = 0;
                if (colorHex && strlen(colorHex) == 7 && colorHex[0] == '#') {
                    uint32_t rgb = strtol(colorHex + 1, NULL, 16);
                    uint8_t r = (rgb >> 16) & 0xFF;
                    uint8_t g = (rgb >> 8) & 0xFF;
                    uint8_t b = rgb & 0xFF;
                    // Swap channels: RGB -> BGR
                    color = dma_display->color565(b, r, g);
                }
                // Agregar comando al buffer en lugar de dibujar inmediatamente
                addDrawCommand(x, y, color, size);
            }
            else if (strcmp(action, "draw_stroke") == 0)
            {
                // Auto-entrar en modo dibujo si no está activo
                if (!drawingMode) {
                    enterDrawingMode();
                }
                
                JsonArray points = doc["points"];
                uint16_t color = doc["color"];
                
                drawDrawingStroke(points, color);
                // No llamar a updateDrawingDisplay aquí - será manejado por el buffer
            }
            else if (strcmp(action, "clear_canvas") == 0)
            {
                // Auto-entrar en modo dibujo si no está activo
                if (!drawingMode) {
                    LOG("Auto-activando modo dibujo para clear_canvas");
                    enterDrawingMode();
                }
                
                clearDrawingCanvas();
                // No necesitamos updateDrawingDisplay - clearDrawingCanvas ya limpia la pantalla
            }
            // ===== FIN HANDLERS DE DIBUJO =====
        }
    }
}

// Función para reconectar al broker MQTT
void mqttReconnect()
{
    int retryCount = 0;

    while (!mqttClient.connected() && retryCount < MQTT_MAX_RETRIES)
    {
        LOGF("[MQTT:mqttReconnect] Intentando conexión (intento %d/%d) a %s:%d...",
                      retryCount + 1, MQTT_MAX_RETRIES, MQTT_BROKER_URL, MQTT_BROKER_PORT);

        String clientId = String(MQTT_CLIENT_ID) + String(frameId);

        if (mqttClient.connect(clientId.c_str(), MQTT_BROKER_USERNAME, MQTT_BROKER_PASSWORD))
        {
            LOG("[MQTT:mqttReconnect] Conectado exitosamente");

            // Suscribirse al topic principal de comandos
            String topic = String("frame/") + String(frameId);
            LOGF("[MQTT:mqttReconnect] Suscribiendo al tema: %s", topic.c_str());
            if (mqttClient.subscribe(topic.c_str())) {
                LOG("[MQTT:mqttReconnect] Suscripción exitosa");
            } else {
                LOG("[MQTT:mqttReconnect] Error: fallo en suscripción al tema");
            }

            // Suscribirse a topics de respuesta (para patrón request/response)
            String responseTopic = String("frame/") + String(frameId) + "/response/#";
            LOGF("[MQTT:mqttReconnect] Suscribiendo a respuestas: %s", responseTopic.c_str());
            if (mqttClient.subscribe(responseTopic.c_str())) {
                LOG("[MQTT:mqttReconnect] Suscripción a respuestas exitosa");
                loadingMsg = "";
            } else {
                LOG("[MQTT:mqttReconnect] Error: fallo en suscripción a respuestas");
            }

            return;  // Conexión exitosa
        }
        else
        {
            const char* stateStr;
            switch(mqttClient.state()) {
                case -4: stateStr = "CONNECTION_TIMEOUT"; break;
                case -3: stateStr = "CONNECTION_LOST"; break;
                case -2: stateStr = "CONNECT_FAILED"; break;
                case -1: stateStr = "DISCONNECTED"; break;
                case 0: stateStr = "CONNECTED"; break;
                case 1: stateStr = "BAD_PROTOCOL"; break;
                case 2: stateStr = "BAD_CLIENT_ID"; break;
                case 3: stateStr = "UNAVAILABLE"; break;
                case 4: stateStr = "BAD_CREDENTIALS"; break;
                case 5: stateStr = "UNAUTHORIZED"; break;
                default: stateStr = "UNKNOWN"; break;
            }
            LOGF("[MQTT:mqttReconnect] Error de conexión, código=%d (%s)", mqttClient.state(), stateStr);
            retryCount++;
            if (retryCount < MQTT_MAX_RETRIES) {
                LOGF("[MQTT:mqttReconnect] Reintentando en %d ms...", MQTT_RETRY_DELAY);
                wait(MQTT_RETRY_DELAY);
            }
        }
    }

    if (!mqttClient.connected()) {
        LOG("[MQTT:mqttReconnect] Error crítico: máximo de reintentos alcanzado - reiniciando ESP32");
        dma_display->clearScreen();
        showLoadingMsg("MQTT Error");
        wait(3000);
        ESP.restart();
    }
}

// Función para dibujar el logo desde logo.h (64x64 RGB)
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

// ===== FUNCIONES BLE PARA PROVISIONING =====

// Callback para conexiones del servidor BLE
class FrameBLEServerCallbacks : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer* pServer) {
        bleDeviceConnected = true;
        LOG("[BLE] Cliente conectado");
    }

    void onDisconnect(NimBLEServer* pServer) {
        bleDeviceConnected = false;
        LOG("[BLE] Cliente desconectado");
        // Reiniciar advertising si no hay credenciales recibidas
        if (!bleCredentialsReceived) {
            NimBLEDevice::startAdvertising();
            LOG("[BLE] Reiniciando advertising");
        }
    }
};

// Callback para recibir credenciales WiFi via BLE
class WifiCredentialsCallback : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* pCharacteristic) {
        std::string value = pCharacteristic->getValue();
        if (value.length() > 0) {
            LOG("[BLE] Credenciales recibidas");

            // Los datos ya vienen en texto plano (la librería BLE del móvil decodifica base64)
            String decoded = String(value.c_str());
            LOGF("[BLE] Datos recibidos: %s", decoded.c_str());

            // Parsear SSID;PASSWORD
            int separatorIndex = decoded.indexOf(';');
            if (separatorIndex > 0) {
                bleReceivedSSID = decoded.substring(0, separatorIndex);
                bleReceivedPassword = decoded.substring(separatorIndex + 1);
                bleCredentialsReceived = true;

                LOGF("[BLE] SSID: %s", bleReceivedSSID.c_str());
                LOG("[BLE] Password recibida (oculta por seguridad)");
            } else {
                LOG("[BLE] Error: formato de credenciales inválido");
            }
        }
    }
};

// Función para enviar respuesta via BLE
void sendBLEResponse(bool success, String frameToken = "", String error = "") {
    if (pResponseChar == nullptr) {
        LOG("[BLE] Error: característica de respuesta no inicializada");
        return;
    }

    JsonDocument doc;
    doc["success"] = success;
    if (frameToken.length() > 0) {
        doc["frameToken"] = frameToken;
    }
    if (error.length() > 0) {
        doc["error"] = error;
    }

    String jsonResponse;
    serializeJson(doc, jsonResponse);
    LOGF("[BLE] Respuesta JSON: %s", jsonResponse.c_str());

    // Usar std::string para asegurar que NimBLE copie los datos correctamente
    // (setValue con const char* puede guardar solo el puntero)
    std::string responseStr(jsonResponse.c_str());
    pResponseChar->setValue(responseStr);

    // Pequeño delay para asegurar que el valor se escriba antes de notificar
    delay(100);

    pResponseChar->notify();

    // Esperar a que la notificación se procese
    delay(500);

    LOG("[BLE] Respuesta enviada");
}

// Función para inicializar el servidor BLE
void setupBLE() {
    LOG("[BLE] Inicializando servidor BLE...");

    NimBLEDevice::init(BLE_DEVICE_NAME);
    NimBLEDevice::setPower(ESP_PWR_LVL_P9);

    pBLEServer = NimBLEDevice::createServer();
    pBLEServer->setCallbacks(new FrameBLEServerCallbacks());

    // Crear servicio con el UUID que espera la app
    NimBLEService* pService = pBLEServer->createService(SERVICE_UUID);

    // Característica para recibir credenciales WiFi (Write)
    pWifiCredentialsChar = pService->createCharacteristic(
        WIFI_CREDENTIALS_CHAR_UUID,
        NIMBLE_PROPERTY::WRITE
    );
    pWifiCredentialsChar->setCallbacks(new WifiCredentialsCallback());

    // Característica para enviar respuesta (Read + Notify)
    pResponseChar = pService->createCharacteristic(
        RESPONSE_CHAR_UUID,
        NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY
    );
    // Inicializar con valor vacío para evitar basura
    pResponseChar->setValue("");

    pService->start();

    // Configurar advertising
    NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(SERVICE_UUID);
    pAdvertising->setScanResponse(true);
    pAdvertising->start();

    LOGF("[BLE] Servidor BLE iniciado - Device name: %s", BLE_DEVICE_NAME);
    LOG("[BLE] Esperando conexión de la app...");
}

// Función para procesar credenciales BLE y conectar a WiFi
bool processBLECredentials() {
    if (!bleCredentialsReceived) {
        return false;
    }

    LOG("[BLE] Procesando credenciales recibidas");

    // Guardar credenciales en Preferences
    preferences.putString("ssid", bleReceivedSSID);
    preferences.putString("password", bleReceivedPassword);

    // Responder INMEDIATAMENTE con el frameToken (MAC)
    // La app usará este token para hacer polling al backend
    String frameToken = WiFi.macAddress();
    LOGF("[BLE] Enviando frameToken inmediatamente: %s", frameToken.c_str());
    sendBLEResponse(true, frameToken);

    // Dar tiempo para que se envíe la respuesta BLE
    delay(BLE_RESPONSE_DELAY);

    // Detener BLE ANTES de intentar WiFi (comparten radio)
    LOG("[BLE] Deteniendo servidor BLE...");
    NimBLEDevice::deinit(true);
    pBLEServer = nullptr;
    pWifiCredentialsChar = nullptr;
    pResponseChar = nullptr;

    // Ahora intentar conectar a WiFi (sin BLE activo)
    showLoadingMsg("Connecting WiFi...");
    WiFi.mode(WIFI_STA);
    WiFi.begin(bleReceivedSSID.c_str(), bleReceivedPassword.c_str());

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < BLE_WIFI_CONNECT_TIMEOUT) {
        delay(500);
        attempts++;
        esp_task_wdt_reset();
        LOGF_NL("[BLE] Conectando a WiFi... intento %d/%d\r", attempts, BLE_WIFI_CONNECT_TIMEOUT);
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
        LOGF("[BLE] WiFi conectado exitosamente. IP: %s", WiFi.localIP().toString().c_str());
        return true;
    } else {
        // Fallo - NO borrar credenciales, reiniciar para volver a modo BLE + reintentos WiFi
        LOG("[BLE] Error: no se pudo conectar a WiFi");
        // Las credenciales se mantienen guardadas para reintentos automáticos

        showLoadingMsg("WiFi Error");
        delay(2000);
        showLoadingMsg("Restarting...");
        delay(1000);

        // Reiniciar para volver al modo BLE con reintentos WiFi
        ESP.restart();
        return false;
    }
}

// Versión no-bloqueante para usar en el loop de espera BLE + WiFi retry
bool processBLECredentialsNonBlocking() {
    if (!bleCredentialsReceived) {
        return false;
    }

    LOG("[BLE] Procesando credenciales recibidas (non-blocking)");

    // Guardar credenciales en Preferences
    preferences.putString("ssid", bleReceivedSSID);
    preferences.putString("password", bleReceivedPassword);

    // Responder INMEDIATAMENTE con el frameToken (MAC)
    String frameToken = WiFi.macAddress();
    LOGF("[BLE] Enviando frameToken: %s", frameToken.c_str());
    sendBLEResponse(true, frameToken);

    // Dar tiempo para que se envíe la respuesta BLE
    delay(BLE_RESPONSE_DELAY);

    // Detener BLE ANTES de intentar WiFi (comparten radio)
    LOG("[BLE] Deteniendo servidor BLE...");
    NimBLEDevice::stopAdvertising();

    // Intentar conectar a WiFi
    showLoadingMsg("Connecting WiFi...");
    WiFi.begin(bleReceivedSSID.c_str(), bleReceivedPassword.c_str());

    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
        esp_task_wdt_reset();
        delay(100);
    }

    if (WiFi.status() == WL_CONNECTED) {
        LOGF("[BLE] WiFi conectado! IP: %s", WiFi.localIP().toString().c_str());
        NimBLEDevice::deinit(true); // Apagar BLE completamente
        return true;
    }

    // Fallo - NO borrar credenciales, reanudar BLE
    LOG("[BLE] WiFi fallo. Reanudando BLE...");
    bleCredentialsReceived = false;
    NimBLEDevice::startAdvertising();
    showLoadingMsg("WiFi Error");
    delay(1000);
    showLoadingMsg("Waiting BLE...");
    return false;
}

// ===== FIN FUNCIONES BLE =====

void setup()
{
    Serial.begin(115200);

    // Mostrar información del modo de desarrollo
    if (DEV) {
        LOG("==========================================");
        LOG("           MODO DESARROLLO ACTIVO        ");
        LOG("==========================================");
        LOGF("- Servidor: %s", serverUrl);
        LOG("- Actualizaciones OTA: DESHABILITADAS");
        LOG("- Seguridad TLS: DESHABILITADA");
        LOG("==========================================");
    } else {
        LOG("==========================================");
        LOG("           MODO PRODUCCIÓN ACTIVO        ");
        LOG("==========================================");
        LOGF("- Servidor: %s", serverUrl);
        LOG("- Actualizaciones OTA: HABILITADAS");
        LOG("- Seguridad TLS: HABILITADA");
        LOG("==========================================");
    }

    // Configuración del panel
    HUB75_I2S_CFG mxconfig(PANEL_RES_X, PANEL_RES_Y, PANEL_CHAIN);
    mxconfig.gpio.e = E_PIN;
    mxconfig.clkphase = false;
    mxconfig.i2sspeed = HUB75_I2S_CFG::HZ_20M;   // 20MHz para mayor refresh rate
    mxconfig.min_refresh_rate = 200;               // Min 200Hz para evitar flicker en cámaras
    dma_display = new MatrixPanel_I2S_DMA(mxconfig);
    dma_display->begin();
    dma_display->setBrightness8(1);  // Arrancar con brillo mínimo para evitar brownout
    dma_display->clearScreen();
    dma_display->setRotation(135);

    // Inicializar Preferences para leer/guardar las credenciales
    preferences.begin("wifi", false);
    String storedSSID = preferences.getString("ssid", "");
    String storedPassword = preferences.getString("password", "");
    allowSpotify = preferences.getBool("allowSpotify", true);
    maxPhotos = preferences.getInt("maxPhotos", 5);
    currentVersion = preferences.getInt("currentVersion", 0);
    frameId = preferences.getInt("frameId", 0);
    if (frameId == 0) {
        // Backward compatibility: read old NVS key
        frameId = preferences.getInt("pixieId", 0);
        if (frameId > 0) {
            preferences.putInt("frameId", frameId);
            preferences.remove("pixieId");
        }
    }
    secsPhotos = preferences.getUInt("secsPhotos", 30000);
    // Cargar configuración de horario
    scheduleEnabled = preferences.getBool("scheduleEnabled", false);
    scheduleOnHour = preferences.getInt("scheduleOnHour", 8);
    scheduleOnMinute = preferences.getInt("scheduleOnMinute", 0);
    scheduleOffHour = preferences.getInt("scheduleOffHour", 22);
    scheduleOffMinute = preferences.getInt("scheduleOffMinute", 0);
    timezoneOffset = preferences.getInt("timezoneOffset", 0);
    clockEnabled = preferences.getBool("clockEnabled", false);

    // Si no hay credenciales guardadas, iniciar modo BLE para provisioning
    if (storedSSID == "") {
        LOG("No WiFi credentials found. Starting BLE provisioning...");

        // Iniciar servidor BLE
        setupBLE();

        // Mostrar mensaje de espera en pantalla
        dma_display->clearScreen();
        dma_display->fillScreen(myWHITE);
        drawLogo();
        showLoadingMsg("Waiting BLE...");

        // Esperar credenciales via BLE
        while (!processBLECredentials()) {
            esp_task_wdt_reset();
            delay(100);
        }

        // Si llegamos aquí, el WiFi está conectado via BLE
        LOG("WiFi conectado via BLE provisioning");
        showLoadingMsg("Connected!");
    } else {
        // Hay credenciales guardadas, intentar conectar a WiFi
        LOG("Conectando a WiFi...");
        WiFi.mode(WIFI_STA);
        WiFi.begin(storedSSID.c_str(), storedPassword.c_str());

        unsigned long startAttemptTime = millis();
        dma_display->clearScreen();
        dma_display->fillScreen(myWHITE);
        drawLogo();

        while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 30000) {
            showLoadingMsg("Connecting WiFi");
            delay(100);
        }

        if (WiFi.status() != WL_CONNECTED) {
            LOG("Failed to connect to WiFi. Starting BLE provisioning + WiFi retry mode...");

            // NO borrar credenciales - pueden ser correctas pero WiFi puede estar caído
            // El usuario podrá enviar nuevas credenciales via BLE si lo desea

            // Iniciar servidor BLE
            setupBLE();
            showLoadingMsg("Waiting BLE...");

            // Variables para reintentar WiFi periódicamente
            unsigned long lastWiFiAttempt = 0;
            const unsigned long WIFI_RETRY_INTERVAL = 180000; // 3 minutos

            // Esperar credenciales via BLE o reconexión WiFi
            while (true) {
                esp_task_wdt_reset();

                // Procesar BLE (no bloqueante)
                if (processBLECredentialsNonBlocking()) {
                    break; // Nuevas credenciales recibidas y WiFi conectado
                }

                // Intentar WiFi con credenciales existentes cada 3 minutos
                if (storedSSID != "" && millis() - lastWiFiAttempt >= WIFI_RETRY_INTERVAL) {
                    lastWiFiAttempt = millis();
                    LOG("[WiFi] Intentando reconectar con credenciales guardadas...");

                    // Pausar BLE temporalmente
                    NimBLEDevice::stopAdvertising();

                    WiFi.begin(storedSSID.c_str(), storedPassword.c_str());
                    unsigned long start = millis();
                    while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
                        esp_task_wdt_reset();
                        delay(100);
                    }

                    if (WiFi.status() == WL_CONNECTED) {
                        LOG("[WiFi] Reconectado!");
                        NimBLEDevice::deinit(true); // Apagar BLE
                        break;
                    }

                    // Reanudar BLE
                    NimBLEDevice::startAdvertising();
                    LOG("[WiFi] Fallo. Continuando en modo BLE...");
                }

                delay(100);
            }

            LOG("WiFi conectado (via BLE o reconexión)");
            showLoadingMsg("Connected!");
        } else {
            LOG("WiFi connected OK");
            showLoadingMsg("Connected to WiFi");
        }
    }

    // Configuración de MQTT (sin TLS para puerto 1883)
    mqttClient.setServer(MQTT_BROKER_URL, MQTT_BROKER_PORT);
    mqttClient.setCallback(mqttCallback);
    mqttClient.setKeepAlive(60);  // Keep-alive de 60 segundos
    mqttClient.setBufferSize(16384); // Buffer grande para fotos (12KB) y covers (8KB) via MQTT

    // Configuración de OTA
    ArduinoOTA.setHostname(("Frame-" + String(frameId)).c_str());
    ArduinoOTA.onStart([]() {
        String type;
        if (ArduinoOTA.getCommand() == U_FLASH)
            type = "sketch";
        else
            type = "filesystem";
        LOGF("Inicio de actualización OTA: %s", type.c_str());
        dma_display->clearScreen();
        showPercetage(0);
    });
    ArduinoOTA.onEnd([]() {
        LOG("Actualización OTA completada.");
    });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        LOGF("Progreso OTA: %u%%", (progress / (total / 100)));
        showPercetage((progress / (total / 100)));
    });
    ArduinoOTA.onError([](ota_error_t error) {
        const char* errorStr;
        if (error == OTA_AUTH_ERROR)
            errorStr = "AUTH_ERROR: Fallo de autenticación";
        else if (error == OTA_BEGIN_ERROR)
            errorStr = "BEGIN_ERROR: Error al iniciar actualización";
        else if (error == OTA_CONNECT_ERROR)
            errorStr = "CONNECT_ERROR: Error de conexión con cliente OTA";
        else if (error == OTA_RECEIVE_ERROR)
            errorStr = "RECEIVE_ERROR: Error al recibir datos de firmware";
        else if (error == OTA_END_ERROR)
            errorStr = "END_ERROR: Error al finalizar escritura de firmware";
        else
            errorStr = "UNKNOWN_ERROR";
        LOGF("[ArduinoOTA:onError] Código de error: %u - %s", error, errorStr);
    });
    ArduinoOTA.begin();

    timeClient.begin();
    timeClient.setTimeOffset(0); // UTC para AWS S3 URLs firmadas
    timeClient.update();

    // Check for updates
    // Comprobar actualizaciones solo si no estamos en modo DEV
    if (!DEV) {
        checkForUpdates();
    } else {
        LOG("Modo DEV activo - saltando comprobación inicial de actualizaciones");
    }

    // Register frame if not registered (via MQTT)
    if (frameId == 0) {
        showLoadingMsg("Registering...");
        if (!registerFrameViaMQTT()) {
            LOG("Error registrando frame via MQTT, reintentando...");
            delay(2000);
            registerFrameViaMQTT();  // Un reintento
        }
    }

    // Conectar a MQTT
    showLoadingMsg("Connecting server");
    mqttReconnect();

    // Solicitar configuración via MQTT (después de conectar)
    requestConfig();

    // Rampa gradual de brillo para evitar brownout por pico de corriente
    {
        int targetBrightness = max(brightness, 10);
        LOG("[Startup] Rampa de brillo...");
        for (int b = 1; b <= targetBrightness; b++) {
            dma_display->setBrightness8(b);
            delay(5);
        }
        startupBrightnessRampDone = true;
        LOGF("[Startup] Brillo objetivo alcanzado: %d", targetBrightness);
    }

    // Inicializar Watchdog Timer
    esp_task_wdt_init(WDT_TIMEOUT, true);
    esp_task_wdt_add(NULL);
    LOG("Watchdog timer inicializado");

    showLoadingMsg("Ready!");
    delay(500);

    // Comprobar si hay música sonando antes de mostrar la primera foto
    if (allowSpotify) {
        LOG("[Startup] Comprobando si hay música sonando...");
        String initialSong = fetchSongId();
        if (initialSong != "" && initialSong != "null") {
            LOG("[Startup] Música detectada - mostrando portada");
            songShowing = initialSong;
            fetchAndDrawCover();
        } else {
            LOG("[Startup] No hay música - mostrando primera foto");
            showPhotoIndex(0);
            photoIndex = 1;
            lastPhotoChange = millis();
        }
    } else {
        LOG("[Startup] Spotify deshabilitado - mostrando primera foto");
        showPhotoIndex(0);
        photoIndex = 1;
        lastPhotoChange = millis();
    }
}

String songOnline = "";

// Función para verificar si la hora actual está dentro del horario de encendido
bool isWithinSchedule() {
    if (!scheduleEnabled) {
        return true;  // Si el horario no está habilitado, siempre encendido
    }

    // Obtener hora UTC del NTPClient
    timeClient.update();
    int utcHours = timeClient.getHours();
    int utcMinutes = timeClient.getMinutes();
    int utcTotalMinutes = utcHours * 60 + utcMinutes;

    // Aplicar timezone offset para obtener hora local
    int localTotalMinutes = (utcTotalMinutes + timezoneOffset + 24 * 60) % (24 * 60);
    int localHour = localTotalMinutes / 60;
    int localMinute = localTotalMinutes % 60;

    // Convertir horarios de encendido y apagado a minutos totales
    int onTimeMinutes = scheduleOnHour * 60 + scheduleOnMinute;
    int offTimeMinutes = scheduleOffHour * 60 + scheduleOffMinute;
    int currentTimeMinutes = localHour * 60 + localMinute;

    // Caso normal: encendido antes que apagado (ej: 8:00 a 22:00)
    if (onTimeMinutes < offTimeMinutes) {
        return currentTimeMinutes >= onTimeMinutes && currentTimeMinutes < offTimeMinutes;
    }
    // Caso nocturno: encendido después que apagado (ej: 22:00 a 8:00)
    else if (onTimeMinutes > offTimeMinutes) {
        return currentTimeMinutes >= onTimeMinutes || currentTimeMinutes < offTimeMinutes;
    }
    // Caso igual: siempre encendido
    return true;
}

// Función para actualizar el estado de la pantalla según el horario
void updateScreenPower() {
    bool shouldBeOn = isWithinSchedule();

    if (shouldBeOn && screenOff) {
        // Encender pantalla
        LOG("[Schedule] Encendiendo pantalla");
        dma_display->setBrightness(max(brightness, 10));
        screenOff = false;
        // Forzar mostrar foto inmediatamente
        lastPhotoChange = 0;
    } else if (!shouldBeOn && !screenOff) {
        // Apagar pantalla
        LOG("[Schedule] Apagando pantalla");
        dma_display->clearScreen();
        dma_display->setBrightness(0);
        screenOff = true;
    }
}

void loop()
{
    esp_task_wdt_reset();

    // Manejo de MQTT
    if (!mqttClient.connected()) {
        mqttReconnect();
    }else{
        mqttClient.loop();
    }

    // Verificar y actualizar estado de la pantalla según el horario
    updateScreenPower();

    // Si la pantalla está apagada, solo procesar OTA y MQTT
    if (screenOff) {
        ArduinoOTA.handle();
        delay(100);
        return;
    }

    // Verificar timeout del modo dibujo
    checkDrawingTimeout();

    // Si estamos en modo dibujo, no ejecutar la lógica de fotos
    if (drawingMode) {
        // Procesar buffer de comandos si ha pasado suficiente tiempo
        if (millis() - lastDrawingUpdate >= DRAWING_UPDATE_INTERVAL) {
            processDrawingBuffer();
        }

        ArduinoOTA.handle();
        delay(10);
        return;
    }

    // Si estamos conectados a WiFi, se ejecuta la lógica original:
    if (allowSpotify) {
        // Solo llamar a fetchSongId si el scroll no está activo (evita bloquear el scroll)
        bool scrollActive = titleNeedsScroll && (titleScrollState == SCROLL_SCROLLING || titleScrollState == SCROLL_RETURNING);
        if (millis() - lastSpotifyCheck >= timeToCheckSpotify && !scrollActive) {
            songOnline = fetchSongId();
            lastSpotifyCheck = millis();
        }
        if (songOnline == "" || songOnline == "null") {
            // Si antes había canción y ahora no, mostrar foto inmediatamente
            if (songShowing != "") {
                songShowing = "";
                lastPhotoChange = 0;  // Forzar cambio de foto inmediato
            }
            if (millis() - lastPhotoChange >= secsPhotos) {
                if (photoIndex >= maxPhotos) {
                    photoIndex = 0;
                }
                LOGF("[Photo] Mostrando foto %d/%d", photoIndex, maxPhotos);
                showPhotoIndex(photoIndex);
                photoIndex++;
                lastPhotoChange = millis();
            }
        } else {
            if (songShowing != songOnline) {
                songShowing = songOnline;
                fetchAndDrawCover();
            }
        }
    } else {
        if (millis() - lastPhotoChange >= secsPhotos) {
            if (photoIndex >= maxPhotos) {
                photoIndex = 0;
            }
            LOGF("[Photo] Mostrando foto %d/%d", photoIndex, maxPhotos);
            showPhotoIndex(photoIndex);
            photoIndex++;
            lastPhotoChange = millis();
        }
    }
    
    // Actualizar el scroll del título si es necesario
    updatePhotoInfo();

    // Actualizar reloj cada 60 segundos
    if (clockEnabled && millis() - lastClockUpdate >= 60000) {
        showClockOverlay();
        lastClockUpdate = millis();
    }

    wait(100); // Reducido de 1000ms a 100ms para mejor respuesta
}
