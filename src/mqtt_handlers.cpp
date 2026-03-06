#include "mqtt_handlers.h"
#include "config.h"
#include "ble_provisioning.h"

// Forward declaration (defined in mqtt_client.cpp)
void mqttCallback(char *topic, byte *payload, unsigned int length);

bool waitForMqttResponse(const char* expectedType, unsigned long timeout) {
    mqttResponseReceived = false;
    mqttResponseSuccess = false;
    mqttResponseType = "";
    unsigned long start = millis();

    while ((millis() - start) < timeout) {
        esp_task_wdt_reset();
        mqttClient.loop();
        yield();
        delay(10);

        // Solo salir si recibimos la respuesta del tipo esperado Y fue exitosa
        if (mqttResponseReceived && mqttResponseType == expectedType) {
            if (mqttResponseSuccess) {
                return true;
            }
            // Respuesta stale (reqId incorrecto) - seguir esperando la buena
            LOGF("[MQTT] Respuesta stale descartada, seguimos esperando '%s'", expectedType);
            mqttResponseReceived = false;
            mqttResponseType = "";
            continue;
        }

        // Si recibimos respuesta de otro tipo (tardía), ignorarla y seguir esperando
        if (mqttResponseReceived && mqttResponseType != expectedType) {
            LOGF("[MQTT] Ignorando respuesta tardía tipo '%s' (esperando '%s')",
                 mqttResponseType.c_str(), expectedType);
            mqttResponseReceived = false;
            mqttResponseType = "";
        }
    }

    LOGF("[MQTT] Timeout esperando respuesta %s", expectedType);
    return false;
}

void handleSongResponse(byte* payload, unsigned int length) {
    songIdBuffer[0] = '\0';

    if (length > 0 && length < sizeof(httpBuffer)) {
        memcpy(httpBuffer, payload, length);
        httpBuffer[length] = '\0';

        // Parsear para extraer el id
        char *idStart = strstr(httpBuffer, "\"id\":\"");
        if (idStart) {
            idStart += 6;
            char *idEnd = strchr(idStart, '"');
            if (idEnd && (idEnd - idStart) < 63) {
                int idLen = idEnd - idStart;
                strncpy(songIdBuffer, idStart, idLen);
                songIdBuffer[idLen] = '\0';
            }
        }
    }

    mqttResponseReceived = true;
    mqttResponseSuccess = true;
    mqttResponseType = "song";
}

void handleCoverResponse(byte* payload, unsigned int length) {
    if (length == 8192) {
        memcpy(spotifyCoverBuffer, payload, length);
        mqttResponseSuccess = true;
        LOG("[MQTT] Cover recibido correctamente");
    } else {
        LOGF("[MQTT] Cover con tamaño incorrecto: %d (esperado 8192)", length);
        mqttResponseSuccess = false;
    }
    mqttResponseReceived = true;
    mqttResponseType = "cover";
}

void handlePhotoResponse(byte* payload, unsigned int length) {
    photoTitle[0] = '\0';
    photoAuthor[0] = '\0';

    // Formato: {"title":"x","author":"y","reqId":N}\n[12288 bytes binarios]
    // Buscar el newline que separa JSON de binario
    int jsonEnd = -1;
    for (unsigned int i = 0; i < min(length, 256u); i++) {
        if (payload[i] == '\n') {
            jsonEnd = i;
            break;
        }
    }

    if (jsonEnd > 0 && (length - jsonEnd - 1) >= 12288) {
        // Parsear JSON metadata
        char jsonBuf[256];
        memcpy(jsonBuf, payload, jsonEnd);
        jsonBuf[jsonEnd] = '\0';

        JsonDocument doc;
        if (deserializeJson(doc, jsonBuf) == DeserializationError::Ok) {
            // Filtrar respuestas stale: ignorar si reqId no coincide
            if (doc.containsKey("reqId")) {
                uint32_t respReqId = doc["reqId"];
                if (respReqId != mqttRequestId) {
                    LOGF("[MQTT] Ignorando foto stale (reqId=%d, esperado=%d): %s",
                         respReqId, mqttRequestId, (const char*)(doc["title"] | "?"));
                    mqttResponseReceived = true;
                    mqttResponseType = "photo";
                    mqttResponseSuccess = false;
                    return;
                }
            }

            strncpy(photoTitle, doc["title"] | "", sizeof(photoTitle) - 1);
            strncpy(photoAuthor, doc["author"] | "", sizeof(photoAuthor) - 1);
            photoTitle[sizeof(photoTitle) - 1] = '\0';
            photoAuthor[sizeof(photoAuthor) - 1] = '\0';
        }

        // Copiar datos binarios (first frame as photo - always works, even for animations)
        memcpy(photoBuffer, payload + jsonEnd + 1, 12288);
        mqttResponseSuccess = true;

        // Check if this is an animation (new firmware detects extra fields)
        if (doc.containsKey("animation") && doc["animation"].as<bool>()) {
            currentAnimationId = doc["animationId"] | -1;
            animFrameCount = doc["totalFrames"] | 0;
            animFps = doc["fps"] | 10;
            animFramesReceived = 0;
            animReady = false;
            animPlaying = false;
            animCurrentFrame = 0;
            LOGF("[MQTT] Animation detected: id=%d, frames=%d, fps=%d", currentAnimationId, animFrameCount, animFps);
        } else {
            // Regular photo: stop any playing animation
            animPlaying = false;
            animReady = false;
            currentAnimationId = -1;
        }

        LOGF("[MQTT] Photo received (reqId=%d): %s by %s", mqttRequestId, photoTitle, photoAuthor);
    } else {
        LOGF("[MQTT] Foto con formato incorrecto: length=%d, jsonEnd=%d", length, jsonEnd);
        mqttResponseSuccess = false;
    }
    mqttResponseReceived = true;
    mqttResponseType = "photo";
}

void handleOtaResponse(byte* payload, unsigned int length) {
    if (length > 0 && length < sizeof(httpBuffer) - 1) {
        memcpy(httpBuffer, payload, length);
        httpBuffer[length] = '\0';
        mqttResponseSuccess = true;
        LOG("[MQTT] Respuesta OTA recibida");
    } else {
        mqttResponseSuccess = false;
    }
    mqttResponseReceived = true;
    mqttResponseType = "ota";
}

void handleConfigResponse(byte* payload, unsigned int length) {
    if (length > 0 && length < sizeof(httpBuffer) - 1) {
        memcpy(httpBuffer, payload, length);
        httpBuffer[length] = '\0';

        JsonDocument doc;
        if (deserializeJson(doc, httpBuffer) == DeserializationError::Ok) {
            if (doc.containsKey("brightness")) {
                brightness = doc["brightness"];
                if (startupBrightnessRampDone) {
                    dma_display->setBrightness(max(brightness, 10));
                }
                preferences.putInt("brightness", brightness);
                LOGF("[MQTT] Config brightness: %d", brightness);
            }
            if (doc.containsKey("pictures_on_queue")) {
                maxPhotos = doc["pictures_on_queue"];
                preferences.putInt("maxPhotos", maxPhotos);
                LOGF("[MQTT] Config max photos: %d", maxPhotos);
            }
            if (doc.containsKey("spotify_enabled")) {
                allowSpotify = doc["spotify_enabled"];
                preferences.putBool("allowSpotify", allowSpotify);
                LOGF("[MQTT] Config spotify: %s", allowSpotify ? "true" : "false");
            }
            if (doc.containsKey("secs_between_photos")) {
                int secsBetweenPhotos = doc["secs_between_photos"];
                secsPhotos = secsBetweenPhotos * 1000;
                preferences.putUInt("secsPhotos", secsPhotos);
                LOGF("[MQTT] Config secs between photos: %d", secsBetweenPhotos);
            }
            if (doc.containsKey("schedule_enabled")) {
                scheduleEnabled = doc["schedule_enabled"];
                preferences.putBool("scheduleEnabled", scheduleEnabled);
                LOGF("[MQTT] Config schedule enabled: %s", scheduleEnabled ? "true" : "false");
            }
            if (doc.containsKey("schedule_on_hour")) {
                scheduleOnHour = doc["schedule_on_hour"];
                preferences.putInt("scheduleOnHour", scheduleOnHour);
                LOGF("[MQTT] Config schedule on hour: %d", scheduleOnHour);
            }
            if (doc.containsKey("schedule_on_minute")) {
                scheduleOnMinute = doc["schedule_on_minute"];
                preferences.putInt("scheduleOnMinute", scheduleOnMinute);
                LOGF("[MQTT] Config schedule on minute: %d", scheduleOnMinute);
            }
            if (doc.containsKey("schedule_off_hour")) {
                scheduleOffHour = doc["schedule_off_hour"];
                preferences.putInt("scheduleOffHour", scheduleOffHour);
                LOGF("[MQTT] Config schedule off hour: %d", scheduleOffHour);
            }
            if (doc.containsKey("schedule_off_minute")) {
                scheduleOffMinute = doc["schedule_off_minute"];
                preferences.putInt("scheduleOffMinute", scheduleOffMinute);
                LOGF("[MQTT] Config schedule off minute: %d", scheduleOffMinute);
            }
            if (doc.containsKey("timezone_offset")) {
                timezoneOffset = doc["timezone_offset"];
                preferences.putInt("timezoneOffset", timezoneOffset);
                LOGF("[MQTT] Config timezone offset: %d", timezoneOffset);
            }
            if (doc.containsKey("clock_enabled")) {
                clockEnabled = doc["clock_enabled"];
                preferences.putBool("clockEnabled", clockEnabled);
                LOGF("[MQTT] Config clock enabled: %s", clockEnabled ? "true" : "false");
            }
            if (doc.containsKey("has_owner")) {
                bool hasOwner = doc["has_owner"];
                LOGF("[MQTT] Config has_owner: %s", hasOwner ? "true" : "false");
                if (!hasOwner) enterWaitingForOwnerMode();
                else exitWaitingForOwnerMode();
            }
            mqttResponseSuccess = true;
            LOG("[MQTT] Configuración recibida correctamente");
        } else {
            LOG("[MQTT] Error parseando config JSON");
            mqttResponseSuccess = false;
        }
    } else {
        mqttResponseSuccess = false;
    }
    mqttResponseReceived = true;
    mqttResponseType = "config";
}

void handleAnimationFrameResponse(byte* payload, unsigned int length) {
    if (length < 4 + ANIM_FRAME_SIZE || !animBuffer) {
        LOGF("[MQTT:anim] Invalid frame (size=%d, buffer=%s)", length, animBuffer ? "ok" : "null");
        return;
    }

    uint8_t frameIndex = payload[0];
    uint8_t totalFrames = payload[1];

    if (frameIndex >= MAX_ANIM_FRAMES || frameIndex >= totalFrames) {
        LOGF("[MQTT:anim] Frame index out of range: %d/%d", frameIndex, totalFrames);
        return;
    }

    memcpy(animBuffer + frameIndex * ANIM_FRAME_SIZE, payload + 4, ANIM_FRAME_SIZE);
    animFramesReceived++;
    LOGF("[MQTT:anim] Frame %d/%d received (%d/%d total)", frameIndex, totalFrames, animFramesReceived, animFrameCount);

    if (animFramesReceived >= animFrameCount) {
        animReady = true;
        animPlaying = true;
        animCurrentFrame = 0;
        animLastFrameTime = millis();
        LOG("[MQTT:anim] All frames received, starting playback");
    } else {
        // Request next frame
        requestAnimationFrame(currentAnimationId, frameIndex + 1);
    }
}

void requestAnimationFrame(int animationId, int frameIndex) {
    char topic[64];
    snprintf(topic, sizeof(topic), "frame/%d/request/animation/frame", frameId);
    char payload[64];
    snprintf(payload, sizeof(payload), "{\"animationId\":%d,\"frame\":%d}", animationId, frameIndex);
    mqttClient.publish(topic, payload);
    LOGF("[MQTT:anim] Requesting frame %d of animation %d", frameIndex, animationId);
}

void handleRegisterResponse(byte* payload, unsigned int length) {
    mqttRegisterFrameId = 0;

    if (length > 0 && length < sizeof(httpBuffer)) {
        memcpy(httpBuffer, payload, length);
        httpBuffer[length] = '\0';

        LOGF("[MQTT:register] Respuesta recibida: %s", httpBuffer);

        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, httpBuffer);
        if (!error) {
            mqttRegisterFrameId = doc["frameId"] | doc["pixieId"] | 0;
            LOGF("[MQTT:register] frameId=%d", mqttRegisterFrameId);

            // Store per-device MQTT token if provided
            const char* token = doc["deviceToken"] | (const char*)nullptr;
            if (token && strlen(token) > 0) {
                mqttToken = String(token);
                preferences.putString("mqttToken", mqttToken);
                LOGF("[MQTT:register] Device token stored (%d chars)", mqttToken.length());
            }

            mqttResponseSuccess = (mqttRegisterFrameId > 0);
        } else {
            LOG("[MQTT:register] Error parseando JSON");
            mqttResponseSuccess = false;
        }
    } else {
        mqttResponseSuccess = false;
    }
    mqttResponseReceived = true;
    mqttResponseType = "register";
}

void requestConfig()
{
    LOG("[Config] Solicitando configuración via MQTT...");

    // Publicar request via MQTT
    String topic = String("frame/") + String(frameId) + "/request/config";
    if (!mqttClient.publish(topic.c_str(), "{}")) {
        LOG("[Config] Error publicando request MQTT");
        return;
    }

    // Esperar respuesta
    if (waitForMqttResponse("config", 10000)) {
        LOG("[Config] Configuración aplicada correctamente");
    } else {
        LOG("[Config] Error recibiendo configuración via MQTT");
    }
}

bool registerFrameViaMQTT()
{
    String macAddress = WiFi.macAddress();
    LOGF("[MQTT:register] Registrando frame con MAC: %s", macAddress.c_str());

    // Client ID temporal basado en MAC
    String tempClientId = "frame-mac-" + macAddress;
    tempClientId.replace(":", "");

    // Configurar MQTT
    mqttClient.setServer(MQTT_BROKER_URL, MQTT_BROKER_PORT);
    mqttClient.setCallback(mqttCallback);
    mqttClient.setKeepAlive(60);

    // Conectar con client ID temporal usando register credentials
    LOGF("[MQTT:register] Conectando con client ID: %s", tempClientId.c_str());
    LOGF("[MQTT:register] Free heap: %d bytes, largest block: %d bytes", ESP.getFreeHeap(), ESP.getMaxAllocHeap());
    if (!mqttClient.connect(tempClientId.c_str(), MQTT_REGISTER_USERNAME, MQTT_REGISTER_PASSWORD)) {
        LOGF("[MQTT:register] Error conectando: %d", mqttClient.state());
        return false;
    }
    LOG("[MQTT:register] Conectado a MQTT");

    // Suscribirse al topic de respuesta
    String responseTopic = "frame/mac/" + macAddress + "/response/register";
    LOGF("[MQTT:register] Suscribiendo a: %s", responseTopic.c_str());
    if (!mqttClient.subscribe(responseTopic.c_str())) {
        LOG("[MQTT:register] Error suscribiendo");
        mqttClient.disconnect();
        return false;
    }

    // Publicar request de registro
    String requestTopic = "frame/mac/" + macAddress + "/request/register";
    LOGF("[MQTT:register] Publicando en: %s", requestTopic.c_str());
    if (!mqttClient.publish(requestTopic.c_str(), "{}")) {
        LOG("[MQTT:register] Error publicando request");
        mqttClient.disconnect();
        return false;
    }

    // Esperar respuesta
    if (waitForMqttResponse("register", 10000)) {
        if (mqttRegisterFrameId > 0) {
            frameId = mqttRegisterFrameId;
            preferences.putInt("frameId", frameId);
            LOGF("[MQTT:register] Frame registrado: ID=%d", frameId);

            // Desconectar para reconectar luego con el client ID correcto
            mqttClient.disconnect();
            return true;
        }
    }

    LOG("[MQTT:register] Timeout o error en registro");
    mqttClient.disconnect();
    return false;
}

void testInit()
{
    preferences.begin("wifi", false);
    preferences.clear();
    preferences.end();
    preferences.begin("wifi", false);
    preferences.putInt("currentVersion", 0);
    preferences.putInt("frameId", 0);
    preferences.putInt("brightness", 50);
    preferences.putInt("maxPhotos", 5);
    preferences.putUInt("secsPhotos", 30000);
    preferences.putString("ssid", "");
    preferences.putString("password", "");
    preferences.putBool("allowSpotify", false);
    LOG("Preferencias reiniciadas a valores de fábrica");
}
