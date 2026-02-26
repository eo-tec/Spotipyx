// Decode photo response: JSON metadata + '\n' + 12288 bytes RGB888
// Backend sends via sharp().removeAlpha().raw() -> R,G,B order
// ESP32 reads bytes as: g=buf[0], b=buf[1], r=buf[2] -> color565(r, g, b)
// So the actual byte order from backend is R,G,B but ESP32 interprets as G,B,R
// Simulator must replicate: color565(buf[2], buf[0], buf[1])

import { color565 } from '../core/Color565';

export interface PhotoData {
  title: string;
  author: string;
  pixels: Uint16Array; // 64*64 RGB565
}

export function decodePhoto(buffer: ArrayBuffer): PhotoData | null {
  const bytes = new Uint8Array(buffer);

  // Find newline separator in first 256 bytes
  let jsonEnd = -1;
  const searchLimit = Math.min(bytes.length, 256);
  for (let i = 0; i < searchLimit; i++) {
    if (bytes[i] === 0x0a) {
      // '\n'
      jsonEnd = i;
      break;
    }
  }

  if (jsonEnd < 0) return null;

  const binaryStart = jsonEnd + 1;
  const binaryLen = bytes.length - binaryStart;
  if (binaryLen < 12288) return null;

  // Parse JSON metadata
  const jsonStr = new TextDecoder().decode(bytes.slice(0, jsonEnd));
  let title = '';
  let author = '';
  try {
    const meta = JSON.parse(jsonStr);
    title = meta.title || '';
    author = meta.author || '';
  } catch {
    // ignore parse errors
  }

  // Decode RGB888 pixels
  // Backend sends R,G,B order via sharp().removeAlpha().raw()
  // ESP32 reads as g=buf[0],b=buf[1],r=buf[2] then color565(r,g,b)
  // which swaps channels — but HUB75 hardware compensates.
  // For the simulator (standard RGB canvas), read bytes as-is: R,G,B
  const pixels = new Uint16Array(64 * 64);
  for (let i = 0; i < 4096; i++) {
    const idx = binaryStart + i * 3;
    const r = bytes[idx];
    const g = bytes[idx + 1];
    const b = bytes[idx + 2];
    pixels[i] = color565(r, g, b);
  }

  return { title, author, pixels };
}
