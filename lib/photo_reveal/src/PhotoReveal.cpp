#include "PhotoReveal.h"

#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <Fonts/Picopixel.h>

namespace reveal {

namespace {

const float CENTER = SIZE / 2.0f;

// ---------------------------------------------------------------------------------------
// Utilidades

struct Rgb {
  float r, g, b;
};

float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }
float lerp(float a, float b, float k) { return a + (b - a) * k; }
Rgb mix(Rgb a, Rgb b, float k) { return {lerp(a.r, b.r, k), lerp(a.g, b.g, k), lerp(a.b, b.b, k)}; }
Rgb scale(Rgb c, float k) { return {c.r * k, c.g * k, c.b * k}; }

// Progreso 0..1 de un tramo [start, start + length) del tiempo.
float phase(uint32_t t, uint32_t start, uint32_t length) {
  if (t <= start) return 0;
  return clamp01((float)(t - start) / length);
}

float easeInQuad(float k) { return k * k; }
float easeOutCubic(float k) { return 1 - (1 - k) * (1 - k) * (1 - k); }
float easeInOutCubic(float k) { return k < 0.5f ? 4 * k * k * k : 1 - powf(-2 * k + 2, 3) / 2; }
float easeOutBack(float k) {
  const float c1 = 1.70158f, c3 = c1 + 1;
  return 1 + c3 * powf(k - 1, 3) + c1 * powf(k - 1, 2);
}
float smoothstep(float a, float b, float v) {
  float k = clamp01((v - a) / (b - a));
  return k * k * (3 - 2 * k);
}

// Ruido determinista por píxel: el mismo valor en el ESP32 y en el Mac.
float hash(int x, int y, uint32_t seed) {
  uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + seed * 2246822519u;
  h = (h ^ (h >> 13)) * 1274126177u;
  h ^= h >> 16;
  return (h & 0xFFFFFF) / 16777216.0f;
}

Rgb fetch(const Source &src, int x, int y) {
  x = x < 0 ? 0 : (x >= SIZE ? SIZE - 1 : x);
  y = y < 0 ? 0 : (y >= SIZE ? SIZE - 1 : y);
  uint8_t r, g, b;
  src.get(x, y, r, g, b);
  return {(float)r, (float)g, (float)b};
}

// Muestreo bilineal en coordenadas de píxel (el centro del píxel x está en x).
Rgb sample(const Source &src, float x, float y) {
  int x0 = (int)floorf(x), y0 = (int)floorf(y);
  float fx = x - x0, fy = y - y0;
  Rgb top = mix(fetch(src, x0, y0), fetch(src, x0 + 1, y0), fx);
  Rgb bottom = mix(fetch(src, x0, y0 + 1), fetch(src, x0 + 1, y0 + 1), fx);
  return mix(top, bottom, fy);
}

uint8_t toByte(float v) { return v <= 0 ? 0 : (v >= 255 ? 255 : (uint8_t)(v + 0.5f)); }
void put(Canvas &out, int x, int y, Rgb c) { out.set(x, y, toByte(c.r), toByte(c.g), toByte(c.b)); }
void addGlow(Canvas &out, int x, int y, Rgb c) { out.add(x, y, (int)c.r, (int)c.g, (int)c.b); }

// Mezcla `c` sobre el píxel con opacidad `a` (para bordes suaves).
void blend(Canvas &out, int x, int y, Rgb c, float a) {
  if (x < 0 || y < 0 || x >= SIZE || y >= SIZE || a <= 0) return;
  Rgb under = fetch(out, x, y);
  put(out, x, y, mix(under, c, clamp01(a)));
}

// Una "chispa" con posición de subpíxel: reparte la luz entre los 4 vecinos.
void splat(Canvas &out, float x, float y, Rgb c) {
  int x0 = (int)floorf(x), y0 = (int)floorf(y);
  float fx = x - x0, fy = y - y0;
  addGlow(out, x0, y0, scale(c, (1 - fx) * (1 - fy)));
  addGlow(out, x0 + 1, y0, scale(c, fx * (1 - fy)));
  addGlow(out, x0, y0 + 1, scale(c, (1 - fx) * fy));
  addGlow(out, x0 + 1, y0 + 1, scale(c, fx * fy));
}

// Los colores del firmware van en RGB565 con los canales del panel (que llegan rotados,
// ver photos.cpp): el campo rojo es el azul real, el verde el rojo y el azul el verde.
Rgb panel565(uint16_t c) {
  return {(float)(((c >> 5) & 0x3F) << 2), (float)((c & 0x1F) << 3), (float)(((c >> 11) & 0x1F) << 3)};
}

// La pantalla anterior oscurecida (k = 1 tal cual, 0 negro).
void dimmed(const Source &from, float k, Canvas &out) {
  for (int y = 0; y < SIZE; y++)
    for (int x = 0; x < SIZE; x++) put(out, x, y, scale(fetch(from, x, y), k));
}

void fillRect(Canvas &out, int x0, int y0, int x1, int y1, Rgb c) {
  for (int y = y0; y <= y1; y++)
    for (int x = x0; x <= x1; x++) put(out, x, y, c);
}

// Rectángulo con bordes en coordenadas de subpíxel, con el borde antialiasado (para que
// los zooms no vayan a saltos de un píxel).
void fillRectF(Canvas &out, float x0, float y0, float x1, float y1, Rgb c) {
  for (int y = (int)floorf(y0); y <= (int)ceilf(y1); y++)
    for (int x = (int)floorf(x0); x <= (int)ceilf(x1); x++) {
      float cx = clamp01(fminf(x + 1, x1) - fmaxf(x, x0));
      float cy = clamp01(fminf(y + 1, y1) - fmaxf(y, y0));
      blend(out, x, y, c, cx * cy);
    }
}

// La foto nueva dentro del cuadrado (ox, oy, lado size) en subpíxeles. Al reducirla
// promedia 2x2 muestras para que no salga ruido; a tamaño completo (size = 64,
// ox = oy = 0) cae exactamente en los píxeles originales.
template <typename Shade>
void drawPhoto(Canvas &out, const Source &photo, float ox, float oy, float size, Shade shade) {
  float k = SIZE / size;  // píxeles de foto por píxel de pantalla
  float spread = (k - 1) * 0.25f;
  for (int y = (int)floorf(oy); y < (int)ceilf(oy + size); y++)
    for (int x = (int)floorf(ox); x < (int)ceilf(ox + size); x++) {
      if (x < 0 || y < 0 || x >= SIZE || y >= SIZE) continue;
      float coverage = clamp01(fminf(x + 1, ox + size) - fmaxf(x, ox)) *
                       clamp01(fminf(y + 1, oy + size) - fmaxf(y, oy));
      float sx = (x + 0.5f - ox) * k - 0.5f, sy = (y + 0.5f - oy) * k - 0.5f;
      Rgb c;
      if (spread < 0.01f) {
        c = sample(photo, sx, sy);
      } else {
        Rgb a = sample(photo, sx - spread, sy - spread), b = sample(photo, sx + spread, sy - spread);
        Rgb d = sample(photo, sx - spread, sy + spread), e = sample(photo, sx + spread, sy + spread);
        c = {(a.r + b.r + d.r + e.r) / 4, (a.g + b.g + d.g + e.g) / 4, (a.b + b.b + d.b + e.b) / 4};
      }
      // Coordenadas normalizadas 0..1 dentro de la foto, para efectos por zona.
      c = shade(c, (x + 0.5f - ox) / size, (y + 0.5f - oy) / size);
      blend(out, x, y, c, coverage);
    }
}

Rgb noShade(Rgb c, float, float) { return c; }

Rgb average(const Source &photo) {
  float r = 0, g = 0, b = 0;
  for (int y = 0; y < SIZE; y += 4)
    for (int x = 0; x < SIZE; x += 4) {
      Rgb c = fetch(photo, x, y);
      r += c.r, g += c.g, b += c.b;
    }
  const float n = (SIZE / 4) * (SIZE / 4);
  return {r / n, g / n, b / n};
}

// ---------------------------------------------------------------------------------------
// Texto con la misma fuente que el resto del firmware (Picopixel de Adafruit GFX).

int textWidth(const char *text, int maxChars) {
  int w = 0;
  for (int i = 0; text[i] && i < maxChars; i++) {
    uint8_t ch = (uint8_t)text[i];
    if (ch < Picopixel.first || ch > Picopixel.last) continue;
    w += Picopixel.glyph[ch - Picopixel.first].xAdvance;
  }
  return w;
}

void drawText(Canvas &out, const char *text, int x, int baseline, Rgb color, int maxChars) {
  for (int i = 0; text[i] && i < maxChars; i++) {
    uint8_t ch = (uint8_t)text[i];
    if (ch < Picopixel.first || ch > Picopixel.last) continue;  // tildes y demás: fuera
    const GFXglyph &glyph = Picopixel.glyph[ch - Picopixel.first];
    const uint8_t *bitmap = Picopixel.bitmap + glyph.bitmapOffset;
    int bit = 0;
    for (int gy = 0; gy < glyph.height; gy++)
      for (int gx = 0; gx < glyph.width; gx++, bit++)
        if (bitmap[bit >> 3] & (0x80 >> (bit & 7)))
          put(out, x + glyph.xOffset + gx, baseline + glyph.yOffset + gy, color);
    x += glyph.xAdvance;
  }
}

// El backend ya manda el nombre sin tildes, pero por si acaso: las letras latinas de
// UTF-8 (á, ñ, ü...) pasan a su versión sin tilde y el resto de no ASCII se quita.
void foldAscii(const char *in, char *out, int size) {
  static const char LATIN1[] = "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYTsaaaaaaaceeeeiiiidnooooo/ouuuuyty";
  int n = 0;
  for (int i = 0; in[i] && n < size - 1; i++) {
    uint8_t c = (uint8_t)in[i];
    if (c < 0x80) {
      out[n++] = (char)c;
    } else if (c == 0xC3 && (uint8_t)in[i + 1] >= 0x80 && (uint8_t)in[i + 1] <= 0xBF) {
      out[n++] = LATIN1[(uint8_t)in[++i] - 0x80];
    }
  }
  out[n] = 0;
}

// "<prefijo><nombre>" que quepa en maxWidth píxeles. Prueba en este orden: el nombre
// entero con prefijo, solo la primera palabra con prefijo, la primera palabra sola y, si
// aun así no cabe, la primera palabra cortada con "..".
void fitName(const char *prefix, const char *sender, int maxWidth, char *out, int size) {
  char name[40], first[40];
  foldAscii(sender ? sender : "", name, sizeof(name));
  int len = 0;
  while (name[len] && name[len] != ' ' && len < (int)sizeof(first) - 1) first[len] = name[len], len++;
  first[len] = 0;
  snprintf(out, size, "%s%s", prefix, name);
  if (textWidth(out, size) <= maxWidth) return;
  snprintf(out, size, "%s%s", prefix, first);
  if (textWidth(out, size) <= maxWidth) return;
  snprintf(out, size, "%s", first);
  for (int cut = len; cut > 1 && textWidth(out, size) > maxWidth; cut--) snprintf(out, size, "%.*s..", cut - 1, first);
}

// ---------------------------------------------------------------------------------------
// CLASSIC: la animación que ya tenía el firmware (displayPhotoFromCenter), con sus
// tiempos aproximados en el panel real, para poder comparar.

const uint32_t CLASSIC_SQUARE_MS = 240;
const uint32_t CLASSIC_SQUARES = 5;
const uint32_t CLASSIC_REVEAL_MS = 420;
const uint32_t CLASSIC_MS = CLASSIC_SQUARE_MS * CLASSIC_SQUARES + CLASSIC_REVEAL_MS;

void renderClassic(const Source &from, const Source &to, uint32_t t, Canvas &out) {
  static const uint16_t colors[CLASSIC_SQUARES] = {0x3080, 0x56AA, 0xF6BD, 0xF680, 0xF746};
  uint32_t squaresEnd = CLASSIC_SQUARE_MS * CLASSIC_SQUARES;
  if (t < squaresEnd) {
    int index = t / CLASSIC_SQUARE_MS;
    float k = (float)(t % CLASSIC_SQUARE_MS) / CLASSIC_SQUARE_MS;
    if (index == 0) out.copyFrom(from);
    else fillRect(out, 0, 0, SIZE - 1, SIZE - 1, panel565(colors[index - 1]));
    int half = (int)((2 + k * (SIZE - 2)) / 2);
    fillRect(out, 32 - half, 32 - half, 31 + half, 31 + half, panel565(colors[index]));
    return;
  }
  // La foto aparece en anillos cuadrados desde el centro.
  float radius = phase(t, squaresEnd, CLASSIC_REVEAL_MS) * 33;
  for (int y = 0; y < SIZE; y++)
    for (int x = 0; x < SIZE; x++) {
      float d = fmaxf(fabsf(x + 0.5f - CENTER), fabsf(y + 0.5f - CENTER));
      put(out, x, y, d <= radius ? fetch(to, x, y) : panel565(colors[CLASSIC_SQUARES - 1]));
    }
}

// ---------------------------------------------------------------------------------------
// POLAROID: la foto llega impresa. Cae una polaroid sobre la pantalla anterior, la
// imagen se revela como la química de verdad (de un gris azulado oscuro a color) y en el
// margen de abajo se escribe "from <nombre>" letra a letra. Luego la cámara se acerca
// hasta que la foto ocupa todo el panel.

const uint32_t POLA_DROP_MS = 650;
const uint32_t POLA_DEVELOP_START = 550;
const uint32_t POLA_DEVELOP_MS = 2000;
const uint32_t POLA_TEXT_START = 900;
const uint32_t POLA_ZOOM_START = 3100;
const uint32_t POLA_ZOOM_MS = 900;
const uint32_t POLA_MS = POLA_ZOOM_START + POLA_ZOOM_MS;

const float POLA_PHOTO = 38;         // lado de la foto dentro de la polaroid
const float POLA_SIDE = 3;           // marco a los lados y arriba
const float POLA_BOTTOM = 13;        // el margen ancho de abajo
const Rgb POLA_PAPER = {205, 200, 188};
const Rgb POLA_INK = {30, 40, 95};
const Rgb POLA_UNDEVELOPED = {14, 22, 26};

void renderPolaroid(const Source &from, const Source &to, const char *sender, uint32_t t, Canvas &out) {
  float zoom = easeInOutCubic(phase(t, POLA_ZOOM_START, POLA_ZOOM_MS));
  float drop = easeOutBack(phase(t, 0, POLA_DROP_MS));

  // Fondo: lo anterior se apaga mientras cae la polaroid y desaparece en el zoom.
  dimmed(from, lerp(1.0f, 0.3f, smoothstep(0, 1, phase(t, 0, POLA_DROP_MS))) * (1 - zoom), out);

  // Geometría: la foto pasa de 38 px en la polaroid a ocupar el panel.
  float size = lerp(POLA_PHOTO, SIZE, zoom);
  float unit = size / POLA_PHOTO;
  float cardH = (POLA_PHOTO + POLA_SIDE + POLA_BOTTOM) * unit;
  float restTop = (SIZE - (POLA_PHOTO + POLA_SIDE + POLA_BOTTOM)) / 2 + POLA_SIDE;  // y de la foto
  float photoY = lerp(lerp(-cardH - 2, restTop, drop), 0, zoom);
  float photoX = (SIZE - size) / 2;
  float cardX0 = photoX - POLA_SIDE * unit, cardY0 = photoY - POLA_SIDE * unit;
  float cardX1 = photoX + size + POLA_SIDE * unit, cardY1 = photoY + size + POLA_BOTTOM * unit;

  // Sombra suave a la derecha y abajo, y el papel.
  if (zoom < 1) {
    fillRectF(out, cardX0 + 1, cardY0 + 1, cardX1 + 1, cardY1 + 1, Rgb{0, 0, 0});
    fillRectF(out, cardX0, cardY0, cardX1, cardY1, POLA_PAPER);
  }

  // El revelado: cada píxel avanza con un poco de ruido y los bordes van algo por
  // detrás del centro, como la emulsión real.
  float develop = phase(t, POLA_DEVELOP_START, POLA_DEVELOP_MS);
  drawPhoto(out, to, photoX, photoY, size, [&](Rgb c, float u, float v) -> Rgb {
    if (develop >= 1) return c;
    float edge = fmaxf(fabsf(u - 0.5f), fabsf(v - 0.5f)) * 2;  // 0 centro, 1 borde
    float k = clamp01(develop * 1.35f - edge * 0.2f - hash((int)(u * 38), (int)(v * 38), 7) * 0.15f);
    float lum = (c.r * 0.3f + c.g * 0.55f + c.b * 0.15f);
    Rgb ghost = {lum * 0.55f, lum * 0.65f, lum * 0.75f};       // primero sale en frío y apagada
    Rgb warm = {c.r * 1.05f + 12, c.g * 0.95f + 4, c.b * 0.8f};  // luego el color, algo cálido
    if (k < 0.45f) return mix(POLA_UNDEVELOPED, ghost, smoothstep(0, 0.45f, k));
    if (k < 0.8f) return mix(ghost, warm, smoothstep(0.45f, 0.8f, k));
    return mix(warm, c, smoothstep(0.8f, 1, k));
  });

  // "from <nombre>" a mano en el margen, una letra cada 70 ms.
  if (zoom < 0.25f && sender && sender[0]) {
    char line[48];
    fitName("from ", sender, (int)(POLA_PHOTO + 2 * POLA_SIDE) - 2, line, sizeof(line));
    int chars = t < POLA_TEXT_START ? 0 : (int)((t - POLA_TEXT_START) / 70);
    int width = textWidth(line, sizeof(line));
    int textX = (int)roundf(CENTER - width / 2.0f);
    int baseline = (int)roundf(photoY + size + 8);
    drawText(out, line, textX, baseline, POLA_INK, chars);
  }
}

// ---------------------------------------------------------------------------------------
// STARDUST: la pantalla anterior se apaga y aparece un orbe que respira con el color
// medio de la foto nueva (un adelanto de lo que viene). Estalla, y cada píxel de la foto
// sale disparado en espiral hasta su sitio, brillando al aterrizar.

const uint32_t STAR_ORB_MS = 900;
const uint32_t STAR_LAUNCH_SPREAD = 900;  // los píxeles salen escalonados en este tramo
const uint32_t STAR_FLIGHT_MS = 1000;
const uint32_t STAR_SPARKLE_MS = 260;
const uint32_t STAR_MS = STAR_ORB_MS + STAR_LAUNCH_SPREAD + STAR_FLIGHT_MS + STAR_SPARKLE_MS;

void renderStardust(const Source &from, const Source &to, uint32_t t, Canvas &out) {
  Rgb tint = average(to);
  float tintMax = fmaxf(tint.r, fmaxf(tint.g, tint.b));
  Rgb orbColor = tintMax > 1 ? scale(tint, 255 / tintMax) : Rgb{255, 255, 255};  // saturado

  float launched = phase(t, STAR_ORB_MS, STAR_LAUNCH_SPREAD + STAR_FLIGHT_MS);
  dimmed(from, lerp(1.0f, 0.22f, smoothstep(0, 1, phase(t, 0, STAR_ORB_MS))) * (1 - launched), out);

  // Cada píxel de la foto es una partícula. Las del centro salen antes.
  for (int y = 0; y < SIZE; y++)
    for (int x = 0; x < SIZE; x++) {
      float dx = x + 0.5f - CENTER, dy = y + 0.5f - CENTER;
      float dist = sqrtf(dx * dx + dy * dy) / 45.0f;
      uint32_t launch = STAR_ORB_MS + (uint32_t)((hash(x, y, 1) * 0.6f + dist * 0.4f) * STAR_LAUNCH_SPREAD);
      if (t < launch) continue;
      Rgb c = fetch(to, x, y);
      uint32_t since = t - launch;
      if (since >= STAR_FLIGHT_MS) {
        // En su sitio, con un destello que se apaga.
        float sparkle = 1 - phase(since, STAR_FLIGHT_MS, STAR_SPARKLE_MS);
        put(out, x, y, mix(c, {255, 255, 255}, sparkle * sparkle * 0.7f));
        continue;
      }
      float k = easeOutCubic((float)since / STAR_FLIGHT_MS);
      float angle = atan2f(dy, dx) + (1 - k) * (2.4f + hash(x, y, 2) * 1.2f);
      float radius = sqrtf(dx * dx + dy * dy) * k;
      float px = CENTER - 0.5f + cosf(angle) * radius, py = CENTER - 0.5f + sinf(angle) * radius;
      Rgb hot = mix(c, {255, 255, 255}, (1 - k) * 0.5f);
      splat(out, px, py, scale(hot, 0.2f + 0.8f * k));
    }

  // El orbe: crece, respira dos veces y se va vaciando según salen las partículas.
  if (t < STAR_ORB_MS + STAR_LAUNCH_SPREAD) {
    float grow = easeOutBack(phase(t, 150, 500));
    float breathe = 1 + 0.18f * sinf(phase(t, 150, STAR_ORB_MS - 150) * 6.2832f * 2);
    float empty = 1 - phase(t, STAR_ORB_MS, STAR_LAUNCH_SPREAD);
    float radius = 3.2f * grow * breathe * empty;
    for (int y = 20; y < 44; y++)
      for (int x = 20; x < 44; x++) {
        float d = hypotf(x + 0.5f - CENTER, y + 0.5f - CENTER);
        float core = clamp01(radius + 0.5f - d);
        float halo = radius > 0 ? expf(-(d - radius) * (d - radius) / (radius * 1.5f)) * 0.5f : 0;
        if (core <= 0 && halo < 0.02f) continue;
        blend(out, x, y, mix(orbColor, {255, 255, 255}, 0.35f), core);
        if (d > radius) addGlow(out, x, y, scale(orbColor, halo));
      }
  }
}

// ---------------------------------------------------------------------------------------
// LETTER: cae un sobre cerrado y se sacude un poco, como un aviso. La solapa se abre,
// sale la foto como una tarjeta y, ya fuera, se acerca hasta llenar el panel mientras
// el sobre se hunde.

const uint32_t LETTER_DROP_MS = 700;
const uint32_t LETTER_SHAKE_START = 800;
const uint32_t LETTER_SHAKE_MS = 450;
const uint32_t LETTER_OPEN_START = 1350;
const uint32_t LETTER_OPEN_MS = 450;
const uint32_t LETTER_RISE_START = 1750;
const uint32_t LETTER_RISE_MS = 700;
const uint32_t LETTER_ZOOM_START = 2600;
const uint32_t LETTER_ZOOM_MS = 850;
const uint32_t LETTER_MS = LETTER_ZOOM_START + LETTER_ZOOM_MS;

const int ENV_W = 48, ENV_H = 32;
const Rgb ENV_PAPER = {222, 205, 172};
const Rgb ENV_SHADE = {175, 155, 125};   // pliegues
const Rgb ENV_INSIDE = {120, 100, 80};   // el interior, que se ve al abrir
const Rgb ENV_FLAP = {205, 186, 152};

// Triángulo relleno con antialiasado por supermuestreo (2x2).
void fillTriangle(Canvas &out, float ax, float ay, float bx, float by, float cx, float cy, Rgb color) {
  auto edge = [](float x0, float y0, float x1, float y1, float x, float y) {
    return (x1 - x0) * (y - y0) - (y1 - y0) * (x - x0);
  };
  float area = edge(ax, ay, bx, by, cx, cy);
  if (fabsf(area) < 0.01f) return;
  int minX = (int)floorf(fminf(ax, fminf(bx, cx))), maxX = (int)ceilf(fmaxf(ax, fmaxf(bx, cx)));
  int minY = (int)floorf(fminf(ay, fminf(by, cy))), maxY = (int)ceilf(fmaxf(ay, fmaxf(by, cy)));
  for (int y = minY; y <= maxY; y++)
    for (int x = minX; x <= maxX; x++) {
      float hits = 0;
      for (int s = 0; s < 4; s++) {
        float px = x + 0.25f + 0.5f * (s & 1), py = y + 0.25f + 0.5f * (s >> 1);
        float w0 = edge(bx, by, cx, cy, px, py), w1 = edge(cx, cy, ax, ay, px, py),
              w2 = edge(ax, ay, bx, by, px, py);
        if (area < 0 ? (w0 <= 0 && w1 <= 0 && w2 <= 0) : (w0 >= 0 && w1 >= 0 && w2 >= 0)) hits += 0.25f;
      }
      blend(out, x, y, color, hits);
    }
}

void line(Canvas &out, float x0, float y0, float x1, float y1, Rgb color) {
  int steps = (int)ceilf(fmaxf(fabsf(x1 - x0), fabsf(y1 - y0)));
  for (int i = 0; i <= steps; i++) {
    float k = steps ? (float)i / steps : 0;
    put(out, (int)roundf(lerp(x0, x1, k)), (int)roundf(lerp(y0, y1, k)), color);
  }
}

void renderLetter(const Source &from, const Source &to, uint32_t t, Canvas &out) {
  float zoom = easeInOutCubic(phase(t, LETTER_ZOOM_START, LETTER_ZOOM_MS));
  float background = lerp(1.0f, 0.3f, smoothstep(0, 1, phase(t, 0, LETTER_DROP_MS))) * (1 - zoom);
  dimmed(from, background, out);

  // El sobre cae con rebote y, en el zoom, se hunde por abajo.
  float drop = easeOutBack(phase(t, 0, LETTER_DROP_MS));
  float shake = phase(t, LETTER_SHAKE_START, LETTER_SHAKE_MS);
  float x0 = (SIZE - ENV_W) / 2.0f + sinf(shake * 3.14159f * 6) * 1.5f * (1 - shake);
  float restY = 24;
  float y0 = lerp(-ENV_H - 18, restY, drop) + easeInQuad(zoom) * 50;
  float x1 = x0 + ENV_W, y1 = y0 + ENV_H, cx = x0 + ENV_W / 2.0f;

  float open = easeInOutCubic(phase(t, LETTER_OPEN_START, LETTER_OPEN_MS));
  float flapTip = lerp(y0 + 18, y0 - 15, open);  // punta de la solapa: de abajo a arriba

  // Detrás: el interior del sobre y, si ya está abierta, la solapa levantada.
  fillRectF(out, x0, y0, x1, y1, ENV_INSIDE);
  if (open > 0.5f) fillTriangle(out, x0, y0, x1, y0, cx, flapTip, ENV_FLAP);

  // La tarjeta con la foto: 36 px con un marco blanco de 1 px. Sube desde dentro del
  // sobre y en el zoom pasa a ocupar el panel.
  float rise = easeOutCubic(phase(t, LETTER_RISE_START, LETTER_RISE_MS));
  float cardPhoto = 34;
  float restPhotoY = lerp(y0 + 14, restY - 17, rise);
  float size = lerp(cardPhoto, SIZE, zoom);
  float photoX = lerp(cx - cardPhoto / 2, 0, zoom);
  float photoY = lerp(restPhotoY, 0, zoom);
  bool cardInFront = zoom > 0;
  auto drawCard = [&]() {
    if (t < LETTER_RISE_START) return;
    float border = 1.5f * (1 - zoom);
    if (border > 0.05f)
      fillRectF(out, photoX - border, photoY - border, photoX + size + border, photoY + size + border,
                {235, 235, 230});
    drawPhoto(out, to, photoX, photoY, size, noShade);
  };
  if (!cardInFront) {
    drawCard();
    // Mientras está dentro, lo que sobresale por debajo del sobre no se ve.
    for (int y = (int)ceilf(y1); y < SIZE; y++)
      for (int x = 0; x < SIZE; x++) put(out, x, y, scale(fetch(from, x, y), background));
  }

  // Delante: el bolsillo del sobre, con la abertura en V y los pliegues.
  float vDepth = 12;
  for (int y = (int)floorf(y0); y <= (int)ceilf(y1); y++)
    for (int x = (int)floorf(x0); x <= (int)ceilf(x1); x++) {
      float px = x + 0.5f;
      float top = y0 + vDepth * (1 - fabsf(px - cx) / (ENV_W / 2.0f));  // la V
      float cover = clamp01(fminf(y + 1.0f, y1) - fmaxf((float)y, top)) * clamp01(fminf(x + 1.0f, x1) - fmaxf((float)x, x0));
      blend(out, x, y, ENV_PAPER, cover);
    }
  line(out, x0, y0, cx, y0 + vDepth, ENV_SHADE);
  line(out, x1 - 1, y0, cx, y0 + vDepth, ENV_SHADE);
  line(out, x0, y1 - 1, cx - 3, y0 + vDepth + 7, ENV_SHADE);
  line(out, x1 - 1, y1 - 1, cx + 3, y0 + vDepth + 7, ENV_SHADE);

  // Cerrado: la solapa encima del bolsillo.
  if (open <= 0.5f) {
    fillTriangle(out, x0, y0, x1, y0, cx, flapTip, ENV_FLAP);
    line(out, x0, y0, cx, flapTip - 1, ENV_SHADE);
    line(out, x1 - 1, y0, cx, flapTip - 1, ENV_SHADE);
  }

  if (cardInFront) drawCard();
}

// ---------------------------------------------------------------------------------------
// DROP: una gota cae en el centro de la pantalla anterior. Al tocar, una onda se abre
// en círculo; dentro ya está la foto nueva, que todavía ondula (refracción) y se va
// calmando. El frente de la onda brilla.

const uint32_t DROP_FALL_MS = 420;
const uint32_t DROP_WAVE_MS = 1500;
const uint32_t DROP_SETTLE_MS = 700;
const uint32_t DROP_MS = DROP_FALL_MS + DROP_WAVE_MS + DROP_SETTLE_MS;

void renderDrop(const Source &from, const Source &to, uint32_t t, Canvas &out) {
  if (t < DROP_FALL_MS) {
    // La gota cae acelerando, con su estela.
    dimmed(from, lerp(1.0f, 0.55f, phase(t, 0, DROP_FALL_MS)), out);
    float y = lerp(-3, CENTER - 0.5f, easeInQuad(phase(t, 0, DROP_FALL_MS)));
    for (int i = 0; i < 5; i++) {
      Rgb c = scale({210, 235, 255}, 1 - i * 0.2f);
      splat(out, CENTER - 1, y - i * 1.4f, c);
      splat(out, CENTER, y - i * 1.4f, c);
    }
    return;
  }
  float k = phase(t, DROP_FALL_MS, DROP_WAVE_MS);
  float front = 47 * (1 - (1 - k) * (1 - k));
  float calm = 1 - phase(t, DROP_FALL_MS + DROP_WAVE_MS / 3, DROP_WAVE_MS * 2 / 3 + DROP_SETTLE_MS);
  for (int y = 0; y < SIZE; y++)
    for (int x = 0; x < SIZE; x++) {
      float dx = x + 0.5f - CENTER, dy = y + 0.5f - CENTER;
      float d = sqrtf(dx * dx + dy * dy);
      float behind = front - d;  // cuánto ha pasado ya la onda por este píxel
      float nx = d > 0.01f ? dx / d : 0, ny = d > 0.01f ? dy / d : 0;
      Rgb c;
      if (behind > 0) {
        // Foto nueva con ondas concéntricas que se amortiguan detrás del frente.
        float wave = sinf(behind * 0.85f) * 2.2f * expf(-behind * 0.07f) * calm;
        c = sample(to, x + nx * wave, y + ny * wave);
        float light = cosf(behind * 0.85f) * expf(-behind * 0.07f) * calm * 28;  // reflejos
        c = {c.r + light, c.g + light, c.b + light};
      } else {
        // Por delante, la pantalla anterior se deforma un poco al acercarse la onda.
        float push = expf(behind * 0.35f) * 1.5f;
        c = scale(sample(from, x - nx * push, y - ny * push), 0.55f);
      }
      // El frente: una cresta luminosa con el borde suave.
      float crest = expf(-behind * behind / 3.0f) * (1 - k * 0.6f);
      c = mix(c, {225, 240, 255}, crest * 0.75f);
      put(out, x, y, c);
    }
}

// ---------------------------------------------------------------------------------------
// Utilidades de los estilos de objetos (postal, película, impresora)

int imin(int a, int b) { return a < b ? a : b; }
int imax(int a, int b) { return a > b ? a : b; }

// Estrecha en horizontal, alrededor de cx, la zona [x0, x1] x [y0, y1] ya pintada (para
// voltear una tarjeta): cada columna lee la que le toca a escala `s` y lo que queda libre
// vuelve a ser el fondo. `shade` oscurece la cara cuando está de canto.
void squeeze(Canvas &out, const Source &from, float background, int x0, int x1, int y0, int y1, float cx,
             float s, float shade) {
  uint8_t row[SIZE][3];
  for (int y = imax(0, y0); y <= imin(SIZE - 1, y1); y++) {
    memcpy(row, out.px[y], sizeof(row));
    for (int x = imax(0, x0); x <= imin(SIZE - 1, x1); x++) {
      int u = s > 0.02f ? (int)roundf(cx + (x + 0.5f - cx) / s - 0.5f) : -1;
      if (u >= x0 && u <= x1 && u >= 0 && u < SIZE)
        put(out, x, y, scale({(float)row[u][0], (float)row[u][1], (float)row[u][2]}, shade));
      else
        put(out, x, y, scale(fetch(from, x, y), background));
    }
  }
}

Rgb hsv(float h, float s, float v) {
  h = (h - floorf(h)) * 6;
  int i = (int)h;
  float f = h - i, p = v * (1 - s), q = v * (1 - s * f), r = v * (1 - s * (1 - f));
  Rgb c;
  switch (i % 6) {
    case 0: c = {v, r, p}; break;
    case 1: c = {q, v, p}; break;
    case 2: c = {p, v, r}; break;
    case 3: c = {p, q, v}; break;
    case 4: c = {r, p, v}; break;
    default: c = {v, p, q}; break;
  }
  return scale(c, 255);
}

const Rgb PAPER = {228, 222, 208};
const Rgb INK = {30, 40, 95};

// ---------------------------------------------------------------------------------------
// POSTCARD: entra una postal por la derecha enseñando el reverso, con su sello y su
// matasellos. Se escribe "from <nombre>" y unas líneas a mano; luego se da la vuelta y por
// delante está la foto, que se acerca hasta llenar el panel.

const uint32_t POST_SLIDE_MS = 650;
const uint32_t POST_WRITE_START = 650;
const uint32_t POST_WRITE_MS = 1100;
const uint32_t POST_FLIP_START = 1950;
const uint32_t POST_FLIP_MS = 700;
const uint32_t POST_ZOOM_START = 2850;
const uint32_t POST_ZOOM_MS = 850;
const uint32_t POST_MS = POST_ZOOM_START + POST_ZOOM_MS;

const int POST_W = 50, POST_H = 40;
const float POST_PHOTO = 36;  // la foto en la cara delantera, con margen de papel
const Rgb POST_LINE = {165, 155, 140};

void drawPostcardBack(Canvas &out, int x, int y, const char *sender, float write) {
  fillRect(out, x, y, x + POST_W - 1, y + POST_H - 1, PAPER);
  // Sello con borde dentado: un paisaje mínimo (cielo, sol y monte).
  int sx = x + 37, sy = y + 14;
  for (int j = 0; j < 12; j++)
    for (int i = 0; i < 10; i++) {
      bool edge = i == 0 || j == 0 || i == 9 || j == 11;
      if (edge) {
        if ((i + j) % 2 == 0) put(out, sx + i, sy + j, {245, 245, 240});
      } else {
        put(out, sx + i, sy + j, j > 11 - (4 - abs(i - 4) / 2) ? Rgb{60, 140, 80} : Rgb{70, 130, 190});
      }
    }
  fillRect(out, sx + 6, sy + 2, sx + 7, sy + 3, {245, 205, 70});
  // Matasellos: dos líneas onduladas que pisan el sello.
  for (int i = 0; i < 9; i++)
    for (int k = 0; k < 2; k++)
      put(out, x + 34 + i, y + 17 + k * 3 + (int)roundf(sinf(i * 0.9f) * 0.8f), {110, 100, 95});
  // Raya del medio y renglones de la dirección.
  for (int j = 15; j < POST_H - 4; j++) put(out, x + 27, y + j, POST_LINE);
  for (int k = 0; k < 3; k++)
    for (int i = 30; i < 47; i++) put(out, x + i, y + 29 + k * 3, POST_LINE);

  // El mensaje a mano: primero "from <nombre>" y luego tres renglones de garabatos.
  // Sin remitente (el dueño lo ha ocultado) solo quedan los garabatos.
  char line[48] = "";
  if (sender && sender[0]) fitName("from ", sender, POST_W - 6, line, sizeof(line));
  int chars = (int)(clamp01(write / 0.35f) * (strlen(line) + 1));
  drawText(out, line, x + 3, y + 10, INK, chars);
  for (int k = 0; k < 3; k++) {
    float done = clamp01((write - 0.35f - k * 0.22f) / 0.22f);
    int len = k == 2 ? 14 : 22;
    for (int i = 0; i < (int)(len * done); i++) {
      if (hash(i / 4, k, 3) < 0.18f && i % 4 == 0) continue;  // huecos entre "palabras"
      put(out, x + 3 + i, y + 17 + k * 6 + (int)roundf(sinf(i * 1.4f + k) * 0.9f), INK);
    }
  }
}

void renderPostcard(const Source &from, const Source &to, const char *sender, uint32_t t, Canvas &out) {
  float zoom = easeInOutCubic(phase(t, POST_ZOOM_START, POST_ZOOM_MS));
  float background = lerp(1.0f, 0.3f, smoothstep(0, 1, phase(t, 0, POST_SLIDE_MS))) * (1 - zoom);
  dimmed(from, background, out);

  float restX = (SIZE - POST_W) / 2.0f, restY = (SIZE - POST_H) / 2.0f;
  float flip = easeInOutCubic(phase(t, POST_FLIP_START, POST_FLIP_MS));
  float angle = flip * 3.14159f;
  int x = (int)roundf(lerp(SIZE + 2, restX, easeOutBack(phase(t, 0, POST_SLIDE_MS))));
  int y = (int)roundf(restY - sinf(angle) * 2);  // al voltearla se levanta un poco
  bool front = angle > 3.14159f / 2;

  if (zoom > 0) {
    // Ya por delante: la foto se acerca y el papel crece con ella hasta salirse.
    float size = lerp(POST_PHOTO, SIZE, zoom);
    float unit = size / POST_PHOTO;
    float px = lerp(restX + 7, 0, zoom), py = lerp(restY + 2, 0, zoom);
    fillRectF(out, px - 7 * unit, py - 2 * unit, px + size + 7 * unit, py + size + 2 * unit, PAPER);
    drawPhoto(out, to, px, py, size, noShade);
    return;
  }
  if (flip <= 0) fillRect(out, x + 1, y + 1, x + POST_W, y + POST_H, {0, 0, 0});  // sombra
  if (front) {
    fillRect(out, x, y, x + POST_W - 1, y + POST_H - 1, PAPER);
    drawPhoto(out, to, x + 7, y + 2, POST_PHOTO, noShade);
  } else {
    drawPostcardBack(out, x, y, sender, phase(t, POST_WRITE_START, POST_WRITE_MS));
  }
  if (flip > 0) {
    float s = fabsf(cosf(angle));
    squeeze(out, from, background, x, x + POST_W - 1, y, y + POST_H - 1, x + POST_W / 2.0f, s, 0.55f + 0.45f * s);
  }
}

// ---------------------------------------------------------------------------------------
// FILM: la pantalla anterior se encoge hasta ser un fotograma de una tira de película.
// La tira corre como en un proyector (con su parpadeo) hasta dejar la foto nueva en el
// centro, se ilumina y se acerca hasta llenar el panel.

const uint32_t FILM_SHRINK_MS = 650;
const uint32_t FILM_SCROLL_START = 650;
const uint32_t FILM_SCROLL_MS = 1300;
const uint32_t FILM_FLASH_START = 2000;
const uint32_t FILM_FLASH_MS = 350;
const uint32_t FILM_ZOOM_START = 2300;
const uint32_t FILM_ZOOM_MS = 850;
const uint32_t FILM_MS = FILM_ZOOM_START + FILM_ZOOM_MS;

const float FILM_FRAME = 36, FILM_PITCH = 42, FILM_TOP = 8, FILM_HEIGHT = 48;
const Rgb FILM_BASE = {28, 22, 18};
const Rgb FILM_HOLE = {175, 165, 145};
const Rgb FILM_EMPTY = {48, 40, 34};

void renderFilm(const Source &from, const Source &to, uint32_t t, Canvas &out) {
  float shrink = easeInOutCubic(phase(t, 0, FILM_SHRINK_MS));
  float scroll = easeInOutCubic(phase(t, FILM_SCROLL_START, FILM_SCROLL_MS));
  float zoom = easeInOutCubic(phase(t, FILM_ZOOM_START, FILM_ZOOM_MS));
  float flash = sinf(phase(t, FILM_FLASH_START, FILM_FLASH_MS) * 3.14159f);
  // Mientras corre, la luz del proyector tiembla un poco.
  float flicker = scroll > 0 && scroll < 1 ? 1 - 0.12f * hash(t / 45, 0, 5) : 1;
  float strip = flicker * (1 - zoom);

  fillRect(out, 0, 0, SIZE - 1, SIZE - 1, {0, 0, 0});
  float offset = scroll * FILM_PITCH;
  fillRectF(out, 0, FILM_TOP, SIZE, FILM_TOP + FILM_HEIGHT, scale(FILM_BASE, strip));
  // Perforaciones arriba y abajo, que se mueven con la tira.
  for (float hx = -fmodf(offset, 6.0f) - 6; hx < SIZE; hx += 6) {
    fillRectF(out, hx + 1, FILM_TOP + 2, hx + 4, FILM_TOP + 4, scale(FILM_HOLE, strip));
    fillRectF(out, hx + 1, FILM_TOP + FILM_HEIGHT - 4, hx + 4, FILM_TOP + FILM_HEIGHT - 2, scale(FILM_HOLE, strip));
  }
  // Fotogramas: -1 y 2 sin revelar, 0 la pantalla anterior, 1 la foto nueva. La nueva
  // se pinta la última para que al crecer no la tape ninguna vecina.
  float frameY = FILM_TOP + (FILM_HEIGHT - FILM_FRAME) / 2;
  static const int ORDER[] = {-1, 0, 2, 1};
  for (int k : ORDER) {
    float fx = (SIZE - FILM_FRAME) / 2 + k * FILM_PITCH - offset;
    if (fx > SIZE || fx + FILM_FRAME < 0) continue;
    if (k == 0) {
      // La pantalla anterior empieza ocupando todo y se encoge hasta su hueco.
      float size = lerp(SIZE, FILM_FRAME, shrink);
      drawPhoto(out, from, lerp(0, fx, shrink), lerp(0, frameY, shrink), size,
                [&](Rgb c, float, float) { return scale(c, lerp(1.0f, 0.8f, shrink) * flicker); });
    } else if (k == 1) {
      float size = lerp(FILM_FRAME, SIZE, zoom);
      drawPhoto(out, to, lerp(fx, 0, zoom), lerp(frameY, 0, zoom), size, [&](Rgb c, float, float) {
        return mix(scale(c, zoom > 0 ? 1 : flicker), {255, 250, 235}, flash * 0.45f);
      });
    } else {
      fillRectF(out, fx, frameY, fx + FILM_FRAME, frameY + FILM_FRAME, scale(FILM_EMPTY, strip));
    }
  }
}

// ---------------------------------------------------------------------------------------
// PRINT: baja una impresora por arriba y la foto sale por la ranura a tirones, con el
// motor, todavía caliente justo al salir. Cuando está fuera la impresora se va y la
// foto se acerca hasta llenar el panel.

const uint32_t PRINT_IN_MS = 450;
const uint32_t PRINT_FEED_START = 550;
const uint32_t PRINT_FEED_MS = 1900;
const uint32_t PRINT_OUT_START = 2550;
const uint32_t PRINT_OUT_MS = 400;
const uint32_t PRINT_ZOOM_START = 2800;
const uint32_t PRINT_ZOOM_MS = 850;
const uint32_t PRINT_MS = PRINT_ZOOM_START + PRINT_ZOOM_MS;

const int PRINT_BODY = 10;                      // alto de la impresora
const float PRINT_PHOTO = 40, PRINT_PAPER = 46;  // la foto y el papel (2 arriba, 4 abajo)
const int PRINT_STEPS = 23;

void renderPrint(const Source &from, const Source &to, uint32_t t, Canvas &out) {
  float zoom = easeInOutCubic(phase(t, PRINT_ZOOM_START, PRINT_ZOOM_MS));
  float background = lerp(1.0f, 0.3f, smoothstep(0, 1, phase(t, 0, PRINT_IN_MS))) * (1 - zoom);
  dimmed(from, background, out);

  // El papel avanza a pasos de motor: cada paso arranca y frena.
  float feed = phase(t, PRINT_FEED_START, PRINT_FEED_MS) * PRINT_STEPS;
  float step = floorf(feed);
  float fed = feed >= PRINT_STEPS ? 1 : (step + smoothstep(0.15f, 0.75f, feed - step)) / PRINT_STEPS;
  float paperTop = PRINT_BODY + fed * PRINT_PAPER - PRINT_PAPER;
  bool feeding = t >= PRINT_FEED_START && fed < 1;

  float size = lerp(PRINT_PHOTO, SIZE, zoom);
  float unit = size / PRINT_PHOTO;
  float px = lerp((SIZE - PRINT_PHOTO) / 2, 0, zoom), py = lerp(paperTop + 2, 0, zoom);
  if (t >= PRINT_FEED_START) {
    fillRectF(out, px - 2 * unit, py - 2 * unit, px + size + 2 * unit, py + size + 4 * unit, PAPER);
    drawPhoto(out, to, px, py, size, [&](Rgb c, float, float v) {
      // Lo recién salido brilla un poco, como tinta caliente.
      float dy = py + v * size - PRINT_BODY;
      return feeding && dy < 4 ? mix(c, {255, 250, 240}, 0.4f * (1 - dy / 4)) : c;
    });
  }

  // La impresora tapa lo que aún no ha salido. Al acabar sube y se va.
  float bodyY = lerp(-PRINT_BODY - 2, 0, easeOutBack(phase(t, 0, PRINT_IN_MS))) -
                easeInQuad(phase(t, PRINT_OUT_START, PRINT_OUT_MS)) * (PRINT_BODY + 4);
  if (bodyY > -PRINT_BODY) {
    float x0 = 4, x1 = SIZE - 4;
    fillRectF(out, x0, bodyY, x1, bodyY + PRINT_BODY, {58, 60, 70});
    fillRectF(out, x0, bodyY, x1, bodyY + 1, {95, 98, 110});                                 // canto de arriba
    fillRectF(out, x0, bodyY + PRINT_BODY - 1, x1, bodyY + PRINT_BODY, {100, 103, 115});     // labio
    fillRectF(out, x0 + 6, bodyY + PRINT_BODY - 2, x1 - 6, bodyY + PRINT_BODY - 1, {5, 5, 8});  // ranura
    bool led = !feeding || (t / 160) % 2 == 0;  // parpadea mientras imprime
    fillRectF(out, x1 - 5, bodyY + 3, x1 - 3, bodyY + 5, led ? Rgb{60, 230, 90} : Rgb{20, 60, 30});
  }
}

// ---------------------------------------------------------------------------------------
// NEWPIC: la llamativa. Fogonazo, rayos de colores girando, confeti y "NEW PICTURE!"
// cayendo letra a letra. Las letras salen volando y la foto se abre en un círculo con
// el borde arcoíris.

const uint32_t NEW_FLASH_MS = 250;
const uint32_t NEW_EXIT_START = 2400;
const uint32_t NEW_IRIS_START = 2500;
const uint32_t NEW_IRIS_MS = 850;
const uint32_t NEW_MS = NEW_IRIS_START + NEW_IRIS_MS;

// Fuente gruesa de 5x7 solo con las letras que hacen falta.
struct BigGlyph {
  char ch;
  uint8_t width;
  const char *rows[7];
};
const BigGlyph BIG_FONT[] = {
    {'N', 5, {"X...X", "XX..X", "X.X.X", "X..XX", "X...X", "X...X", "X...X"}},
    {'E', 5, {"XXXXX", "X....", "X....", "XXXX.", "X....", "X....", "XXXXX"}},
    {'W', 5, {"X...X", "X...X", "X...X", "X.X.X", "X.X.X", "XX.XX", "X...X"}},
    {'P', 5, {"XXXX.", "X...X", "X...X", "XXXX.", "X....", "X....", "X...."}},
    {'I', 3, {"XXX", ".X.", ".X.", ".X.", ".X.", ".X.", "XXX"}},
    {'C', 5, {".XXX.", "X...X", "X....", "X....", "X....", "X...X", ".XXX."}},
    {'T', 5, {"XXXXX", "..X..", "..X..", "..X..", "..X..", "..X..", "..X.."}},
    {'U', 5, {"X...X", "X...X", "X...X", "X...X", "X...X", "X...X", ".XXX."}},
    {'R', 5, {"XXXX.", "X...X", "X...X", "XXXX.", "X.X..", "X..X.", "X...X"}},
    {'!', 1, {"X", "X", "X", "X", "X", ".", "X"}},
};

const BigGlyph *bigGlyph(char ch) {
  for (const BigGlyph &g : BIG_FONT)
    if (g.ch == ch) return &g;
  return nullptr;
}

int bigWidth(const char *text, int s) {
  int w = 0;
  for (int i = 0; text[i]; i++) {
    const BigGlyph *g = bigGlyph(text[i]);
    w += ((g ? g->width : 3) + 1) * s;
  }
  return w - s;
}

// Letra con contorno oscuro y relleno en degradado vertical (de `top` a `bottom`).
void drawBigGlyph(Canvas &out, const BigGlyph &g, float x, float y, int s, Rgb top, Rgb bottom, Rgb outline) {
  int ix = (int)roundf(x), iy = (int)roundf(y);
  for (int pass = 0; pass < 2; pass++)
    for (int gy = 0; gy < 7; gy++)
      for (int gx = 0; gx < g.width; gx++) {
        if (g.rows[gy][gx] != 'X') continue;
        int cx = ix + gx * s, cy = iy + gy * s;
        if (pass == 0) fillRect(out, cx - 1, cy - 1, cx + s, cy + s, outline);
        else fillRect(out, cx, cy, cx + s - 1, cy + s - 1, mix(top, bottom, gy / 6.0f));
      }
}

void renderNewPic(const Source &from, const Source &to, const char *sender, uint32_t t, Canvas &out) {
  // Fondo: fogonazo blanco y rayos de colores que giran.
  float flashIn = phase(t, 0, 80), flashOut = phase(t, 80, NEW_FLASH_MS - 80);
  float spin = t * 0.0011f;
  for (int y = 0; y < SIZE; y++)
    for (int x = 0; x < SIZE; x++) {
      Rgb c;
      if (t < 80) {
        c = mix(fetch(from, x, y), {255, 255, 255}, flashIn);
      } else {
        float dx = x + 0.5f - CENTER, dy = y + 0.5f - CENTER;
        float ray = (atan2f(dy, dx) / 6.2832f + 0.5f) * 14 + spin * 14 / 6.2832f;
        int index = (int)floorf(ray);
        float d = sqrtf(dx * dx + dy * dy);
        c = hsv(index / 14.0f + t * 0.00012f, 0.85f, (index % 2 ? 0.42f : 0.7f) * (0.55f + 0.45f * fminf(1, d / 30)));
        c = mix({255, 255, 255}, c, flashOut);
      }
      put(out, x, y, c);
    }

  // Confeti cayendo.
  if (t > 150) {
    for (int i = 0; i < 46; i++) {
      float speed = 16 + hash(i, 0, 11) * 26;
      float y = fmodf((t - 150) * speed / 1000 + hash(i, 1, 11) * 72, 72) - 6;
      float x = hash(i, 2, 11) * SIZE + sinf(t * 0.004f + i) * 2;
      Rgb c = hsv(hash(i, 3, 11), 0.75f, 1);
      put(out, (int)x, (int)y, c);
      if (i % 3 == 0) put(out, (int)x + 1, (int)y, c);
    }
  }

  // La foto se abre en un círculo con el borde arcoíris.
  float radius = 47 * easeInOutCubic(phase(t, NEW_IRIS_START, NEW_IRIS_MS));
  if (radius > 0)
    for (int y = 0; y < SIZE; y++)
      for (int x = 0; x < SIZE; x++) {
        float dx = x + 0.5f - CENTER, dy = y + 0.5f - CENTER;
        float d = sqrtf(dx * dx + dy * dy);
        blend(out, x, y, fetch(to, x, y), radius - d + 0.5f);
        float ring = 1 - fabsf(d - radius) / 1.6f;
        if (ring > 0) blend(out, x, y, hsv(atan2f(dy, dx) / 6.2832f + t * 0.0005f, 0.6f, 1), ring);
      }

  // "NEW": tres letras grandes que caen con rebote, flotan y salen volando.
  const Rgb top = {255, 245, 140}, bottom = {255, 160, 30}, outline = {25, 10, 45};
  const char *big = "NEW";
  const int S = 3;
  float x = (SIZE - bigWidth(big, S)) / 2.0f;
  for (int i = 0; big[i]; i++) {
    const BigGlyph *g = bigGlyph(big[i]);
    float land = phase(t, 250 + i * 140, 500);
    if (land > 0) {
      float y = lerp(-24, 8, easeOutBack(land));
      if (t > 1200) y += sinf((t - 1200) * 0.008f + i * 1.3f);
      y -= easeInQuad(phase(t, NEW_EXIT_START + i * 70, 350)) * 45;
      drawBigGlyph(out, *g, x, y, S, top, bottom, outline);
    }
    x += (g->width + 1) * S;
  }

  // "PICTURE!" debajo, letra a letra, y quién la manda. Salen hacia abajo.
  float exitDown = easeInQuad(phase(t, NEW_EXIT_START, 350)) * 40;
  const char *small = "PICTURE!";
  x = (SIZE - bigWidth(small, 1)) / 2.0f;
  for (int i = 0; small[i]; i++) {
    const BigGlyph *g = bigGlyph(small[i]);
    float pop = phase(t, 950 + i * 55, 120);
    if (pop > 0) {
      float pulse = small[i] == '!' && t > 1500 ? -fabsf(sinf((t - 1500) * 0.006f)) * 1.5f : 0;
      drawBigGlyph(out, *g, x, 36 - (1 - pop) * 3 + exitDown + pulse, 1, {255, 255, 255}, {220, 235, 255},
                   outline);
    }
    x += g->width + 1;
  }
  if (sender && sender[0] && t > 1450) {
    char line[48];
    fitName("from ", sender, SIZE - 4, line, sizeof(line));
    int chars = (int)((t - 1450) / 45);
    int tx = (int)roundf(CENTER - textWidth(line, 40) / 2.0f), baseline = (int)roundf(53 + exitDown);
    drawText(out, line, tx + 1, baseline + 1, outline, chars);  // sombra
    drawText(out, line, tx, baseline, {255, 255, 255}, chars);
  }
}

}  // namespace

void Canvas::add(int x, int y, int r, int g, int b) {
  if (x < 0 || y < 0 || x >= SIZE || y >= SIZE) return;
  int v[3] = {px[y][x][0] + r, px[y][x][1] + g, px[y][x][2] + b};
  for (int i = 0; i < 3; i++) px[y][x][i] = v[i] > 255 ? 255 : (v[i] < 0 ? 0 : v[i]);
}

void Canvas::copyFrom(const Source &src) {
  for (int y = 0; y < SIZE; y++)
    for (int x = 0; x < SIZE; x++) src.get(x, y, px[y][x][0], px[y][x][1], px[y][x][2]);
}

const char *styleName(Style style) {
  switch (style) {
    case Style::CLASSIC: return "classic";
    case Style::POLAROID: return "polaroid";
    case Style::STARDUST: return "stardust";
    case Style::LETTER: return "letter";
    case Style::POSTCARD: return "postcard";
    case Style::FILM: return "film";
    case Style::PRINT: return "print";
    case Style::NEWPIC: return "newpic";
    case Style::DROP: return "drop";
    default: return "?";
  }
}

uint32_t durationMs(Style style) {
  switch (style) {
    case Style::CLASSIC: return CLASSIC_MS;
    case Style::POLAROID: return POLA_MS;
    case Style::STARDUST: return STAR_MS;
    case Style::LETTER: return LETTER_MS;
    case Style::POSTCARD: return POST_MS;
    case Style::FILM: return FILM_MS;
    case Style::PRINT: return PRINT_MS;
    case Style::NEWPIC: return NEW_MS;
    case Style::DROP: return DROP_MS;
    default: return 0;
  }
}

void render(Style style, const Source &from, const Source &to, const char *sender, uint32_t t, Canvas &out) {
  if (t >= durationMs(style)) {
    out.copyFrom(to);  // el final es siempre la foto tal cual
    return;
  }
  switch (style) {
    case Style::CLASSIC: renderClassic(from, to, t, out); break;
    case Style::POLAROID: renderPolaroid(from, to, sender, t, out); break;
    case Style::STARDUST: renderStardust(from, to, t, out); break;
    case Style::LETTER: renderLetter(from, to, t, out); break;
    case Style::POSTCARD: renderPostcard(from, to, sender, t, out); break;
    case Style::FILM: renderFilm(from, to, t, out); break;
    case Style::PRINT: renderPrint(from, to, t, out); break;
    case Style::NEWPIC: renderNewPic(from, to, sender, t, out); break;
    case Style::DROP: renderDrop(from, to, t, out); break;
    default: out.copyFrom(to); break;
  }
}

}  // namespace reveal
