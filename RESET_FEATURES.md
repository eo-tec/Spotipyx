# Funcionalidades de Reset de Variables de Entorno

## Descripción

Este proyecto ahora incluye funcionalidades para resetear automáticamente las variables de entorno al arrancar el programa, así como opciones para resetearlas manualmente a través de diferentes interfaces.

## Características

### 1. Reset Automático al Arrancar

- **Control**: Variable `AUTO_RESET_ON_STARTUP` en `main.cpp`
- **Valor por defecto**: `true` (habilitado)
- **Comportamiento**: Al arrancar el dispositivo, todas las variables de entorno se resetean a sus valores por defecto
- **Ubicación**: Se ejecuta automáticamente en la función `setup()`

### 2. Reset Manual por Interfaz Web

- **Ruta**: `/reset_env`
- **Acceso**: Modo AP (Access Point)
- **Comportamiento**: Resetea las variables sin reiniciar el dispositivo
- **Botón**: Disponible en la interfaz web del modo AP

### 3. Reset Manual por MQTT

- **Comando**: `reset_env_vars`
- **Tópico**: MQTT específico del pixie
- **Comportamiento**: Resetea las variables remotamente sin reiniciar
- **Respuesta**: Mensaje visual en la pantalla del dispositivo

### 4. Reset Completo del Dispositivo

- **Ruta**: `/reset`
- **Comando MQTT**: `factory_reset`
- **Comportamiento**: Resetea todo y reinicia el dispositivo

## Variables que se Resetean

### Preferencias (Preferences)
- `currentVersion`: 0
- `pixieId`: 0
- `brightness`: 50
- `maxPhotos`: 5
- `secsPhotos`: 30000 (30 segundos)
- `ssid`: "" (vacío)
- `password`: "" (vacío)
- `allowSpotify`: false
- `code`: "0000"

### Variables Globales
- `brightness`: 50
- `maxPhotos`: 5
- `secsPhotos`: 30000
- `allowSpotify`: false
- `activationCode`: "0000"
- `currentVersion`: 0
- `pixieId`: 0

## Configuración

### Habilitar/Deshabilitar Reset Automático

Para cambiar el comportamiento del reset automático, modifica la variable en `main.cpp`:

```cpp
// Variable para controlar el reset automático al arrancar
const bool AUTO_RESET_ON_STARTUP = true; // true = habilitado, false = deshabilitado
```

### Valores por Defecto

Los valores por defecto se pueden modificar en la función `resetEnvironmentVariables()` y en las funciones de reset manual.

## Uso

### 1. Reset Automático
- Simplemente reinicia el dispositivo
- Las variables se resetean automáticamente si `AUTO_RESET_ON_STARTUP = true`

### 2. Reset por Interfaz Web
1. Conecta al modo AP del dispositivo
2. Accede a `http://192.168.4.1`
3. Haz clic en "Reset Env Vars"
4. Las variables se resetean sin reiniciar

### 3. Reset por MQTT
1. Envía el comando `reset_env_vars` al tópico MQTT del pixie
2. El dispositivo resetea las variables y muestra confirmación

### 4. Reset Completo
1. Usa `/reset` en la interfaz web o `factory_reset` por MQTT
2. El dispositivo se reinicia completamente

## Notas Importantes

- El reset automático se ejecuta **antes** de cualquier otra inicialización
- Las credenciales WiFi se borran en todos los tipos de reset
- El dispositivo entrará en modo AP después de un reset si no hay credenciales WiFi
- Los valores por defecto están optimizados para la mayoría de casos de uso

## Solución de Problemas

### Reset no Funciona
- Verifica que `AUTO_RESET_ON_STARTUP = true`
- Revisa los logs del Serial Monitor
- Asegúrate de que el dispositivo tenga suficiente memoria

### Variables no se Resetean
- Verifica que la función `resetEnvironmentVariables()` se esté llamando
- Revisa que las preferencias se estén limpiando correctamente
- Confirma que las variables globales se estén actualizando

### Problemas de Memoria
- El reset de preferencias libera memoria flash
- Considera deshabilitar el reset automático si hay problemas de memoria
- Monitorea el uso de memoria después del reset 