// Photo carousel manager
import { DmaDisplay } from '../core/DmaDisplay';
import { MqttClient } from '../mqtt/MqttClient';
import { decodePhoto, PhotoData } from '../decoders/photoDecoder';
import { fadeOut, fadeIn, displayFromCenter } from './animations';
import { ScrollEngine } from './ScrollEngine';

export class PhotoManager {
  private display: DmaDisplay;
  private mqtt: MqttClient;
  private scroll: ScrollEngine;

  public currentPhotoIndex: number = 0;
  public maxPhotos: number = 5;
  private isLoading: boolean = false;
  private clockEnabled: boolean = false;
  private timezoneOffset: number = 0;

  onLog?: (msg: string) => void;

  constructor(display: DmaDisplay, mqtt: MqttClient, scroll: ScrollEngine) {
    this.display = display;
    this.mqtt = mqtt;
    this.scroll = scroll;
  }

  setConfig(maxPhotos: number, clockEnabled: boolean, timezoneOffset: number): void {
    this.maxPhotos = maxPhotos;
    this.clockEnabled = clockEnabled;
    this.timezoneOffset = timezoneOffset;
  }

  isLoadingPhoto(): boolean {
    return this.isLoading;
  }

  async showPhoto(index: number): Promise<boolean> {
    if (this.isLoading) return false;
    this.isLoading = true;

    this.onLog?.(`[Photo] Requesting photo index=${index}`);

    try {
      const response = await this.mqtt.requestPhoto({ index });
      if (!response) {
        this.onLog?.('[Photo] No response');
        return false;
      }

      const photo = decodePhoto(response);
      if (!photo) {
        this.onLog?.('[Photo] Failed to decode');
        return false;
      }

      this.onLog?.(`[Photo] Received: "${photo.title}" by ${photo.author}`);

      // Fade out current, load new, fade in
      await fadeOut(this.display);
      this.display.clearScreen();
      this.scroll.reset();

      // Copy pixels to screen buffer
      const buf = this.display.getScreenBuffer();
      buf.set(photo.pixels);

      await fadeIn(this.display);

      // Show photo info (title, author)
      this.scroll.setInfo(this.display, photo.title, photo.author);

      // Show clock overlay
      if (this.clockEnabled) {
        this.showClockOverlay();
      }

      this.display.render();
      return true;
    } finally {
      this.isLoading = false;
    }
  }

  async showPhotoById(id: number): Promise<boolean> {
    if (this.isLoading) return false;
    this.isLoading = true;

    this.onLog?.(`[Photo] Requesting photo id=${id}`);

    try {
      const response = await this.mqtt.requestPhoto({ id });
      if (!response) return false;

      const photo = decodePhoto(response);
      if (!photo) return false;

      this.onLog?.(`[Photo] Received: "${photo.title}" by ${photo.author}`);

      // Display from center animation (for new photos via push notification)
      await displayFromCenter(this.display, photo.pixels);

      this.scroll.setInfo(this.display, photo.title, photo.author);
      if (this.clockEnabled) this.showClockOverlay();
      this.display.render();
      return true;
    } finally {
      this.isLoading = false;
    }
  }

  async showNextPhoto(): Promise<boolean> {
    if (this.currentPhotoIndex >= this.maxPhotos) {
      this.currentPhotoIndex = 0;
    }
    const result = await this.showPhoto(this.currentPhotoIndex);
    if (result) this.currentPhotoIndex++;
    return result;
  }

  showClockOverlay(): void {
    if (!this.clockEnabled) return;

    const now = new Date();
    const utcH = now.getUTCHours();
    const utcM = now.getUTCMinutes();
    const totalMinutes = utcH * 60 + utcM;
    const localMinutes = (totalMinutes + this.timezoneOffset + 24 * 60) % (24 * 60);
    const localH = Math.floor(localMinutes / 60);
    const localM = localMinutes % 60;

    const timeStr =
      (localH < 10 ? '0' : '') + localH + ':' + (localM < 10 ? '0' : '') + localM;

    this.display.setFont('picopixel');
    this.display.setTextSize(1);

    const bounds = this.display.getTextBounds(timeStr, 0, 0);
    const xPos = 64 - bounds.w - 2;
    const yPos = bounds.h + 1;

    this.display.fillRect(xPos - 1, 0, bounds.w + 3, bounds.h + 2, 0);
    this.display.setTextColor(0xffff);
    this.display.setCursor(xPos, yPos);
    this.display.print(timeStr);
  }
}
