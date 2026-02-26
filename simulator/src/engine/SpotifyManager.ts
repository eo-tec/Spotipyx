// Spotify song check and cover display manager
import { DmaDisplay } from '../core/DmaDisplay';
import { MqttClient } from '../mqtt/MqttClient';
import { decodeCover } from '../decoders/coverDecoder';
import { pushUp } from './animations';
import { ScrollEngine } from './ScrollEngine';

export class SpotifyManager {
  private display: DmaDisplay;
  private mqtt: MqttClient;
  private scroll: ScrollEngine;

  public songShowing: string = '';
  private enabled: boolean = false;
  private clockEnabled: boolean = false;
  private timezoneOffset: number = 0;

  onLog?: (msg: string) => void;

  constructor(display: DmaDisplay, mqtt: MqttClient, scroll: ScrollEngine) {
    this.display = display;
    this.mqtt = mqtt;
    this.scroll = scroll;
  }

  setConfig(enabled: boolean, clockEnabled: boolean, timezoneOffset: number): void {
    this.enabled = enabled;
    this.clockEnabled = clockEnabled;
    this.timezoneOffset = timezoneOffset;
  }

  isEnabled(): boolean {
    return this.enabled;
  }

  /** Check if a song is currently playing. Returns song ID or empty string. */
  async checkSong(): Promise<string> {
    const response = await this.mqtt.requestSong();
    if (!response) {
      this.onLog?.('[Spotify] Song check: no response from backend');
      return '';
    }

    try {
      const text = new TextDecoder().decode(response);
      const data = JSON.parse(text);
      return data.id || '';
    } catch {
      this.onLog?.('[Spotify] Song check: failed to parse response');
      return '';
    }
  }

  /** Fetch cover art and display with push-up animation */
  async fetchAndDrawCover(songId: string): Promise<boolean> {
    this.onLog?.(`[Spotify] Fetching cover for ${songId}`);

    const response = await this.mqtt.requestCover(songId);
    if (!response) {
      this.onLog?.('[Spotify] No cover response');
      return false;
    }

    const coverPixels = decodeCover(response);
    if (!coverPixels) {
      this.onLog?.('[Spotify] Failed to decode cover');
      return false;
    }

    this.onLog?.('[Spotify] Push-up animation starting');

    // Clear scroll state
    this.scroll.reset();

    // Push up animation
    await pushUp(this.display, coverPixels);

    this.songShowing = songId;

    // Show clock overlay
    if (this.clockEnabled) {
      this.showClockOverlay();
    }
    this.display.render();

    this.onLog?.('[Spotify] Animation done');
    return true;
  }

  clearSong(): void {
    this.songShowing = '';
  }

  private showClockOverlay(): void {
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
