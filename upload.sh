#!/bin/bash
# Upload script para ESP32

cd "$(dirname "$0")"
pio run -e esp32dev -t upload
