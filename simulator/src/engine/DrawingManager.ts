// Drawing mode handler — replicates ESP32 drawing behavior
import { DmaDisplay } from '../core/DmaDisplay';
import { color565 } from '../core/Color565';

const DRAWING_TIMEOUT = 60000; // 60s inactivity timeout
const UPDATE_INTERVAL = 20; // 20ms = 50 FPS

interface DrawCommand {
  x: number;
  y: number;
  color: number; // RGB565
  size: number;
}

export class DrawingManager {
  private display: DmaDisplay;
  public active: boolean = false;
  private drawingBuffer: Uint16Array;
  private commandBuffer: DrawCommand[] = [];
  private lastActivity: number = 0;
  private lastUpdate: number = 0;

  onLog?: (msg: string) => void;
  onModeChange?: (active: boolean) => void;

  constructor(display: DmaDisplay) {
    this.display = display;
    this.drawingBuffer = new Uint16Array(64 * 64);
  }

  enter(): void {
    this.active = true;
    this.lastActivity = Date.now();
    this.drawingBuffer.fill(0);
    this.commandBuffer = [];
    this.display.clearScreen();
    this.display.render();
    this.onLog?.('[Drawing] Entered drawing mode');
    this.onModeChange?.(true);
  }

  exit(): void {
    this.active = false;
    this.display.clearScreen();
    this.display.render();
    this.onLog?.('[Drawing] Exited drawing mode');
    this.onModeChange?.(false);
  }

  /** Handle MQTT draw commands */
  handleCommand(action: string, data: Record<string, unknown>): void {
    switch (action) {
      case 'enter_draw_mode':
        this.enter();
        break;

      case 'exit_draw_mode':
        this.exit();
        break;

      case 'draw_pixel': {
        if (!this.active) this.enter();

        const x = data.x as number;
        const y = data.y as number;
        const colorHex = data.color as string;
        const size = (data.size as number) || 1;

        // Convert hex color to RGB565 with BGR swap matching ESP32 line 1804
        const c = this.hexToColor565(colorHex);
        this.addCommand(x, y, c, size);
        break;
      }

      case 'draw_stroke': {
        if (!this.active) this.enter();

        const points = data.points as Array<{ x: number; y: number }>;
        const colorHex = data.color as string;
        const c = this.hexToColor565(colorHex);

        if (points) {
          for (const p of points) {
            this.drawPixel(p.x, p.y, c);
          }
        }
        this.lastActivity = Date.now();
        break;
      }

      case 'clear_canvas':
        if (!this.active) this.enter();
        this.commandBuffer = [];
        this.drawingBuffer.fill(0);
        this.display.clearScreen();
        this.display.render();
        this.lastActivity = Date.now();
        break;
    }
  }

  /** Convert hex #RRGGBB to RGB565.
   *  ESP32 does color565(b, r, g) for HUB75 hardware swap.
   *  Simulator renders to standard RGB canvas, so use normal order. */
  private hexToColor565(hex: string): number {
    if (!hex || hex.length !== 7 || hex[0] !== '#') return 0;
    const rgb = parseInt(hex.substring(1), 16);
    const r = (rgb >> 16) & 0xff;
    const g = (rgb >> 8) & 0xff;
    const b = rgb & 0xff;
    return color565(r, g, b);
  }

  private drawPixel(x: number, y: number, c: number): void {
    if (x < 0 || x >= 64 || y < 0 || y >= 64) return;
    this.drawingBuffer[y * 64 + x] = c;
  }

  private addCommand(x: number, y: number, c: number, size: number): void {
    if (this.commandBuffer.length < 100) {
      this.commandBuffer.push({ x, y, color: c, size });
    }
    this.lastActivity = Date.now();
  }

  /** Process buffered commands and update display. Called from main loop. */
  processBuffer(): void {
    if (!this.active) return;

    const now = Date.now();

    // Check timeout
    if (now - this.lastActivity > DRAWING_TIMEOUT) {
      this.onLog?.('[Drawing] Timeout — exiting');
      this.exit();
      return;
    }

    // Process command buffer at UPDATE_INTERVAL
    if (now - this.lastUpdate < UPDATE_INTERVAL) return;

    if (this.commandBuffer.length > 0) {
      for (const cmd of this.commandBuffer) {
        const halfSize = Math.floor((cmd.size - 1) / 2);
        const extra = (cmd.size - 1) % 2;
        for (let dy = -halfSize; dy <= halfSize + extra; dy++) {
          for (let dx = -halfSize; dx <= halfSize + extra; dx++) {
            const px = cmd.x + dx;
            const py = cmd.y + dy;
            if (px >= 0 && px < 64 && py >= 0 && py < 64) {
              this.drawingBuffer[py * 64 + px] = cmd.color;
            }
          }
        }
      }
      this.commandBuffer = [];

      // Update display from drawing buffer
      const buf = this.display.getScreenBuffer();
      buf.set(this.drawingBuffer);
      this.display.render();
    }

    this.lastUpdate = now;
  }
}
