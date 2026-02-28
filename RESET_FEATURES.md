# Funcionalidades de Reset de Variables de Entorno

## Descripción

Este proyecto incluye opciones para resetear las variables de entorno manualmente a través de diferentes interfaces.

## Características

### 1. Reset Manual por MQTT

- **Comando**: `reset_env_vars`
- **Tópico**: MQTT específico del frame
- **Comportamiento**: Resetea las variables remotamente sin reiniciar
- **Respuesta**: Mensaje visual en la pantalla del dispositivo

### 2. Reset Completo del Dispositivo

- **Comando MQTT**: `factory_reset`
- **Comportamiento**: Resetea todo y reinicia el dispositivo

## Variables que se Resetean

### Preferencias (Preferences)
- `currentVersion`: 0
- `frameId`: 0
- `brightness`: 50
- `maxPhotos`: 5
- `secsPhotos`: 30000 (30 segundos)
- `ssid`: "" (vacío)
- `password`: "" (vacío)
- `allowSpotify`: false

### Variables Globales
- `brightness`: 50
- `maxPhotos`: 5
- `secsPhotos`: 30000
- `allowSpotify`: false
- `currentVersion`: 0
- `frameId`: 0

## Uso

### 1. Reset por MQTT
1. Envía el comando `reset_env_vars` al tópico MQTT del frame
2. El dispositivo resetea las variables y muestra confirmación

### 2. Reset Completo
1. Usa `factory_reset` por MQTT
2. El dispositivo se reinicia completamente

## Notas Importantes

- Las credenciales WiFi se borran en todos los tipos de reset
- El dispositivo entrará en modo BLE provisioning después de un reset si no hay credenciales WiFi
- Los valores por defecto están optimizados para la mayoría de casos de uso

## Solución de Problemas

### Variables no se Resetean
- Revisa los logs del Serial Monitor
- Verifica que las preferencias se estén limpiando correctamente
- Confirma que las variables globales se estén actualizando
