#include "mqtt_client.h"
#include "config.h"
#include "display.h"
#include "drawing.h"
#include "photos.h"
#include "ota.h"
#include "mqtt_handlers.h"

void mqttCallback(char *topic, byte *payload, unsigned int length)
{
    String topicStr = String(topic);

    // Manejar respuestas del patrón request/response
    if (topicStr.indexOf("/response/") != -1) {
        if (topicStr.endsWith("/response/song")) {
            handleSongResponse(payload, length);
        }
        else if (topicStr.endsWith("/response/cover")) {
            handleCoverResponse(payload, length);
        }
        else if (topicStr.endsWith("/response/photo")) {
            handlePhotoResponse(payload, length);
        }
        else if (topicStr.endsWith("/response/ota")) {
            handleOtaResponse(payload, length);
        }
        else if (topicStr.endsWith("/response/config")) {
            handleConfigResponse(payload, length);
        }
        else if (topicStr.endsWith("/response/register")) {
            handleRegisterResponse(payload, length);
        }
        return;  // No procesar como comando normal
    }

    // Crear un buffer para el mensaje (solo para comandos, no respuestas binarias)
    char message[length + 1];
    memcpy(message, payload, length);
    message[length] = '\0';

    // Imprimir el mensaje
    LOGF("Mensaje recibido en el topic: %s", topic);
    LOGF("Mensaje: %s", message);

    // Crear un documento JSON
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, message);

    if (!error)
    {
        const char *action = doc["action"];
        if (action)
        {
            if (strcmp(action, "update_photo") == 0)
            {
                LOG("Se recibio una nueva foto por MQTT");
                int id = doc["id"];
                onReceiveNewPic(id);
            }
            else if (strcmp(action, "update_info") == 0)
            {
                LOG("[MQTT] Recibida actualización de configuración");

                if (doc.containsKey("brightness")) {
                    brightness = doc["brightness"];
                    if (startupBrightnessRampDone) {
                        dma_display->setBrightness(max(brightness, 10));
                    }
                    preferences.putInt("brightness", brightness);
                    LOGF("[MQTT] Brightness: %d", brightness);
                }
                if (doc.containsKey("pictures_on_queue")) {
                    maxPhotos = doc["pictures_on_queue"];
                    preferences.putInt("maxPhotos", maxPhotos);
                    LOGF("[MQTT] Max photos: %d", maxPhotos);
                }
                if (doc.containsKey("spotify_enabled")) {
                    allowSpotify = doc["spotify_enabled"];
                    preferences.putBool("allowSpotify", allowSpotify);
                    LOGF("[MQTT] Spotify enabled: %s", allowSpotify ? "true" : "false");
                }
                if (doc.containsKey("secs_between_photos")) {
                    int secsBetweenPhotos = doc["secs_between_photos"];
                    secsPhotos = secsBetweenPhotos * 1000;
                    preferences.putUInt("secsPhotos", secsPhotos);
                    LOGF("[MQTT] Secs between photos: %d", secsBetweenPhotos);
                }
                if (doc.containsKey("schedule_enabled")) {
                    scheduleEnabled = doc["schedule_enabled"];
                    preferences.putBool("scheduleEnabled", scheduleEnabled);
                    LOGF("[MQTT] Schedule enabled: %s", scheduleEnabled ? "true" : "false");
                }
                if (doc.containsKey("schedule_on_hour")) {
                    scheduleOnHour = doc["schedule_on_hour"];
                    preferences.putInt("scheduleOnHour", scheduleOnHour);
                    LOGF("[MQTT] Schedule on hour: %d", scheduleOnHour);
                }
                if (doc.containsKey("schedule_on_minute")) {
                    scheduleOnMinute = doc["schedule_on_minute"];
                    preferences.putInt("scheduleOnMinute", scheduleOnMinute);
                    LOGF("[MQTT] Schedule on minute: %d", scheduleOnMinute);
                }
                if (doc.containsKey("schedule_off_hour")) {
                    scheduleOffHour = doc["schedule_off_hour"];
                    preferences.putInt("scheduleOffHour", scheduleOffHour);
                    LOGF("[MQTT] Schedule off hour: %d", scheduleOffHour);
                }
                if (doc.containsKey("schedule_off_minute")) {
                    scheduleOffMinute = doc["schedule_off_minute"];
                    preferences.putInt("scheduleOffMinute", scheduleOffMinute);
                    LOGF("[MQTT] Schedule off minute: %d", scheduleOffMinute);
                }
                if (doc.containsKey("timezone_offset")) {
                    timezoneOffset = doc["timezone_offset"];
                    preferences.putInt("timezoneOffset", timezoneOffset);
                    LOGF("[MQTT] Timezone offset: %d", timezoneOffset);
                }
                if (doc.containsKey("clock_enabled")) {
                    clockEnabled = doc["clock_enabled"];
                    preferences.putBool("clockEnabled", clockEnabled);
                    LOGF("[MQTT] Clock enabled: %s", clockEnabled ? "true" : "false");
                }
            }
            else if (strcmp(action, "update_bin") == 0)
            {
                #ifdef DEV_MODE
                LOG("Update command received via MQTT - ignored in DEV_MODE");
                #else
                LOG("Se recibio una actualizacion de binario por MQTT");
                checkForUpdates();
                #endif
            }
            else if (strcmp(action, "factory_reset") == 0)
            {
                LOG("Factory reset recibido via MQTT");
                dma_display->clearScreen();
                showLoadingMsg("Factory Reset...");
                delay(2000);
                preferences.clear();
                ESP.restart();
            }
            else if (strcmp(action, "reset_env_vars") == 0)
            {
                LOG("Reset de variables de entorno recibido via MQTT");
                dma_display->clearScreen();
                showLoadingMsg("Resetting Env Vars...");
                delay(1000);

                // Resetear variables de entorno sin reiniciar el dispositivo
                preferences.begin("wifi", false);
                preferences.clear();
                preferences.end();

                // Reinicializar con valores por defecto
                preferences.begin("wifi", false);
                preferences.putInt("currentVersion", 0);
                preferences.putInt("frameId", 0);
                preferences.putInt("brightness", 50);
                preferences.putInt("maxPhotos", 5);
                preferences.putUInt("secsPhotos", 30000);
                preferences.putString("ssid", "");
                preferences.putString("password", "");
                preferences.putBool("allowSpotify", false);
                preferences.putString("mqttToken", "");
                preferences.end();

                // Resetear variables globales
                brightness = 50;
                maxPhotos = 5;
                secsPhotos = 30000;
                allowSpotify = false;
                currentVersion = 0;
                frameId = 0;
                mqttToken = "";
                scheduleEnabled = false;
                scheduleOnHour = 8;
                scheduleOnMinute = 0;
                scheduleOffHour = 22;
                scheduleOffMinute = 0;
                timezoneOffset = 0;
                screenOff = false;

                showLoadingMsg("Env Vars Reset!");
                delay(2000);
                dma_display->clearScreen();
            }
            // ===== HANDLERS DE DIBUJO =====
            else if (strcmp(action, "enter_draw_mode") == 0)
            {
                LOG("Entrando en modo dibujo via MQTT");
                enterDrawingMode();
            }
            else if (strcmp(action, "exit_draw_mode") == 0)
            {
                LOG("Saliendo del modo dibujo via MQTT");
                exitDrawingMode();
            }
            else if (strcmp(action, "draw_pixel") == 0)
            {
                // Auto-entrar en modo dibujo si no está activo
                if (!drawingMode) {
                    LOG("Auto-activando modo dibujo para draw_pixel");
                    enterDrawingMode();
                }

                int x = doc["x"];
                int y = doc["y"];
                const char* colorHex = doc["color"];
                int size = doc["size"] | 1;

                // Convert hex color to RGB565 (with BGR correction)
                uint16_t color = 0;
                if (colorHex && strlen(colorHex) == 7 && colorHex[0] == '#') {
                    uint32_t rgb = strtol(colorHex + 1, NULL, 16);
                    uint8_t r = (rgb >> 16) & 0xFF;
                    uint8_t g = (rgb >> 8) & 0xFF;
                    uint8_t b = rgb & 0xFF;
                    // Swap channels: RGB -> BGR
                    color = dma_display->color565(b, r, g);
                }
                addDrawCommand(x, y, color, size);
            }
            else if (strcmp(action, "draw_stroke") == 0)
            {
                // Auto-entrar en modo dibujo si no está activo
                if (!drawingMode) {
                    enterDrawingMode();
                }

                JsonArray points = doc["points"];
                uint16_t color = doc["color"];

                drawDrawingStroke(points, color);
            }
            else if (strcmp(action, "clear_canvas") == 0)
            {
                // Auto-entrar en modo dibujo si no está activo
                if (!drawingMode) {
                    LOG("Auto-activando modo dibujo para clear_canvas");
                    enterDrawingMode();
                }

                clearDrawingCanvas();
            }
            // ===== FIN HANDLERS DE DIBUJO =====
        }
    }
}

void mqttReconnect()
{
    int retryCount = 0;

    while (!mqttClient.connected() && retryCount < MQTT_MAX_RETRIES)
    {
        LOGF("[MQTT:mqttReconnect] Intentando conexión (intento %d/%d) a %s:%d...",
                      retryCount + 1, MQTT_MAX_RETRIES, MQTT_BROKER_URL, MQTT_BROKER_PORT);

        String clientId = String(MQTT_CLIENT_ID) + String(frameId);

        // Use per-device token if available, otherwise fallback to register credentials
        const char* mqttUser;
        const char* mqttPass;
        if (mqttToken.length() > 0) {
            String macClean = WiFi.macAddress();
            macClean.replace(":", "");
            // Use static buffers to avoid dangling pointers
            static char userBuf[32];
            static char passBuf[64];
            strncpy(userBuf, macClean.c_str(), sizeof(userBuf) - 1);
            userBuf[sizeof(userBuf) - 1] = '\0';
            strncpy(passBuf, mqttToken.c_str(), sizeof(passBuf) - 1);
            passBuf[sizeof(passBuf) - 1] = '\0';
            mqttUser = userBuf;
            mqttPass = passBuf;
            LOGF("[MQTT:mqttReconnect] Using device credentials (user=%s)", mqttUser);
        } else {
            // Sin token: conectar anónimo (allow_anonymous=true en broker)
            mqttUser = nullptr;
            mqttPass = nullptr;
            LOG("[MQTT:mqttReconnect] Connecting anonymous (no device token yet)");
        }

        bool connected = mqttUser
            ? mqttClient.connect(clientId.c_str(), mqttUser, mqttPass)
            : mqttClient.connect(clientId.c_str());
        if (connected)
        {
            LOG("[MQTT:mqttReconnect] Conectado exitosamente");

            // Suscribirse al topic principal de comandos
            String topic = String("frame/") + String(frameId);
            LOGF("[MQTT:mqttReconnect] Suscribiendo al tema: %s", topic.c_str());
            if (mqttClient.subscribe(topic.c_str())) {
                LOG("[MQTT:mqttReconnect] Suscripción exitosa");
            } else {
                LOG("[MQTT:mqttReconnect] Error: fallo en suscripción al tema");
            }

            // Suscribirse a topics de respuesta (para patrón request/response)
            String responseTopic = String("frame/") + String(frameId) + "/response/#";
            LOGF("[MQTT:mqttReconnect] Suscribiendo a respuestas: %s", responseTopic.c_str());
            if (mqttClient.subscribe(responseTopic.c_str())) {
                LOG("[MQTT:mqttReconnect] Suscripción a respuestas exitosa");
                loadingMsg = "";
            } else {
                LOG("[MQTT:mqttReconnect] Error: fallo en suscripción a respuestas");
            }

            return;
        }
        else
        {
            const char* stateStr;
            switch(mqttClient.state()) {
                case -4: stateStr = "CONNECTION_TIMEOUT"; break;
                case -3: stateStr = "CONNECTION_LOST"; break;
                case -2: stateStr = "CONNECT_FAILED"; break;
                case -1: stateStr = "DISCONNECTED"; break;
                case 0: stateStr = "CONNECTED"; break;
                case 1: stateStr = "BAD_PROTOCOL"; break;
                case 2: stateStr = "BAD_CLIENT_ID"; break;
                case 3: stateStr = "UNAVAILABLE"; break;
                case 4: stateStr = "BAD_CREDENTIALS"; break;
                case 5: stateStr = "UNAUTHORIZED"; break;
                default: stateStr = "UNKNOWN"; break;
            }
            LOGF("[MQTT:mqttReconnect] Error de conexión, código=%d (%s)", mqttClient.state(), stateStr);
            retryCount++;
            if (retryCount < MQTT_MAX_RETRIES) {
                LOGF("[MQTT:mqttReconnect] Reintentando en %d ms...", MQTT_RETRY_DELAY);
                wait(MQTT_RETRY_DELAY);
            }
        }
    }

    if (!mqttClient.connected()) {
        LOG("[MQTT:mqttReconnect] Error crítico: máximo de reintentos alcanzado - reiniciando ESP32");
        dma_display->clearScreen();
        showLoadingMsg("MQTT Error");
        wait(3000);
        ESP.restart();
    }
}
