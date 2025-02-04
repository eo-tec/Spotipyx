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
#include <pics.h>

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

const char *ssid = "WifiAlvaro";
const char *password = "EoTec96!";
const char *serverUrl = "https://my-album-production.up.railway.app/";

int brightness = 30;
int wifiBrightness = 0;
int maxIndex = 5;

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org");

String songShowing = "";

unsigned long lastPhotoChange = -60000;
const unsigned long photoChangeInterval = 30000;

// Función para mostrar el icono de WiFi parpadeando
void showWiFiIcon(int n)
{
  uint16_t draw[32][32];
  if (n == 0)
  {
    memcpy(draw, wifiArray, sizeof(draw));
  }
  else if(n == 1)
  {
    memcpy(draw, wifi1, sizeof(draw));
  }else if(n == 2)
  {
    memcpy(draw, wifi2, sizeof(draw));
  }else if(n == 3)
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

// Función para mostrar la hora en el panel
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

uint16_t screenBuffer[PANEL_RES_Y][PANEL_RES_X]; // Buffer to store the current screen content

void pushUpAnimation(int y, JsonArray &data)
{
  unsigned long startTime = millis();
  for (int i = 0; i <= y; i++) // Change the loop condition to include the last row
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

void fetchAndDrawCover()
{
  lastPhotoChange = millis(); // Reiniciar el temporizador de cambio de foto
  WiFiClientSecure client;                          // Cliente seguro para HTTPS
  HTTPClient http;

  // Configuración opcional del certificado (puedes agregar el tuyo si lo tienes)
  // client.setCACert(rootCACertificate);

  // Deshabilitar validación del certificado (Úsalo solo para pruebas)
  client.setInsecure();

  Serial.println("Conectando al servidor...");
  http.begin(client, String(serverUrl) + "cover-64x64"); // Asociar cliente seguro con la URL del servidor
  int httpCode = http.GET();                             // Realizar la solicitud GET

  if (httpCode == 200) // Si la respuesta HTTP es OK
  {
    String payload = http.getString();
    StaticJsonDocument<100000> doc;
    DeserializationError error = deserializeJson(doc, payload);

    if (error)
    {
      Serial.print("Error al parsear JSON: ");
      Serial.println(error.c_str());
      showTime(); // Mostrar la hora si no se puede parsear el JSON
      http.end();
      return;
    }

    JsonArray data = doc["data"].as<JsonArray>();
    int width = doc["width"];
    int height = doc["height"];

    // Push up animation before showing new cover

    // dma_display->clearScreen();
    for (int y = 0; y < height; y++)
    {
      JsonArray row = data[y].as<JsonArray>();
      pushUpAnimation(y, data);
      /*for (int x = 0; x < width; x++)
      {
        uint16_t color = row[x];
        dma_display->drawPixel(x, y, color);
      }*/
    }
  }
  else
  {
    Serial.printf("Error en la solicitud: %d\n", httpCode);
    showTime(); // Mostrar la hora si el servidor devuelve un error
  }

  http.end(); // Finalizar la conexión HTTP
}

String fetchSongId()
{
  WiFiClientSecure client; // Usar WiFiClientSecure para HTTPS
  HTTPClient http;

  // Si tienes un certificado raíz del servidor, puedes configurarlo aquí
  // client.setCACert(rootCACertificate);

  // Para deshabilitar la validación del certificado (solo para pruebas), usa:
  client.setInsecure(); // No valida el certificado (Úsalo solo si no tienes un certificado válido)

  http.begin(client, String(serverUrl) + "id-playing"); // Asociar el cliente seguro (HTTPS) con la URL

  int httpCode = http.GET(); // Realizar la solicitud GET

  if (httpCode == 200)
  {                                    // Si la respuesta HTTP es OK
    String payload = http.getString(); // Obtener el cuerpo de la respuesta
    StaticJsonDocument<1024> doc;
    DeserializationError error = deserializeJson(doc, payload);

    if (!error)
    {
      String songId = doc["id"].as<String>(); // Obtener el ID como string
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

  http.end(); // Finalizar la conexión HTTP
  return "";  // Devuelve un string vacío si hay error
}

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

void showPhoto(int photoIndex)
{
  WiFiClientSecure client; // Usar WiFiClientSecure para HTTPS
  HTTPClient http;
  songShowing = "photo";

  client.setInsecure(); // No valida el certificado (Úsalo solo si no tienes un certificado válido)

  Serial.println("Conectando al servidor para obtener la foto...");
  http.begin(client, String(serverUrl) + "get-photo/?id=" + String(photoIndex)); // Asociar cliente con la URL del servidor
  int httpCode = http.GET();                                                     // Realizar la solicitud GET

  if (httpCode == 200) // Si la respuesta HTTP es OK
  {
    fadeOut(); // Fade out before showing new photo

    String payload = http.getString();
    DynamicJsonDocument doc(100000);
    DeserializationError error = deserializeJson(doc, payload);

    if (error)
    {
      Serial.print("Error al parsear JSON: ");
      Serial.println(error.c_str());
      showTime(); // Mostrar la hora si no se puede parsear el JSON
      http.end();
      fadeIn(); // Fade in back to original brightness
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

    // Display title at the bottom left
    dma_display->setFont(&Picopixel);
    dma_display->setTextSize(1);
    dma_display->setTextColor(myWHITE);
    int16_t x1, y1;
    uint16_t w, h;
    dma_display->getTextBounds(title, 0, PANEL_RES_Y - 2, &x1, &y1, &w, &h);
    dma_display->fillRect(x1 - 1, y1 - 1, w + 3, h + 2, myBLACK); // Draw black rectangle
    dma_display->setCursor(1, PANEL_RES_Y - 2);
    dma_display->print(title);

    Serial.print("Width: ");
    Serial.println(w);
    Serial.print("Name length: ");
    Serial.println(name.length());
    Serial.print("Sum: ");
    Serial.println(w + name.length());

    // Display name at the bottom right
    if (w + name.length() * 3 > PANEL_RES_X && title.length() > 0)
    {
      dma_display->getTextBounds(name, 0, PANEL_RES_Y - 10, &x1, &y1, &w, &h);
      dma_display->fillRect(x1 - 1, y1 - 1, w + 3, h + 2, myBLACK); // Draw black rectangle
      dma_display->setCursor(1, PANEL_RES_Y - 10);
      dma_display->print(name);
    }
    else
    {
      dma_display->getTextBounds(name, 0, PANEL_RES_Y - 2, &x1, &y1, &w, &h);
      dma_display->fillRect(PANEL_RES_X - w - 1, y1 - 1, w + 2, h + 2, myBLACK); // Draw black rectangle
      dma_display->setCursor(PANEL_RES_X - w, PANEL_RES_Y - 2);
      dma_display->print(name);
    }

    fadeIn(); // Fade in after showing new photo
  }
  else
  {
    Serial.printf("Error en la solicitud: %d\n", httpCode);
    showTime(); // Mostrar la hora si el servidor devuelve un error
    fadeIn();   // Fade in back to original brightness
  }

  http.end(); // Finalizar la conexión HTTP
}

void setup()
{
  Serial.begin(115200);

  HUB75_I2S_CFG mxconfig(PANEL_RES_X, PANEL_RES_Y, PANEL_CHAIN);
  mxconfig.gpio.e = E_PIN;
  mxconfig.clkphase = false;

  dma_display = new MatrixPanel_I2S_DMA(mxconfig);
  dma_display->begin();
  dma_display->setBrightness8(brightness);
  dma_display->clearScreen();
  dma_display->setRotation(45);

  WiFi.begin(ssid, password);

  Serial.println("Conectando al WiFi...");
  bool wifiIconOn = true;
  int wifiIconCounter = 0;

  while (WiFi.status() != WL_CONNECTED)
  {
    showWiFiIcon(wifiIconCounter++);
    wifiIconOn = !wifiIconOn;
    if (wifiIconCounter > 3)
    {
      wifiIconCounter = 0;
    }
    delay(500);
  }
  Serial.println("\nWiFi conectado!");

  // Configuración de OTA
  ArduinoOTA.setHostname("Pixie"); // Establece el hostname
  ArduinoOTA.onStart([]()
                     {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH)
      type = "sketch";
    else // U_SPIFFS
      type = "filesystem";

    // Notificación por puerto serie
    Serial.println("Inicio de actualización OTA: " + type); });

  ArduinoOTA.onEnd([]()
                   { Serial.println("\nActualización OTA completada."); });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total)
                        { Serial.printf("Progreso: %u%%\n", (progress / (total / 100))); 
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
  timeClient.setTimeOffset(3600); // Ajusta según tu zona horaria
  showTime();
  lastPhotoChange = millis();
}

String songOnline = "";
int photoIndex = 0;
void loop()
{
  songOnline = fetchSongId();

  if (songOnline == "" || songOnline == "null")
  {
    // showTime();
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
