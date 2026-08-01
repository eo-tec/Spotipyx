#include "globals.h"
#include "config.h"
#include "ble_config.h"
#include <ArduinoOTA.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

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
#include <esp_ota_ops.h>

// Auto-rollback OTA
#define OTA_MAX_BOOT_ATTEMPTS 3       // reinicios sin validar antes de revertir
#define OTA_VALIDATE_AFTER_MS 60000UL // uptime estable (ms) para dar la version por buena

void setup()
{
#ifdef HW_V2
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); // Disable brownout (v2 PSU is marginal)
#endif
    Serial.begin(115200);
    delay(3000); // Wait for USB-CDC enumeration so early logs are visible

    LOGF("Reset reason: %d", (int)esp_reset_reason());
    Serial.flush();

    LOG("==========================================");
    #ifdef DEV_MODE
    LOG("           DEV_MODE BUILD               ");
    LOG("- OTA updates: DISABLED");
    #else
    LOG("           RELEASE BUILD                ");
    LOG("- OTA updates: ENABLED");
    #endif
    LOGF("- MQTT broker: %s:%d", MQTT_BROKER_URL, MQTT_BROKER_PORT);
    LOGF("- Free heap: %d bytes", ESP.getFreeHeap());
    hasPsram = psramFound();
    if (hasPsram) {
        animFrameWidth = 64;
        animFrameSize = ANIM_FRAME_SIZE_64;
    } else {
        animFrameWidth = 32;
        animFrameSize = ANIM_FRAME_SIZE_32;
    }
    LOGF("- PSRAM: %s → animation frames: %dx%d (%d bytes/frame)",
         hasPsram ? "YES" : "NO", animFrameWidth, animFrameWidth, animFrameSize);
    LOG("==========================================");

    // Configuración del panel
    HUB75_I2S_CFG mxconfig(PANEL_RES_X, PANEL_RES_Y, PANEL_CHAIN);
#ifdef HW_V2
    mxconfig.gpio.r1 = R1_PIN;
    mxconfig.gpio.g1 = G1_PIN;
    mxconfig.gpio.b1 = B1_PIN;
    mxconfig.gpio.r2 = R2_PIN;
    mxconfig.gpio.g2 = G2_PIN;
    mxconfig.gpio.b2 = B2_PIN;
    mxconfig.gpio.a = A_PIN;
    mxconfig.gpio.b = B_PIN;
    mxconfig.gpio.c = C_PIN;
    mxconfig.gpio.d = D_PIN;
    mxconfig.gpio.e = E_PIN;
    mxconfig.gpio.clk = CLK_PIN;
    mxconfig.gpio.lat = LAT_PIN;
    mxconfig.gpio.oe = OE_PIN;
#else
    mxconfig.gpio.e = E_PIN;
#endif
    mxconfig.clkphase = false;
#ifdef HW_V2
    // Medido en v2: a 20 MHz el driver alcanza 137 Hz usando lsbMsbTransitionBit=1,
    // frente a los 100 Hz con bit=2 de la config anterior (8 MHz / 60). Mejora a la
    // vez el refresco (menos parpadeo en camara) y la profundidad de color
    // percibida (menos banding en fotos), porque cada bit de transicion que se
    // ahorra es gradacion que se conserva.
    mxconfig.i2sspeed = HUB75_I2S_CFG::HZ_20M;
    mxconfig.min_refresh_rate = 120;
#else
    mxconfig.i2sspeed = HUB75_I2S_CFG::HZ_20M;
    mxconfig.min_refresh_rate = 200;
#endif
    dma_display = new MatrixPanel_I2S_DMA(mxconfig);

    auto initPanel = [&]() {
        LOG("HUB75 begin()...");
        Serial.flush();
        dma_display->begin();
        dma_display->setBrightness8(1);
        dma_display->clearScreen();
        dma_display->setRotation(135);
        LOG("HUB75 ready");
        Serial.flush();
    };

    // Inicializar Preferences para leer/guardar las credenciales
    preferences.begin("wifi", false);
    LOG("Preferences begin ok");
    Serial.flush();
    String storedSSID = preferences.getString("ssid", "");
    String storedPassword = preferences.getString("password", "");
    allowSpotify = preferences.getBool("allowSpotify", true);
    maxPhotos = preferences.getInt("maxPhotos", 5);
    currentVersion = preferences.getInt("currentVersion", 0);

    // --- Auto-rollback OTA --------------------------------------------------
    // Si venimos de instalar una version nueva (pendingVer>0) contamos arranques.
    // Si entra en boot-loop (varios reinicios sin validar) volvemos al slot OTA
    // anterior, que conserva la ultima version buena. La validacion (marcar la
    // version como buena) se hace en loop() tras un arranque estable.
    otaPendingVersion = preferences.getInt("pendingVer", 0);
    {
        int lastGoodVer = preferences.getInt("lastGoodVer", 0);
        if (otaPendingVersion > 0) {
            int bootCount = preferences.getInt("bootCount", 0) + 1;
            preferences.putInt("bootCount", bootCount); // persistir YA, antes de nada que pueda crashear
            LOGF("[Rollback] Version a prueba %d, arranque %d/%d", otaPendingVersion, bootCount, OTA_MAX_BOOT_ATTEMPTS);
            if (bootCount > OTA_MAX_BOOT_ATTEMPTS && lastGoodVer > 0) {
                const esp_partition_t *prev = esp_ota_get_next_update_partition(NULL);
                if (prev && esp_ota_set_boot_partition(prev) == ESP_OK) {
                    LOGF("[Rollback] Boot-loop detectado: revirtiendo de v%d a v%d", otaPendingVersion, lastGoodVer);
                    currentVersion = lastGoodVer;
                    preferences.putInt("currentVersion", lastGoodVer);
                    preferences.putInt("pendingVer", 0);
                    preferences.putInt("bootCount", 0);
                    Serial.flush();
                    ESP.restart();
                } else {
                    LOG("[Rollback] No se pudo cambiar la particion de arranque; continuando");
                }
            }
        } else if (lastGoodVer < currentVersion) {
            // Arranque normal: sembrar la ultima version buena conocida
            preferences.putInt("lastGoodVer", currentVersion);
        }
    }
    // ------------------------------------------------------------------------

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
    scheduleEnabled = preferences.getBool("schEnabled", false);
    scheduleOnHour = preferences.getInt("scheduleOnHour", 8);
    scheduleOnMinute = preferences.getInt("schOnMin", 0);
    scheduleOffHour = preferences.getInt("scheduleOffHour", 22);
    scheduleOffMinute = preferences.getInt("schOffMin", 0);
    timezoneOffset = preferences.getInt("timezoneOffset", 0);
    clockEnabled = preferences.getBool("clockEnabled", false);
    mqttToken = preferences.getString("mqttToken", "");

    // Si no hay credenciales guardadas, iniciar modo BLE para provisioning
    if (storedSSID == "") {
        LOG("No WiFi credentials found. Starting BLE provisioning...");

        // Iniciar servidor BLE ANTES de HUB75 para evitar interrupt WDT
        setupBLE();

        // Ahora sí, inicializar el panel
        initPanel();

        // Mostrar mensaje de espera en pantalla
        dma_display->clearScreen();
        dma_display->fillScreen(myWHITE);
        drawLogo();
        showLoadingMsg("Waiting BLE...");

        // Esperar credenciales via BLE
        while (!processBLECredentials()) {
            esp_task_wdt_reset();
            processBLENetworkScan();
            delay(100);
        }

        // Si llegamos aquí, el WiFi está conectado via BLE
        LOG("WiFi conectado via BLE provisioning");
        showLoadingMsg("Connected!");
    } else {
        // Hay credenciales guardadas, inicializar panel y conectar a WiFi
        initPanel();

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

                processBLENetworkScan();

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
    mqttClient.setBufferSize(13000); // fits photo response (~12.5KB) and anim frames (8.2KB)
    // PubSubClient espera bloqueando DENTRO de loop() a que llegue el resto de un
    // paquete fragmentado por TCP. Con el default de 15 s, una rafaga de comandos
    // de dibujo congelaba el loop entero varios segundos y se perdian hasta el
    // 65% de los mensajes (medido: bloqueo maximo de 15,01 s, exactamente el
    // default). Con 2 s se reciben el 100%.
    mqttClient.setSocketTimeout(2);

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
        processBLENetworkScan();
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
        // y si NO estamos cargando una animación (fetchSongId bloquea el hilo hasta 5s
        // esperando respuesta y congelaría la descarga de frames).
        bool scrollActive = titleNeedsScroll && (titleScrollState == SCROLL_SCROLLING || titleScrollState == SCROLL_RETURNING);
        bool animDownloading = (currentAnimationId > 0 && !animReady);
        if (millis() - lastSpotifyCheck >= timeToCheckSpotify && !scrollActive && !animDownloading) {
            songOnline = fetchSongId();
            lastSpotifyCheck = millis();
        }
        if (songOnline == "" || songOnline == "null") {
            // Si antes había canción y ahora no, mostrar foto inmediatamente
            if (songShowing != "") {
                songShowing = "";
                lastPhotoChange = 0;
            }
            // Don't change photo while animation is downloading or playing
            bool animBusy = (currentAnimationId > 0) || animPlaying;
            if (!animBusy && millis() - lastPhotoChange >= secsPhotos) {
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
                stopAnimation();
                fetchAndDrawCover();
            }
        }
    } else {
        bool animBusy = (currentAnimationId > 0) || animPlaying;
        if (!animBusy && millis() - lastPhotoChange >= secsPhotos) {
            if (photoIndex >= maxPhotos) {
                photoIndex = 0;
            }
            LOGF("[Photo] Mostrando foto %d/%d", photoIndex, maxPhotos);
            showPhotoIndex(photoIndex);
            photoIndex++;
            lastPhotoChange = millis();
        }
    }

    // Actualizar el scroll del título si es necesario (solo si no hay animación
    // reproduciéndose)
    if (!animPlaying) {
        updatePhotoInfo();
    }

    // Si la descarga de la animación terminó, sustituir la foto anterior y arrancar
    startAnimationPlaybackIfReady();

    // Reproducir animación frame a frame
    updateAnimationPlayback();

    // Reintentar frames perdidos durante la descarga pipelined
    checkAnimationDownloadTimeout();

    // Actualizar reloj cada 60 segundos
    if (clockEnabled && millis() - lastClockUpdate >= 60000) {
        showClockOverlay();
        lastClockUpdate = millis();
    }

    // Validar la version a prueba tras un arranque estable (uptime suficiente sin
    // reset/crash). Cancela el rollback: esta version pasa a ser la "ultima buena".
    if (otaPendingVersion > 0 && millis() > OTA_VALIDATE_AFTER_MS) {
        preferences.putInt("lastGoodVer", otaPendingVersion);
        preferences.putInt("pendingVer", 0);
        preferences.putInt("bootCount", 0);
        esp_ota_mark_app_valid_cancel_rollback(); // inofensivo si el rollback de bootloader esta off
        LOGF("[Rollback] Version %d validada como estable", otaPendingVersion);
        otaPendingVersion = 0;
    }

    // Durante la descarga de una animación iteramos rápido para drenar los frames
    // MQTT cuanto antes; durante la reproducción, para que sea fluida
    bool animActive = animPlaying || (currentAnimationId > 0 && !animReady);
    wait(animActive ? 5 : 100);
}
