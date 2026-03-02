#include "spotify.h"
#include "display.h"
#include "clock.h"
#include "mqtt_handlers.h"

String fetchSongId()
{
    songIdBuffer[0] = '\0';

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

void fetchAndDrawCover()
{
    lastPhotoChange = millis();

    LOG("[Spotify] Fetching cover via MQTT...");
    esp_task_wdt_reset();

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
        esp_task_wdt_reset();

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

            wait(15);
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
