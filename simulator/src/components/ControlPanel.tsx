interface ControlPanelProps {
  brightness: number;
  onBrightnessChange: (val: number) => void;
  spotifyEnabled: boolean;
  onSpotifyToggle: () => void;
  clockEnabled: boolean;
  onClockToggle: () => void;
  onPrevPhoto: () => void;
  onNextPhoto: () => void;
  connected: boolean;
}

export default function ControlPanel({
  brightness,
  onBrightnessChange,
  spotifyEnabled,
  onSpotifyToggle,
  clockEnabled,
  onClockToggle,
  onPrevPhoto,
  onNextPhoto,
  connected,
}: ControlPanelProps) {
  return (
    <div className="panel control-panel">
      <h3>Controls</h3>

      <div className="field">
        <label>Brightness: {brightness}</label>
        <input
          type="range"
          min={0}
          max={255}
          value={brightness}
          onChange={(e) => onBrightnessChange(parseInt(e.target.value, 10))}
          disabled={!connected}
        />
      </div>

      <div className="button-row">
        <button onClick={onPrevPhoto} disabled={!connected}>
          Prev
        </button>
        <button onClick={onNextPhoto} disabled={!connected}>
          Next
        </button>
      </div>

      <div className="toggle-row">
        <label>
          <input
            type="checkbox"
            checked={spotifyEnabled}
            onChange={onSpotifyToggle}
            disabled={!connected}
          />
          Spotify
        </label>
        <label>
          <input
            type="checkbox"
            checked={clockEnabled}
            onChange={onClockToggle}
            disabled={!connected}
          />
          Clock
        </label>
      </div>
    </div>
  );
}
