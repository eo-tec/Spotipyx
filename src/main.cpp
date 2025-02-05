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

const char *serverUrl = "https://my-album-production.up.railway.app/";

int brightness = 30;
int wifiBrightness = 0;
int maxIndex = 5;

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org");

String songShowing = "";
unsigned long lastPhotoChange = -60000;
const unsigned long photoChangeInterval = 30000;

// Declaraciones globales para las credenciales y el servidor
Preferences preferences;
WebServer server(80);
bool apMode = false; // Se activará si no hay credenciales guardadas

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
  WiFiClientSecure client;
  HTTPClient http;
  client.setInsecure();

  Serial.println("Conectando al servidor...");
  http.begin(client, String(serverUrl) + "cover-64x64");
  int httpCode = http.GET();

  if (httpCode == 200)
  {
    String payload = http.getString();
    StaticJsonDocument<100000> doc;
    DeserializationError error = deserializeJson(doc, payload);
    if (error)
    {
      Serial.print("Error al parsear JSON: ");
      Serial.println(error.c_str());
      showTime();
      http.end();
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
}

// Función para obtener el ID de la canción en reproducción (ya existente)
String fetchSongId()
{
  WiFiClientSecure client;
  HTTPClient http;
  client.setInsecure();
  http.begin(client, String(serverUrl) + "id-playing");
  int httpCode = http.GET();

  if (httpCode == 200)
  {
    String payload = http.getString();
    StaticJsonDocument<1024> doc;
    DeserializationError error = deserializeJson(doc, payload);
    if (!error)
    {
      String songId = doc["id"].as<String>();
      http.end();
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

// Función para mostrar una foto (ya existente)
void showPhoto(int photoIndex)
{
  WiFiClientSecure client;
  HTTPClient http;
  songShowing = "photo";
  client.setInsecure();

  Serial.println("Conectando al servidor para obtener la foto...");
  http.begin(client, String(serverUrl) + "get-photo/?id=" + String(photoIndex));
  int httpCode = http.GET();

  if (httpCode == 200)
  {
    fadeOut();
    String payload = http.getString();
    DynamicJsonDocument doc(100000);
    DeserializationError error = deserializeJson(doc, payload);
    if (error)
    {
      Serial.print("Error al parsear JSON: ");
      Serial.println(error.c_str());
      showTime();
      http.end();
      fadeIn();
      return;
    }
    JsonObject photo = doc["photo"].as<JsonObject>();
    JsonArray data = photo["data"].as<JsonArray>();
    int width = photo["width"];
    int height = photo["height"];
    String title = doc["title"].as<String>();
    String name = doc["username"].as<String>();

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

    // Mostrar título en la parte inferior izquierda
    dma_display->setFont(&Picopixel);
    dma_display->setTextSize(1);
    dma_display->setTextColor(myWHITE);
    int16_t x1, y1;
    uint16_t w, h;
    dma_display->getTextBounds(title, 0, PANEL_RES_Y - 2, &x1, &y1, &w, &h);
    dma_display->fillRect(x1 - 1, y1 - 1, w + 3, h + 2, myBLACK);
    dma_display->setCursor(1, PANEL_RES_Y - 2);
    dma_display->print(title);

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
    fadeIn();
  }
  else
  {
    Serial.printf("Error en la solicitud: %d\n", httpCode);
    showTime();
    fadeIn();
  }
  http.end();
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
  dma_display->setBrightness8(brightness);
  dma_display->clearScreen();
  dma_display->setRotation(45);

  // Inicializar Preferences para leer/guardar las credenciales
  preferences.begin("wifi", false);
  String storedSSID = preferences.getString("ssid", "");
  String storedPassword = preferences.getString("password", "");

  // Si no hay credenciales guardadas se activa el modo AP
  if (storedSSID == "")
  {
    apMode = true;
    Serial.println("No se encontraron credenciales WiFi. Iniciando modo AP...");
    WiFi.mode(WIFI_AP);
    WiFi.softAP("Pixie", "12345678");

    // Definir las rutas del servidor web
    server.on("/", HTTP_GET, []()
              {
      String html = "<html><head><title>Configurar WiFi</title></head><body>";
      html += "<h1>Configurar WiFi</h1>";
      html += "<form action='/save' method='POST'>";
      html += "SSID: <input type='text' name='ssid'><br>";
      html += "Password: <input type='password' name='password'><br>";
      html += "<input type='submit' value='Guardar'>";
      html += "</form></body></html>";
      server.send(200, "text/html", html); });

    server.on("/save", HTTP_POST, []()
              {
      if (server.hasArg("ssid") && server.hasArg("password"))
      {
        String newSSID = server.arg("ssid");
        String newPassword = server.arg("password");
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

    // Mostrar en el panel la imagen QR (se asume que "qr" es una matriz 64x64 definida en pics.h)
    for (int y = 0; y < 64; y++)
    {
      for (int x = 0; x < 64; x++)
      {
        dma_display->drawPixel(x, y, qr[y][x]);
      }
    }
  }
  else
  {
    // Si se encontraron credenciales, conectar en modo STA
    Serial.println("Conectando a WiFi usando credenciales almacenadas...");
    WiFi.mode(WIFI_STA);
    WiFi.begin(storedSSID.c_str(), storedPassword.c_str());
    int wifiIconCounter = 0;
    while (WiFi.status() != WL_CONNECTED)
    {
      showWiFiIcon(wifiIconCounter++);
      if (wifiIconCounter > 3)
        wifiIconCounter = 0;
      delay(500);
    }
    Serial.println("\nWiFi conectado!");
  }

  // Configuración de OTA (se activa tanto en modo AP como STA)
  ArduinoOTA.setHostname("Pixie");
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

  timeClient.begin();
  timeClient.setTimeOffset(3600);
  // showTime();
  lastPhotoChange = millis();
}

String songOnline = "";
int photoIndex = 0;

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
    // Si estamos conectados a WiFi (modo STA), se ejecuta la lógica original:
    songOnline = fetchSongId();
    if (songOnline == "" || songOnline == "null")
    {
      if (millis() - lastPhotoChange >= photoChangeInterval)
      {
        showPhoto(photoIndex++);
        lastPhotoChange = millis();
        if (photoIndex >= maxIndex)
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
    delay(1000);
    for (int i = 0; i < 10; i++)
    {
      ArduinoOTA.handle();
    }
  }
}
