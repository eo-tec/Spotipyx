#include "ota.h"
#include "config.h"
#include "display.h"
#include "mqtt_handlers.h"
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Update.h>

void checkForUpdates()
{
    #ifdef DEV_MODE
    LOG("DEV_MODE active - skipping update check");
    return;
    #endif

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

    // Publicar request via MQTT con hw_version
    String topic = String("frame/") + String(frameId) + "/request/ota";
    String payload = String("{\"hw_version\":\"") + HW_VERSION + "\"}";
    if (!mqttClient.publish(topic.c_str(), payload.c_str())) {
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
        LOGF("[OTA] New version available: %d (current: %d)", latestVersion, currentVersion);
        showUpdateMessage();

        LOGF("[OTA] Download URL: %s", updateUrl.c_str());
        LOGF("[OTA] URL starts with https: %s", updateUrl.startsWith("https://") ? "yes" : "no");
        LOGF("[OTA] Free heap before download: %d bytes", ESP.getFreeHeap());

        // La descarga del firmware sigue usando HTTP (necesario para streaming)
        WiFiClient *updateClient;
        if (updateUrl.startsWith("https://")) {
            LOG("[OTA] Creating WiFiClientSecure (insecure mode)...");
            WiFiClientSecure *secureClient = new WiFiClientSecure();
            secureClient->setInsecure();
            updateClient = secureClient;
        } else {
            LOG("[OTA] Creating plain WiFiClient...");
            updateClient = new WiFiClient();
        }

        HTTPClient updateHttp;
        LOG("[OTA] Calling HTTPClient.begin()...");
        updateHttp.begin(*updateClient, updateUrl);
        updateHttp.setTimeout(30000);
        LOG("[OTA] Calling HTTP GET...");
        int updateHttpCode = updateHttp.GET();
        LOGF("[OTA] HTTP response code: %d", updateHttpCode);

        if (updateHttpCode == 200)
        {
            WiFiClient *updateStream = updateHttp.getStreamPtr();
            size_t contentLength = updateHttp.getSize();
            LOGF("[OTA] Content-Length: %d bytes", contentLength);
            LOGF("[OTA] Free heap after GET: %d bytes", ESP.getFreeHeap());

            Update.onProgress([](size_t written, size_t total)
                              {
                int percent = (written * 100) / total;
                showPercetage(percent);
                LOGF("[OTA] Progress: %d%% (%d/%d bytes)", percent, written, total); });

            LOG("[OTA] Calling Update.begin()...");
            if (Update.begin(contentLength))
            {
                LOG("[OTA] Update.begin() OK, writing stream...");
                size_t written = Update.writeStream(*updateStream);
                LOGF("[OTA] writeStream returned: %d bytes (expected %d)", written, contentLength);
                if (written == contentLength)
                {
                    LOG("[OTA] Firmware written OK, calling Update.end()...");
                    if (Update.end())
                    {
                        LOG("[OTA] Update completed successfully, restarting...");
                        preferences.putInt("currentVersion", latestVersion);
                        ESP.restart();
                    }
                    else
                    {
                        LOGF("[OTA] Update.end() FAILED: %s", Update.errorString());
                    }
                }
                else
                {
                    LOGF("[OTA] Write error: only %d/%d bytes written", written, contentLength);
                    LOGF("[OTA] Update error: %s", Update.errorString());
                }
            }
            else
            {
                LOGF("[OTA] Update.begin() FAILED (contentLength=%d): %s", contentLength, Update.errorString());
            }
        }
        else
        {
            LOGF("[OTA] HTTP error downloading firmware: %d", updateHttpCode);
            if (updateHttpCode < 0) {
                LOGF("[OTA] Connection error (negative code means HTTPClient error, not HTTP status)");
            }
            String response = updateHttp.getString();
            LOGF("[OTA] Server response: %s", response.substring(0, 200).c_str());
        }
        updateHttp.end();
        delete updateClient;
    }
    else
    {
        LOG("No new updates available.");
    }
}
