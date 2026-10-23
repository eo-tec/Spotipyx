// Animaciones de "te ha llegado una foto" para el panel de 64x64.
//
// Código portable: no depende de Arduino ni del panel, así que lo compilan igual el
// firmware (photos.cpp) y el simulador del Mac (native-sim/). Cada estilo es una función
// pura del tiempo: render(t) pinta el fotograma del instante t sin guardar estado, de
// modo que se puede saltar a cualquier momento, grabar a fps fijos o reproducir a la
// velocidad que dé el ESP32. En t >= durationMs() el lienzo es exactamente la foto nueva.
#pragma once

#include <stdint.h>

namespace reveal {

const int SIZE = 64;

// Lo que se puede leer píxel a píxel: la pantalla anterior o la foto nueva, cada una
// en su formato (RGB565, GBR del backend, RGB...).
class Source {
 public:
  virtual void get(int x, int y, uint8_t &r, uint8_t &g, uint8_t &b) const = 0;
  virtual ~Source() {}
};

// Lienzo RGB888 de 64x64 (12 KB) donde se compone cada fotograma.
class Canvas : public Source {
 public:
  uint8_t px[SIZE][SIZE][3];

  void get(int x, int y, uint8_t &r, uint8_t &g, uint8_t &b) const override {
    r = px[y][x][0];
    g = px[y][x][1];
    b = px[y][x][2];
  }
  void set(int x, int y, uint8_t r, uint8_t g, uint8_t b) {
    if (x < 0 || y < 0 || x >= SIZE || y >= SIZE) return;
    px[y][x][0] = r;
    px[y][x][1] = g;
    px[y][x][2] = b;
  }
  // Suma con saturación (para brillos y partículas).
  void add(int x, int y, int r, int g, int b);
  void copyFrom(const Source &src);
};

enum class Style : uint8_t {
  CLASSIC = 0,  // la de siempre: cuadrados de colores y la foto desde el centro
  POLAROID,     // cae una polaroid, se revela con "de <nombre>" y se acerca hasta llenar
  STARDUST,     // un orbe con el color de la foto estalla en partículas que la dibujan
  LETTER,       // cae un sobre, se sacude, se abre y saca la foto
  DROP,         // una gota cae en el centro y la onda descubre la foto
  POSTCARD,     // una postal con sello y "de <nombre>" se da la vuelta y es la foto
  FILM,         // la pantalla se encoge en una tira de película que corre hasta la foto
  PRINT,        // una impresora saca la foto a tirones por la ranura
  NEWPIC,       // la llamativa: rayos de colores, confeti y "NEW PICTURE!"
  COUNT
};

const char *styleName(Style style);
uint32_t durationMs(Style style);

// Pinta en `out` el instante t (ms desde que empieza) de la transición de `from` (lo que
// había en pantalla) a `to` (la foto nueva). `sender` es quien la manda (puede ser
// vacío o nullptr). `out` no puede ser el mismo objeto que `from` ni que `to`.
void render(Style style, const Source &from, const Source &to, const char *sender, uint32_t t,
            Canvas &out);

}  // namespace reveal
