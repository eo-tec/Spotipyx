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
const char* MQTT_BROKER_URL = "c9df3292c56a47f08840ec694f893966.s1.eu.hivemq.cloud";
const int MQTT_BROKER_PORT = 8883;
const char* MQTT_BROKER_USERNAME = "server";
const char* MQTT_BROKER_PASSWORD = "Test1234!";
const char* MQTT_CLIENT_ID = "pixie-"; // Se completará con el ID del pixie

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

const bool DEV = true;
const char *serverUrl = DEV ? "http://192.168.18.53:3000/" : "https://my-album-production.up.railway.app/";

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org");

String songShowing = "";
unsigned long lastPhotoChange = -60000;
unsigned long secsPhotos = 30000; // 30 segundos por defecto

// Declaraciones globales para las credenciales y el servidor
Preferences preferences;
WebServer server(80);
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

String loadingMsg = "";
void showLoadingMsg(String msg) {
    dma_display->setTextSize(1);
    if (loadingMsg != msg) {
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
            dma_display->drawPixel(x + 16, y + 16, draw[y][x] > 0 ? myWHITE : myBLACK);
        }
    }
}

// Función para mostrar el porcentaje durante la actualización OTA (ya existente)
void showPercetage(int percentage)
{
    dma_display->clearScreen();
    dma_display->setTextSize(1);
    dma_display->setTextColor(myWHITE);
    dma_display->setFont(&FreeSans12pt7b);
    dma_display->setCursor(8, 38); // Centrar en el panel
    dma_display->print((percentage < 10 ? "0" : "") + String(min(percentage, 99)) + "%");
    dma_display->setFont();
    dma_display->setCursor(8, 50); // Centrar en el panel
    dma_display->print("Updating");
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

// Buffer para la pantalla (ya existente)
uint16_t screenBuffer[PANEL_RES_Y][PANEL_RES_X]; // Buffer to store the current screen content

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
            dma_display->drawPixel(j, i, color);
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
        String payload = http.getString();
        DynamicJsonDocument doc(200000); // Aumentado de 100000 a 150000
        DeserializationError error = deserializeJson(doc, payload);
        if (error)
        {
            Serial.print("Error al parsear JSON: ");
            Serial.println(error.c_str());
            showTime();
            http.end();
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
        String payload = http.getString();
        DynamicJsonDocument doc(2048); // Aumentado de 1024 a 2048
        DeserializationError error = deserializeJson(doc, payload);
        if (!error)
        {
            String songId = doc["id"].as<String>();
            http.end();
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
    delete client;
    return "";
}

// Funciones de fade in/out (ya existentes)
void fadeOut()
{
    for (int b = brightness; b >= 0; b--)
    {
        dma_display->setBrightness8(b);
        delay(30);
    }
}

void fadeIn()
{
    for (int b = 0; b <= brightness; b++)
    {
        dma_display->setBrightness8(b);
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
        String payload = http.getString();
        DynamicJsonDocument doc(2048); // Aumentado de 200 a 2048
        DeserializationError error = deserializeJson(doc, payload);
        if (!error)
        {
            brightness = doc["pixie"]["brightness"];
            dma_display->setBrightness(max(brightness, 10));
            preferences.putInt("brightness", brightness);
            maxPhotos = doc["pixie"]["pictures_on_queue"];
            preferences.putInt("maxPhotos", maxPhotos);
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

// Función para mostrar una foto con animación de expansión desde el centro
void showPhotoFromCenter(JsonArray &data, int width, int height, String title, String name)
{
    int centerX = width / 2;
    int centerY = height / 2;

    // Empezamos desde el centro y expandimos hacia afuera
    for (int radius = 0; radius <= max(width, height); radius++)
    {
        for (int y = centerY - radius; y <= centerY + radius; y++)
        {
            for (int x = centerX - radius; x <= centerX + radius; x++)
            {
                if (x >= 0 && x < width && y >= 0 && y < height)
                {
                    // Solo dibujamos los píxeles en el borde del cuadrado actual
                    if (abs(x - centerX) == radius || abs(y - centerY) == radius)
                    {
                        JsonArray row = data[y].as<JsonArray>();
                        uint16_t color = row[x];
                        dma_display->drawPixel(x, y, color);
                    }
                }
            }
        }
        delay(5); // Ajusta este valor para controlar la velocidad de la animación
    }
    showPhotoInfo(title, name);
}

// Función para mostrar una foto (ya existente)
void showPhoto(int photoIndex)
{
    getPixie();
    WiFiClient *client = getWiFiClient();
    HTTPClient http;
    songShowing = "photo";

    Serial.println("Conectando al servidor para obtener la foto...");
    http.begin(*client, String(serverUrl) + "public/photo/" + "get-photo/?id=" + String(photoIndex));
    Serial.println(String(serverUrl) + "public/photo/" + "get-photo/?id=" + String(photoIndex));
    int httpCode = http.GET();

    if (httpCode == 200)
    {
        Serial.println("Foto recibida. Mostrando en el panel...");
        String payload = http.getString();
        DynamicJsonDocument doc(200000); // Aumentado de 100000 a 150000
        DeserializationError error = deserializeJson(doc, payload);
        if (error)
        {
            Serial.print("Error al parsear JSON: ");
            Serial.println(error.c_str());
            showTime();
            http.end();
            delete client;
            fadeIn();
            return;
        }
        JsonObject photo = doc["photo"].as<JsonObject>();
        JsonArray data = photo["data"].as<JsonArray>();
        int width = photo["width"];
        int height = photo["height"];
        String title = doc["title"].as<String>();
        String name = doc["username"].as<String>();
        fadeOut();
        Serial.println("Title: " + title);
        Serial.println("Username: " + name);
        dma_display->clearScreen();
        for (int y = 0; y < height; y++)
        {
            JsonArray row = data[y].as<JsonArray>();
            for (int x = 0; x < width; x++)
            {
                uint16_t color = row[x];
                dma_display->drawPixel(x, y, color);
            }
        }
        showPhotoInfo(title, name);
        fadeIn();
    }
    else
    {
        Serial.printf("Error en la solicitud: %d\n", httpCode);
        showTime();
        fadeIn();
    }
    http.end();
    delete client;
}

// Función para la animación de nueva foto
void onReceiveNewPic()
{
    // Reiniciar el índice de foto a 0
    photoIndex = 0;

    // Obtener y mostrar la nueva foto
    WiFiClient *client = getWiFiClient();
    HTTPClient http;

    http.begin(*client, String(serverUrl) + "public/photo/" + "get-photo/?id=" + String(photoIndex));
    int httpCode = http.GET();

    if (httpCode == 200)
    {
        String payload = http.getString();
        DynamicJsonDocument doc(200000);
        DeserializationError error = deserializeJson(doc, payload);

        if (!error)
        {
            JsonObject photo = doc["photo"].as<JsonObject>();
            JsonArray data = photo["data"].as<JsonArray>();
            int width = photo["width"];
            int height = photo["height"];
            String title = doc["title"].as<String>();
            String name = doc["username"].as<String>();


            // Animación de cuadrados crecientes
            int centerX = PANEL_RES_X / 2;
            int centerY = PANEL_RES_Y / 2;

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
                                dma_display->drawPixel(x, y, colors[colorIndex]);
                            }
                        }
                    }
                    delay(5); // Ajusta este valor para controlar la velocidad de la animación
                }
            }
            // Mostrar la foto con animación desde el centro
            showPhotoFromCenter(data, width, height, title, name);
        }
    }

    http.end();
    delete client;
}

void showUpdateMessage()
{
    dma_display->setFont();
    dma_display->setCursor(8, 50); // Centrar en el panel
    dma_display->print("Updating");
}

void showCheckMessage()
{
    showLoadingMsg("Checking updates");
}

void checkForUpdates()
{
    WiFiClientSecure client;
    HTTPClient http;
    client.setInsecure();
    showCheckMessage();
    Serial.println("Checking for updates...");
    http.begin(client, String(serverUrl) + "version/latest");
    int httpCode = http.GET();

    if (httpCode == 200)
    {
        String payload = http.getString();
        StaticJsonDocument<1024> doc;
        DeserializationError error = deserializeJson(doc, payload);
        if (!error)
        {
            int latestVersion = doc["version"];
            String updateUrl = doc["url"].as<String>();

            if (latestVersion > currentVersion)
            {
                Serial.printf("New version available: %d\n", latestVersion);
                showUpdateMessage();
                Serial.println("Downloading update...");
                http.begin(client, updateUrl);
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
                                preferences.putInt("currentVersion", latestVersion);
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
        String payload = http.getString();
        DynamicJsonDocument responseDoc(2048); // Aumentado de 200 a 2048
        DeserializationError error = deserializeJson(responseDoc, payload);
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
    preferences.putInt("brightness", 10);
    preferences.putInt("maxPhotos", 5);
    preferences.putUInt("secsPhotos", 30000);
    preferences.putBool("allowSpotify", true);
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
void mqttCallback(char* topic, byte* payload, unsigned int length) {
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

    if (!error) {
        const char* action = doc["action"];
        if (action) {
            if (strcmp(action, "update_photo") == 0) {
                onReceiveNewPic();
            } else if (strcmp(action, "update_info") == 0) {
                getPixie();
            }
        }
    }
}

// Función para reconectar al broker MQTT
void mqttReconnect() {
    while (!mqttClient.connected()) {
        Serial.print("Intentando conexión MQTT...");
        String clientId = String(MQTT_CLIENT_ID) + String(pixieId);
        
        if (mqttClient.connect(clientId.c_str(), MQTT_BROKER_USERNAME, MQTT_BROKER_PASSWORD)) {
            Serial.println("conectado");
            // Suscribirse al tema específico del pixie
            String topic = String("pixie/") + String(pixieId);
            Serial.println("Suscribiendo al tema: " + topic);
            mqttClient.subscribe(topic.c_str());
        } else {
            Serial.print("falló, rc=");
            Serial.print(mqttClient.state());
            Serial.println(" reintentando en 5 segundos");
            delay(5000);
        }
    }
}

// Función para dibujar el logo con efecto arcoiris
void drawLogo() {
    static uint8_t hue = 0; // Variable estática para mantener el estado del color
    const int LOGO_SIZE = 32; // Tamaño del logo (32x32)
    const int offset = 16;
    
    for (int y = 0; y < LOGO_SIZE; y++) {
        for (int x = 0; x < LOGO_SIZE; x++) {
            int index = y * LOGO_SIZE + x;
            const uint32_t pixel = logo[0][index];
            
            // Extraer los componentes ARGB
            uint8_t alpha = (pixel >> 24) & 0xFF;
            uint8_t red = (pixel >> 16) & 0xFF;
            uint8_t green = (pixel >> 8) & 0xFF;
            uint8_t blue = pixel & 0xFF;
            
            if (alpha > 0) { // Si el píxel no es transparente
                if (red == 0xFF && green == 0xFF && blue == 0xFF) { // Si el píxel es blanco
                    // Convertir el color HSV a RGB
                    uint8_t r, g, b;
                    uint8_t h = hue + (x + y) * 2; // Variar el tono según la posición
                    uint8_t s = 255; // Saturación máxima
                    uint8_t v = 255; // Valor máximo
                    
                    // Conversión HSV a RGB
                    uint8_t region = h / 43;
                    uint8_t remainder = (h - (region * 43)) * 6;
                    uint8_t p = (v * (255 - s)) >> 8;
                    uint8_t q = (v * (255 - ((s * remainder) >> 8))) >> 8;
                    uint8_t t = (v * (255 - ((s * (255 - remainder)) >> 8))) >> 8;
                    
                    switch (region) {
                        case 0:
                            r = v; g = t; b = p;
                            break;
                        case 1:
                            r = q; g = v; b = p;
                            break;
                        case 2:
                            r = p; g = v; b = t;
                            break;
                        case 3:
                            r = p; g = q; b = v;
                            break;
                        case 4:
                            r = t; g = p; b = v;
                            break;
                        default:
                            r = v; g = p; b = q;
                            break;
                    }
                    
                    // Dibujar el píxel con el nuevo color
                    dma_display->drawPixel(x + offset, y + offset, dma_display->color565(r, b, g));
                } else {
                    // Mantener el color original para píxeles no blancos
                    dma_display->drawPixel(x + offset, y + offset, dma_display->color565(red, blue, green));
                }
            }
        }
    }
    
    // Incrementar el tono para la siguiente llamada
    hue += 2;
}

// Función para mostrar un mensaje de carga centrado


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
    secsPhotos = preferences.getUInt("secsPhotos", 30000); // Leer el intervalo de cambio de fotos

    // Si no hay credenciales guardadas se activa el modo AP
    if (storedSSID == "")
    {
        apMode = true;
        Serial.println("No se encontraron credenciales WiFi. Iniciando modo AP...");
        WiFi.mode(WIFI_AP);
        WiFi.softAP("Pixie", "12345678");

        // Mostrar credenciales en el panel
        showAPCredentials("Pixie", "12345678");

        // Definir las rutas del servidor web
        server.on("/", HTTP_GET, []()
                  {
      String html = "<html><head><title>Configurar WiFi</title>";
      html += "<style>";
      html += "body { font-family: Arial, sans-serif; margin: 0; padding: 20px; display: flex; justify-content: center; align-items: center; min-height: 100vh; background-color: #f5f5f5; }";
      html += ".container { background-color: white; padding: 30px; border-radius: 10px; box-shadow: 0 2px 10px rgba(0,0,0,0.1); width: 100%; max-width: 400px; }";
      html += "h1 { color: #333; text-align: center; margin-bottom: 30px; }";
      html += ".form-group { margin-bottom: 20px; }";
      html += "label { display: block; margin-bottom: 5px; color: #666; }";
      html += "input[type='text'], input[type='password'] { width: 100%; padding: 10px; border: 1px solid #ddd; border-radius: 5px; box-sizing: border-box; }";
      html += "button { width: 100%; padding: 12px; background-color: #4CAF50; color: white; border: none; border-radius: 5px; cursor: pointer; font-size: 16px; }";
      html += "button:hover { background-color: #45a049; }";
      html += "</style></head><body>";
      html += "<div class='container'>";
      html += "<h1>Configurar WiFi</h1>";
      html += "<form action='/save' method='POST'>";
      html += "<div class='form-group'>";
      html += "<label for='ssid'>SSID:</label>";
      html += "<input type='text' id='ssid' name='ssid' required>";
      html += "</div>";
      html += "<div class='form-group'>";
      html += "<label for='password'>Contraseña:</label>";
      html += "<input type='password' id='password' name='password' required>";
      html += "</div>";
      html += "<button type='submit'>Guardar</button>";
      html += "</form>";
      html += "</div>";
      html += "</body></html>";
      server.send(200, "text/html", html); });

        server.on("/health", HTTP_GET, []()
                  { server.send(200, "application/json", "{\"status\":\"ok\"}"); });

        server.on("/save", HTTP_POST, []()
                  {
      if (server.hasArg("ssid") && server.hasArg("password"))
      {
        String newSSID = server.arg("ssid");
        String newPassword = server.arg("password");
        bool newAllowSpotify = server.hasArg("allowSpotify");
        int newMaxPhotos = server.arg("maxPhotos").toInt();
        // Guardar las credenciales en Preferences
        preferences.putString("ssid", newSSID);
        preferences.putString("password", newPassword);
        String response = "Credenciales guardadas. Reiniciando...";
        server.send(200, "text/html", response);
        delay(1000);
        ESP.restart();
      }
      else
      {
        server.send(400, "text/html", "Faltan datos");
      } });
        server.begin();
    }
    else
    {
        // Si se encontraron credenciales, conectar en modo STA
        Serial.println("Conectando a WiFi usando credenciales almacenadas...");
        WiFi.mode(WIFI_STA);
        WiFi.begin(storedSSID.c_str(), storedPassword.c_str());
        int wifiIconCounter = 0;
        // pintar toda la pantalla de blanco
        dma_display->clearScreen();
        dma_display->fillScreen(myWHITE);
        while (WiFi.status() != WL_CONNECTED)
        {
            drawLogo();
            showLoadingMsg("Connecting WiFi");
            delay(10);
        }
        Serial.println("\nWiFi conectado!");
        showLoadingMsg("Connected to WiFi");

        // Configurar cliente WiFi seguro para MQTT
        espClient.setInsecure();
        
        // Configuración de MQTT
        mqttClient.setServer(MQTT_BROKER_URL, MQTT_BROKER_PORT);
        mqttClient.setCallback(mqttCallback);
        
        // Intentar conectar a MQTT
        mqttReconnect();
    }

    // Configuración de OTA (se activa tanto en modo AP como STA)
    ArduinoOTA.setHostname(("Pixie-" + String(pixieId)).c_str());
    ArduinoOTA.onStart([]()
                       {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH)
      type = "sketch";
    else
      type = "filesystem";
    Serial.println("Inicio de actualización OTA: " + type); });
    ArduinoOTA.onEnd([]()
                     { Serial.println("\nActualización OTA completada."); });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total)
                          {
    Serial.printf("Progreso: %u%%\n", (progress / (total / 100)));
    showPercetage((progress / (total / 100))); });
    ArduinoOTA.onError([](ota_error_t error)
                       {
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
      Serial.println("Error al finalizar"); });
    ArduinoOTA.begin();

    if (apMode)
    {
        while (true)
        {
            server.handleClient();
            ArduinoOTA.handle();
            delay(10);
        }
    }

    timeClient.begin();
    timeClient.setTimeOffset(7200);
    timeClient.update();

    // Check for updates
    checkForUpdates();

    // Register Pixie if not registered
    if (pixieId == 0)
    {
        registerPixie();
    }
    else
    {
        getPixie();
    }
    showLoadingMsg("Ready!");
}

String songOnline = "";

void loop()
{
    // Si estamos en modo AP, solo atender el servidor web y OTA
    if (apMode)
    {
        server.handleClient();
        ArduinoOTA.handle();
        delay(10);
        return;
    }
    else
    {
        // Manejo de MQTT
        if (!mqttClient.connected()) {
            mqttReconnect();
        }
        mqttClient.loop();

        // Si estamos conectados a WiFi (modo STA), se ejecuta la lógica original:
        if (allowSpotify)
        {
            songOnline = fetchSongId();
            if (songOnline == "" || songOnline == "null")
            {
                if (millis() - lastPhotoChange >= secsPhotos)
                {
                    showPhoto(photoIndex++);
                    lastPhotoChange = millis();
                    if (photoIndex >= maxPhotos)
                    {
                        photoIndex = 0;
                    }
                }
            }
            else
            {
                if (songShowing != songOnline)
                {
                    songShowing = songOnline;
                    fetchAndDrawCover();
                }
            }
        }
        else
        {
            if (millis() - lastPhotoChange >= secsPhotos)
            {
                showPhoto(photoIndex++);
                lastPhotoChange = millis();
                if (photoIndex >= maxPhotos)
                {
                    photoIndex = 0;
                }
            }
        }
        delay(1000);
        for (int i = 0; i < 10; i++)
        {
            ArduinoOTA.handle();
        }
    }
}
