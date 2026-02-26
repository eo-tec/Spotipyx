import { useState, useCallback, useRef } from 'react';
import PixelPanel, { PixelPanelRef } from './components/PixelPanel';
import ConfigPanel from './components/ConfigPanel';
import ControlPanel from './components/ControlPanel';
import { DmaDisplay } from './core/DmaDisplay';
import { MqttClient } from './mqtt/MqttClient';
import { MainLoop } from './engine/MainLoop';
import './App.css';

export default function App() {
  const panelRef = useRef<PixelPanelRef>(null);
  const mainLoopRef = useRef<MainLoop | null>(null);
  const displayRef = useRef<DmaDisplay | null>(null);
  const mqttRef = useRef<MqttClient | null>(null);

  const [connected, setConnected] = useState(false);
  const [connecting, setConnecting] = useState(false);
  const [brightness, setBrightness] = useState(50);
  const [spotifyEnabled, setSpotifyEnabled] = useState(false);
  const [clockEnabled, setClockEnabled] = useState(false);
  const [logs, setLogs] = useState<string[]>([]);

  const addLog = useCallback((msg: string) => {
    setLogs((prev) => [...prev.slice(-99), `[${new Date().toLocaleTimeString()}] ${msg}`]);
  }, []);

  const handleCanvasReady = useCallback((canvas: HTMLCanvasElement) => {
    if (!displayRef.current) {
      displayRef.current = new DmaDisplay(canvas, 8);
      displayRef.current.render();
    }
  }, []);

  const handleConnect = useCallback(
    async (pixieId: number, brokerUrl: string) => {
      if (!displayRef.current) return;
      setConnecting(true);

      try {
        const mqtt = new MqttClient();
        mqttRef.current = mqtt;

        mqtt.setStatusCallback((status) => {
          setConnected(status);
          if (!status) addLog('Disconnected from MQTT');
        });

        await mqtt.connect(brokerUrl, 'server', 'Test1234!', pixieId);
        addLog(`Connected to ${brokerUrl} as pixie-${pixieId}`);

        const mainLoop = new MainLoop(displayRef.current, mqtt);
        mainLoopRef.current = mainLoop;

        mainLoop.onLog = addLog;
        mainLoop.photo.onLog = addLog;
        mainLoop.spotify.onLog = addLog;
        mainLoop.drawing.onLog = addLog;

        // Request config first
        await mainLoop.requestConfig();

        // Sync UI state
        setBrightness(mainLoop.config.brightness);
        setSpotifyEnabled(mainLoop.config.spotifyEnabled);
        setClockEnabled(mainLoop.config.clockEnabled);

        mainLoop.start();

        // Expose for debugging
        (window as unknown as Record<string, unknown>).__mainLoop = mainLoop;
      } catch (err) {
        addLog(`Connection failed: ${err}`);
      } finally {
        setConnecting(false);
      }
    },
    [addLog]
  );

  const handleDisconnect = useCallback(() => {
    mainLoopRef.current?.stop();
    mainLoopRef.current = null;
    mqttRef.current?.disconnect();
    mqttRef.current = null;
    setConnected(false);
    addLog('Disconnected');
  }, [addLog]);

  const handleBrightnessChange = useCallback(
    (val: number) => {
      setBrightness(val);
      if (mainLoopRef.current) {
        mainLoopRef.current.config.brightness = val;
        mainLoopRef.current.display.setBrightness8(Math.max(val, 10));
        mainLoopRef.current.display.render();
      }
    },
    []
  );

  const handleSpotifyToggle = useCallback(() => {
    setSpotifyEnabled((prev) => {
      const next = !prev;
      if (mainLoopRef.current) {
        mainLoopRef.current.config.spotifyEnabled = next;
        mainLoopRef.current.applyConfig();
      }
      return next;
    });
  }, []);

  const handleClockToggle = useCallback(() => {
    setClockEnabled((prev) => {
      const next = !prev;
      if (mainLoopRef.current) {
        mainLoopRef.current.config.clockEnabled = next;
        mainLoopRef.current.applyConfig();
        if (next) {
          mainLoopRef.current.photo.showClockOverlay();
          mainLoopRef.current.display.render();
        }
      }
      return next;
    });
  }, []);

  const handlePrevPhoto = useCallback(() => {
    mainLoopRef.current?.prevPhoto();
  }, []);

  const handleNextPhoto = useCallback(() => {
    mainLoopRef.current?.nextPhoto();
  }, []);

  return (
    <div className="app">
      <header>
        <h1>Spotipyx Simulator</h1>
        <span className="subtitle">64x64 LED Panel</span>
      </header>
      <div className="main-layout">
        <div className="panel-area">
          <PixelPanel ref={panelRef} scale={8} onCanvasReady={handleCanvasReady} />
        </div>
        <div className="controls-area">
          <ConfigPanel
            onConnect={handleConnect}
            onDisconnect={handleDisconnect}
            connected={connected}
            connecting={connecting}
          />
          <ControlPanel
            brightness={brightness}
            onBrightnessChange={handleBrightnessChange}
            spotifyEnabled={spotifyEnabled}
            onSpotifyToggle={handleSpotifyToggle}
            clockEnabled={clockEnabled}
            onClockToggle={handleClockToggle}
            onPrevPhoto={handlePrevPhoto}
            onNextPhoto={handleNextPhoto}
            connected={connected}
          />
          <div className="panel log-panel">
            <h3>Log</h3>
            <div className="log-content">
              {logs.map((l, i) => (
                <div key={i} className="log-line">
                  {l}
                </div>
              ))}
            </div>
          </div>
        </div>
      </div>
    </div>
  );
}
