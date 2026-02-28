# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

frame. is an ESP32-based digital photo frame with Spotify integration. The device displays photos and Spotify album covers on a 64x64 LED matrix panel, with features including:

- WiFi connectivity with BLE credential configuration
- MQTT communication for all server interactions
- Photo display with animations
- Spotify cover art integration
- Over-the-Air (OTA) updates
- Brightness control and display settings

## Build Commands

This is a PlatformIO project for ESP32 development:

```bash
# Build the project
pio run

# Build and upload to ESP32
pio run --target upload

# Open serial monitor
pio device monitor

# Clean build
pio run --target clean
```

## Architecture

### Core Components

**Main Application (`src/main.cpp`)**
- Contains the main application logic and ESP32 setup
- Manages WiFi connection, MQTT, and display operations
- Handles photo cycling and Spotify integration
- Implements OTA updates and device registration

**Display Assets (`src/pics.h`)**
- Contains bitmap data for WiFi icons and logos
- Defines various display states and animations

**Configuration (`src/config.h`)**
- Contains MQTT broker credentials and API server URLs
- Gitignored — copy `src/config.h.example` to get started

### Key Architecture Details

**Display System**
- Uses ESP32-HUB75-MatrixPanel-I2S-DMA library for 64x64 LED matrix
- Implements screen buffer for fade animations
- Supports multiple display modes (photos, Spotify covers, time, loading states)

**Network Communication**
- **WiFi Configuration**: BLE-based setup for credential input
- MQTT client connects to broker for all server communication
- All API interactions handled via MQTT (no HTTP/HTTPS)

**WiFi Setup System**
- **BLE Configuration**: Device advertises as "frame." for WiFi credential setup
- **Network Scanning**: Automatic scanning and display of available WiFi networks via BLE
- **Real-time Feedback**: Connection status and error handling with automatic restart

**Photo Management**
- Photos are fetched as binary RGB data from the backend
- Supports photo metadata (title, username) display
- Implements various transition animations (fade, push-up, center-out)

**Configuration Management**
- Uses ESP32 Preferences library for persistent storage
- Stores WiFi credentials, device settings, and frame ID
- Configurable photo display intervals and Spotify integration

### Important Constants

- Panel resolution: 64x64 pixels
- Serial baud rate: 115200
- Default photo interval: 30 seconds

### Device Registration

The device registers with the backend using its MAC address and receives a unique frame ID for identification in MQTT topics.

## Development Notes

- All server communication is handled via MQTT
- OTA updates are supported with progress display on the LED matrix
- BLE-based WiFi configuration for user-friendly setup experience
- Serial communication is available for debugging
