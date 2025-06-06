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
#include <Preferences.h> // Para guardar las credenciales en memoria
#include <WebServer.h>   // Para el servidor web en modo AP
#include <PubSubClient.h>

// Configuración MQTT
const char *MQTT_BROKER_URL = "c9df3292c56a47f08840ec694f893966.s1.eu.hivemq.cloud";
const int MQTT_BROKER_PORT = 8883;
const char *MQTT_BROKER_USERNAME = "server";
const char *MQTT_BROKER_PASSWORD = "Test1234!";
const char *MQTT_CLIENT_ID = "pixie-"; // Se completará con el ID del pixie

WiFiClientSecure espClient;
PubSubClient mqttClient(espClient);

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

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org");

String songShowing = "";
unsigned long lastPhotoChange = -60000;
unsigned long secsPhotos = 30000; // 30 segundos por defecto
unsigned long lastSpotifyCheck = 0;
unsigned long timeToCheckSpotify = 5000; // 5 segundos

// Declaraciones globales para las credenciales y el servidor
Preferences preferences;
bool apMode = false; // Se activará si no hay credenciales guardadas
bool allowSpotify = true;

WiFiClient *getWiFiClient()
{
    if (DEV)
    {
        return new WiFiClient();
    }
    else
    {
        WiFiClientSecure *clientSecure = new WiFiClientSecure();
        clientSecure->setInsecure();
        return clientSecure;
    }
}

void wait(int ms)
{
    unsigned long startTime = millis();
    while (millis() - startTime < ms)
    {
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
    String currentTime = String(timeClient.getHours() < 10 ? "0" : "") + String(timeClient.getHours()) + ":" +
                         String(timeClient.getMinutes() < 10 ? "0" : "") + String(timeClient.getMinutes());
    dma_display->setTextSize(1);
    dma_display->setTextColor(myWHITE);
    dma_display->setFont(&FreeSans12pt7b);
    dma_display->setCursor(3, 38); // Centrar en el panel
    dma_display->print(currentTime);
    Serial.print("Hora: ");
    Serial.println(currentTime);
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

// Función para descargar y mostrar la portada (ya existente)
void fetchAndDrawCover()
{
    lastPhotoChange = millis();
    WiFiClient *client = getWiFiClient();
    HTTPClient http;

    Serial.println("Conectando al servidor...");
    http.begin(*client, String(serverUrl) + "public/spotify/" + "cover-64x64?pixie_id=" + String(pixieId));
    int httpCode = http.GET();

    if (httpCode == 200)
    {
        DynamicJsonDocument doc(150000); // Ajusta esto si tienes PSRAM
        delay(250);                      // Puede ayudar
        DeserializationError error = deserializeJson(doc, http.getStream());
        if (error)
        {
            Serial.print("Error al parsear JSON: ");
            Serial.println(error.c_str());
            showTime();
            http.end();
            delay(50);
            delete client;
            return;
        }
        JsonArray data = doc["data"].as<JsonArray>();
        int width = doc["width"];
        int height = doc["height"];

        // Animación "push up" antes de mostrar la nueva portada
        for (int y = 0; y < height; y++)
        {
            pushUpAnimation(y, data);
        }
    }
    else
    {
        Serial.printf("Error en la solicitud: %d\n", httpCode);
        showTime();
    }
    http.end();
    delay(50);
    delete client;
}

// Función para obtener el ID de la canción en reproducción (ya existente)
String fetchSongId()
{
    WiFiClient *client = getWiFiClient();
    HTTPClient http;
    http.begin(*client, String(serverUrl) + "public/spotify/" + "id-playing?pixie_id=" + String(pixieId));
    int httpCode = http.GET();

    if (httpCode == 200)
    {
        DynamicJsonDocument doc(2048); // Ajusta esto si tienes PSRAM
        delay(250);                    // Puede ayudar
        DeserializationError error = deserializeJson(doc, http.getStream());
        if (!error)
        {
            String songId = doc["id"].as<String>();
            http.end();
            delay(50);
            delete client;
            if (songId == "" || songId == "null")
            {
                Serial.println("No hay canción en reproducción.");
            }
            else
            {
                Serial.printf("ID de la canción en reproducción: %s\n", songId.c_str());
            }
            return songId;
        }
        else
        {
            Serial.print("Error al parsear JSON: ");
            Serial.println(error.c_str());
        }
    }
    else
    {
        Serial.printf("Error en la solicitud: %d\n", httpCode);
    }
    http.end();
    delay(50);
    delete client;
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

void getPixie()
{
    WiFiClient *client = getWiFiClient();
    HTTPClient http;

    Serial.println("Fetching Pixie details...");

    http.begin(*client, String(serverUrl) + "public/pixie/?id=" + String(pixieId));
    int httpCode = http.GET();

    if (httpCode == 200)
    {
        DynamicJsonDocument doc(2048); // Aumentado de 200 a 2048
        delay(250);                    // Puede ayudar
        DeserializationError error = deserializeJson(doc, http.getStream());
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
        }
        else
        {
            Serial.print("Error parsing JSON: ");
            Serial.println(error.c_str());
        }
    }
    else
    {
        Serial.printf("Failed to fetch Pixie details. HTTP code: %d\n", httpCode);
    }
    http.end();
    delay(50);
    delete client;
}

void showPhotoInfo(String title, String name)
{
    // Mostrar título en la parte inferior izquierda
    dma_display->setFont(&Picopixel);
    dma_display->setTextSize(1);
    dma_display->setTextColor(myWHITE);
    int16_t x1, y1;
    uint16_t w, h;
    if (title.length() > 0)
    {
        dma_display->getTextBounds(title, 0, PANEL_RES_Y - 2, &x1, &y1, &w, &h);
        dma_display->fillRect(x1 - 1, y1 - 1, w + 3, h + 2, myBLACK);
        dma_display->setCursor(1, PANEL_RES_Y - 2);
        dma_display->print(title);
    }

    if (name.length() > 0)
    {
        // Mostrar nombre en la parte inferior derecha (o en otra posición según ancho)
        if (w + name.length() * 3 > PANEL_RES_X && title.length() > 0)
        {
            dma_display->getTextBounds(name, 0, PANEL_RES_Y - 10, &x1, &y1, &w, &h);
            dma_display->fillRect(x1 - 1, y1 - 1, w + 3, h + 2, myBLACK);
            dma_display->setCursor(1, PANEL_RES_Y - 10);
            dma_display->print(name);
        }
        else
        {
            dma_display->getTextBounds(name, 0, PANEL_RES_Y - 2, &x1, &y1, &w, &h);
            dma_display->fillRect(PANEL_RES_X - w - 1, y1 - 1, w + 2, h + 2, myBLACK);
            dma_display->setCursor(PANEL_RES_X - w, PANEL_RES_Y - 2);
            dma_display->print(name);
        }
    }
}

void showPhoto(String url)
{
    WiFiClient *client = getWiFiClient();
    HTTPClient http;

    Serial.println("Conectando a: " + url);
    http.begin(*client, url);

    int httpCode = http.GET();
    if (httpCode != 200)
    {
        Serial.printf("Error HTTP: %d\n", httpCode);
        http.end();
        delay(50);
        delete client;
        return;
    }

    WiFiClient *stream = http.getStreamPtr();

    // Leer cabecera
    uint16_t titleLen = (stream->read() << 8) | stream->read();
    uint16_t usernameLen = (stream->read() << 8) | stream->read();

    char title[titleLen + 1];
    char username[usernameLen + 1];

    for (int i = 0; i < titleLen; i++)
        title[i] = stream->read();
    for (int i = 0; i < usernameLen; i++)
        username[i] = stream->read();
    title[titleLen] = '\0';
    username[usernameLen] = '\0';

    fadeOut();
    dma_display->clearScreen();

    const int bytesPerLine = 64 * 3;
    uint8_t lineBuffer[bytesPerLine];

    for (int y = 0; y < 64; y++)
    {
        size_t received = stream->readBytes(lineBuffer, bytesPerLine);

        if (received != bytesPerLine)
        {
            Serial.printf("Error: esperados %d bytes pero recibidos %d\n", bytesPerLine, received);
            break;
        }

        for (int x = 0; x < 64; x++)
        {
            uint8_t g = lineBuffer[x * 3];
            uint8_t b = lineBuffer[x * 3 + 1];
            uint8_t r = lineBuffer[x * 3 + 2];

            uint16_t color = dma_display->color565(r, g, b);
            screenBuffer[y][x] = color;
        }
    }

    fadeIn();
    showPhotoInfo(String(title), String(username));

    http.end();
    delay(50);
    delete client;
}

void showPhotoFromCenter(const String &url)
{
    WiFiClient *client = getWiFiClient();
    HTTPClient http;

    Serial.println("Conectando a: " + url);
    http.begin(*client, url);

    int httpCode = http.GET();
    if (httpCode != 200)
    {
        Serial.printf("Error HTTP: %d\n", httpCode);
        http.end();
        delay(50);
        delete client;
        return;
    }

    WiFiClient *stream = http.getStreamPtr();

    // Leer cabecera: título y username
    uint16_t titleLen = (stream->read() << 8) | stream->read();
    uint16_t usernameLen = (stream->read() << 8) | stream->read();

    char title[titleLen + 1];
    char username[usernameLen + 1];

    for (int i = 0; i < titleLen; i++)
        title[i] = stream->read();
    for (int i = 0; i < usernameLen; i++)
        username[i] = stream->read();
    title[titleLen] = '\0';
    username[usernameLen] = '\0';

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

    // Reservamos el buffer de la imagen entera en RAM
    const int width = 64;
    const int height = 64;
    const int totalPixels = width * height;
    const int totalBytes = totalPixels * 3;

    uint8_t *imageBuffer = (uint8_t *)malloc(totalBytes);
    if (!imageBuffer)
    {
        Serial.println("Error: no hay suficiente memoria para cargar la imagen.");
        http.end();
        delay(50);
        delete client;
        return;
    }

    // Leer toda la imagen RGB888 (64x64x3)
    size_t read = stream->readBytes(imageBuffer, totalBytes);
    if (read != totalBytes)
    {
        Serial.printf("Error: se esperaban %d bytes, pero se recibieron %d\n", totalBytes, read);
        free(imageBuffer);
        http.end();
        delay(50);
        delete client;
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
                        uint8_t r = imageBuffer[index + 2]; // ⚠️ BGR → RGB
                        uint8_t g = imageBuffer[index + 0];
                        uint8_t b = imageBuffer[index + 1];
                        uint16_t color = dma_display->color565(r, g, b);
                        drawPixelWithBuffer(x, y, color);
                    }
                }
            }
        }
        delay(5); // Controla la velocidad de la animación
    }
    showPhotoInfo(String(title), String(username));

    free(imageBuffer);
    http.end();
    delay(50);
    delete client;
    songShowing = "";
}

void showPhotoIndex(int photoIndex)
{
    String url = String(serverUrl) + "public/photo/get-photo-by-pixie?index=" + String(photoIndex) + "&pixieId=" + String(pixieId);
    Serial.println("Llamando a: " + url);
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
    Serial.println("Se limpia pantalla");
    dma_display->setFont();
    dma_display->setCursor(8, 50); // Centrar en el panel
    dma_display->print("Updating");
    Serial.println("Se muestra mensaje de actualizacion");
}

void showCheckMessage()
{
    showLoadingMsg("Checking updates");
}

void checkForUpdates()
{
    WiFiClient *client = getWiFiClient();
    HTTPClient http;

    Serial.println("Conectando a: " + String(serverUrl) + "public/version/latest");
    http.begin(*client, String(serverUrl) + "public/version/latest");
    int httpCode = http.GET();

    if (httpCode == 200)
    {
        StaticJsonDocument<1024> doc;
        delay(250); // Puede ayudar
        DeserializationError error = deserializeJson(doc, http.getStream());
        if (!error)
        {
            int latestVersion = doc["version"];
            String updateUrl = doc["url"].as<String>();
            Serial.println("Latest version: " + String(latestVersion));
            Serial.println("Update URL: " + updateUrl);

            if (latestVersion > currentVersion)
            {
                Serial.printf("New version available: %d\n", latestVersion);
                showUpdateMessage();
                Serial.println("Downloading update...");
                http.begin(*client, updateUrl);
                int updateHttpCode = http.GET();

                if (updateHttpCode == 200)
                {
                    WiFiClient *updateClient = http.getStreamPtr();
                    size_t contentLength = http.getSize();
                    Update.onProgress([](size_t written, size_t total)
                                      {
            int percent = (written * 100) / total;
            showPercetage(percent);
            Serial.printf("Progreso: %d%% (%d/%d bytes)\n", percent, written, total); });

                    if (Update.begin(contentLength))
                    {
                        size_t written = Update.writeStream(*updateClient);
                        if (written == contentLength)
                        {
                            Serial.println("Update successfully written.");
                            if (Update.end())
                            {
                                Serial.println("Update successfully completed.");
                                // TODO: Guardar la nueva version
                                // preferences.putInt("currentVersion", latestVersion);
                                preferences.putInt("currentVersion", 0);
                                ESP.restart();
                            }
                            else
                            {
                                Serial.printf("Update failed. Error: %s\n", Update.errorString());
                            }
                        }
                        else
                        {
                            Serial.printf("Update failed. Written only: %d/%d\n", written, contentLength);
                        }
                    }
                    else
                    {
                        Serial.println("Not enough space for update.");
                    }
                }
                else
                {
                    Serial.printf("Failed to download update. HTTP code: %d\n", updateHttpCode);
                }
            }
            else
            {
                Serial.println("No new updates available.");
            }
        }
        else
        {
            Serial.print("Error parsing JSON: ");
            Serial.println(error.c_str());
        }
    }
    else
    {
        Serial.printf("Failed to check for updates. HTTP code: %d\n", httpCode);
    }
    http.end();
    delay(50);
}

void registerPixie()
{
    WiFiClient *client = getWiFiClient();
    HTTPClient http;

    String macAddress = WiFi.macAddress();
    Serial.println("Registering Pixie with MAC: " + macAddress);

    http.begin(*client, String(serverUrl) + "public/pixie/add");
    http.addHeader("Content-Type", "application/json");

    DynamicJsonDocument doc(2048); // Aumentado de 200 a 2048
    doc["mac"] = macAddress;
    String requestBody;
    serializeJson(doc, requestBody);

    int httpCode = http.POST(requestBody);

    if (httpCode == 200)
    {
        DynamicJsonDocument responseDoc(2048); // Aumentado de 200 a 2048
        delay(250);                            // Puede ayudar
        DeserializationError error = deserializeJson(responseDoc, http.getStream());
        if (!error)
        {
            pixieId = responseDoc["pixie"]["id"];
            preferences.putInt("pixieId", pixieId);
            Serial.printf("Pixie registered with ID: %d\n", pixieId);
        }
        else
        {
            Serial.print("Error parsing JSON: ");
            Serial.println(error.c_str());
        }
    }
    else
    {
        Serial.printf("Failed to register Pixie. HTTP code: %d\n", httpCode);
    }
    http.end();
    delay(50);
    delete client;
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
    Serial.println("Preferencias reiniciadas a valores de fábrica");
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
    dma_display->print("Connect to:");
    dma_display->setCursor(1, 20);
    dma_display->print("SSID: Pixie");
    dma_display->setCursor(1, 30);
    dma_display->print("Pass: 12345678");

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
    Serial.println("Mensaje recibido en el topic: " + String(topic));
    Serial.println("Mensaje: " + String(message));

    // Crear un documento JSON
    DynamicJsonDocument doc(1024);
    DeserializationError error = deserializeJson(doc, message);

    if (!error)
    {
        const char *action = doc["action"];
        if (action)
        {
            if (strcmp(action, "update_photo") == 0)
            {
                Serial.println("Se recibio una nueva foto por MQTT");
                int id = doc["id"];
                onReceiveNewPic(id);
            }
            else if (strcmp(action, "update_info") == 0)
            {
                getPixie();
            }
            else if (strcmp(action, "update_bin") == 0)
            {
                Serial.println("Se recibio una actualizacion de binario por MQTT");
                checkForUpdates();
            }
        }
    }
}

// Función para reconectar al broker MQTT
void mqttReconnect()
{
    while (!mqttClient.connected())
    {
        Serial.print("Intentando conexión MQTT...");
        String clientId = String(MQTT_CLIENT_ID) + String(pixieId);

        if (mqttClient.connect(clientId.c_str(), MQTT_BROKER_USERNAME, MQTT_BROKER_PASSWORD))
        {
            Serial.println("conectado");
            // Suscribirse al tema específico del pixie
            String topic = String("pixie/") + String(pixieId);
            Serial.println("Suscribiendo al tema: " + topic);
            mqttClient.subscribe(topic.c_str());
        }
        else
        {
            Serial.print("falló, rc=");
            Serial.print(mqttClient.state());
            Serial.println(" reintentando en 5 segundos");
            delay(5000);
        }
    }
}

// Función para dibujar el logo con efecto arcoiris
void drawLogo()
{
    static uint8_t hue = 0;   // Variable estática para mantener el estado del color
    const int LOGO_SIZE = 32; // Tamaño del logo (32x32)
    const int offsetX = 16;
    const int offsetY = 10;

    for (int y = 0; y < LOGO_SIZE; y++)
    {
        for (int x = 0; x < LOGO_SIZE; x++)
        {
            int index = y * LOGO_SIZE + x;
            const uint32_t pixel = logo[0][index];

            // Extraer los componentes ARGB
            uint8_t alpha = (pixel >> 24) & 0xFF;
            uint8_t red = (pixel >> 16) & 0xFF;
            uint8_t green = (pixel >> 8) & 0xFF;
            uint8_t blue = pixel & 0xFF;

            if (alpha > 0)
            { // Si el píxel no es transparente
                if (red == 0xFF && green == 0xFF && blue == 0xFF)
                { // Si el píxel es blanco
                    // Convertir el color HSV a RGB
                    uint8_t r, g, b;
                    uint8_t h = hue + (x + y) * 2; // Variar el tono según la posición
                    uint8_t s = 255;               // Saturación máxima
                    uint8_t v = 255;               // Valor máximo

                    // Conversión HSV a RGB
                    uint8_t region = h / 43;
                    uint8_t remainder = (h - (region * 43)) * 6;
                    uint8_t p = (v * (255 - s)) >> 8;
                    uint8_t q = (v * (255 - ((s * remainder) >> 8))) >> 8;
                    uint8_t t = (v * (255 - ((s * (255 - remainder)) >> 8))) >> 8;

                    switch (region)
                    {
                    case 0:
                        r = v;
                        g = t;
                        b = p;
                        break;
                    case 1:
                        r = q;
                        g = v;
                        b = p;
                        break;
                    case 2:
                        r = p;
                        g = v;
                        b = t;
                        break;
                    case 3:
                        r = p;
                        g = q;
                        b = v;
                        break;
                    case 4:
                        r = t;
                        g = p;
                        b = v;
                        break;
                    default:
                        r = v;
                        g = p;
                        b = q;
                        break;
                    }

                    // Dibujar el píxel con el nuevo color
                    drawPixelWithBuffer(x + offsetX, y + offsetY, dma_display->color565(r, b, g));
                }
                else
                {
                    // Mantener el color original para píxeles no blancos
                    drawPixelWithBuffer(x + offsetX, y + offsetY, dma_display->color565(red, blue, green));
                }
            }
        }
    }

    // Incrementar el tono para la siguiente llamada
    hue += 2;
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

void setup()
{
    Serial.begin(115200);

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

    // Si no hay credenciales guardadas, esperar por Serial
    if (storedSSID == "") {
        Serial.println("Esperando credenciales WiFi...");
        dma_display->fillScreen(myWHITE);
        showLoadingMsg("Waiting WiFi pass");
        
        while (true) {
            drawLogo();
            if (Serial.available()) {
                String input = Serial.readStringUntil('\n');
                input.trim();
                
                if (processCredentials(input)) {
                    Serial.println("OK");
                    storedSSID = preferences.getString("ssid", "");
                    storedPassword = preferences.getString("password", "");
                    break;
                } else {
                    Serial.println("ERROR");
                }
            }
            delay(100);
        }
    }

    // Intentar conectar a WiFi
    Serial.println("Conectando a WiFi...");
    WiFi.mode(WIFI_STA);
    WiFi.begin(storedSSID.c_str(), storedPassword.c_str());

    unsigned long startAttemptTime = millis();
    dma_display->clearScreen();
    dma_display->fillScreen(myWHITE);

    while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 30000) {
        drawLogo();
        showLoadingMsg("Connecting WiFi");
        delay(10);
    }

    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("ERROR");
        showLoadingMsg("Waiting WiFi pass");
        while (true) {
            if (Serial.available()) {
                String input = Serial.readStringUntil('\n');
                input.trim();
                
                if (processCredentials(input)) {
                    delay(150);
                    Serial.println("OK");
                    delay(150);
                    ESP.restart();
                    break;
                } else {
                    Serial.println("ERROR");
                }
            }
            delay(100);
        }
        ESP.restart();
    } else {
        Serial.println("OK");
        showLoadingMsg("Connected to WiFi");

        // Configurar cliente WiFi seguro para MQTT
        espClient.setInsecure();

        // Configuración de MQTT
        mqttClient.setServer(MQTT_BROKER_URL, MQTT_BROKER_PORT);
        mqttClient.setCallback(mqttCallback);
    }

    // Configuración de OTA
    ArduinoOTA.setHostname(("Pixie-" + String(pixieId)).c_str());
    ArduinoOTA.onStart([]() {
        String type;
        if (ArduinoOTA.getCommand() == U_FLASH)
            type = "sketch";
        else
            type = "filesystem";
        Serial.println("Inicio de actualización OTA: " + type);
    });
    ArduinoOTA.onEnd([]() {
        Serial.println("\nActualización OTA completada.");
    });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        Serial.printf("Progreso: %u%%\n", (progress / (total / 100)));
        showPercetage((progress / (total / 100)));
    });
    ArduinoOTA.onError([](ota_error_t error) {
        Serial.printf("Error [%u]: ", error);
        if (error == OTA_AUTH_ERROR)
            Serial.println("Error de autenticación");
        else if (error == OTA_BEGIN_ERROR)
            Serial.println("Error al iniciar");
        else if (error == OTA_CONNECT_ERROR)
            Serial.println("Error de conexión");
        else if (error == OTA_RECEIVE_ERROR)
            Serial.println("Error al recibir");
        else if (error == OTA_END_ERROR)
            Serial.println("Error al finalizar");
    });
    ArduinoOTA.begin();

    timeClient.begin();
    timeClient.setTimeOffset(7200);
    timeClient.update();

    // Check for updates
    checkForUpdates();

    // Register Pixie if not registered
    if (pixieId == 0) {
        registerPixie();
    } else {
        getPixie();
    }
    showLoadingMsg("Ready!");
}

String songOnline = "";

void loop()
{
    // Manejo de MQTT
    if (!mqttClient.connected()) {
        mqttReconnect();
    }else{
        mqttClient.loop();
    }    

    // Si estamos conectados a WiFi, se ejecuta la lógica original:
    if (allowSpotify) {
        if (millis() - lastSpotifyCheck >= timeToCheckSpotify) {
            songOnline = fetchSongId();
            lastSpotifyCheck = millis();
        }
        if (songOnline == "" || songOnline == "null") {
            if (millis() - lastPhotoChange >= secsPhotos) {
                songShowing = "";
                showPhotoIndex(photoIndex++);
                lastPhotoChange = millis();
                if (photoIndex >= maxPhotos) {
                    photoIndex = 0;
                }
            }
        } else {
            lastPhotoChange = -secsPhotos;
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
    wait(1000);
}
