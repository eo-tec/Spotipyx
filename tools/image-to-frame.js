#!/usr/bin/env node
/**
 * Convierte una imagen a formato binario compatible con frame. ESP32
 *
 * Formatos de salida:
 *
 * 1. Binario (.bin) - Para servir via HTTP:
 *    - 2 bytes: longitud del título (Big Endian)
 *    - 2 bytes: longitud del username (Big Endian)
 *    - N bytes: título (UTF-8)
 *    - M bytes: username (UTF-8)
 *    - 12288 bytes: datos de imagen (64x64x3 RGB)
 *
 * 2. Header C (.h) - Para embeber en firmware:
 *    - Array de uint8_t con los datos binarios
 *    - Constantes con dimensiones
 *
 * Uso:
 *   node image-to-frame.js <imagen> [opciones]
 *
 * Opciones:
 *   -o, --output <archivo>   Archivo de salida (default: <nombre>.bin)
 *   -t, --title <texto>      Título de la imagen
 *   -u, --username <texto>   Nombre de usuario
 *   --no-crop               No recortar, solo redimensionar
 *   --header                Generar archivo .h para embeber en firmware
 */

const sharp = require('sharp');
const fs = require('fs');
const path = require('path');

function generateHeaderFile(varName, rgbBuffer, title, username) {
    const lines = [];
    lines.push(`// Generado automáticamente por image-to-frame.js`);
    lines.push(`// Imagen: 64x64 RGB`);
    lines.push(`#pragma once`);
    lines.push(``);
    lines.push(`#include <stdint.h>`);
    lines.push(``);
    lines.push(`#define ${varName}_WIDTH 64`);
    lines.push(`#define ${varName}_HEIGHT 64`);
    lines.push(`#define ${varName}_SIZE ${rgbBuffer.length}`);
    lines.push(``);

    if (title) {
        lines.push(`const char ${varName}_TITLE[] = "${title.replace(/"/g, '\\"')}";`);
    }
    if (username) {
        lines.push(`const char ${varName}_USERNAME[] = "${username.replace(/"/g, '\\"')}";`);
    }
    lines.push(``);

    // Generar array de datos RGB
    lines.push(`const uint8_t ${varName}_DATA[] PROGMEM = {`);

    // Formato: 16 bytes por línea
    const bytesPerLine = 16;
    for (let i = 0; i < rgbBuffer.length; i += bytesPerLine) {
        const chunk = [];
        for (let j = i; j < Math.min(i + bytesPerLine, rgbBuffer.length); j++) {
            chunk.push(`0x${rgbBuffer[j].toString(16).padStart(2, '0')}`);
        }
        const isLast = i + bytesPerLine >= rgbBuffer.length;
        lines.push(`    ${chunk.join(', ')}${isLast ? '' : ','}`);
    }

    lines.push(`};`);
    lines.push(``);

    // Función helper para mostrar la imagen
    lines.push(`// Función helper para mostrar la imagen en el display`);
    lines.push(`// Uso: show${varName}Image(dma_display);`);
    lines.push(`inline void show${varName}Image(MatrixPanel_I2S_DMA* display) {`);
    lines.push(`    for (int y = 0; y < 64; y++) {`);
    lines.push(`        for (int x = 0; x < 64; x++) {`);
    lines.push(`            int idx = (y * 64 + x) * 3;`);
    lines.push(`            uint8_t r = pgm_read_byte(&${varName}_DATA[idx]);`);
    lines.push(`            uint8_t g = pgm_read_byte(&${varName}_DATA[idx + 1]);`);
    lines.push(`            uint8_t b = pgm_read_byte(&${varName}_DATA[idx + 2]);`);
    lines.push(`            display->drawPixel(x, y, display->color565(r, g, b));`);
    lines.push(`        }`);
    lines.push(`    }`);
    lines.push(`}`);

    return lines.join('\n');
}

async function convertImageToFrame(inputPath, options = {}) {
    const {
        output,
        title = '',
        username = '',
        crop = true
    } = options;

    // Verificar que el archivo existe
    if (!fs.existsSync(inputPath)) {
        console.error(`Error: El archivo "${inputPath}" no existe`);
        process.exit(1);
    }

    // Determinar nombre de salida
    const baseName = path.basename(inputPath, path.extname(inputPath));
    const extension = options.header ? '.h' : '.bin';
    const outputPath = output || `${baseName}${extension}`;

    console.log(`Procesando: ${inputPath}`);
    console.log(`Salida: ${outputPath}`);
    console.log(`Formato: ${options.header ? 'Header C (.h)' : 'Binario (.bin)'}`);

    try {
        // Leer imagen
        const imageBuffer = fs.readFileSync(inputPath);
        let sharpInstance = sharp(imageBuffer);

        // Obtener metadata para crop centrado
        const metadata = await sharpInstance.metadata();

        if (crop && metadata.width && metadata.height) {
            const size = Math.min(metadata.width, metadata.height);
            const left = Math.floor((metadata.width - size) / 2);
            const top = Math.floor((metadata.height - size) / 2);

            console.log(`Imagen original: ${metadata.width}x${metadata.height}`);
            console.log(`Recortando desde (${left}, ${top}) tamaño ${size}x${size}`);

            sharpInstance = sharpInstance.extract({
                left,
                top,
                width: size,
                height: size
            });
        }

        // Redimensionar a 64x64 y obtener datos RGB raw
        const rgbBuffer = await sharpInstance
            .rotate() // Corrige rotación según EXIF
            .resize(64, 64)
            .removeAlpha()
            .raw()
            .toBuffer();

        // Crear header binario
        const titleBuf = Buffer.from(title, 'utf-8');
        const usernameBuf = Buffer.from(username, 'utf-8');
        const headerBuf = Buffer.alloc(4);
        headerBuf.writeUInt16BE(titleBuf.length, 0);
        headerBuf.writeUInt16BE(usernameBuf.length, 2);

        // Combinar todo
        const finalBuffer = Buffer.concat([headerBuf, titleBuf, usernameBuf, rgbBuffer]);

        if (options.header) {
            // Generar archivo .h para embeber en firmware
            const varName = baseName.replace(/[^a-zA-Z0-9_]/g, '_').toUpperCase();
            const headerContent = generateHeaderFile(varName, rgbBuffer, title, username);
            fs.writeFileSync(outputPath, headerContent);

            console.log(`\n✓ Header C generado!`);
            console.log(`  Variable: ${varName}_DATA`);
            console.log(`  Tamaño imagen: 12288 bytes (64x64x3)`);
        } else {
            // Guardar archivo binario
            fs.writeFileSync(outputPath, finalBuffer);

            console.log(`\n✓ Conversión exitosa!`);
            console.log(`  Tamaño: ${finalBuffer.length} bytes`);
            console.log(`  Header: 4 bytes`);
            console.log(`  Título: "${title}" (${titleBuf.length} bytes)`);
            console.log(`  Username: "${username}" (${usernameBuf.length} bytes)`);
            console.log(`  Imagen: 12288 bytes (64x64x3)`);
        }

        return outputPath;

    } catch (err) {
        console.error('Error al procesar la imagen:', err.message);
        process.exit(1);
    }
}

// Parsear argumentos
function parseArgs(args) {
    const options = { crop: true, header: false };
    let inputPath = null;

    for (let i = 0; i < args.length; i++) {
        const arg = args[i];

        if (arg === '-o' || arg === '--output') {
            options.output = args[++i];
        } else if (arg === '-t' || arg === '--title') {
            options.title = args[++i];
        } else if (arg === '-u' || arg === '--username') {
            options.username = args[++i];
        } else if (arg === '--no-crop') {
            options.crop = false;
        } else if (arg === '--header' || arg === '-H') {
            options.header = true;
        } else if (arg === '-h' || arg === '--help') {
            showHelp();
            process.exit(0);
        } else if (!arg.startsWith('-')) {
            inputPath = arg;
        }
    }

    return { inputPath, options };
}

function showHelp() {
    console.log(`
Convierte una imagen a formato compatible con frame. ESP32

Uso:
  node image-to-frame.js <imagen> [opciones]

Opciones:
  -o, --output <archivo>   Archivo de salida (default: <nombre>.bin o .h)
  -t, --title <texto>      Título de la imagen
  -u, --username <texto>   Nombre de usuario
  --no-crop                No recortar al centro, solo redimensionar
  -H, --header             Generar archivo .h para embeber en firmware
  -h, --help               Mostrar esta ayuda

Ejemplos:
  # Generar archivo binario para servir via HTTP
  node image-to-frame.js foto.jpg
  node image-to-frame.js foto.png -o mi_foto.bin -t "Mi foto" -u "Alvaro"

  # Generar header C para embeber en firmware
  node image-to-frame.js logo.png --header
  node image-to-frame.js icono.jpg -H -o include/icono.h
`);
}

// Ejecutar si se llama directamente
if (require.main === module) {
    const args = process.argv.slice(2);

    if (args.length === 0) {
        showHelp();
        process.exit(1);
    }

    const { inputPath, options } = parseArgs(args);

    if (!inputPath) {
        console.error('Error: Debes especificar una imagen de entrada');
        showHelp();
        process.exit(1);
    }

    convertImageToFrame(inputPath, options);
}

module.exports = { convertImageToFrame };
