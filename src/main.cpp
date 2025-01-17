#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <Fonts/FreeSans12pt7b.h>
#include <ArduinoOTA.h>
#include <pics.h>

// Pines del panel
#define R1_PIN 25
#define G1_PIN 26
#define B1_PIN 27
#define R2_PIN 14
#define G2_PIN 12
#define B2_PIN 13
#define A_PIN 23
#define B_PIN 22
#define C_PIN 5
#define D_PIN 17
#define E_PIN 18
#define LAT_PIN 4
#define OE_PIN 15
#define CLK_PIN 16

#define PANEL_RES_X 64
#define PANEL_RES_Y 64
#define PANEL_CHAIN 1

MatrixPanel_I2S_DMA *dma_display = nullptr;

HUB75_I2S_CFG::i2s_pins _pins = {R1_PIN, G1_PIN, B1_PIN, R2_PIN, G2_PIN, B2_PIN, A_PIN, B_PIN, C_PIN, D_PIN, E_PIN, LAT_PIN, OE_PIN, CLK_PIN};

uint16_t myBLACK = 0;
uint16_t myWHITE = 0xFFFF;
uint16_t myRED = 0xF800;
uint16_t myGREEN = 0x07E0;
uint16_t myBLUE = 0x001F;

const char *ssid = "WifiAlvaro";
const char *password = "EoTec96!";
const char *serverUrl = "https://my-album-71oa.onrender.com/cover-64x64";

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org");

String songShowing = "";

// Función para mostrar el icono de WiFi parpadeando
void showWiFiIcon(bool on)
{
  dma_display->clearScreen();
  if (on)
  {
    for (int y = 0; y < 32; y++)
    {
      for (int x = 0; x < 32; x++)
      {
        dma_display->drawPixel(x, y, wifiArray[y][x] > 0 ? myGREEN : myBLACK);
      }
    }
  }
}

void showPercetage(int percentage){
  dma_display->clearScreen();
  dma_display->setTextSize(1);
  dma_display->setTextColor(myWHITE);
  dma_display->setFont(&FreeSans12pt7b);
  dma_display->setCursor(3, 38); // Centrar en el panel
  dma_display->print(String(percentage) + "%");
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
}

// Función para obtener y mostrar la portada de la canción
#include <WiFiClientSecure.h>

void fetchAndDrawCover()
{
  WiFiClientSecure client; // Cliente seguro para HTTPS
  HTTPClient http;

  // Configuración opcional del certificado (puedes agregar el tuyo si lo tienes)
  // client.setCACert(rootCACertificate);

  // Deshabilitar validación del certificado (Úsalo solo para pruebas)
  client.setInsecure();

  Serial.println("Conectando al servidor...");
  http.begin(client, serverUrl); // Asociar cliente seguro con la URL del servidor
  int httpCode = http.GET();     // Realizar la solicitud GET

  if (httpCode == 200) // Si la respuesta HTTP es OK
  {
    String payload = http.getString();
    DynamicJsonDocument doc(100000);
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

  const char *url = "https://my-album-71oa.onrender.com/id-playing"; // Dirección del servidor
  http.begin(client, url);                                           // Asociar el cliente seguro (HTTPS) con la URL

  int httpCode = http.GET(); // Realizar la solicitud GET

  if (httpCode == 200)
  {                                    // Si la respuesta HTTP es OK
    String payload = http.getString(); // Obtener el cuerpo de la respuesta
    DynamicJsonDocument doc(1024);
    DeserializationError error = deserializeJson(doc, payload);

    if (!error)
    {
      String songId = doc["id"].as<String>(); // Obtener el ID como string
      http.end();
      if (songId == "")
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

void setup()
{
  Serial.begin(115200);

  HUB75_I2S_CFG mxconfig(PANEL_RES_X, PANEL_RES_Y, PANEL_CHAIN);
  mxconfig.gpio.e = E_PIN;
  mxconfig.clkphase = false;

  dma_display = new MatrixPanel_I2S_DMA(mxconfig);
  dma_display->begin();
  dma_display->setBrightness8(40);
  dma_display->clearScreen();
  dma_display->setRotation(45);

  WiFi.begin(ssid, password);

  Serial.println("Conectando al WiFi...");
  bool wifiIconOn = true;

  while (WiFi.status() != WL_CONNECTED)
  {
    showWiFiIcon(wifiIconOn);
    wifiIconOn = !wifiIconOn;
    delay(500);
  }
  Serial.println("\nWiFi conectado!");

  // Configuración de OTA
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
                          showPercetage((progress / (total / 100)));
                        });

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
}

String songOnline = "";
void loop()
{
  songOnline = fetchSongId();
  if (songShowing != songOnline)
  {
    songShowing = songOnline;
    fetchAndDrawCover();
  }

  if (songOnline == "")
  {
    showTime();
  }
  delay(1000);
  for(int i = 0; i < 10; i++){
    ArduinoOTA.handle();
  }
}
