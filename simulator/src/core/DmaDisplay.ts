// Canvas-based display replicating ESP32 HUB75 DMA panel API
import { color565 as toColor565, rgb565ToRgb, scaleColor565 } from './Color565';
import { getGlyph, measureText } from './PicopixelFont';

const WIDTH = 64;
const HEIGHT = 64;

export class DmaDisplay {
  private canvas: HTMLCanvasElement;
  private ctx: CanvasRenderingContext2D;
  private scale: number;

  // Internal screen buffer in RGB565 (matches ESP32 screenBuffer[64][64])
  public screenBuffer: Uint16Array;

  private brightness: number = 255;
  private cursorX: number = 0;
  private cursorY: number = 0;
  private textColor: number = 0xffff;
  private textSize: number = 1;
  private textWrap: boolean = true;
  private useFont: boolean = true; // true = Picopixel, false = default (not implemented, fallback)

  constructor(canvas: HTMLCanvasElement, scale: number = 8) {
    this.canvas = canvas;
    this.scale = scale;
    this.canvas.width = WIDTH * scale;
    this.canvas.height = HEIGHT * scale;
    this.canvas.style.imageRendering = 'pixelated';
    this.ctx = canvas.getContext('2d', { willReadFrequently: true })!;
    this.ctx.imageSmoothingEnabled = false;
    this.screenBuffer = new Uint16Array(WIDTH * HEIGHT);
  }

  // --- Color ---
  static color565(r: number, g: number, b: number): number {
    return toColor565(r, g, b);
  }

  // --- Drawing primitives ---
  drawPixel(x: number, y: number, c: number): void {
    if (x < 0 || x >= WIDTH || y < 0 || y >= HEIGHT) return;
    this.screenBuffer[y * WIDTH + x] = c;
  }

  drawPixelWithBuffer(x: number, y: number, c: number): void {
    this.drawPixel(x, y, c);
  }

  fillRect(x: number, y: number, w: number, h: number, c: number): void {
    for (let py = y; py < y + h; py++) {
      for (let px = x; px < x + w; px++) {
        this.drawPixel(px, py, c);
      }
    }
  }

  fillScreen(c: number): void {
    this.screenBuffer.fill(c);
  }

  clearScreen(): void {
    this.fillScreen(0);
  }

  // --- Brightness ---
  setBrightness8(b: number): void {
    this.brightness = Math.max(0, Math.min(255, b));
  }

  getBrightness(): number {
    return this.brightness;
  }

  // --- Text ---
  setCursor(x: number, y: number): void {
    this.cursorX = x;
    this.cursorY = y;
  }

  setTextColor(c: number): void {
    this.textColor = c;
  }

  setTextSize(s: number): void {
    this.textSize = s;
  }

  setTextWrap(w: boolean): void {
    this.textWrap = w;
  }

  setFont(font?: string): void {
    // 'picopixel' uses bitmap font; undefined/null uses default (also picopixel for simulator)
    this.useFont = font === 'picopixel' || font === undefined || font === null;
  }

  /** Adafruit GFX-compatible getTextBounds.
   *  Returns bounding box of text at given cursor position.
   *  Like ESP32: x1,y1 = upper-left corner of bounding box; w,h = dimensions */
  getTextBounds(
    text: string,
    cx: number,
    cy: number
  ): { x1: number; y1: number; w: number; h: number } {
    const m = measureText(text);
    // Picopixel baseline: yOffset is negative. cy is the baseline Y.
    // The text box top is at cy + minYOffset (which is negative)
    let minYOffset = 0;
    for (const ch of text) {
      const g = getGlyph(ch);
      if (g) minYOffset = Math.min(minYOffset, g.yOffset);
    }
    return {
      x1: cx,
      y1: cy + minYOffset,
      w: m.width,
      h: m.height,
    };
  }

  /** Print text at cursor position using Picopixel font */
  print(text: string): void {
    let cx = this.cursorX;
    const cy = this.cursorY;

    for (const ch of text) {
      const g = getGlyph(ch);
      if (!g) {
        cx += 2 * this.textSize;
        continue;
      }

      // Draw glyph
      const startX = cx + g.xOffset * this.textSize;
      const startY = cy + g.yOffset * this.textSize;

      let bitIndex = 0;
      for (let row = 0; row < g.height; row++) {
        for (let col = 0; col < g.width; col++) {
          const byteIdx = Math.floor(bitIndex / 8);
          const bitIdx = 7 - (bitIndex % 8);
          const on = (g.bitmap[byteIdx] >> bitIdx) & 1;
          if (on) {
            for (let sy = 0; sy < this.textSize; sy++) {
              for (let sx = 0; sx < this.textSize; sx++) {
                this.drawPixel(
                  startX + col * this.textSize + sx,
                  startY + row * this.textSize + sy,
                  this.textColor
                );
              }
            }
          }
          bitIndex++;
        }
      }

      cx += g.xAdvance * this.textSize;
    }
    this.cursorX = cx;
  }

  // --- Render to canvas ---
  render(): void {
    const imgData = this.ctx.createImageData(WIDTH * this.scale, HEIGHT * this.scale);
    const data = imgData.data;
    const bFactor = this.brightness / 255;
    const half = this.scale / 2;
    const radius = half - 0.5;
    const r2 = radius * radius;

    for (let y = 0; y < HEIGHT; y++) {
      for (let x = 0; x < WIDTH; x++) {
        const c = this.screenBuffer[y * WIDTH + x];
        const [r, g, b] = rgb565ToRgb(c);
        const br = Math.round(r * bFactor);
        const bg = Math.round(g * bFactor);
        const bb = Math.round(b * bFactor);

        for (let sy = 0; sy < this.scale; sy++) {
          for (let sx = 0; sx < this.scale; sx++) {
            const px = x * this.scale + sx;
            const py = y * this.scale + sy;
            const idx = (py * WIDTH * this.scale + px) * 4;
            const dx = sx - half + 0.5;
            const dy = sy - half + 0.5;
            if (dx * dx + dy * dy <= r2) {
              data[idx] = br;
              data[idx + 1] = bg;
              data[idx + 2] = bb;
              data[idx + 3] = 255;
            } else {
              data[idx + 3] = 255; // black background
            }
          }
        }
      }
    }

    this.ctx.putImageData(imgData, 0, 0);
  }

  getScreenBuffer(): Uint16Array {
    return this.screenBuffer;
  }

  getWidth(): number {
    return WIDTH;
  }

  getHeight(): number {
    return HEIGHT;
  }
}
