import { useRef, useEffect, forwardRef, useImperativeHandle } from 'react';

interface PixelPanelProps {
  scale?: number;
  onCanvasReady?: (canvas: HTMLCanvasElement) => void;
}

export interface PixelPanelRef {
  getCanvas(): HTMLCanvasElement | null;
}

const PixelPanel = forwardRef<PixelPanelRef, PixelPanelProps>(
  ({ scale = 8, onCanvasReady }, ref) => {
    const canvasRef = useRef<HTMLCanvasElement>(null);

    useImperativeHandle(ref, () => ({
      getCanvas: () => canvasRef.current,
    }));

    useEffect(() => {
      if (canvasRef.current && onCanvasReady) {
        onCanvasReady(canvasRef.current);
      }
    }, [onCanvasReady]);

    return (
      <canvas
        ref={canvasRef}
        width={64 * scale}
        height={64 * scale}
        style={{
          imageRendering: 'pixelated',
          border: '2px solid #333',
          borderRadius: 8,
          background: '#000',
        }}
      />
    );
  }
);

PixelPanel.displayName = 'PixelPanel';

export default PixelPanel;
