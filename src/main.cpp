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
#include <pics.h>        // Aquí se asume que en pics.h están definidas las imágenes (incluido "qr")
#include <logo.h>        // Logo personalizado 64x64 RGB
#include <Preferences.h> // Para guardar las credenciales en memoria
#include <WebServer.h>   // Para el servidor web en modo AP
#include <PubSubClient.h>
#include <esp_task_wdt.h>

// Macros de logging con timestamp
#define LOG(msg) Serial.printf("[%lu] %s\n", millis(), msg)
#define LOGF(fmt, ...) Serial.printf("[%lu] " fmt "\n", millis(), ##__VA_ARGS__)
#define LOGF_NL(fmt, ...) Serial.printf("[%lu] " fmt, millis(), ##__VA_ARGS__)

// Configuración MQTT
const char *MQTT_BROKER_URL = "mqtt.mypixelframe.com";
const int MQTT_BROKER_PORT = 1883;
const char *MQTT_BROKER_USERNAME = "server";
const char *MQTT_BROKER_PASSWORD = "Test1234!";
const char *MQTT_CLIENT_ID = "pixie-"; // Se completará con el ID del pixie

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
int wifiBrightness = 0;
int maxIndex = 5;
int maxPhotos = 5;
int currentVersion = 0;
int pixieId = 0;
int photoIndex = 0;

const bool DEV = false;
const char *serverUrl = DEV ? "http://192.168.18.53:3000/" : "https://api.mypixelframe.com/";

// Variable para controlar el reset automático al arrancar
const bool AUTO_RESET_ON_STARTUP = false; // Cambiar a false para deshabilitar el reset automático

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org");

String songShowing = "";
unsigned long lastPhotoChange = -60000;
unsigned long secsPhotos = 30000; // 30 segundos por defecto
unsigned long lastSpotifyCheck = 0;
unsigned long timeToCheckSpotify = 5000; // 5 segundos
String activationCode = "0000";

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
#define ACTIVATION_TIMEOUT 300000   // Timeout activación: 5 minutos
#define HTTP_TIMEOUT 10000          // Timeout para llamadas HTTP API (ms)
#define HTTP_TIMEOUT_DOWNLOAD 30000 // Timeout para descargas (ms)

// Dirty rectangle tracking
int dirtyMinX = PANEL_RES_X;
int dirtyMaxX = -1;
int dirtyMinY = PANEL_RES_Y;
int dirtyMaxY = -1;

// Declaraciones globales para las credenciales y el servidor
Preferences preferences;
bool apMode = false; // Se activará si no hay credenciales guardadas
bool allowSpotify = true;
WebServer webServer(80); // Servidor web para configuración WiFi

// getWiFiClient() eliminado - ahora usamos clientes estáticos reutilizables:
// - spotifyClient para Spotify
// - httpClient para fotos
// - httpClient para API general (getPixie, registerPixie, checkForUpdates)

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

// Función para mostrar el icono de WiFi parpadeando (ya existente)
void showWiFiIcon(int n)
{
    uint16_t draw[32][32];
    if (n == 0)
    {
        memcpy(draw, wifiArray, sizeof(draw));
    }
    else if (n == 1)
    {
        memcpy(draw, wifi1, sizeof(draw));
    }
    else if (n == 2)
    {
        memcpy(draw, wifi2, sizeof(draw));
    }
    else if (n == 3)
    {
        memcpy(draw, wifi3, sizeof(draw));
    }
    dma_display->clearScreen();
    for (int y = 0; y < 32; y++)
    {
        for (int x = 0; x < 32; x++)
        {
            drawPixelWithBuffer(x + 16, y + 16, draw[y][x] > 0 ? myWHITE : myBLACK);
        }
    }
}

// Función para mostrar el porcentaje durante la actualización OTA (ya existente)
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

// Cliente WiFi reutilizable para Spotify (evita fragmentación)
static WiFiClientSecure *spotifyClient = nullptr;
static HTTPClient spotifyHttp;

// Cliente WiFi reutilizable para HTTP general (fotos, API, etc.)
static WiFiClientSecure *httpClient = nullptr;
static HTTPClient http;

void ensureSpotifyClient() {
    // Siempre crear cliente nuevo para evitar memory leaks de mbedTLS
    // Los buffers SSL (~40KB) deben liberarse entre conexiones
    if (spotifyClient != nullptr) {
        spotifyClient->stop();
        delete spotifyClient;
    }
    spotifyClient = new WiFiClientSecure();
    spotifyClient->setInsecure();
}

void ensureHttpClient() {
    // Solo crear cliente si no existe - evitar new/delete constantes
    // Ver: https://github.com/espressif/arduino-esp32/issues/6561
    if (httpClient == nullptr) {
        httpClient = new WiFiClientSecure();
        httpClient->setInsecure();
    } else {
        httpClient->stop();  // Cerrar conexión anterior si existe
    }
}

// Función para descargar y mostrar la portada
void fetchAndDrawCover()
{
    lastPhotoChange = millis();
    ensureSpotifyClient();

    LOG("[Spotify] Fetching cover...");

    esp_task_wdt_reset();  // Reset watchdog antes de operación larga

    // Construir URL en buffer estático
    snprintf(httpBuffer, sizeof(httpBuffer), "%spublic/spotify/cover-64x64-binary?pixie_id=%d", serverUrl, pixieId);

    LOG("[Spotify] HTTP begin");
    spotifyHttp.begin(*spotifyClient, httpBuffer);
    spotifyHttp.setTimeout(HTTP_TIMEOUT_DOWNLOAD);

    LOG("[Spotify] HTTP GET start");
    int httpCode = spotifyHttp.GET();
    LOGF("[Spotify] HTTP GET done, code: %d", httpCode);

    esp_task_wdt_reset();  // Reset watchdog después de GET

    if (httpCode == 200)
    {
        WiFiClient *stream = spotifyHttp.getStreamPtr();

        const int totalBytes = 64 * 64 * 2;
        LOGF("[Spotify] Reading %d bytes", totalBytes);
        size_t totalReceived = stream->readBytes(spotifyCoverBuffer, totalBytes);
        LOGF("[Spotify] Read complete, got %d bytes", totalReceived);

        esp_task_wdt_reset();  // Reset watchdog después de descarga

        if (totalReceived != totalBytes)
        {
            LOGF("[Spotify] Error: esperados %d bytes pero recibidos %d", totalBytes, totalReceived);
            spotifyHttp.end();
            // Liberar cliente SSL para recuperar ~40KB de memoria
            if (spotifyClient != nullptr) {
                spotifyClient->stop();
                delete spotifyClient;
                spotifyClient = nullptr;
            }
            return;
        }

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
    }
    else
    {
        LOGF("[Spotify] Error HTTP: %d", httpCode);
        showTime();
    }
    spotifyHttp.end();

    // Liberar cliente SSL para recuperar ~40KB de memoria
    if (spotifyClient != nullptr) {
        spotifyClient->stop();
        delete spotifyClient;
        spotifyClient = nullptr;
    }
    LOG("[Spotify] Done");
}

// Función para obtener el ID de la canción en reproducción
String fetchSongId()
{
    ensureSpotifyClient();

    // Construir URL en buffer estático
    snprintf(httpBuffer, sizeof(httpBuffer), "%spublic/spotify/id-playing?pixie_id=%d", serverUrl, pixieId);

    spotifyHttp.begin(*spotifyClient, httpBuffer);
    spotifyHttp.setTimeout(HTTP_TIMEOUT);
    int httpCode = spotifyHttp.GET();

    songIdBuffer[0] = '\0';  // Limpiar buffer

    if (httpCode == 200)
    {
        // Leer directamente en buffer estático
        WiFiClient *stream = spotifyHttp.getStreamPtr();
        int len = spotifyHttp.getSize();

        if (len > 0 && len < (int)sizeof(httpBuffer)) {
            int bytesRead = stream->readBytes(httpBuffer, min(len, (int)sizeof(httpBuffer) - 1));
            httpBuffer[bytesRead] = '\0';

            // Parseo manual simple para extraer el id
            char *idStart = strstr(httpBuffer, "\"id\":\"");
            if (idStart) {
                idStart += 6;
                char *idEnd = strchr(idStart, '"');
                if (idEnd && (idEnd - idStart) < 63) {
                    int idLen = idEnd - idStart;
                    strncpy(songIdBuffer, idStart, idLen);
                    songIdBuffer[idLen] = '\0';
                    // Solo log si es una canción diferente
                    if (songShowing != String(songIdBuffer)) {
                        LOGF("Nueva canción: %s", songIdBuffer);
                    }
                }
            }
        }

    }
    else if (httpCode > 0)
    {
        LOGF("[Spotify:fetchSongId] Error HTTP al obtener ID canción: %d", httpCode);
    }
    else
    {
        LOGF("[Spotify:fetchSongId] Error de conexión: %s", spotifyHttp.errorToString(httpCode).c_str());
    }

    spotifyHttp.end();

    // Liberar cliente SSL para recuperar ~40KB de memoria
    if (spotifyClient != nullptr) {
        spotifyClient->stop();
        delete spotifyClient;
        spotifyClient = nullptr;
    }
    return String(songIdBuffer);
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

void getPixie()
{
    ensureHttpClient();

    LOG("Fetching Pixie details...");

    http.begin(*httpClient, String(serverUrl) + "public/pixie/?id=" + String(pixieId));
    http.setTimeout(HTTP_TIMEOUT);
    int httpCode = http.GET();

    if (httpCode == 200)
    {
        JsonDocument doc;
        delay(250);                    // Puede ayudar
        DeserializationError error = deserializeJson(doc, http.getStream());
        LOGF("Code: %s", doc["pixie"]["code"].as<String>().c_str());
        if (!error)
        {
            brightness = doc["pixie"]["brightness"];
            dma_display->setBrightness(max(brightness, 10));
            preferences.putInt("brightness", brightness);
            maxPhotos = doc["pixie"]["pictures_on_queue"];
            allowSpotify = doc["pixie"]["spotify_enabled"];
            preferences.putInt("maxPhotos", maxPhotos);
            preferences.putBool("allowSpotify", allowSpotify);
            int secsBetweenPhotos = doc["pixie"]["secs_between_photos"];
            secsPhotos = secsBetweenPhotos * 1000; // Convertir a milisegundos
            preferences.putUInt("secsPhotos", secsPhotos);
            activationCode = doc["pixie"]["code"].as<String>();
            preferences.putString("code", activationCode);
        }
        else
        {
            LOGF("[Pixie:getPixie] Error parseando JSON: %s", error.c_str());
        }
    }
    else
    {
        LOGF("[Pixie:getPixie] Error HTTP al obtener detalles: %d", httpCode);
    }

    http.end();

    // Liberar cliente SSL para recuperar memoria
    if (httpClient != nullptr) {
        httpClient->stop();
        delete httpClient;
        httpClient = nullptr;
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

void showPhoto(String url)
{
    // Evitar condiciones de carrera
    if (isLoadingPhoto) {
        LOG("Ya hay una foto cargándose, ignorando petición");
        return;
    }
    isLoadingPhoto = true;

    LOGF("[Photo] Heap libre: %d bytes", ESP.getFreeHeap());

    ensureHttpClient();

    LOGF("Conectando a: %s", url.c_str());
    http.begin(*httpClient, url);
    http.setTimeout(HTTP_TIMEOUT_DOWNLOAD);

    int httpCode = http.GET();
    LOGF("[Photo] HTTP GET done, code: %d, heap: %d", httpCode, ESP.getFreeHeap());

    if (httpCode != 200)
    {
        LOGF("[Photo:showPhoto] Error HTTP al descargar foto: %d (URL: %s)", httpCode, url.c_str());
        http.end();
        if (httpCode < 0 && httpClient != nullptr) {
            LOG("[Photo] Error de conexión - cerrando cliente");
            httpClient->stop();
        }
        isLoadingPhoto = false;
        return;
    }

    WiFiClient *stream = http.getStreamPtr();

    // Leer cabecera con validación de errores
    int b1 = stream->read();
    int b2 = stream->read();
    int b3 = stream->read();
    int b4 = stream->read();

    if (b1 < 0 || b2 < 0 || b3 < 0 || b4 < 0) {
        LOG("[Photo] Error leyendo cabecera - bytes inválidos");
        http.end();
        isLoadingPhoto = false;
        return;
    }

    uint16_t titleLen = (b1 << 8) | b2;
    uint16_t usernameLen = (b3 << 8) | b4;

    // Limitar tamaño máximo para evitar stack overflow
    const uint16_t MAX_HEADER_LEN = 128;
    if (titleLen > MAX_HEADER_LEN || usernameLen > MAX_HEADER_LEN) {
        LOGF("[Photo] Cabecera corrupta: titleLen=%d, usernameLen=%d", titleLen, usernameLen);
        http.end();
        isLoadingPhoto = false;
        return;
    }

    char title[MAX_HEADER_LEN + 1];
    char username[MAX_HEADER_LEN + 1];

    for (int i = 0; i < titleLen; i++) {
        int c = stream->read();
        if (c < 0) {
            LOG("[Photo] Error leyendo título");
            http.end();
            isLoadingPhoto = false;
            return;
        }
        title[i] = (char)c;
    }
    for (int i = 0; i < usernameLen; i++) {
        int c = stream->read();
        if (c < 0) {
            LOG("[Photo] Error leyendo username");
            http.end();
            isLoadingPhoto = false;
            return;
        }
        username[i] = (char)c;
    }
    title[titleLen] = '\0';
    username[usernameLen] = '\0';

    // Usar buffer estático para evitar fragmentación de memoria
    const int totalBytes = 64 * 64 * 3;

    // Reset watchdog antes de lectura larga
    esp_task_wdt_reset();
    size_t received = stream->readBytes(photoBuffer, totalBytes);
    esp_task_wdt_reset();
    LOGF("[Photo] Read complete, got %d bytes, heap: %d", received, ESP.getFreeHeap());

    // Validar que se recibió la imagen completa
    if (received != totalBytes)
    {
        LOGF("[Photo:showPhoto] Error de descarga: esperados %d bytes pero recibidos %d - DESCARTANDO imagen", totalBytes, received);
        http.end();
        isLoadingPhoto = false;
        return;
    }

    // Solo hacer fadeOut DESPUÉS de confirmar que la imagen está completa
    fadeOut();
    dma_display->clearScreen();

    // Resetear el estado del scroll del título anterior
    titleNeedsScroll = false;

    // Copiar al screenBuffer desde el buffer estático validado
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
    showPhotoInfo(String(title), String(username));

    http.end();

    // Liberar cliente SSL para dar memoria a Spotify
    if (httpClient != nullptr) {
        httpClient->stop();
        delete httpClient;
        httpClient = nullptr;
    }
    isLoadingPhoto = false;
}

void showPhotoFromCenter(const String &url)
{
    // Evitar condiciones de carrera
    if (isLoadingPhoto) {
        LOG("Ya hay una foto cargándose, ignorando petición");
        return;
    }
    isLoadingPhoto = true;

    LOGF("[PhotoCenter] Heap libre: %d bytes", ESP.getFreeHeap());

    ensureHttpClient();

    LOGF("Conectando a: %s", url.c_str());
    http.begin(*httpClient, url);
    http.setTimeout(HTTP_TIMEOUT_DOWNLOAD);

    int httpCode = http.GET();
    if (httpCode != 200)
    {
        LOGF("[Photo:showPhotoFromCenter] Error HTTP al descargar foto: %d (URL: %s)", httpCode, url.c_str());
        http.end();
        if (httpCode < 0 && httpClient != nullptr) {
            LOG("[PhotoCenter] Error de conexión - cerrando cliente");
            httpClient->stop();
        }
        isLoadingPhoto = false;
        return;
    }

    WiFiClient *stream = http.getStreamPtr();

    // Leer cabecera con validación de errores
    int b1 = stream->read();
    int b2 = stream->read();
    int b3 = stream->read();
    int b4 = stream->read();

    if (b1 < 0 || b2 < 0 || b3 < 0 || b4 < 0) {
        LOG("[PhotoCenter] Error leyendo cabecera - bytes inválidos");
        http.end();
        isLoadingPhoto = false;
        return;
    }

    uint16_t titleLen = (b1 << 8) | b2;
    uint16_t usernameLen = (b3 << 8) | b4;

    // Limitar tamaño máximo para evitar stack overflow
    const uint16_t MAX_HEADER_LEN = 128;
    if (titleLen > MAX_HEADER_LEN || usernameLen > MAX_HEADER_LEN) {
        LOGF("[PhotoCenter] Cabecera corrupta: titleLen=%d, usernameLen=%d", titleLen, usernameLen);
        http.end();
        isLoadingPhoto = false;
        return;
    }

    char title[MAX_HEADER_LEN + 1];
    char username[MAX_HEADER_LEN + 1];

    for (int i = 0; i < titleLen; i++) {
        int c = stream->read();
        if (c < 0) {
            LOG("[PhotoCenter] Error leyendo título");
            http.end();
            isLoadingPhoto = false;
            return;
        }
        title[i] = (char)c;
    }
    for (int i = 0; i < usernameLen; i++) {
        int c = stream->read();
        if (c < 0) {
            LOG("[PhotoCenter] Error leyendo username");
            http.end();
            isLoadingPhoto = false;
            return;
        }
        username[i] = (char)c;
    }
    title[titleLen] = '\0';
    username[usernameLen] = '\0';

    // Resetear el estado del scroll del título anterior
    titleNeedsScroll = false;

    // 🟥 ANIMACIÓN DE CUADRADOS DE COLORES
    int centerX = 32;
    int centerY = 32;
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
            delay(5); // Ajusta este valor para controlar la velocidad de la animación
        }
    }

    // 🖼️ IMAGEN

    // Usar buffer estático para evitar fragmentación de memoria
    const int width = 64;
    const int height = 64;
    const int totalPixels = width * height;
    const int totalBytes = totalPixels * 3;

    // Reset watchdog antes de lectura larga
    esp_task_wdt_reset();
    // Leer toda la imagen RGB888 (64x64x3) al buffer estático
    size_t read = stream->readBytes(photoBuffer, totalBytes);
    esp_task_wdt_reset();
    LOGF("[PhotoCenter] Read complete, got %d bytes, heap: %d", read, ESP.getFreeHeap());

    if (read != totalBytes)
    {
        LOGF("[Photo:showPhotoFromCenter] Error de descarga: esperados %d bytes pero recibidos %d", totalBytes, read);
        http.end();
        isLoadingPhoto = false;
        return;
    }

    // Mostrar imagen desde el centro
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
                        uint8_t r = photoBuffer[index + 2]; // ⚠️ BGR → RGB
                        uint8_t g = photoBuffer[index + 0];
                        uint8_t b = photoBuffer[index + 1];
                        uint16_t color = dma_display->color565(r, g, b);
                        drawPixelWithBuffer(x, y, color);
                    }
                }
            }
        }
        delay(5); // Controla la velocidad de la animación
    }
    showPhotoInfo(String(title), String(username));

    http.end();

    // Liberar cliente SSL para dar memoria a Spotify
    if (httpClient != nullptr) {
        httpClient->stop();
        delete httpClient;
        httpClient = nullptr;
    }
    songShowing = "";
    isLoadingPhoto = false;
}

void showPhotoIndex(int photoIndex)
{
    String url = String(serverUrl) + "public/photo/get-photo-by-pixie?index=" + String(photoIndex) + "&pixieId=" + String(pixieId);
    LOGF("Llamando a: %s", url.c_str());
    showPhoto(url);
}

void showPhotoId(int photoId)
{
    String url = String(serverUrl) + "public/photo/get-photo?id=" + String(photoId);
    showPhoto(url);
}

// Función para la animación de nueva foto
void onReceiveNewPic(int id)
{
    photoIndex = 1;

    String url = String(serverUrl) + "public/photo/get-photo?id=" + String(id);
    // Mostrar la foto con animación desde el centro
    showPhotoFromCenter(url);
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

    ensureHttpClient();

    LOGF("Conectando a: %spublic/version/latest", serverUrl);
    http.begin(*httpClient, String(serverUrl) + "public/version/latest");
    http.setTimeout(HTTP_TIMEOUT);
    int httpCode = http.GET();

    if (httpCode == 200)
    {
        JsonDocument doc;
        delay(250); // Puede ayudar
        DeserializationError error = deserializeJson(doc, http.getStream());
        if (!error)
        {
            int latestVersion = doc["version"];
            String updateUrl = doc["url"].as<String>();
            LOGF("Latest version: %d", latestVersion);
            LOGF("Update URL: %s", updateUrl.c_str());

            if (latestVersion > currentVersion)
            {
                LOGF("New version available: %d", latestVersion);
                showUpdateMessage();
                LOG("Downloading update...");
                http.end();
                
                // Detectar si la URL es HTTP o HTTPS y crear el cliente apropiado
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
                updateHttp.setTimeout(30000); // 30 segundos timeout
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
                            LOG("[OTA:checkForUpdates] Firmware escrito correctamente");
                            if (Update.end())
                            {
                                LOG("[OTA:checkForUpdates] Actualización completada exitosamente");
                                // TODO: Guardar la nueva version
                                preferences.putInt("currentVersion", latestVersion);
                                ESP.restart();
                            }
                            else
                            {
                                LOGF("[OTA:checkForUpdates] Error al finalizar Update: %s", Update.errorString());
                            }
                        }
                        else
                        {
                            LOGF("[OTA:checkForUpdates] Error de escritura: solo %d/%d bytes escritos", written, contentLength);
                        }
                    }
                    else
                    {
                        LOG("[OTA:checkForUpdates] Error: espacio insuficiente para actualización");
                    }
                }
                else
                {
                    LOGF("[OTA:checkForUpdates] Error HTTP al descargar firmware: %d", updateHttpCode);
                    if (updateHttpCode == 404) {
                        LOG("[OTA:checkForUpdates] Archivo no encontrado o URL expirada");
                    }
                    // Imprimir la respuesta para debug
                    String response = updateHttp.getString();
                    LOGF("[OTA:checkForUpdates] Respuesta del servidor: %s", response.c_str());
                }
                updateHttp.end();
                delete updateClient;
            }
            else
            {
                LOG("No new updates available.");
            }
        }
        else
        {
            LOGF("[OTA:checkForUpdates] Error parseando JSON de versión: %s", error.c_str());
        }
    }
    else
    {
        LOGF("[OTA:checkForUpdates] Error HTTP al verificar actualizaciones: %d", httpCode);
    }

    http.end();

    // Liberar cliente SSL para recuperar memoria
    if (httpClient != nullptr) {
        httpClient->stop();
        delete httpClient;
        httpClient = nullptr;
    }
}

void registerPixie()
{
    ensureHttpClient();

    String macAddress = WiFi.macAddress();
    LOGF("Registering Pixie with MAC: %s", macAddress.c_str());

    http.begin(*httpClient, String(serverUrl) + "public/pixie/add");
    http.setTimeout(HTTP_TIMEOUT);
    http.addHeader("Content-Type", "application/json");

    JsonDocument doc;
    doc["mac"] = macAddress;
    String requestBody;
    serializeJson(doc, requestBody);

    int httpCode = http.POST(requestBody);

    if (httpCode == 200)
    {
        JsonDocument responseDoc;
        delay(250);                            // Puede ayudar
        DeserializationError error = deserializeJson(responseDoc, http.getStream());
        if (!error)
        {
            pixieId = responseDoc["pixie"]["id"];
            String activationCode = responseDoc["code"].as<String>();
            preferences.putInt("pixieId", pixieId);
            preferences.putString("code", activationCode);
            LOGF("Pixie registered with ID: %d and code: %s", pixieId, activationCode.c_str());
        }
        else
        {
            LOGF("[Pixie:registerPixie] Error parseando JSON de registro: %s", error.c_str());
        }
    }
    else
    {
        LOGF("[Pixie:registerPixie] Error HTTP al registrar dispositivo: %d", httpCode);
    }

    http.end();

    // Liberar cliente SSL para recuperar memoria
    if (httpClient != nullptr) {
        httpClient->stop();
        delete httpClient;
        httpClient = nullptr;
    }
}

// Función para reiniciar todas las preferencias a valores de fábrica
void testInit()
{
    preferences.begin("wifi", false);
    preferences.clear(); // Borra todas las preferencias
    preferences.end();
    preferences.begin("wifi", false);
    preferences.putInt("currentVersion", 0);
    preferences.putInt("pixieId", 0);
    preferences.putInt("brightness", 50);
    preferences.putInt("maxPhotos", 5);
    preferences.putUInt("secsPhotos", 30000);
    preferences.putString("ssid", "");
    preferences.putString("password", "");
    preferences.putBool("allowSpotify", false);
    preferences.putString("code", "0000"); // Inicializar el código de activación vacío
    LOG("Preferencias reiniciadas a valores de fábrica");
}

// Función para mostrar el SSID y contraseña en el panel
void showAPCredentials(const char *ssid, const char *password)
{
    dma_display->clearScreen();
    dma_display->setTextSize(1);
    dma_display->setTextColor(myWHITE);

    // Mostrar SSID
    dma_display->setFont(&Picopixel);
    dma_display->setCursor(1, 10);
    dma_display->print("Connect to WiFi:");
    dma_display->setCursor(1, 20);
    dma_display->print(String(ssid));
    dma_display->setCursor(1, 30);
    dma_display->print("Pass: " + String(password));

    // Mostrar contraseña
    dma_display->setCursor(1, 45);
    dma_display->print("Go to:");
    dma_display->setCursor(1, 55);
    dma_display->print("192.168.4.1");
}

// Función para manejar los mensajes MQTT recibidos
void mqttCallback(char *topic, byte *payload, unsigned int length)
{
    // Crear un buffer para el mensaje
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
                getPixie();
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
                preferences.putInt("pixieId", 0);
                preferences.putInt("brightness", 50);
                preferences.putInt("maxPhotos", 5);
                preferences.putUInt("secsPhotos", 30000);
                preferences.putString("ssid", "");
                preferences.putString("password", "");
                preferences.putBool("allowSpotify", false);
                preferences.putString("code", "0000");
                preferences.end();
                
                // Resetear variables globales
                brightness = 50;
                maxPhotos = 5;
                secsPhotos = 30000;
                allowSpotify = false;
                activationCode = "0000";
                currentVersion = 0;
                pixieId = 0;
                
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

        String clientId = String(MQTT_CLIENT_ID) + String(pixieId);

        if (mqttClient.connect(clientId.c_str(), MQTT_BROKER_USERNAME, MQTT_BROKER_PASSWORD))
        {
            LOG("[MQTT:mqttReconnect] Conectado exitosamente");
            String topic = String("pixie/") + String(pixieId);
            LOGF("[MQTT:mqttReconnect] Suscribiendo al tema: %s", topic.c_str());
            if (mqttClient.subscribe(topic.c_str())) {
                LOG("[MQTT:mqttReconnect] Suscripción exitosa");
                loadingMsg = "";
            } else {
                LOG("[MQTT:mqttReconnect] Error: fallo en suscripción al tema");
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

// Función para generar la página HTML de configuración WiFi
String generateConfigHTML() {
    String html = R"(<!DOCTYPE html>
<html>
<head>
<title>Pixie WiFi Setup</title>
<meta name="viewport" content="width=device-width,initial-scale=1">
<style>
body{font-family:Arial;margin:20px;background:#f5f5f5}
.container{max-width:400px;margin:0 auto;background:white;padding:20px;border-radius:10px}
h1{text-align:center;color:#333}
.form-group{margin-bottom:15px}
label{display:block;margin-bottom:5px;font-weight:bold}
input,select{width:100%;padding:10px;border:1px solid #ddd;border-radius:5px;box-sizing:border-box}
button{background:#4CAF50;color:white;padding:12px 20px;border:none;border-radius:5px;cursor:pointer;width:100%;margin-top:10px}
button:hover{background:#45a049}
.scan-btn{background:#2196F3;margin-bottom:10px}
.network-item{padding:8px;background:#f9f9f9;border:1px solid #ddd;margin:5px 0;cursor:pointer}
.network-item:hover{background:#e3f2fd}
</style>
</head>
<body>
<div class="container">
<h1>Pixie WiFi Setup</h1>
<form method="post" action="/connect">
<div class="form-group">
<a href="/scan"><button type="button" class="scan-btn">Scan Networks</button></a>
</div>
<div class="form-group">
<label for="ssid">WiFi Network:</label>
<input type="text" name="ssid" required>
</div>
<div class="form-group">
<label for="password">Password:</label>
<input type="password" name="password" required>
</div>
<button type="submit">Connect to WiFi</button>
</form>
<div class="form-group">
<a href="/reset"><button type="button" class="scan-btn">Reset Device</button></a>
<a href="/reset_env"><button type="button" class="scan-btn">Reset Env Vars</button></a>
<a href="/status"><button type="button" class="scan-btn">Connection Status</button></a>
</div>
</div>
</body>
</html>)";
    return html;
}

// Función para manejar el escaneo de redes WiFi
void handleWifiScan() {
    LOG("Scanning WiFi networks...");
    int networkCount = WiFi.scanNetworks();
    
    String html = R"(<!DOCTYPE html>
<html>
<head>
<title>WiFi Networks - Pixie</title>
<meta name="viewport" content="width=device-width,initial-scale=1">
<style>
body{font-family:Arial;margin:20px;background:#f5f5f5}
.container{max-width:400px;margin:0 auto;background:white;padding:20px;border-radius:10px}
h1{text-align:center;color:#333}
.network-item{padding:10px;background:#f9f9f9;border:1px solid #ddd;margin:5px 0;cursor:pointer;border-radius:5px}
.network-item:hover{background:#e3f2fd}
.back-btn{background:#666;color:white;padding:10px;text-decoration:none;border-radius:5px;display:inline-block;margin-bottom:20px}
</style>
</head>
<body>
<div class="container">
<a href="/" class="back-btn">Back</a>
<h1>Available Networks</h1>)";
    
    for (int i = 0; i < networkCount; i++) {
        html += "<div class=\"network-item\" onclick=\"selectNetwork('" + WiFi.SSID(i) + "')\">";
        html += "<strong>" + WiFi.SSID(i) + "</strong><br>";
        html += "Signal: " + String(WiFi.RSSI(i)) + " dBm | ";
        html += String(WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "Open" : "Encrypted");
        html += "</div>";
    }
    
    html += R"(
<script>
function selectNetwork(ssid) {
    localStorage.setItem('selectedSSID', ssid);
    window.location.href = '/';
}
window.onload = function() {
    var ssid = localStorage.getItem('selectedSSID');
    if (ssid) {
        var input = parent.document.querySelector('input[name="ssid"]');
        if (input) input.value = ssid;
    }
};
</script>
</div>
</body>
</html>)";
    
    webServer.send(200, "text/html", html);
    WiFi.scanDelete();
}

// Función para manejar la conexión WiFi
void handleWifiConnect() {
    String ssid = webServer.arg("ssid");
    String password = webServer.arg("password");
    
    String html = R"(<!DOCTYPE html>
<html>
<head>
<title>Connecting - Pixie</title>
<meta name="viewport" content="width=device-width,initial-scale=1">
<style>
body{font-family:Arial;margin:20px;background:#f5f5f5;text-align:center}
.container{max-width:400px;margin:0 auto;background:white;padding:20px;border-radius:10px}
h1{color:#333}
.status{padding:20px;margin:20px 0;border-radius:5px;font-weight:bold}
.success{background:#d4edda;color:#155724;border:1px solid #c3e6cb}
.error{background:#f8d7da;color:#721c24;border:1px solid #f5c6cb}
.loading{background:#d1ecf1;color:#0c5460;border:1px solid #bee5eb}
</style>
</head>
<body>
<div class="container">)";
    
    if (ssid.length() > 0) {
        html += "<h1>Connecting to WiFi...</h1>";
        html += "<div class=\"status loading\">Connecting to: " + ssid + "</div>";
        
        // Guardar credenciales
        preferences.putString("ssid", ssid);
        preferences.putString("password", password);
        
        // Enviar respuesta inmediatamente
        webServer.send(200, "text/html", html + R"(
<script>
setTimeout(function() {
    window.location.href = '/status';
}, 3000);
</script>
</div>
</body>
</html>)");
        
        // Intentar conectar en segundo plano
        WiFi.begin(ssid.c_str(), password.c_str());
        
        // Dar tiempo para que se envíe la respuesta
        delay(1000);
        
        // Esperar hasta 10 segundos para la conexión
        int attempts = 0;
        while (WiFi.status() != WL_CONNECTED && attempts < 20) {
            delay(500);
            attempts++;
        }
        
        if (WiFi.status() == WL_CONNECTED) {
            apMode = false;
            ESP.restart();
        }
    } else {
        html += "<h1>Error</h1>";
        html += "<div class=\"status error\">SSID is required</div>";
        html += "<a href=\"/\" style=\"color:#007bff;text-decoration:none\">Go Back</a>";
        webServer.send(400, "text/html", html + "</div></body></html>");
    }
}

// Función para manejar el reset del dispositivo
void handleDeviceReset() {
    String html = R"(<!DOCTYPE html>
<html>
<head>
<title>Reset - Pixie</title>
<meta name="viewport" content="width=device-width,initial-scale=1">
<style>
body{font-family:Arial;margin:20px;background:#f5f5f5;text-align:center}
.container{max-width:400px;margin:0 auto;background:white;padding:20px;border-radius:10px}
h1{color:#333}
.status{padding:20px;margin:20px 0;border-radius:5px;font-weight:bold;background:#d4edda;color:#155724}
</style>
</head>
<body>
<div class="container">
<h1>Device Reset</h1>
<div class="status">Device reset successfully! Restarting...</div>
<p>Please reconnect to the Pixie AP to configure WiFi again.</p>
</div>
</body>
</html>)";
    
    webServer.send(200, "text/html", html);
    delay(2000);
    preferences.clear();
    ESP.restart();
}

// Función para manejar el reset de variables de entorno
void handleEnvVarsReset() {
    String html = R"(<!DOCTYPE html>
<html>
<head>
<title>Reset Env Vars - Pixie</title>
<meta name="viewport" content="width=device-width,initial-scale=1">
<style>
body{font-family:Arial;margin:20px;background:#f5f5f5;text-align:center}
.container{max-width:400px;margin:0 auto;background:white;padding:20px;border-radius:10px}
h1{color:#333}
.status{padding:20px;margin:20px 0;border-radius:5px;font-weight:bold;background:#d4edda;color:#155724}
</style>
</head>
<body>
<div class="container">
<h1>Environment Variables Reset</h1>
<div class="status">Environment variables reset successfully!</div>
<p>Device will continue running with default values.</p>
<a href="/" style="color:#007bff;text-decoration:none;padding:10px;background:#f8f9fa;border-radius:5px;display:inline-block;margin-top:20px">Back to Main</a>
</div>
</body>
</html>)";
    
    webServer.send(200, "text/html", html);
    
    // Resetear variables de entorno
    preferences.begin("wifi", false);
    preferences.clear();
    preferences.end();
    
    // Reinicializar con valores por defecto
    preferences.begin("wifi", false);
    preferences.putInt("currentVersion", 0);
    preferences.putInt("pixieId", 0);
    preferences.putInt("brightness", 50);
    preferences.putInt("maxPhotos", 5);
    preferences.putUInt("secsPhotos", 30000);
    preferences.putString("ssid", "");
    preferences.putString("password", "");
    preferences.putBool("allowSpotify", false);
    preferences.putString("code", "0000");
    preferences.end();
    
    // Resetear variables globales
    brightness = 50;
    maxPhotos = 5;
    secsPhotos = 30000;
    allowSpotify = false;
    activationCode = "0000";
    currentVersion = 0;
    pixieId = 0;
}

// Función para manejar el status de conexión
void handleConnectionStatus() {
    String html = R"(<!DOCTYPE html>
<html>
<head>
<title>Status - Pixie</title>
<meta name="viewport" content="width=device-width,initial-scale=1">
<meta http-equiv="refresh" content="3">
<style>
body{font-family:Arial;margin:20px;background:#f5f5f5;text-align:center}
.container{max-width:400px;margin:0 auto;background:white;padding:20px;border-radius:10px}
h1{color:#333}
.status{padding:20px;margin:20px 0;border-radius:5px;font-weight:bold}
.success{background:#d4edda;color:#155724}
.error{background:#f8d7da;color:#721c24}
.loading{background:#d1ecf1;color:#0c5460}
</style>
</head>
<body>
<div class="container">)";
    
    if (WiFi.status() == WL_CONNECTED) {
        html += "<h1>Connected!</h1>";
        html += "<div class=\"status success\">Successfully connected to WiFi</div>";
        html += "<p>IP Address: " + WiFi.localIP().toString() + "</p>";
        html += "<p>Device will restart in normal mode...</p>";
        webServer.send(200, "text/html", html + "</div></body></html>");
        delay(3000);
        apMode = false;
        ESP.restart();
    } else {
        html += "<h1>Connection Failed</h1>";
        html += "<div class=\"status error\">Failed to connect to WiFi</div>";
        html += "<a href=\"/\" style=\"color:#007bff;text-decoration:none;padding:10px;background:#f8f9fa;border-radius:5px;display:inline-block;margin-top:20px\">Try Again</a>";
        webServer.send(200, "text/html", html + "</div></body></html>");
    }
}

// Función para configurar el servidor web en modo AP
void setupWebServer() {
    webServer.on("/", HTTP_GET, []() {
        webServer.send(200, "text/html", generateConfigHTML());
    });
    
    webServer.on("/scan", HTTP_GET, handleWifiScan);
    webServer.on("/connect", HTTP_POST, handleWifiConnect);
    webServer.on("/reset", HTTP_GET, handleDeviceReset);
    webServer.on("/reset_env", HTTP_GET, handleEnvVarsReset);
    webServer.on("/status", HTTP_GET, handleConnectionStatus);
    
    webServer.begin();
    LOG("Web server started on http://192.168.4.1");
}

// Función para iniciar el modo AP
void startAPMode() {
    apMode = true;
    WiFi.mode(WIFI_AP);
    WiFi.softAP("frame.", "12345678");
    
    IPAddress IP = WiFi.softAPIP();
    LOGF("AP IP address: %s", IP.toString().c_str());
    
    setupWebServer();
    showAPCredentials("frame.", "12345678");
}

// Función para procesar las credenciales recibidas por Serial
bool processCredentials(String input) {
    // Formato esperado: #SSID;ssid#PASS;contraseña
    if (!input.startsWith("#SSID;")) {
        return false;
    }

    // Extraer SSID
    int ssidStart = input.indexOf("#SSID;") + 6;
    int ssidEnd = input.indexOf("#PASS;");
    String ssid = input.substring(ssidStart, ssidEnd);

    // Extraer contraseña
    int passStart = ssidEnd + 6;
    String password = input.substring(passStart);

    // Guardar las credenciales
    preferences.putString("ssid", ssid);
    preferences.putString("password", password);

    return true;
}

void showActivationCode(String code) {
    dma_display->clearScreen();
    dma_display->setTextSize(1);
    dma_display->setTextColor(myWHITE);
    dma_display->setFont(&Picopixel);
    
    // Mostrar "Activation code:" centrado
    int16_t x1, y1;
    uint16_t w, h;
    const char* label = "Activation code:";
    dma_display->getTextBounds(label, 0, 0, &x1, &y1, &w, &h);
    int16_t labelX = (PANEL_RES_X - w) / 2;
    dma_display->setCursor(labelX, 20);
    dma_display->print(label);
    
    // Mostrar el código con fuente más pequeña
    dma_display->setFont();  // Usar fuente por defecto (más pequeña)
    dma_display->setTextSize(2);  // Tamaño 2 para que sea visible pero no muy grande
    
    // Obtener el ancho del código con tamaño 2
    dma_display->getTextBounds(code, 0, 0, &x1, &y1, &w, &h);
    
    // Si el código es muy ancho, reducir el tamaño
    if (w > PANEL_RES_X - 4) {  // Dejar 2 píxeles de margen a cada lado
        dma_display->setTextSize(1);
        dma_display->getTextBounds(code, 0, 0, &x1, &y1, &w, &h);
    }
    
    // Calcular posición X centrada
    int16_t codeX = (PANEL_RES_X - w) / 2;
    
    // Asegurar que nunca sea negativo (por si acaso)
    if (codeX < 0) codeX = 0;
    
    // Posición Y para el código
    int16_t codeY = 40;
    
    dma_display->setCursor(codeX, codeY);
    dma_display->print(code);
    
    // Restaurar configuración de fuente
    dma_display->setFont(&Picopixel);
    dma_display->setTextSize(1);
}

void checkActivation() {
    String currentCode = preferences.getString("code", "0000");
    LOGF("Código de activación: %s", currentCode.c_str());

    if (currentCode != "0000")
    {
        showActivationCode(currentCode);

        unsigned long startTime = millis();
        int attempts = 0;

        while (currentCode != "0000" && millis() - startTime < ACTIVATION_TIMEOUT)
        {
            esp_task_wdt_reset();  // Reset watchdog durante espera larga
            getPixie();
            currentCode = preferences.getString("code", "0000");
            attempts++;

            if (attempts % 10 == 0) {
                LOGF("Esperando activación... (%lu segundos)",
                              (millis() - startTime) / 1000);
            }
            delay(2000);
        }

        dma_display->clearScreen();
        if (currentCode == "0000")
        {
            showLoadingMsg("Activated!");
            LOG("Dispositivo activado correctamente");
            delay(2000);
        }
        else
        {
            showLoadingMsg("Timeout");
            LOG("Timeout de activación - reiniciando ESP32...");
            delay(2000);
            ESP.restart();
        }
    }
}

// Función para resetear las variables de entorno al inicio
void resetEnvironmentVariables() {
    if (AUTO_RESET_ON_STARTUP) {
        LOG("==========================================");
        LOG("        RESETEO AUTOMÁTICO ACTIVADO      ");
        LOG("==========================================");
        LOG("Reseteando variables de entorno...");
        
        // Limpiar todas las preferencias guardadas
        preferences.begin("wifi", false);
        preferences.clear();
        preferences.end();
        
        // Reinicializar con valores por defecto
        preferences.begin("wifi", false);
        preferences.putInt("currentVersion", 0);
        preferences.putInt("pixieId", 0);
        preferences.putInt("brightness", 50);
        preferences.putInt("maxPhotos", 5);
        preferences.putUInt("secsPhotos", 30000);
        preferences.putString("ssid", "");
        preferences.putString("password", "");
        preferences.putBool("allowSpotify", false);
        preferences.putString("code", "0000");
        preferences.end();
        
        // Resetear variables globales
        brightness = 50;
        maxPhotos = 5;
        secsPhotos = 30000;
        allowSpotify = false;
        activationCode = "0000";
        currentVersion = 0;
        pixieId = 0;
        
        LOG("Variables de entorno reseteadas exitosamente!");
        LOG("==========================================");
    }
}

void setup()
{
    Serial.begin(115200);
    
    // Resetear variables de entorno al inicio si está habilitado
    resetEnvironmentVariables();
    
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
    dma_display = new MatrixPanel_I2S_DMA(mxconfig);
    dma_display->begin();
    dma_display->setBrightness8(max(brightness, 10));
    dma_display->clearScreen();
    dma_display->setRotation(135);

    // Inicializar Preferences para leer/guardar las credenciales
    preferences.begin("wifi", false);
    String storedSSID = preferences.getString("ssid", "");
    String storedPassword = preferences.getString("password", "");
    allowSpotify = preferences.getBool("allowSpotify", true);
    maxPhotos = preferences.getInt("maxPhotos", 5);
    currentVersion = preferences.getInt("currentVersion", 0);
    pixieId = preferences.getInt("pixieId", 0);
    secsPhotos = preferences.getUInt("secsPhotos", 30000);
    activationCode = preferences.getString("code", "0000");

    // Si no hay credenciales guardadas, iniciar modo AP
    if (storedSSID == "") {
        LOG("No WiFi credentials found. Starting AP mode...");
        startAPMode();
        return; // Sale del setup, el modo AP seguirá corriendo en el loop
    }

    // Intentar conectar a WiFi
    LOG("Conectando a WiFi...");
    WiFi.mode(WIFI_STA);
    WiFi.begin(storedSSID.c_str(), storedPassword.c_str());

    unsigned long startAttemptTime = millis();
    dma_display->clearScreen();
    dma_display->fillScreen(myWHITE);
    drawLogo();  // Dibujar logo solo una vez

    while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 30000) {
        showLoadingMsg("Connecting WiFi");
        delay(100);
    }

    if (WiFi.status() != WL_CONNECTED) {
        LOG("Failed to connect to WiFi. Starting AP mode...");
        startAPMode();
        return; // Sale del setup, el modo AP seguirá corriendo en el loop
    } else {
        LOG("WiFi connected OK");
        showLoadingMsg("Connected to WiFi");

        // Configuración de MQTT (sin TLS para puerto 1883)
        mqttClient.setServer(MQTT_BROKER_URL, MQTT_BROKER_PORT);
        mqttClient.setCallback(mqttCallback);
        mqttClient.setKeepAlive(60);  // Keep-alive de 60 segundos
        mqttClient.setBufferSize(512); // Aumentar buffer para mensajes más grandes
    }

    // Configuración de OTA
    ArduinoOTA.setHostname(("Pixie-" + String(pixieId)).c_str());
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

    // Register Pixie if not registered
    if (pixieId == 0) {
        registerPixie();
    } else {
        getPixie();
    }

    // Verificar estado de activación
    checkActivation();

    // Conectar a MQTT
    showLoadingMsg("Connecting server");
    mqttReconnect();

    // Inicializar Watchdog Timer
    esp_task_wdt_init(WDT_TIMEOUT, true);
    esp_task_wdt_add(NULL);
    LOG("Watchdog timer inicializado");

    showLoadingMsg("Ready!");
}

String songOnline = "";

void loop()
{
    esp_task_wdt_reset();

    // Si estamos en modo AP, manejar el servidor web
    if (apMode) {
        webServer.handleClient();
        
        // Refrescar la pantalla cada 5 segundos para mantener visible la información
        static unsigned long lastRefresh = 0;
        if (millis() - lastRefresh > 5000) {
            showAPCredentials("frame.", "12345678");
            lastRefresh = millis();
        }
        
        delay(10);
        return;
    }

    // Manejo de MQTT
    if (!mqttClient.connected()) {
        mqttReconnect();
    }else{
        mqttClient.loop();
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
                showPhotoIndex(photoIndex++);
                lastPhotoChange = millis();
                if (photoIndex >= maxPhotos) {
                    photoIndex = 0;
                }
            }
        } else {
            if (songShowing != songOnline) {
                songShowing = songOnline;
                fetchAndDrawCover();
            }
        }
    } else {
        if (millis() - lastPhotoChange >= secsPhotos) {
            showPhotoIndex(photoIndex++);
            lastPhotoChange = millis();
            if (photoIndex >= maxPhotos) {
                photoIndex = 0;
            }
        }
    }
    
    // Actualizar el scroll del título si es necesario
    updatePhotoInfo();
    
    wait(100); // Reducido de 1000ms a 100ms para mejor respuesta
}
