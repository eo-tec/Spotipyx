#!/usr/bin/env python3
"""
ESP32 Serial Logger - Graba la salida del ESP32 a un archivo con reconexión automática.

Uso:
    python serial_logger.py [puerto] [baudrate]

Ejemplos:
    python serial_logger.py                      # Auto-detecta puerto, 115200 baud
    python serial_logger.py /dev/ttyUSB0         # Puerto específico
    python serial_logger.py /dev/ttyUSB0 115200  # Puerto y baudrate específicos
"""

import serial
import serial.tools.list_ports
import sys
import time
from datetime import datetime
import os
import signal

# Configuración por defecto
DEFAULT_BAUDRATE = 115200
RECONNECT_DELAY = 2  # Segundos entre intentos de reconexión
LOG_DIR = "logs"

running = True

def signal_handler(sig, frame):
    global running
    print("\n[Logger] Deteniendo...")
    running = False

def find_esp32_port():
    """Busca automáticamente el puerto del ESP32."""
    ports = serial.tools.list_ports.comports()

    # Buscar puertos comunes de ESP32
    esp32_identifiers = ['CP210', 'CH340', 'CH910', 'USB', 'UART', 'Serial', 'ttyUSB', 'ttyACM']

    for port in ports:
        for identifier in esp32_identifiers:
            if identifier.lower() in port.description.lower() or identifier.lower() in port.device.lower():
                return port.device

    # Si no encuentra, mostrar puertos disponibles
    if ports:
        print("[Logger] Puertos disponibles:")
        for port in ports:
            print(f"  - {port.device}: {port.description}")
        return ports[0].device

    return None

def get_log_filename():
    """Genera nombre de archivo con timestamp."""
    if not os.path.exists(LOG_DIR):
        os.makedirs(LOG_DIR)

    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    return os.path.join(LOG_DIR, f"esp32_log_{timestamp}.txt")

def connect_serial(port, baudrate):
    """Intenta conectar al puerto serie."""
    try:
        ser = serial.Serial(
            port=port,
            baudrate=baudrate,
            timeout=1,
            bytesize=serial.EIGHTBITS,
            parity=serial.PARITY_NONE,
            stopbits=serial.STOPBITS_ONE
        )
        return ser
    except serial.SerialException as e:
        return None

def main():
    global running

    signal.signal(signal.SIGINT, signal_handler)
    signal.signal(signal.SIGTERM, signal_handler)

    # Parsear argumentos
    port = sys.argv[1] if len(sys.argv) > 1 else None
    baudrate = int(sys.argv[2]) if len(sys.argv) > 2 else DEFAULT_BAUDRATE

    # Auto-detectar puerto si no se especifica
    if not port:
        port = find_esp32_port()
        if not port:
            print("[Logger] ERROR: No se encontró ningún puerto serie.")
            print("[Logger] Conecta el ESP32 y vuelve a intentar.")
            sys.exit(1)

    log_file = get_log_filename()
    print(f"[Logger] Puerto: {port}")
    print(f"[Logger] Baudrate: {baudrate}")
    print(f"[Logger] Archivo de log: {log_file}")
    print(f"[Logger] Presiona Ctrl+C para detener")
    print("-" * 60)

    ser = None
    reconnect_count = 0

    with open(log_file, 'a', encoding='utf-8') as f:
        # Escribir cabecera
        f.write(f"=== ESP32 Serial Log ===\n")
        f.write(f"Inicio: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}\n")
        f.write(f"Puerto: {port} @ {baudrate} baud\n")
        f.write("=" * 60 + "\n\n")
        f.flush()

        while running:
            # Intentar conectar si no hay conexión
            if ser is None or not ser.is_open:
                if reconnect_count > 0:
                    timestamp = datetime.now().strftime("%H:%M:%S")
                    msg = f"\n[{timestamp}] === RECONECTANDO (intento {reconnect_count}) ===\n"
                    print(msg, end='')
                    f.write(msg)
                    f.flush()

                ser = connect_serial(port, baudrate)

                if ser:
                    reconnect_count += 1
                    timestamp = datetime.now().strftime("%H:%M:%S")
                    msg = f"[{timestamp}] === CONECTADO ===\n\n"
                    print(msg, end='')
                    f.write(msg)
                    f.flush()
                else:
                    time.sleep(RECONNECT_DELAY)
                    continue

            # Leer datos
            try:
                if ser.in_waiting > 0:
                    line = ser.readline()
                    try:
                        decoded = line.decode('utf-8', errors='replace').rstrip()
                    except:
                        decoded = str(line)

                    if decoded:
                        timestamp = datetime.now().strftime("%H:%M:%S.%f")[:-3]
                        log_line = f"[{timestamp}] {decoded}"
                        print(decoded)  # Mostrar sin timestamp en consola para legibilidad
                        f.write(log_line + "\n")
                        f.flush()
                else:
                    time.sleep(0.01)  # Pequeña pausa para no saturar CPU

            except serial.SerialException as e:
                timestamp = datetime.now().strftime("%H:%M:%S")
                msg = f"\n[{timestamp}] === DESCONECTADO: {e} ===\n"
                print(msg, end='')
                f.write(msg)
                f.flush()

                try:
                    ser.close()
                except:
                    pass
                ser = None
                time.sleep(RECONNECT_DELAY)

            except Exception as e:
                timestamp = datetime.now().strftime("%H:%M:%S")
                msg = f"\n[{timestamp}] === ERROR: {e} ===\n"
                print(msg, end='')
                f.write(msg)
                f.flush()
                time.sleep(0.1)

        # Cerrar conexión
        if ser and ser.is_open:
            ser.close()

        # Escribir pie
        f.write(f"\n{'=' * 60}\n")
        f.write(f"Fin: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}\n")
        f.write(f"Total reconexiones: {reconnect_count}\n")

    print(f"\n[Logger] Log guardado en: {log_file}")

if __name__ == "__main__":
    main()
