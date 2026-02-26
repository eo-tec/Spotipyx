// RGB565 color utilities matching ESP32 DMA display behavior

/** Convert 8-bit RGB to RGB565 (same as dma_display->color565) */
export function color565(r: number, g: number, b: number): number {
  return ((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3);
}

/** Extract 8-bit RGB from RGB565 */
export function rgb565ToRgb(c: number): [number, number, number] {
  const r = ((c >> 11) & 0x1f) << 3;
  const g = ((c >> 5) & 0x3f) << 2;
  const b = (c & 0x1f) << 3;
  return [r, g, b];
}

/** Scale an RGB565 color by a factor (0..1) for fade animations */
export function scaleColor565(c: number, factor: number): number {
  const [r, g, b] = rgb565ToRgb(c);
  return color565(
    Math.round(r * factor),
    Math.round(g * factor),
    Math.round(b * factor)
  );
}
