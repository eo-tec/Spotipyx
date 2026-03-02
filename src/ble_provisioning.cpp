#include "ble_provisioning.h"
#include "ble_config.h"
#include "display.h"

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

    NimBLEDevice::init(BLE_DEVICE_NAME);
    NimBLEDevice::setPower(ESP_PWR_LVL_P9);

    pBLEServer = NimBLEDevice::createServer();
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

    pService->start();

    // Configurar advertising
    NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(SERVICE_UUID);
    pAdvertising->setScanResponse(true);
    pAdvertising->start();

    LOGF("[BLE] Servidor BLE iniciado - Device name: %s", BLE_DEVICE_NAME);
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
    showLoadingMsg("Connecting WiFi...");
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

        showLoadingMsg("WiFi Error");
        delay(2000);
        showLoadingMsg("Restarting...");
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
    showLoadingMsg("Connecting WiFi...");
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
    showLoadingMsg("WiFi Error");
    delay(1000);
    showLoadingMsg("Waiting BLE...");
    return false;
}
