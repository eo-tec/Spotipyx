import { useState } from 'react';

interface ConfigPanelProps {
  onConnect: (pixieId: number, brokerUrl: string) => void;
  onDisconnect: () => void;
  connected: boolean;
  connecting: boolean;
}

export default function ConfigPanel({
  onConnect,
  onDisconnect,
  connected,
  connecting,
}: ConfigPanelProps) {
  const [pixieId, setPixieId] = useState(() => {
    return localStorage.getItem('sim_pixieId') || '12';
  });
  const [brokerUrl, setBrokerUrl] = useState(() => {
    const saved = localStorage.getItem('sim_brokerUrl');
    // Migrate old default that pointed to non-existent remote WS port
    if (saved && saved.includes('mypixelframe.com:9001')) {
      localStorage.removeItem('sim_brokerUrl');
      return 'ws://localhost:9001';
    }
    return saved || 'ws://localhost:9001';
  });

  const handleConnect = () => {
    const id = parseInt(pixieId, 10);
    if (isNaN(id) || id <= 0) return;
    localStorage.setItem('sim_pixieId', pixieId);
    localStorage.setItem('sim_brokerUrl', brokerUrl);
    onConnect(id, brokerUrl);
  };

  return (
    <div className="panel config-panel">
      <h3>Connection</h3>
      <div className="field">
        <label>Pixie ID</label>
        <input
          type="number"
          value={pixieId}
          onChange={(e) => setPixieId(e.target.value)}
          placeholder="e.g. 42"
          disabled={connected || connecting}
        />
      </div>
      <div className="field">
        <label>Broker URL</label>
        <input
          type="text"
          value={brokerUrl}
          onChange={(e) => setBrokerUrl(e.target.value)}
          placeholder="ws://..."
          disabled={connected || connecting}
        />
      </div>
      {!connected ? (
        <button onClick={handleConnect} disabled={connecting || !pixieId}>
          {connecting ? 'Connecting...' : 'Connect'}
        </button>
      ) : (
        <button onClick={onDisconnect} className="disconnect">
          Disconnect
        </button>
      )}
      <div className={`status ${connected ? 'online' : 'offline'}`}>
        {connected ? 'Connected' : 'Disconnected'}
      </div>
    </div>
  );
}
