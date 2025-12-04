#!/bin/bash

# Factory Reset Script for Spotipyx ESP32
# Erases flash memory and uploads fresh firmware via USB

PORT="${1:-/dev/cu.usbserial-110}"

echo "==================================="
echo "  Spotipyx Factory Reset"
echo "==================================="
echo ""
echo "Puerto: $PORT"
echo ""

# Check if port exists
if [ ! -e "$PORT" ]; then
    echo "❌ Error: Puerto $PORT no encontrado"
    echo ""
    echo "Puertos disponibles:"
    ls /dev/cu.usb* 2>/dev/null || echo "  Ninguno"
    exit 1
fi

# Step 1: Erase flash
echo "📦 Paso 1/2: Borrando memoria flash..."
pio pkg exec -- esptool.py --port "$PORT" erase_flash

if [ $? -ne 0 ]; then
    echo "❌ Error borrando la flash"
    exit 1
fi

echo ""
echo "✅ Flash borrada"
echo ""

# Step 2: Upload firmware (USB only)
echo "📤 Paso 2/2: Subiendo firmware..."
pio run -e esp32dev --target upload --upload-port "$PORT"

if [ $? -ne 0 ]; then
    echo "❌ Error subiendo el firmware"
    exit 1
fi

echo ""
echo "==================================="
echo "✅ Factory reset completado!"
echo "==================================="
echo ""
echo "El dispositivo creará un AP WiFi:"
echo "  SSID: Pixie"
echo "  Pass: 12345678"
echo "  URL:  http://192.168.4.1"
echo ""
