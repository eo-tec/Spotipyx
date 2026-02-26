// WebSocket-to-TCP MQTT proxy.
// mqtt.js in the browser sends MQTT protocol bytes over WebSocket frames.
// This proxy unwraps those frames and forwards raw bytes to the TCP MQTT broker,
// and wraps TCP responses back into WebSocket frames.
import { WebSocketServer } from 'ws';
import net from 'net';

const WS_PORT = 9001;
const MQTT_HOST = 'mqtt.mypixelframe.com';
const MQTT_PORT = 1883;

const wss = new WebSocketServer({
  port: WS_PORT,
  // mqtt.js requests the 'mqtt' sub-protocol — we must accept it
  handleProtocols: (protocols) => {
    if (protocols.has('mqtt')) return 'mqtt';
    if (protocols.has('mqttv3.1')) return 'mqttv3.1';
    return false;
  },
});

console.log(`[Proxy] Listening on ws://localhost:${WS_PORT}`);
console.log(`[Proxy] Bridging to ${MQTT_HOST}:${MQTT_PORT}`);

wss.on('connection', (ws) => {
  console.log('[Proxy] Browser client connected');

  const tcp = net.createConnection({ host: MQTT_HOST, port: MQTT_PORT }, () => {
    console.log('[Proxy] TCP connected to MQTT broker');
  });

  // Browser -> TCP broker
  ws.on('message', (data) => {
    const buf = Buffer.isBuffer(data) ? data : Buffer.from(data);
    tcp.write(buf);
  });

  // TCP broker -> Browser
  tcp.on('data', (data) => {
    if (ws.readyState === 1) {
      ws.send(data);
    }
  });

  ws.on('close', () => {
    console.log('[Proxy] Browser disconnected');
    tcp.end();
  });

  tcp.on('close', () => {
    console.log('[Proxy] MQTT broker disconnected');
    if (ws.readyState === 1) ws.close();
  });

  tcp.on('error', (err) => {
    console.error('[Proxy] TCP error:', err.message);
    if (ws.readyState === 1) ws.close();
  });

  ws.on('error', (err) => {
    console.error('[Proxy] WS error:', err.message);
    tcp.end();
  });
});
