// Decode Spotify cover response: 8192 bytes RGB565 big-endian
// Backend encodes with BGR correction:
//   rgb565 = ((b & 0xf8) << 8) | ((r & 0xfc) << 3) | (g >> 3)
// ESP32 hardware draws this directly. Simulator must decode back to proper RGB565.
// The "red" channel in the encoded value is actually blue,
// "green" channel is actually red, "blue" channel is actually green.

import { color565 } from '../core/Color565';

export function decodeCover(buffer: ArrayBuffer): Uint16Array | null {
  if (buffer.byteLength < 8192) return null;

  const bytes = new Uint8Array(buffer);
  const pixels = new Uint16Array(64 * 64);

  for (let i = 0; i < 4096; i++) {
    const bufIdx = i * 2;
    // Read big-endian uint16
    const encoded = (bytes[bufIdx] << 8) | bytes[bufIdx + 1];

    // Decode BGR-swapped value back to RGB
    // encoded = ((b & 0xf8) << 8) | ((r & 0xfc) << 3) | (g >> 3)
    // "red" bits (15..11) = b>>3, "green" bits (10..5) = r>>2, "blue" bits (4..0) = g>>3
    const b5 = (encoded >> 11) & 0x1f; // these are blue bits
    const r6 = (encoded >> 5) & 0x3f; // these are red bits
    const g5 = encoded & 0x1f; // these are green bits

    // Reconstruct as proper RGB565
    const b = b5 << 3;
    const r = r6 << 2;
    const g = g5 << 3;
    pixels[i] = color565(r, g, b);
  }

  return pixels;
}

/** Get the raw big-endian uint16 values for push-up animation
 *  (animation reads line by line from the decoded cover) */
export function decodeCoverRaw(buffer: ArrayBuffer): Uint16Array | null {
  return decodeCover(buffer);
}
