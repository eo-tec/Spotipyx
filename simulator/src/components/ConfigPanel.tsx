import { useState } from 'react';

interface ConfigPanelProps {
  onConnect: (pixieId: number, brokerUrl: string, username: string, password: string) => void;
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
    return localStorage.getItem('sim_brokerUrl') || 'ws://localhost:9001';
  });
  const [username, setUsername] = useState(() => {
    return localStorage.getItem('sim_username') || 'server';
  });
  const [password, setPassword] = useState(() => {
    return localStorage.getItem('sim_password') || '';
  });

  const handleConnect = () => {
    const id = parseInt(pixieId, 10);
    if (isNaN(id) || id <= 0) return;
    localStorage.setItem('sim_pixieId', pixieId);
    localStorage.setItem('sim_brokerUrl', brokerUrl);
    localStorage.setItem('sim_username', username);
    localStorage.setItem('sim_password', password);
    onConnect(id, brokerUrl, username, password);
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
      <div className="field">
        <label>Username</label>
        <input
          type="text"
          value={username}
          onChange={(e) => setUsername(e.target.value)}
          placeholder="MQTT username"
          disabled={connected || connecting}
        />
      </div>
      <div className="field">
        <label>Password</label>
        <input
          type="password"
          value={password}
          onChange={(e) => setPassword(e.target.value)}
          placeholder="MQTT password"
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
