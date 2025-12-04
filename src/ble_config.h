#ifndef BLE_CONFIG_H
#define BLE_CONFIG_H

// UUIDs coordinados con la app React Native (frame-app/src/config/bluetooth.ts)
#define BLE_DEVICE_NAME "frame."
#define SERVICE_UUID "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define WIFI_CREDENTIALS_CHAR_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define RESPONSE_CHAR_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a9"

// Timeouts BLE
#define BLE_WIFI_CONNECT_TIMEOUT 30  // Intentos de conexión WiFi (30 * 500ms = 15s)
#define BLE_RESPONSE_DELAY 2000       // Delay para enviar respuesta antes de desconectar (aumentado para dar tiempo a la notificación)

#endif
