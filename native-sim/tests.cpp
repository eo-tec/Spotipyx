// Tests del código portable: cada estilo tiene que acabar exactamente en la foto nueva
// (el firmware deja ese fotograma en pantalla) y no puede pintar fuera de tiempo.
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "PhotoReveal.h"

namespace {

int failures = 0;

void check(bool ok, const char *what) {
  if (!ok) {
    std::printf("FALLO: %s\n", what);
    failures++;
  }
}

void pattern(reveal::Canvas &c, int seed) {
  for (int y = 0; y < reveal::SIZE; y++)
    for (int x = 0; x < reveal::SIZE; x++) c.set(x, y, (x * 4 + seed) & 255, (y * 4) & 255, (x ^ y ^ seed) & 255);
}

}  // namespace

int main() {
  static reveal::Canvas from, to, out;
  pattern(from, 0);
  pattern(to, 77);
  for (int s = 0; s < (int)reveal::Style::COUNT; s++) {
    auto style = (reveal::Style)s;
    char what[96];
    uint32_t duration = reveal::durationMs(style);
    std::snprintf(what, sizeof(what), "%s dura algo razonable (%u ms)", reveal::styleName(style), duration);
    check(duration > 500 && duration < 5000, what);

    reveal::render(style, from, to, "Lucia", duration, out);
    std::snprintf(what, sizeof(what), "%s acaba en la foto", reveal::styleName(style));
    check(std::memcmp(out.px, to.px, sizeof(out.px)) == 0, what);

    // Los fotogramas intermedios no pueden reventar con nombres raros o vacíos.
    for (uint32_t t = 0; t < duration; t += 40) {
      reveal::render(style, from, to, "", t, out);
      reveal::render(style, from, to, nullptr, t, out);
      reveal::render(style, from, to, "Un nombre larguísimo de verdad", t, out);
    }
    // Justo antes del final ya debería parecerse mucho a la foto (sin saltos al acabar).
    reveal::render(style, from, to, "Lucia", duration - 20, out);
    long diff = 0;
    for (int y = 0; y < reveal::SIZE; y++)
      for (int x = 0; x < reveal::SIZE; x++)
        for (int c = 0; c < 3; c++) diff += std::abs(out.px[y][x][c] - to.px[y][x][c]);
    float mean = diff / (float)(reveal::SIZE * reveal::SIZE * 3);
    std::snprintf(what, sizeof(what), "%s no salta en el último fotograma (dif. media %.1f)",
                  reveal::styleName(style), mean);
    check(mean < 12, what);
    std::printf("%-9s %5u ms  dif. final %.1f\n", reveal::styleName(style), duration, mean);
  }
  if (failures) return 1;
  std::printf("OK\n");
  return 0;
}
