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
