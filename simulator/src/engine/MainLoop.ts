// 100ms tick orchestrator matching ESP32 main loop
import { DmaDisplay } from '../core/DmaDisplay';
import { MqttClient } from '../mqtt/MqttClient';
import { ScrollEngine } from './ScrollEngine';
import { PhotoManager } from './PhotoManager';
import { SpotifyManager } from './SpotifyManager';
import { DrawingManager } from './DrawingManager';

export interface MainLoopConfig {
  secsPhotos: number; // ms between photos (default 30000)
  spotifyCheck: number; // ms between spotify checks (default 5000)
  clockUpdate: number; // ms between clock updates (default 60000)
  clockEnabled: boolean;
  spotifyEnabled: boolean;
  timezoneOffset: number;
  maxPhotos: number;
  brightness: number;
}

const DEFAULT_CONFIG: MainLoopConfig = {
  secsPhotos: 30000,
  spotifyCheck: 5000,
  clockUpdate: 60000,
  clockEnabled: false,
  spotifyEnabled: false,
  timezoneOffset: 0,
  maxPhotos: 5,
  brightness: 50,
};

export class MainLoop {
  public display: DmaDisplay;
  public mqtt: MqttClient;
  public scroll: ScrollEngine;
  public photo: PhotoManager;
  public spotify: SpotifyManager;
  public drawing: DrawingManager;

  public config: MainLoopConfig;
  private timerId: ReturnType<typeof setInterval> | null = null;

  private lastPhotoChange: number = -60000;
  private lastSpotifyCheck: number = 0;
  private lastClockUpdate: number = 0;
  private songOnline: string = '';
  private isRunning: boolean = false;
  private tickRunning: boolean = false;

  onLog?: (msg: string) => void;

  constructor(display: DmaDisplay, mqtt: MqttClient) {
    this.display = display;
    this.mqtt = mqtt;
    this.config = { ...DEFAULT_CONFIG };

    this.scroll = new ScrollEngine();
    this.photo = new PhotoManager(display, mqtt, this.scroll);
    this.spotify = new SpotifyManager(display, mqtt, this.scroll);
    this.drawing = new DrawingManager(display);

    // Wire up MQTT command handler
    this.mqtt.onCommand((action, data) => {
      this.handleCommand(action, data);
    });
  }

  private log(msg: string): void {
    this.onLog?.(msg);
    console.log(msg);
  }

  private handleCommand(action: string, data: Record<string, unknown>): void {
    // Drawing commands
    if (
      action === 'enter_draw_mode' ||
      action === 'exit_draw_mode' ||
      action === 'draw_pixel' ||
      action === 'draw_stroke' ||
      action === 'clear_canvas'
    ) {
      this.drawing.handleCommand(action, data);
      return;
    }

    // Config update
    if (action === 'update_info') {
      this.log('[MQTT] Config update received');
      if (data.brightness !== undefined) {
        this.config.brightness = data.brightness as number;
        this.display.setBrightness8(Math.max(this.config.brightness, 10));
      }
      if (data.pictures_on_queue !== undefined) {
        this.config.maxPhotos = data.pictures_on_queue as number;
      }
      if (data.spotify_enabled !== undefined) {
        this.config.spotifyEnabled = data.spotify_enabled as boolean;
      }
      if (data.secs_between_photos !== undefined) {
        this.config.secsPhotos = (data.secs_between_photos as number) * 1000;
      }
      if (data.clock_enabled !== undefined) {
        this.config.clockEnabled = data.clock_enabled as boolean;
      }
      if (data.timezone_offset !== undefined) {
        this.config.timezoneOffset = data.timezone_offset as number;
      }
      this.applyConfig();
    }

    // New photo push
    if (action === 'update_photo') {
      const id = data.id as number;
      this.log(`[MQTT] New photo pushed: id=${id}`);
      this.photo.showPhotoById(id).then(() => {
        this.lastPhotoChange = Date.now();
        this.spotify.clearSong();
      });
    }
  }

  applyConfig(): void {
    this.display.setBrightness8(Math.max(this.config.brightness, 10));
    this.photo.setConfig(
      this.config.maxPhotos,
      this.config.clockEnabled,
      this.config.timezoneOffset
    );
    this.spotify.setConfig(
      this.config.spotifyEnabled,
      this.config.clockEnabled,
      this.config.timezoneOffset
    );
  }

  async requestConfig(): Promise<void> {
    this.log('[Config] Requesting configuration via MQTT...');
    const response = await this.mqtt.requestConfig();
    if (!response) {
      this.log('[Config] No response');
      return;
    }

    try {
      const text = new TextDecoder().decode(response);
      const data = JSON.parse(text);

      if (data.brightness !== undefined) this.config.brightness = data.brightness;
      if (data.pictures_on_queue !== undefined) this.config.maxPhotos = data.pictures_on_queue;
      if (data.spotify_enabled !== undefined) this.config.spotifyEnabled = data.spotify_enabled;
      if (data.secs_between_photos !== undefined)
        this.config.secsPhotos = data.secs_between_photos * 1000;
      if (data.clock_enabled !== undefined) this.config.clockEnabled = data.clock_enabled;
      if (data.timezone_offset !== undefined) this.config.timezoneOffset = data.timezone_offset;

      this.applyConfig();
      this.log('[Config] Applied successfully');
    } catch {
      this.log('[Config] Failed to parse response');
    }
  }

  start(): void {
    if (this.isRunning) return;
    this.isRunning = true;
    this.lastPhotoChange = Date.now() - this.config.secsPhotos; // trigger first photo immediately
    this.applyConfig();

    this.timerId = setInterval(() => this.tick(), 100);
    this.log('[MainLoop] Started');
  }

  stop(): void {
    if (this.timerId) {
      clearInterval(this.timerId);
      this.timerId = null;
    }
    this.isRunning = false;
    this.log('[MainLoop] Stopped');
  }

  private async tick(): Promise<void> {
    if (!this.mqtt.connected) return;

    // Prevent concurrent ticks — setInterval doesn't wait for async completion
    if (this.tickRunning) return;
    this.tickRunning = true;

    try {
      const now = Date.now();

      // Drawing mode takes priority
      if (this.drawing.active) {
        this.drawing.processBuffer();
        return;
      }

      // Spotify mode
      if (this.config.spotifyEnabled) {
        // Check song periodically
        if (now - this.lastSpotifyCheck >= this.config.spotifyCheck) {
          if (!this.photo.isLoadingPhoto()) {
            this.lastSpotifyCheck = now;
            this.songOnline = await this.spotify.checkSong();
            this.log(`[Spotify] Song check: ${this.songOnline ? `playing "${this.songOnline}"` : 'nothing playing'}`);
          }
        }

        if (!this.songOnline) {
          // No song playing — if we were showing a song, force photo change
          if (this.spotify.songShowing) {
            this.spotify.clearSong();
            this.lastPhotoChange = 0; // force immediate photo
          }

          // Photo carousel
          if (now - this.lastPhotoChange >= this.config.secsPhotos) {
            if (!this.photo.isLoadingPhoto()) {
              await this.photo.showNextPhoto();
              this.lastPhotoChange = Date.now();
            }
          }
        } else {
          // Song is playing — check if changed
          if (this.spotify.songShowing !== this.songOnline) {
            if (!this.photo.isLoadingPhoto()) {
              await this.spotify.fetchAndDrawCover(this.songOnline);
              this.lastPhotoChange = Date.now();
            }
          }
        }
      } else {
        // No spotify — pure photo carousel
        if (now - this.lastPhotoChange >= this.config.secsPhotos) {
          if (!this.photo.isLoadingPhoto()) {
            await this.photo.showNextPhoto();
            this.lastPhotoChange = Date.now();
          }
        }
      }

      // Update scroll
      this.scroll.update(this.display, now);

      // Update clock overlay
      if (this.config.clockEnabled && now - this.lastClockUpdate >= this.config.clockUpdate) {
        this.photo.showClockOverlay();
        this.lastClockUpdate = now;
      }

      // Always render after tick
      this.display.render();
    } finally {
      this.tickRunning = false;
    }
  }

  /** Force showing next photo immediately */
  nextPhoto(): void {
    this.lastPhotoChange = 0;
  }

  /** Force showing previous photo */
  prevPhoto(): void {
    this.photo.currentPhotoIndex = Math.max(0, this.photo.currentPhotoIndex - 2);
    this.lastPhotoChange = 0;
  }
}
