#include "ble_provisioning.h"
#include "ble_config.h"
#include "display.h"
#include "messages.h"

// Callback para conexiones del servidor BLE
class FrameBLEServerCallbacks : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer* pServer) {
        bleDeviceConnected = true;
        LOG("[BLE] Cliente conectado");
    }

    void onDisconnect(NimBLEServer* pServer) {
        bleDeviceConnected = false;
        LOG("[BLE] Cliente desconectado");
        // Reiniciar advertising si no hay credenciales recibidas
        if (!bleCredentialsReceived) {
            NimBLEDevice::startAdvertising();
            LOG("[BLE] Reiniciando advertising");
        }
    }
};

// Callback para recibir credenciales WiFi via BLE
class WifiCredentialsCallback : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* pCharacteristic) {
        std::string value = pCharacteristic->getValue();
        if (value.length() > 0) {
            LOG("[BLE] Credenciales recibidas");

            // Los datos ya vienen en texto plano (la librería BLE del móvil decodifica base64)
            String decoded = String(value.c_str());
            LOGF("[BLE] Datos recibidos: %s", decoded.c_str());

            // Parsear SSID;PASSWORD
            int separatorIndex = decoded.indexOf(';');
            if (separatorIndex > 0) {
                bleReceivedSSID = decoded.substring(0, separatorIndex);
                bleReceivedPassword = decoded.substring(separatorIndex + 1);
                bleCredentialsReceived = true;

                LOGF("[BLE] SSID: %s", bleReceivedSSID.c_str());
                LOG("[BLE] Password recibida (oculta por seguridad)");
            } else {
                LOG("[BLE] Error: formato de credenciales inválido");
            }
        }
    }
};

// Callback para peticiones de scan de redes WiFi.
// Solo levanta un flag: escanear aquí bloquearía la tarea del stack BLE.
class NetworksCallback : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* pCharacteristic) {
        std::string value = pCharacteristic->getValue();
        if (String(value.c_str()) == "SCAN") {
            LOG("[BLE] Scan de redes solicitado por la app");
            bleNetworkScanRequested = true;
        }
    }
};

static void publishNetworksState(const String& json) {
    if (pNetworksChar == nullptr) return;
    pNetworksChar->setValue(std::string(json.c_str()));
    pNetworksChar->notify();
}

// Máquina de estados no bloqueante del scan de redes. Se llama desde los bucles
// donde el BLE está activo. El scan es asíncrono para no cortar la conexión BLE.
void processBLENetworkScan() {
    if (pNetworksChar == nullptr) return;

    static bool scanInFlight = false;

    if (bleNetworkScanRequested && !scanInFlight) {
        bleNetworkScanRequested = false;
        scanInFlight = true;
        publishNetworksState("{\"state\":\"scanning\"}");
        // No tocar el modo si ya hay WiFi conectado (caso wifiOnly): scanNetworks
        // funciona igual y evitamos tirar la conexión.
        if (WiFi.status() != WL_CONNECTED) {
            WiFi.mode(WIFI_STA);
        }
        WiFi.scanNetworks(true, false); // async, sin ocultas
        LOG("[BLE] Scan WiFi lanzado");
        return;
    }

    if (!scanInFlight) return;

    int n = WiFi.scanComplete();
    if (n == WIFI_SCAN_RUNNING) return;

    scanInFlight = false;

    if (n < 0) {
        LOG("[BLE] Scan WiFi fallido");
        publishNetworksState("{\"state\":\"error\"}");
        WiFi.scanDelete();
        return;
    }

    // Los resultados vienen ordenados por RSSI: al deduplicar por SSID nos
    // quedamos con la primera aparición, que es la de mejor señal.
    String json = "{\"state\":\"done\",\"networks\":[";
    int included = 0;
    for (int i = 0; i < n && included < BLE_SCAN_MAX_NETWORKS; i++) {
        String ssid = WiFi.SSID(i);
        if (ssid.length() == 0) continue; // red oculta

        bool duplicate = false;
        for (int j = 0; j < i; j++) {
            if (WiFi.SSID(j) == ssid) { duplicate = true; break; }
        }
        if (duplicate) continue;

        // Escapar comillas y barras para no romper el JSON
        String safeSsid;
        for (unsigned int c = 0; c < ssid.length(); c++) {
            char ch = ssid.charAt(c);
            if (ch == '"' || ch == '\\') safeSsid += '\\';
            safeSsid += ch;
        }

        String entry = String(included > 0 ? "," : "") + "{\"s\":\"" + safeSsid +
                       "\",\"r\":" + String(WiFi.RSSI(i)) +
                       ",\"e\":" + String(WiFi.encryptionType(i) != WIFI_AUTH_OPEN ? 1 : 0) + "}";

        // Cortar antes de pasarnos del tamaño que cabe en una lectura BLE
        if (json.length() + entry.length() + 2 > BLE_SCAN_MAX_PAYLOAD) {
            LOGF("[BLE] Lista truncada en %d redes por tamaño", included);
            break;
        }

        json += entry;
        included++;
    }
    json += "]}";

    WiFi.scanDelete();
    LOGF("[BLE] Scan completado: %d redes encontradas, %d enviadas (%d bytes)",
         n, included, json.length());
    publishNetworksState(json);
}

void sendBLEResponse(bool success, String frameToken, String error) {
    if (pResponseChar == nullptr) {
        LOG("[BLE] Error: característica de respuesta no inicializada");
        return;
    }

    JsonDocument doc;
    doc["success"] = success;
    if (frameToken.length() > 0) {
        doc["frameToken"] = frameToken;
    }
    if (error.length() > 0) {
        doc["error"] = error;
    }

    String jsonResponse;
    serializeJson(doc, jsonResponse);
    LOGF("[BLE] Respuesta JSON: %s", jsonResponse.c_str());

    // Usar std::string para asegurar que NimBLE copie los datos correctamente
    std::string responseStr(jsonResponse.c_str());
    pResponseChar->setValue(responseStr);

    // Pequeño delay para asegurar que el valor se escriba antes de notificar
    delay(100);

    pResponseChar->notify();

    // Esperar a que la notificación se procese
    delay(500);

    LOG("[BLE] Respuesta enviada");
}

void setupBLE() {
    LOG("[BLE] Inicializando servidor BLE...");
    Serial.flush();

    // Nombre con MAC para que la app identifique el frame en el scan sin conectar.
    // Formato: frame.AABBCCDDEEFF (MAC WiFi STA sin dos puntos).
    String mac = WiFi.macAddress();
    mac.replace(":", "");
    mac.toUpperCase();
    String bleName = String(BLE_DEVICE_NAME_PREFIX) + mac;

    NimBLEDevice::init(bleName.c_str());
    LOG("[BLE] NimBLEDevice::init done");
    Serial.flush();
#ifdef HW_V2
    NimBLEDevice::setPower(ESP_PWR_LVL_N12); // lowest TX power on v2 to reduce current
#else
    NimBLEDevice::setPower(ESP_PWR_LVL_P9);
#endif
    LOG("[BLE] setPower done");
    Serial.flush();

    pBLEServer = NimBLEDevice::createServer();
    LOG("[BLE] createServer done");
    Serial.flush();
    pBLEServer->setCallbacks(new FrameBLEServerCallbacks());

    // Crear servicio con el UUID que espera la app
    NimBLEService* pService = pBLEServer->createService(SERVICE_UUID);

    // Característica para recibir credenciales WiFi (Write)
    pWifiCredentialsChar = pService->createCharacteristic(
        WIFI_CREDENTIALS_CHAR_UUID,
        NIMBLE_PROPERTY::WRITE
    );
    pWifiCredentialsChar->setCallbacks(new WifiCredentialsCallback());

    // Característica para enviar respuesta (Read + Notify)
    pResponseChar = pService->createCharacteristic(
        RESPONSE_CHAR_UUID,
        NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY
    );
    // Inicializar con valor vacío para evitar basura
    pResponseChar->setValue("");

    // Característica de redes WiFi cercanas (Write "SCAN" + Read + Notify).
    // Los firmwares antiguos no la tienen: la app lo detecta y cae al modo manual.
    pNetworksChar = pService->createCharacteristic(
        NETWORKS_CHAR_UUID,
        NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::NOTIFY
    );
    pNetworksChar->setCallbacks(new NetworksCallback());
    pNetworksChar->setValue("{\"state\":\"idle\"}");
    bleNetworkScanRequested = false;

    pService->start();

    // Configurar advertising (el nombre ya se fijó en NimBLEDevice::init)
    NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(SERVICE_UUID);
    pAdvertising->setScanResponse(true);
    pAdvertising->start();

    LOGF("[BLE] Servidor BLE iniciado - Device name: %s", bleName.c_str());
    LOG("[BLE] Esperando conexión de la app...");
}

bool processBLECredentials() {
    if (!bleCredentialsReceived) {
        return false;
    }

    LOG("[BLE] Procesando credenciales recibidas");

    // Guardar credenciales en Preferences
    preferences.putString("ssid", bleReceivedSSID);
    preferences.putString("password", bleReceivedPassword);

    // Responder INMEDIATAMENTE con el frameToken (MAC)
    String frameToken = WiFi.macAddress();
    LOGF("[BLE] Enviando frameToken inmediatamente: %s", frameToken.c_str());
    sendBLEResponse(true, frameToken);

    // Dar tiempo para que se envíe la respuesta BLE
    delay(BLE_RESPONSE_DELAY);

    // Detener BLE ANTES de intentar WiFi (comparten radio)
    LOG("[BLE] Deteniendo servidor BLE...");
    NimBLEDevice::deinit(true);
    pBLEServer = nullptr;
    pWifiCredentialsChar = nullptr;
    pResponseChar = nullptr;

    // Ahora intentar conectar a WiFi (sin BLE activo)
    showLoadingMsg(MSG_CONNECTING);
    WiFi.mode(WIFI_STA);
    WiFi.begin(bleReceivedSSID.c_str(), bleReceivedPassword.c_str());

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < BLE_WIFI_CONNECT_TIMEOUT) {
        delay(500);
        attempts++;
        esp_task_wdt_reset();
        LOGF_NL("[BLE] Conectando a WiFi... intento %d/%d\r", attempts, BLE_WIFI_CONNECT_TIMEOUT);
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
        LOGF("[BLE] WiFi conectado exitosamente. IP: %s", WiFi.localIP().toString().c_str());
        return true;
    } else {
        // Fallo - NO borrar credenciales, reiniciar para volver a modo BLE + reintentos WiFi
        LOG("[BLE] Error: no se pudo conectar a WiFi");

        showLoadingMsg(MSG_WIFI_ERROR);
        delay(2000);
        showLoadingMsg(MSG_RESTARTING);
        delay(1000);

        // Reiniciar para volver al modo BLE con reintentos WiFi
        ESP.restart();
        return false;
    }
}

bool processBLECredentialsNonBlocking() {
    if (!bleCredentialsReceived) {
        return false;
    }

    LOG("[BLE] Procesando credenciales recibidas (non-blocking)");

    // Guardar credenciales en Preferences
    preferences.putString("ssid", bleReceivedSSID);
    preferences.putString("password", bleReceivedPassword);

    // Responder INMEDIATAMENTE con el frameToken (MAC)
    String frameToken = WiFi.macAddress();
    LOGF("[BLE] Enviando frameToken: %s", frameToken.c_str());
    sendBLEResponse(true, frameToken);

    // Dar tiempo para que se envíe la respuesta BLE
    delay(BLE_RESPONSE_DELAY);

    // Detener BLE ANTES de intentar WiFi (comparten radio)
    LOG("[BLE] Deteniendo servidor BLE...");
    NimBLEDevice::stopAdvertising();

    // Intentar conectar a WiFi
    showLoadingMsg(MSG_CONNECTING);
    WiFi.begin(bleReceivedSSID.c_str(), bleReceivedPassword.c_str());

    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
        esp_task_wdt_reset();
        delay(100);
    }

    if (WiFi.status() == WL_CONNECTED) {
        LOGF("[BLE] WiFi conectado! IP: %s", WiFi.localIP().toString().c_str());
        NimBLEDevice::deinit(true); // Apagar BLE completamente
        return true;
    }

    // Fallo - NO borrar credenciales, reanudar BLE
    LOG("[BLE] WiFi fallo. Reanudando BLE...");
    bleCredentialsReceived = false;
    NimBLEDevice::startAdvertising();
    showLoadingMsg(MSG_WIFI_ERROR);
    delay(1000);
    showLoadingMsg(MSG_SETUP);
    return false;
}

// Los handlers MQTT corren en la tarea de red (core 0): aqui solo se cambia el
// ESTADO (sin heap, seguro desde cualquier core). Los efectos (BLE + display)
// los aplica el core 1 via processPendingOwnerUI() en el loop.
void enterWaitingForOwnerMode() {
    if (waitingForOwner) return;

    waitingForOwner = true;
    bleCredentialsReceived = false;
    pendingOwnerUI = 1;
    LOG("[Owner] Entering waiting-for-owner mode (BLE active, WiFi/MQTT maintained)");
}

void exitWaitingForOwnerMode() {
    if (!waitingForOwner) return;

    waitingForOwner = false;
    pendingOwnerUI = 2;
    LOG("[Owner] Exiting waiting-for-owner mode");
}

// Solo core 1. Idempotente sobre el estado real de BLE, asi un enter+exit
// rapido (antes de procesarse) queda en no-op.
void processPendingOwnerUI() {
    uint8_t action = pendingOwnerUI;
    if (action == 0) return;
    pendingOwnerUI = 0;

    if (action == 1 && waitingForOwner) {
        if (pBLEServer == nullptr) {
            setupBLE();
        }
        dma_display->clearScreen();
        dma_display->fillScreen(myWHITE);
        drawLogo();
        showLoadingMsg(MSG_LINK_APP);
    } else if (action == 2 && !waitingForOwner) {
        if (pBLEServer != nullptr) {
            NimBLEDevice::deinit(true);
            pBLEServer = nullptr;
            pWifiCredentialsChar = nullptr;
            pResponseChar = nullptr;
        }
    }
}

bool processBLECredentialsAlreadyConnected() {
    if (!bleCredentialsReceived) return false;

    LOG("[Owner] BLE credentials received while already connected to WiFi");

    // WiFi already connected - just send MAC as response, don't reconnect
    String frameToken = WiFi.macAddress();
    LOGF("[Owner] Sending frameToken: %s", frameToken.c_str());
    sendBLEResponse(true, frameToken);

    delay(BLE_RESPONSE_DELAY);

    // Stop BLE and exit waiting mode
    bleCredentialsReceived = false;
    exitWaitingForOwnerMode();
    return true;
}
