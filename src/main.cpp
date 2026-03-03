#include "globals.h"
#include "config.h"
#include "ble_config.h"
#include <ArduinoOTA.h>

// Modules
#include "schedule.h"
#include "drawing.h"
#include "display.h"
#include "clock.h"
#include "ble_provisioning.h"
#include "ota.h"
#include "mqtt_handlers.h"
#include "photos.h"
#include "spotify.h"
#include "mqtt_client.h"

void setup()
{
    Serial.begin(115200);

    LOG("==========================================");
    #ifdef DEV_MODE
    LOG("           DEV_MODE BUILD               ");
    LOG("- OTA updates: DISABLED");
    #else
    LOG("           RELEASE BUILD                ");
    LOG("- OTA updates: ENABLED");
    #endif
    LOGF("- MQTT broker: %s:%d", MQTT_BROKER_URL, MQTT_BROKER_PORT);
    LOG("==========================================");

    // Configuración del panel
    HUB75_I2S_CFG mxconfig(PANEL_RES_X, PANEL_RES_Y, PANEL_CHAIN);
    mxconfig.gpio.e = E_PIN;
    mxconfig.clkphase = false;
    mxconfig.i2sspeed = HUB75_I2S_CFG::HZ_20M;
    mxconfig.min_refresh_rate = 200;
    dma_display = new MatrixPanel_I2S_DMA(mxconfig);
    dma_display->begin();
    dma_display->setBrightness8(1);
    dma_display->clearScreen();
    dma_display->setRotation(135);

    // Inicializar Preferences para leer/guardar las credenciales
    preferences.begin("wifi", false);
    String storedSSID = preferences.getString("ssid", "");
    String storedPassword = preferences.getString("password", "");
    allowSpotify = preferences.getBool("allowSpotify", true);
    maxPhotos = preferences.getInt("maxPhotos", 5);
    currentVersion = preferences.getInt("currentVersion", 0);
    frameId = preferences.getInt("frameId", 0);
    if (frameId == 0) {
        // Backward compatibility: read old NVS key
        frameId = preferences.getInt("pixieId", 0);
        if (frameId > 0) {
            preferences.putInt("frameId", frameId);
            preferences.remove("pixieId");
        }
    }
    secsPhotos = preferences.getUInt("secsPhotos", 30000);
    // Cargar configuración de horario
    scheduleEnabled = preferences.getBool("scheduleEnabled", false);
    scheduleOnHour = preferences.getInt("scheduleOnHour", 8);
    scheduleOnMinute = preferences.getInt("scheduleOnMinute", 0);
    scheduleOffHour = preferences.getInt("scheduleOffHour", 22);
    scheduleOffMinute = preferences.getInt("scheduleOffMinute", 0);
    timezoneOffset = preferences.getInt("timezoneOffset", 0);
    clockEnabled = preferences.getBool("clockEnabled", false);
    mqttToken = preferences.getString("mqttToken", "");

    // Si no hay credenciales guardadas, iniciar modo BLE para provisioning
    if (storedSSID == "") {
        LOG("No WiFi credentials found. Starting BLE provisioning...");

        // Iniciar servidor BLE
        setupBLE();

        // Mostrar mensaje de espera en pantalla
        dma_display->clearScreen();
        dma_display->fillScreen(myWHITE);
        drawLogo();
        showLoadingMsg("Waiting BLE...");

        // Esperar credenciales via BLE
        while (!processBLECredentials()) {
            esp_task_wdt_reset();
            delay(100);
        }

        // Si llegamos aquí, el WiFi está conectado via BLE
        LOG("WiFi conectado via BLE provisioning");
        showLoadingMsg("Connected!");
    } else {
        // Hay credenciales guardadas, intentar conectar a WiFi
        LOG("Conectando a WiFi...");
        WiFi.mode(WIFI_STA);
        WiFi.begin(storedSSID.c_str(), storedPassword.c_str());

        unsigned long startAttemptTime = millis();
        dma_display->clearScreen();
        dma_display->fillScreen(myWHITE);
        drawLogo();

        while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 30000) {
            showLoadingMsg("Connecting WiFi");
            delay(100);
        }

        if (WiFi.status() != WL_CONNECTED) {
            LOG("Failed to connect to WiFi. Starting BLE provisioning + WiFi retry mode...");

            // Iniciar servidor BLE
            setupBLE();
            showLoadingMsg("Waiting BLE...");

            // Variables para reintentar WiFi periódicamente
            unsigned long lastWiFiAttempt = 0;
            const unsigned long WIFI_RETRY_INTERVAL = 180000; // 3 minutos

            // Esperar credenciales via BLE o reconexión WiFi
            while (true) {
                esp_task_wdt_reset();

                // Procesar BLE (no bloqueante)
                if (processBLECredentialsNonBlocking()) {
                    break;
                }

                // Intentar WiFi con credenciales existentes cada 3 minutos
                if (storedSSID != "" && millis() - lastWiFiAttempt >= WIFI_RETRY_INTERVAL) {
                    lastWiFiAttempt = millis();
                    LOG("[WiFi] Intentando reconectar con credenciales guardadas...");

                    // Pausar BLE temporalmente
                    NimBLEDevice::stopAdvertising();

                    WiFi.begin(storedSSID.c_str(), storedPassword.c_str());
                    unsigned long start = millis();
                    while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
                        esp_task_wdt_reset();
                        delay(100);
                    }

                    if (WiFi.status() == WL_CONNECTED) {
                        LOG("[WiFi] Reconectado!");
                        NimBLEDevice::deinit(true);
                        break;
                    }

                    // Reanudar BLE
                    NimBLEDevice::startAdvertising();
                    LOG("[WiFi] Fallo. Continuando en modo BLE...");
                }

                delay(100);
            }

            LOG("WiFi conectado (via BLE o reconexión)");
            showLoadingMsg("Connected!");
        } else {
            LOG("WiFi connected OK");
            showLoadingMsg("Connected to WiFi");
        }
    }

    // Configuración de MQTT
    mqttClient.setServer(MQTT_BROKER_URL, MQTT_BROKER_PORT);
    mqttClient.setCallback(mqttCallback);
    mqttClient.setKeepAlive(60);
    mqttClient.setBufferSize(16384);

    // Configuración de OTA
    ArduinoOTA.setHostname(("Frame-" + String(frameId)).c_str());
    ArduinoOTA.onStart([]() {
        String type;
        if (ArduinoOTA.getCommand() == U_FLASH)
            type = "sketch";
        else
            type = "filesystem";
        LOGF("Inicio de actualización OTA: %s", type.c_str());
        dma_display->clearScreen();
        showPercetage(0);
    });
    ArduinoOTA.onEnd([]() {
        LOG("Actualización OTA completada.");
    });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        LOGF("Progreso OTA: %u%%", (progress / (total / 100)));
        showPercetage((progress / (total / 100)));
    });
    ArduinoOTA.onError([](ota_error_t error) {
        const char* errorStr;
        if (error == OTA_AUTH_ERROR)
            errorStr = "AUTH_ERROR: Fallo de autenticación";
        else if (error == OTA_BEGIN_ERROR)
            errorStr = "BEGIN_ERROR: Error al iniciar actualización";
        else if (error == OTA_CONNECT_ERROR)
            errorStr = "CONNECT_ERROR: Error de conexión con cliente OTA";
        else if (error == OTA_RECEIVE_ERROR)
            errorStr = "RECEIVE_ERROR: Error al recibir datos de firmware";
        else if (error == OTA_END_ERROR)
            errorStr = "END_ERROR: Error al finalizar escritura de firmware";
        else
            errorStr = "UNKNOWN_ERROR";
        LOGF("[ArduinoOTA:onError] Código de error: %u - %s", error, errorStr);
    });
    ArduinoOTA.begin();

    timeClient.begin();
    timeClient.setTimeOffset(0);
    timeClient.update();

    // OTA migration: if device has frameId but no mqttToken, re-register to get credentials
    if (frameId > 0 && mqttToken.length() == 0) {
        LOGF("[Migration] Frame %d has no MQTT token - re-registering to obtain credentials", frameId);
        frameId = 0;
    }

    // Register frame if not registered (via MQTT)
    if (frameId == 0) {
        showLoadingMsg("Registering...");
        if (!registerFrameViaMQTT()) {
            LOG("Error registrando frame via MQTT, reintentando...");
            delay(2000);
            registerFrameViaMQTT();
        }
    }

    // Conectar a MQTT
    showLoadingMsg("Connecting server");
    mqttReconnect();

    // Solicitar configuración via MQTT (después de conectar)
    requestConfig();

    // If config indicated no owner, enter waiting mode and skip normal startup
    if (waitingForOwner) {
        LOG("[Startup] No owner detected - entering waiting-for-owner mode");
        // Check for updates before entering waiting mode
        #ifndef DEV_MODE
        checkForUpdates();
        #endif
        // Initialize WDT so the loop can reset it
        esp_task_wdt_init(WDT_TIMEOUT, true);
        esp_task_wdt_add(NULL);
        return;
    }

    // Check for updates on startup (after MQTT is connected)
    #ifndef DEV_MODE
    checkForUpdates();
    #else
    LOG("DEV_MODE active - skipping startup update check");
    #endif

    // Rampa gradual de brillo para evitar brownout por pico de corriente
    {
        int targetBrightness = max(brightness, 10);
        LOG("[Startup] Rampa de brillo...");
        for (int b = 1; b <= targetBrightness; b++) {
            dma_display->setBrightness8(b);
            delay(5);
        }
        startupBrightnessRampDone = true;
        LOGF("[Startup] Brillo objetivo alcanzado: %d", targetBrightness);
    }

    // Inicializar Watchdog Timer
    esp_task_wdt_init(WDT_TIMEOUT, true);
    esp_task_wdt_add(NULL);
    LOG("Watchdog timer inicializado");

    showLoadingMsg("Ready!");
    delay(500);

    // Comprobar si hay música sonando antes de mostrar la primera foto
    if (allowSpotify) {
        LOG("[Startup] Comprobando si hay música sonando...");
        String initialSong = fetchSongId();
        if (initialSong != "" && initialSong != "null") {
            LOG("[Startup] Música detectada - mostrando portada");
            songShowing = initialSong;
            fetchAndDrawCover();
        } else {
            LOG("[Startup] No hay música - mostrando primera foto");
            showPhotoIndex(0);
            photoIndex = 1;
            lastPhotoChange = millis();
        }
    } else {
        LOG("[Startup] Spotify deshabilitado - mostrando primera foto");
        showPhotoIndex(0);
        photoIndex = 1;
        lastPhotoChange = millis();
    }
}

void loop()
{
    esp_task_wdt_reset();

    // Manejo de MQTT
    if (!mqttClient.connected()) {
        mqttReconnect();
    }else{
        mqttClient.loop();
    }

    // If waiting for owner, handle BLE and skip normal operation
    if (waitingForOwner) {
        if (bleCredentialsReceived && WiFi.status() == WL_CONNECTED) {
            processBLECredentialsAlreadyConnected();
        }
        ArduinoOTA.handle();
        delay(100);
        return;
    }

    // Verificar y actualizar estado de la pantalla según el horario
    updateScreenPower();

    // Si la pantalla está apagada, solo procesar OTA y MQTT
    if (screenOff) {
        ArduinoOTA.handle();
        delay(100);
        return;
    }

    // Verificar timeout del modo dibujo
    checkDrawingTimeout();

    // Si estamos en modo dibujo, no ejecutar la lógica de fotos
    if (drawingMode) {
        // Procesar buffer de comandos si ha pasado suficiente tiempo
        if (millis() - lastDrawingUpdate >= DRAWING_UPDATE_INTERVAL) {
            processDrawingBuffer();
        }

        ArduinoOTA.handle();
        delay(10);
        return;
    }

    // Si estamos conectados a WiFi, se ejecuta la lógica original:
    if (allowSpotify) {
        // Solo llamar a fetchSongId si el scroll no está activo (evita bloquear el scroll)
        bool scrollActive = titleNeedsScroll && (titleScrollState == SCROLL_SCROLLING || titleScrollState == SCROLL_RETURNING);
        if (millis() - lastSpotifyCheck >= timeToCheckSpotify && !scrollActive) {
            songOnline = fetchSongId();
            lastSpotifyCheck = millis();
        }
        if (songOnline == "" || songOnline == "null") {
            // Si antes había canción y ahora no, mostrar foto inmediatamente
            if (songShowing != "") {
                songShowing = "";
                lastPhotoChange = 0;
            }
            if (millis() - lastPhotoChange >= secsPhotos) {
                if (photoIndex >= maxPhotos) {
                    photoIndex = 0;
                }
                LOGF("[Photo] Mostrando foto %d/%d", photoIndex, maxPhotos);
                showPhotoIndex(photoIndex);
                photoIndex++;
                lastPhotoChange = millis();
            }
        } else {
            if (songShowing != songOnline) {
                songShowing = songOnline;
                fetchAndDrawCover();
            }
        }
    } else {
        if (millis() - lastPhotoChange >= secsPhotos) {
            if (photoIndex >= maxPhotos) {
                photoIndex = 0;
            }
            LOGF("[Photo] Mostrando foto %d/%d", photoIndex, maxPhotos);
            showPhotoIndex(photoIndex);
            photoIndex++;
            lastPhotoChange = millis();
        }
    }

    // Actualizar el scroll del título si es necesario
    updatePhotoInfo();

    // Actualizar reloj cada 60 segundos
    if (clockEnabled && millis() - lastClockUpdate >= 60000) {
        showClockOverlay();
        lastClockUpdate = millis();
    }

    wait(100);
}
