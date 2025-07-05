#!/bin/bash

# PlatformIO Helper Script for Pixie ESP32 Development
# ====================================================

PIO_CMD="~/.platformio/penv/bin/platformio"

case "$1" in
    "build")
        echo "🔨 Building ESP32 firmware..."
        $PIO_CMD run
        ;;
    "upload")
        echo "📤 Building and uploading to ESP32..."
        $PIO_CMD run --target upload
        ;;
    "ota")
        echo "📡 Building and uploading via OTA..."
        $PIO_CMD run --target upload --environment ota
        ;;
    "monitor")
        echo "🖥️  Opening serial monitor..."
        $PIO_CMD device monitor
        ;;
    "clean")
        echo "🧹 Cleaning build files..."
        $PIO_CMD run --target clean
        ;;
    "debug")
        echo "🐛 Starting debug session..."
        $PIO_CMD debug
        ;;
    "test")
        echo "🧪 Running tests..."
        $PIO_CMD test
        ;;
    "devices")
        echo "📱 Listing connected devices..."
        $PIO_CMD device list
        ;;
    "size")
        echo "📊 Analyzing memory usage..."
        $PIO_CMD run --target size
        ;;
    "verbose")
        echo "🔍 Building with verbose output..."
        $PIO_CMD run --verbose
        ;;
    *)
        echo "Pixie ESP32 Development Helper"
        echo "=============================="
        echo ""
        echo "Usage: $0 [command]"
        echo ""
        echo "Available commands:"
        echo "  build    - Build the firmware"
        echo "  upload   - Build and upload via USB"
        echo "  ota      - Build and upload via OTA"
        echo "  monitor  - Open serial monitor"
        echo "  clean    - Clean build files"
        echo "  debug    - Start debug session"
        echo "  test     - Run tests"
        echo "  devices  - List connected devices"
        echo "  size     - Analyze memory usage"
        echo "  verbose  - Build with verbose output"
        echo ""
        echo "Example: $0 build"
        ;;
esac