# PCB Design Notes — frame. v2

## MCU: ESP32-S3-WROOM-1-N8R8

- **Chip**: ESP32-S3 (dual-core Xtensa LX7)
- **Flash**: 8MB
- **PSRAM**: 8MB (octal SPI)
- **WiFi**: 802.11 b/g/n
- **BLE**: 5.0

### Why ESP32-S3 over ESP32 original

- CPU más rápida (LX7 vs LX6)
- PSRAM octal-SPI (doble velocidad que ESP32)
- BLE 5.0 (más rango y throughput para provisioning)
- USB nativo — elimina chip UART externo (CP2102/FTDI)
- Más GPIO disponibles
- Librería HUB75 DMA compatible

### Why N8R8

- 8MB flash: margen amplio para OTA dual con firmware grande + assets
- 8MB PSRAM: animaciones de vídeo a 64x64 sin problema (20 frames × 8KB = 160KB), y libertad total para futuras features
- Diferencia de coste despreciable en producción

## USB nativo

El ESP32-S3 tiene USB integrado (GPIO 19 = D-, GPIO 20 = D+). Esto permite:

- Programación y debug sin chip UART externo
- Ahorro en BOM (eliminar CP2102/CH340/FTDI)
- Menos espacio en PCB
- USB CDC para serial monitor

## Display

- Panel HUB75 64x64 LED matrix
- Librería: ESP32-HUB75-MatrixPanel-I2S-DMA

## Memoria y animaciones

- Con PSRAM: frames de animación a 64x64 RGB565 (8192 bytes/frame), buffer completo en PSRAM via `ps_malloc()`
- Sin PSRAM (fallback para dev kits actuales): downscale a 32x32 RGB565 (2048 bytes/frame) en RAM interna
- Máximo 20 frames por animación, 5 FPS
- Detección automática al boot con `psramFound()`

## Migración desde ESP32-WROOM-32E

- Pinout diferente — requiere rediseño de PCB
- El WROOM-32E-N4R2 sirve como paso intermedio (mismo footprint que WROOM actual, 2MB PSRAM integrada)
