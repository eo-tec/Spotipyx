#!/bin/bash
# Serial Monitor para ESP32 - Activa venv y lanza el logger

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
VENV_PATH="$SCRIPT_DIR/.venv"
LOGGER_SCRIPT="$SCRIPT_DIR/tools/serial_logger.py"

# Crear venv si no existe
if [ ! -d "$VENV_PATH" ]; then
    echo "[Setup] Creando entorno virtual..."
    python3 -m venv "$VENV_PATH"
    "$VENV_PATH/bin/pip" install pyserial
fi

# Lanzar logger con argumentos pasados al script
"$VENV_PATH/bin/python" "$LOGGER_SCRIPT" "$@"
