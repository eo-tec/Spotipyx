#ifndef BLE_CONFIG_H
#define BLE_CONFIG_H

// UUIDs coordinados con la app React Native (frame-app/src/config/bluetooth.ts)
// Nombre BLE real = prefijo + MAC sin ":" (ej. "frame.781C3CA5B4C5") — ver setupBLE()
#define BLE_DEVICE_NAME_PREFIX "frame."
#define SERVICE_UUID "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define WIFI_CREDENTIALS_CHAR_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define RESPONSE_CHAR_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a9"
// Lista de redes WiFi cercanas. La app escribe "SCAN" y lee/recibe el resultado.
// Su ausencia en firmwares antiguos es la señal de "no soportado" para la app.
#define NETWORKS_CHAR_UUID "beb5483e-36e1-4688-b7f5-ea07361b26aa"

// Scan de redes WiFi
#define BLE_SCAN_MAX_NETWORKS 12    // techo de redes reportadas (ordenadas por señal)
#define BLE_SCAN_MAX_PAYLOAD 450    // margen bajo el límite práctico de ~500 B por lectura BLE

// Timeouts BLE
#define BLE_WIFI_CONNECT_TIMEOUT 30  // Intentos de conexión WiFi (30 * 500ms = 15s)
#define BLE_RESPONSE_DELAY 2000       // Delay para enviar respuesta antes de desconectar (aumentado para dar tiempo a la notificación)

#endif
