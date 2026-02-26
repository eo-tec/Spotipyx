// MQTT WebSocket wrapper for Spotipyx simulator
import mqtt, { MqttClient as MqttJsClient } from 'mqtt';
import {
  commandTopic,
  responseTopic,
  requestTopic,
} from './topics';

export type CommandHandler = (action: string, data: Record<string, unknown>) => void;

export class MqttClient {
  private client: MqttJsClient | null = null;
  private pixieId: number = 0;
  private pendingResponses: Map<
    string,
    { resolve: (data: ArrayBuffer | null) => void; timer: ReturnType<typeof setTimeout> }
  > = new Map();
  private commandHandler: CommandHandler | null = null;
  private _connected = false;
  private onStatusChange: ((connected: boolean) => void) | null = null;

  get connected(): boolean {
    return this._connected;
  }

  setStatusCallback(cb: (connected: boolean) => void): void {
    this.onStatusChange = cb;
  }

  connect(
    brokerUrl: string,
    username: string,
    password: string,
    pixieId: number
  ): Promise<void> {
    return new Promise((resolve, reject) => {
      this.pixieId = pixieId;
      const clientId = `pixie-sim-${pixieId}`;
      let settled = false;

      this.client = mqtt.connect(brokerUrl, {
        username,
        password,
        clientId,
        protocolVersion: 4,
        reconnectPeriod: 5000,
        connectTimeout: 10000,
      });

      this.client.on('connect', () => {
        console.log('[MQTT] Connected');
        this._connected = true;
        this.onStatusChange?.(true);

        // Subscribe to command topic and response wildcard
        this.client!.subscribe(commandTopic(pixieId), { qos: 1 });
        this.client!.subscribe(responseTopic(pixieId), { qos: 1 });
        if (!settled) {
          settled = true;
          resolve();
        }
      });

      this.client.on('error', (err) => {
        console.error('[MQTT] Error:', err);
        if (!settled) {
          settled = true;
          reject(err);
        }
      });

      this.client.on('close', () => {
        this._connected = false;
        this.onStatusChange?.(false);
      });

      this.client.on('message', (topic: string, payload: Uint8Array) => {
        this.handleMessage(topic, payload);
      });
    });
  }

  disconnect(): void {
    if (this.client) {
      this.client.end(true);
      this.client = null;
      this._connected = false;
      this.onStatusChange?.(false);
    }
    // Clear pending
    for (const [, entry] of this.pendingResponses) {
      clearTimeout(entry.timer);
      entry.resolve(null);
    }
    this.pendingResponses.clear();
  }

  onCommand(handler: CommandHandler): void {
    this.commandHandler = handler;
  }

  private handleMessage(topic: string, payload: Uint8Array): void {
    // Check if it's a response
    if (topic.includes('/response/')) {
      const parts = topic.split('/');
      const responseType = parts[parts.length - 1];

      const pending = this.pendingResponses.get(responseType);
      if (pending) {
        clearTimeout(pending.timer);
        this.pendingResponses.delete(responseType);
        // Convert Uint8Array to ArrayBuffer (works in both browser and Node)
        const ab = payload.buffer.slice(
          payload.byteOffset,
          payload.byteOffset + payload.byteLength
        );
        pending.resolve(ab);
      }
      return;
    }

    // Command on pixie/{id} topic
    if (topic === commandTopic(this.pixieId)) {
      try {
        const text = new TextDecoder().decode(payload);
        const data = JSON.parse(text);
        if (data.action && this.commandHandler) {
          this.commandHandler(data.action, data);
        }
      } catch {
        // Not JSON, ignore
      }
    }
  }

  private request(
    type: string,
    payload: string = '{}',
    timeout: number = 10000
  ): Promise<ArrayBuffer | null> {
    if (!this.client || !this._connected) return Promise.resolve(null);

    return new Promise((resolve) => {
      const timer = setTimeout(() => {
        this.pendingResponses.delete(type);
        console.warn(`[MQTT] Timeout waiting for ${type}`);
        resolve(null);
      }, timeout);

      this.pendingResponses.set(type, { resolve, timer });

      this.client!.publish(requestTopic(this.pixieId, type), payload, { qos: 1 });
    });
  }

  requestConfig(): Promise<ArrayBuffer | null> {
    return this.request('config');
  }

  requestPhoto(indexOrId: { index: number } | { id: number }): Promise<ArrayBuffer | null> {
    return this.request('photo', JSON.stringify(indexOrId), 15000);
  }

  requestSong(): Promise<ArrayBuffer | null> {
    return this.request('song', '{}', 5000);
  }

  requestCover(songId: string): Promise<ArrayBuffer | null> {
    return this.request('cover', JSON.stringify({ songId }), 15000);
  }
}
