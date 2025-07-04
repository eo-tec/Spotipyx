#include "Pixie.h"


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

int brightness = 10;
int wifiBrightness = 0;
int maxIndex = 5;
int maxPhotos = 5;
int currentVersion = 0;
int pixieId = 0;

Pixie::Pixie(const char *serverUrl, bool devMode)
{
    this->serverUrl = serverUrl;
    this->devMode = devMode;
}

WiFiClient *Pixie::getWiFiClient()
{
    if (devMode)
    {
        return new WiFiClient();
    }
    else
    {
        WiFiClientSecure *clientSecure = new WiFiClientSecure();
        return clientSecure;
    }
}

String Pixie::getCover()
{
    WiFiClient *client = getWiFiClient();
    HTTPClient http;

    http.begin(*client, String(serverUrl) + "public/spotify/cover-64x64");
    int httpCode = http.GET();

    String payload;
    if (httpCode == 200)
    {
        payload = http.getString();
    }
    http.end();
    delete client;
    return payload;
}

String Pixie::getSongId()
{
    WiFiClient *client = getWiFiClient();
    HTTPClient http;

    http.begin(*client, String(serverUrl) + "public/spotify/id-playing");
    int httpCode = http.GET();

    String songId;
    if (httpCode == 200)
    {
        String payload = http.getString();
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, payload);
        if (!error)
        {
            songId = doc["id"].as<String>();
        }
    }
    http.end();
    delete client;
    return songId;
}

String Pixie::getPhoto(int photoIndex, int pixieId)
{
    WiFiClient *client = getWiFiClient();
    HTTPClient http;

    http.begin(*client, String(serverUrl) + "public/photo/get-photo/?id=" + String(photoIndex));
    int httpCode = http.GET();

    String payload;
    if (httpCode == 200)
    {
        payload = http.getString();
    }
    http.end();
    delete client;
    return payload;
}

int Pixie::registerPixie(const String &macAddress)
{
    WiFiClient *client = getWiFiClient();
    HTTPClient http;

    http.begin(*client, String(serverUrl) + "public/pixie/add");
    http.addHeader("Content-Type", "application/json");

    JsonDocument doc;
    doc["mac"] = macAddress;
    String requestBody;
    serializeJson(doc, requestBody);

    int httpCode = http.POST(requestBody);

    int pixieId = -1;
    if (httpCode == 200)
    {
        String payload = http.getString();
        JsonDocument responseDoc;
        DeserializationError error = deserializeJson(responseDoc, payload);
        if (!error)
        {
            pixieId = responseDoc["pixie"]["id"];
        }
    }
    http.end();
    delete client;
    return pixieId;
}

void Pixie::fadeOut(MatrixPanel_I2S_DMA *dma_display, int brightness)
{
    for (int b = brightness; b >= 0; b--)
    {
        dma_display->setBrightness8(b);
        delay(30);
    }
}

void Pixie::fadeIn(MatrixPanel_I2S_DMA *dma_display, int brightness)
{
    for (int b = 0; b <= brightness; b++)
    {
        dma_display->setBrightness8(b);
        delay(30);
    }
}

void Pixie::showUpdateMessage(MatrixPanel_I2S_DMA *dma_display)
{
    dma_display->setFont();
    dma_display->setCursor(8, 50); // Centrar en el panel
    dma_display->print("Updating");
}

void Pixie::showCheckMessage(MatrixPanel_I2S_DMA *dma_display)
{
    dma_display->setFont();
    dma_display->setCursor(8, 50); // Centrar en el panel
    dma_display->print("Checking");
}

void Pixie::checkForUpdates(MatrixPanel_I2S_DMA *dma_display, Preferences &preferences, int currentVersion)
{
    WiFiClientSecure client;
    HTTPClient http;
    client.setInsecure();
    showCheckMessage(dma_display);
    Serial.println("Checking for updates...");
    http.begin(client, String(serverUrl) + "version/latest");
    int httpCode = http.GET();

    if (httpCode == 200)
    {
        String payload = http.getString();
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, payload);
        if (!error)
        {
            int latestVersion = doc["version"];
            String updateUrl = doc["url"].as<String>();

            if (latestVersion > currentVersion)
            {
                Serial.printf("New version available: %d\n", latestVersion);
                showUpdateMessage(dma_display);
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
            dma_display->clearScreen();
            dma_display->setTextSize(1);
            dma_display->setTextColor(myWHITE);
            dma_display->setFont(&FreeSans12pt7b);
            dma_display->setCursor(8, 38); // Centrar en el panel
            dma_display->print((percent < 10 ? "0" : "") + String(min(percent, 99)) + "%");
            dma_display->setFont();
            dma_display->setCursor(8, 50); // Centrar en el panel
            dma_display->print("Updating");
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

void Pixie::showWiFiIcon(MatrixPanel_I2S_DMA *dma_display, int n, uint16_t myWHITE, uint16_t myBLACK)
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

void Pixie::showPercetage(MatrixPanel_I2S_DMA *dma_display, int percentage, uint16_t myWHITE)
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

void Pixie::showTime(MatrixPanel_I2S_DMA *dma_display, NTPClient &timeClient)
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

void Pixie::pushUpAnimation(MatrixPanel_I2S_DMA *dma_display, int y, JsonArray &data)
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

void Pixie::showPhoto(MatrixPanel_I2S_DMA *dma_display, int photoIndex, int pixieId, uint16_t myWHITE, uint16_t myBLACK)
{
    String payload = getPhoto(photoIndex, pixieId);

    if (!payload.isEmpty())
    {
        Serial.println("Foto recibida. Mostrando en el panel...");
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, payload);
        if (error)
        {
            Serial.print("Error al parsear JSON: ");
            Serial.println(error.c_str());
            showTime(dma_display, timeClient);
            fadeIn(dma_display, brightness);
            return;
        }
        JsonObject photo = doc["photo"].as<JsonObject>();
        JsonArray data = photo["data"].as<JsonArray>();
        int width = photo["width"];
        int height = photo["height"];
        String title = doc["title"].as<String>();
        String name = doc["username"].as<String>();
        fadeOut(dma_display, brightness);
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
        fadeIn(dma_display, brightness);
    }
    else
    {
        Serial.println("Error al obtener la foto.");
        showTime(dma_display, timeClient);
        fadeIn(dma_display, brightness);
    }
}
