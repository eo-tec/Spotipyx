# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Spotipyx is an ESP32-based digital photo frame with Spotify integration. The device displays photos and Spotify album covers on a 64x64 LED matrix panel, with features including:

- WiFi connectivity with serial credential input
- MQTT communication for remote control
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

# Build and upload via OTA
pio run --target upload --environment ota

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

**Pixie Library (`lib/Pixie/`)**
- Provides API abstraction for server communication
- Contains static utility functions for display operations
- Handles HTTP requests to the backend server

**Display Assets (`src/pics.h`)**
- Contains bitmap data for WiFi icons and logos
- Defines various display states and animations

### Key Architecture Details

**Display System**
- Uses ESP32-HUB75-MatrixPanel-I2S-DMA library for 64x64 LED matrix
- Implements screen buffer for fade animations
- Supports multiple display modes (photos, Spotify covers, time, loading states)

**Network Communication**
- **WiFi Configuration**: Web-based setup via Access Point mode with user-friendly interface
- **Fallback**: Serial communication support with format: `#SSID;ssid#PASS;password` 
- MQTT client connects to HiveMQ cloud broker for real-time updates
- HTTP/HTTPS clients for API communication with backend server

**WiFi Setup System**
- **Access Point Mode**: Device creates "Pixie" AP (password: "12345678") when no credentials stored
- **Web Interface**: Simple HTML form at `192.168.4.1` for WiFi configuration  
- **Network Scanning**: Automatic scanning and display of available WiFi networks
- **Real-time Feedback**: Connection status and error handling with automatic restart

**Photo Management**
- Photos are fetched as binary RGB data from the backend
- Supports photo metadata (title, username) display
- Implements various transition animations (fade, push-up, center-out)

**Configuration Management**
- Uses ESP32 Preferences library for persistent storage
- Stores WiFi credentials, device settings, and Pixie ID
- Configurable photo display intervals and Spotify integration

### Important Constants

- Panel resolution: 64x64 pixels
- Serial baud rate: 115200
- Default photo interval: 30 seconds
- MQTT broker: HiveMQ cloud (TLS on port 8883)
- Server URLs: Development (192.168.18.53:3000) vs Production (api.mypixelframe.com)

### Device Registration

The device registers with the backend using its MAC address and receives a unique Pixie ID for identification in API calls and MQTT topics.

## Development Notes

- The project uses both secure (HTTPS/TLS) and insecure HTTP connections based on DEV flag
- OTA updates are supported with progress display on the LED matrix
- Web-based WiFi configuration with multiple endpoints for complete user experience
- Serial communication is available as fallback for credential input and debugging

### Web Server Endpoints (AP Mode)

- `GET /` - Main WiFi configuration page
- `GET /scan` - WiFi network scanning and selection page  
- `POST /connect` - WiFi connection handler with real-time feedback
- `GET /reset` - Device reset functionality
- `GET /status` - Connection status check and final confirmation