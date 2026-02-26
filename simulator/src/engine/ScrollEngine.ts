// 4-state text scroll machine matching ESP32 updatePhotoInfo()
import { DmaDisplay } from '../core/DmaDisplay';
import { measureText } from '../core/PicopixelFont';

const SCROLL_SPEED = 300; // ms per character
const SCROLL_PAUSE = 2000; // ms pause at start/end
const PANEL_WIDTH = 64;

enum ScrollState {
  PAUSED_START,
  SCROLLING,
  PAUSED_END,
  RETURNING,
}

export class ScrollEngine {
  private state: ScrollState = ScrollState.PAUSED_START;
  private offset: number = 0;
  private lastTime: number = 0;
  private pauseStart: number = 0;

  private title: string = '';
  private name: string = '';
  private titleY: number = 0;
  private nameY: number = 0;
  private needsScroll: boolean = false;
  private sameLine: boolean = true;

  reset(): void {
    this.state = ScrollState.PAUSED_START;
    this.offset = 0;
    this.needsScroll = false;
    this.title = '';
    this.name = '';
  }

  /** Set new title/name and calculate layout. Returns true if text is displayed. */
  setInfo(display: DmaDisplay, title: string, name: string): boolean {
    this.title = title;
    this.name = name;
    this.offset = 0;
    this.state = ScrollState.PAUSED_START;
    this.pauseStart = Date.now();

    if (!title && !name) {
      this.needsScroll = false;
      return false;
    }

    const titleM = measureText(title);
    const nameM = measureText(name);

    // Layout decision matching ESP32
    const horizontalMargin = 4;
    if (title && name) {
      this.sameLine = titleM.width + nameM.width + horizontalMargin <= PANEL_WIDTH;
    } else {
      this.sameLine = true;
    }

    if (this.sameLine) {
      this.titleY = PANEL_WIDTH - 2; // 62 (baseline)
      this.nameY = PANEL_WIDTH - 2;
    } else {
      this.titleY = PANEL_WIDTH - 2;
      this.nameY = PANEL_WIDTH - 12;
    }

    // Check if title needs scrolling
    this.needsScroll = titleM.width > PANEL_WIDTH - 2;

    // Draw initial text
    this.drawText(display);
    return true;
  }

  private drawText(display: DmaDisplay): void {
    display.setFont('picopixel');
    display.setTextSize(1);
    display.setTextColor(0xffff); // white

    const scrolledTitle = this.title.substring(this.offset);

    if (this.title) {
      // Clear title area
      const bounds = display.getTextBounds(scrolledTitle, 1, this.titleY);
      display.fillRect(0, bounds.y1 - 1, PANEL_WIDTH, bounds.h + 2, 0);

      display.setCursor(1, this.titleY);
      display.print(scrolledTitle);
    }

    if (this.name && this.offset === 0) {
      // Name only drawn when not scrolling or at start
      if (this.sameLine) {
        const nameM = measureText(this.name);
        const nameX = PANEL_WIDTH - nameM.width;
        const bounds = display.getTextBounds(this.name, nameX, this.nameY);
        display.fillRect(nameX - 1, bounds.y1 - 1, nameM.width + 2, bounds.h + 2, 0);
        display.setCursor(nameX, this.nameY);
        display.print(this.name);
      } else {
        const bounds = display.getTextBounds(this.name, 1, this.nameY);
        display.fillRect(0, bounds.y1 - 1, PANEL_WIDTH, bounds.h + 2, 0);
        display.setCursor(1, this.nameY);
        display.print(this.name);
      }
    }
  }

  /** Called from main loop, returns true if display was updated */
  update(display: DmaDisplay, now: number): boolean {
    if (!this.needsScroll || !this.title) return false;

    switch (this.state) {
      case ScrollState.PAUSED_START:
        if (now - this.pauseStart >= SCROLL_PAUSE) {
          this.state = ScrollState.SCROLLING;
          this.lastTime = now;
        }
        break;

      case ScrollState.SCROLLING:
        if (now - this.lastTime >= SCROLL_SPEED) {
          this.offset++;
          const scrolled = this.title.substring(this.offset);
          const m = measureText(scrolled);

          this.drawText(display);

          if (m.width <= PANEL_WIDTH - 2) {
            this.state = ScrollState.PAUSED_END;
            this.pauseStart = now;
          }
          this.lastTime = now;
          return true;
        }
        break;

      case ScrollState.PAUSED_END:
        if (now - this.pauseStart >= SCROLL_PAUSE) {
          this.state = ScrollState.RETURNING;
          this.lastTime = now;
        }
        break;

      case ScrollState.RETURNING:
        if (now - this.lastTime >= SCROLL_SPEED) {
          if (this.offset > 0) {
            this.offset--;
            this.drawText(display);
            this.lastTime = now;
            return true;
          } else {
            this.state = ScrollState.PAUSED_START;
            this.pauseStart = now;
          }
        }
        break;
    }

    return false;
  }
}
