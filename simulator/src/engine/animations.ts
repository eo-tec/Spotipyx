// Animation functions matching ESP32 display transitions
import { DmaDisplay } from '../core/DmaDisplay';
import { rgb565ToRgb, color565, scaleColor565 } from '../core/Color565';

function delay(ms: number): Promise<void> {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

/** Fade out: scale screenBuffer down to black over 20 steps */
export async function fadeOut(display: DmaDisplay): Promise<void> {
  const buf = display.getScreenBuffer();
  // Snapshot the current buffer
  const snapshot = new Uint16Array(buf);
  const steps = 20;

  for (let step = 0; step <= steps; step++) {
    const factor = (steps - step) / steps;
    for (let i = 0; i < snapshot.length; i++) {
      const [r, g, b] = rgb565ToRgb(snapshot[i]);
      const sr = Math.round(r * factor);
      const sg = Math.round(g * factor);
      const sb = Math.round(b * factor);
      buf[i] = color565(sr, sg, sb);
    }
    display.render();
    await delay(30);
  }
}

/** Fade in: scale screenBuffer from black to current values over 20 steps */
export async function fadeIn(display: DmaDisplay): Promise<void> {
  const buf = display.getScreenBuffer();
  // Snapshot the target buffer
  const target = new Uint16Array(buf);
  const steps = 20;

  for (let step = 0; step <= steps; step++) {
    const factor = step / steps;
    for (let i = 0; i < target.length; i++) {
      const [r, g, b] = rgb565ToRgb(target[i]);
      const sr = Math.round(r * factor);
      const sg = Math.round(g * factor);
      const sb = Math.round(b * factor);
      buf[i] = color565(sr, sg, sb);
    }
    display.render();
    await delay(30);
  }
}

/** Push up animation for Spotify covers.
 *  Shifts all rows up by 1 and draws new row at bottom from cover data. */
export async function pushUp(
  display: DmaDisplay,
  coverPixels: Uint16Array
): Promise<void> {
  const buf = display.getScreenBuffer();
  const W = 64;

  for (let y = 0; y < 64; y++) {
    // Shift all rows up by 1
    for (let moveY = 0; moveY < 63; moveY++) {
      for (let x = 0; x < W; x++) {
        buf[moveY * W + x] = buf[(moveY + 1) * W + x];
      }
    }

    // Draw new row at y=63 from cover data
    for (let x = 0; x < W; x++) {
      buf[63 * W + x] = coverPixels[y * W + x];
    }

    display.render();
    await delay(15);
  }
}

/** Display photo from center with 5-color expanding squares animation,
 *  then reveal photo pixels expanding from center. */
export async function displayFromCenter(
  display: DmaDisplay,
  photoPixels: Uint16Array
): Promise<void> {
  const W = 64;
  const H = 64;
  const centerX = 32;
  const centerY = 32;

  // 5 animation colors matching ESP32
  const colors = [0x3080, 0x56aa, 0xf6bd, 0xf680, 0xf746];

  // Color squares expanding from center
  for (const c of colors) {
    for (let size = 2; size <= Math.max(W, H); size += 2) {
      for (let y = centerY - Math.floor(size / 2); y <= centerY + Math.floor(size / 2); y++) {
        for (let x = centerX - Math.floor(size / 2); x <= centerX + Math.floor(size / 2); x++) {
          display.drawPixel(x, y, c);
        }
      }
      display.render();
      await delay(5);
    }
  }

  // Reveal photo pixels expanding from center
  for (let radius = 0; radius <= Math.max(W, H); radius++) {
    for (let y = centerY - radius; y <= centerY + radius; y++) {
      for (let x = centerX - radius; x <= centerX + radius; x++) {
        if (x >= 0 && x < W && y >= 0 && y < H) {
          if (Math.abs(x - centerX) === radius || Math.abs(y - centerY) === radius) {
            display.drawPixelWithBuffer(x, y, photoPixels[y * W + x]);
          }
        }
      }
    }
    display.render();
    await delay(5);
  }
}
