// Simulador del panel HUB75 en el Mac para las animaciones de foto nueva. Compila el
// mismo código que el ESP32 (lib/photo_reveal) y pinta cada LED como un punto en una
// ventana SDL.
//
//   ./build/reveal-sim                                abre la ventana
//   ./build/reveal-sim --style polaroid               empieza con ese estilo
//   ./build/reveal-sim --sender Ana                   quién manda la foto
//   ./build/reveal-sim --photos a.ppm,b.ppm           fotos propias (PPM 64x64, ver README)
//   ./build/reveal-sim --style drop --record DIR      sin ventana: guarda los fotogramas
//
// Teclas: 1-9 eligen estilo y lo lanzan, N o espacio manda otra foto con el estilo
// actual, S cámara lenta (x0,25), Q o Esc sale.

#include <SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "PhotoReveal.h"

namespace {

const int PITCH = 10;  // píxeles de pantalla por LED
const int IMAGE = reveal::SIZE * PITCH;

struct Options {
  reveal::Style style = reveal::Style::POLAROID;
  std::string sender = "Lucia";
  std::vector<std::string> photos = {"photos/faro.ppm", "photos/amigos.ppm"};
  std::string recordDir;
};

bool loadPpm(const std::string &path, reveal::Canvas &out) {
  FILE *f = std::fopen(path.c_str(), "rb");
  if (!f) return false;
  int w = 0, h = 0, max = 0;
  bool ok = std::fscanf(f, "P6 %d %d %d", &w, &h, &max) == 3 && w == reveal::SIZE && h == reveal::SIZE &&
            max == 255 && std::fgetc(f) != EOF &&
            std::fread(out.px, 1, sizeof(out.px), f) == sizeof(out.px);
  std::fclose(f);
  return ok;
}

// La "pantalla anterior" de la primera foto: un degradado suave, como si hubiera otra.
void placeholder(reveal::Canvas &c) {
  for (int y = 0; y < reveal::SIZE; y++)
    for (int x = 0; x < reveal::SIZE; x++) c.set(x, y, 40 + x, 60 + y / 2, 120 + (x + y) / 2);
}

// Pinta el lienzo como LEDs redondos sobre fondo casi negro, con un poco de halo.
void renderLeds(const reveal::Canvas &canvas, std::vector<uint32_t> &pixels) {
  static std::vector<uint8_t> mask;
  if (mask.empty()) {
    mask.resize(PITCH * PITCH);
    float c = (PITCH - 1) / 2.0f, radius = PITCH * 0.42f;
    for (int y = 0; y < PITCH; y++)
      for (int x = 0; x < PITCH; x++) {
        float d = std::hypot(x - c, y - c);
        float a = d <= radius ? 1.0f : std::fmax(0.0f, 1.0f - (d - radius) / 1.2f);
        mask[y * PITCH + x] = (uint8_t)(a * 255);
      }
  }
  const uint8_t base = 14;  // el panel apagado no es negro del todo
  for (int ly = 0; ly < reveal::SIZE; ly++)
    for (int lx = 0; lx < reveal::SIZE; lx++) {
      const uint8_t *p = canvas.px[ly][lx];
      for (int y = 0; y < PITCH; y++)
        for (int x = 0; x < PITCH; x++) {
          uint8_t a = mask[y * PITCH + x];
          auto channel = [&](uint8_t v) { return (uint32_t)(base + (std::max<int>(v, base) - base) * a / 255); };
          pixels[(ly * PITCH + y) * IMAGE + lx * PITCH + x] =
              0xFF000000u | (channel(p[0]) << 16) | (channel(p[1]) << 8) | channel(p[2]);
        }
    }
}

void writePpm(const std::string &path, const std::vector<uint32_t> &pixels) {
  FILE *f = std::fopen(path.c_str(), "wb");
  if (!f) return;
  std::fprintf(f, "P6\n%d %d\n255\n", IMAGE, IMAGE);
  for (uint32_t p : pixels) {
    uint8_t rgb[3] = {(uint8_t)(p >> 16), (uint8_t)(p >> 8), (uint8_t)p};
    std::fwrite(rgb, 1, 3, f);
  }
  std::fclose(f);
}

reveal::Style parseStyle(const std::string &name) {
  for (int s = 0; s < (int)reveal::Style::COUNT; s++)
    if (name == reveal::styleName((reveal::Style)s)) return (reveal::Style)s;
  std::fprintf(stderr, "Estilo desconocido: %s (classic, polaroid, stardust, letter, drop, postcard, film, print, newpic)\n", name.c_str());
  std::exit(1);
}

Options parseArgs(int argc, char **argv) {
  Options options;
  for (int i = 1; i < argc; i++) {
    std::string arg = argv[i];
    auto value = [&]() { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
    if (arg == "--style") options.style = parseStyle(value());
    else if (arg == "--sender") options.sender = value();
    else if (arg == "--record") options.recordDir = value();
    else if (arg == "--photos") {
      options.photos.clear();
      std::string list = value();
      for (size_t start = 0; start <= list.size();) {
        size_t comma = list.find(',', start);
        if (comma == std::string::npos) comma = list.size();
        if (comma > start) options.photos.push_back(list.substr(start, comma - start));
        start = comma + 1;
      }
    } else {
      std::fprintf(stderr, "Uso: %s [--style NOMBRE] [--sender NOMBRE] [--photos a.ppm,b.ppm] [--record DIR]\n",
                   argv[0]);
      std::exit(1);
    }
  }
  return options;
}

// Sin ventana: 0,8 s con la foto anterior, la animación y 1,5 s con la foto nueva, a 25
// fps. La anterior es la segunda foto de la lista y la nueva la primera.
int record(const Options &options, const std::vector<reveal::Canvas> &photos) {
  const reveal::Canvas &from = photos.size() > 1 ? photos[1] : photos[0];
  const reveal::Canvas &to = photos[0];
  static reveal::Canvas out;
  std::vector<uint32_t> pixels(IMAGE * IMAGE);
  uint32_t lead = 800, end = lead + reveal::durationMs(options.style) + 1500;
  int frame = 0;
  for (uint32_t t = 0; t <= end; t += 40, frame++) {
    if (t < lead) out.copyFrom(from);
    else reveal::render(options.style, from, to, options.sender.c_str(), t - lead, out);
    renderLeds(out, pixels);
    char path[512];
    std::snprintf(path, sizeof(path), "%s/frame-%04d.ppm", options.recordDir.c_str(), frame);
    writePpm(path, pixels);
  }
  std::printf("%s: %d fotogramas en %s\n", reveal::styleName(options.style), frame, options.recordDir.c_str());
  return 0;
}

}  // namespace

int main(int argc, char **argv) {
  std::setvbuf(stdout, nullptr, _IOLBF, 0);
  Options options = parseArgs(argc, argv);

  std::vector<reveal::Canvas> photos(options.photos.size());
  for (size_t i = 0; i < photos.size(); i++)
    if (!loadPpm(options.photos[i], photos[i])) {
      std::fprintf(stderr, "No se pudo leer %s (tiene que ser PPM binario de 64x64)\n", options.photos[i].c_str());
      return 1;
    }
  if (!options.recordDir.empty()) return record(options, photos);

  if (SDL_Init(SDL_INIT_VIDEO) != 0) {
    std::fprintf(stderr, "SDL: %s\n", SDL_GetError());
    return 1;
  }
  SDL_Window *window = SDL_CreateWindow("frame. · foto nueva", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                        IMAGE, IMAGE, 0);
  SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_PRESENTVSYNC);
  SDL_Texture *texture =
      SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, IMAGE, IMAGE);
  std::vector<uint32_t> pixels(IMAGE * IMAGE);

  // `from` es lo que había en pantalla al llegar la foto; `screen` lo que se ve ahora.
  static reveal::Canvas from, screen;
  placeholder(screen);
  size_t next = 0;
  reveal::Style style = options.style;
  bool slow = false, playing = false;
  double clock = 0;  // ms de animación (en cámara lenta avanza más despacio)
  uint32_t last = SDL_GetTicks();

  auto send = [&]() {
    from = screen;  // la animación sale de lo que haya, aunque sea otra a medias
    playing = true;
    clock = 0;
    std::printf("Foto nueva de %s · estilo %s (%u ms)\n", options.sender.c_str(), reveal::styleName(style),
                reveal::durationMs(style));
    char title[96];
    std::snprintf(title, sizeof(title), "frame. · %s%s", reveal::styleName(style), slow ? " (x0,25)" : "");
    SDL_SetWindowTitle(window, title);
  };
  for (int s = 0; s < (int)reveal::Style::COUNT; s++) std::printf("%d %s · ", s + 1, reveal::styleName((reveal::Style)s));
  std::printf("N otra foto · S cámara lenta · Q salir\n");
  send();

  bool running = true;
  while (running) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      if (event.type == SDL_QUIT) running = false;
      if (event.type != SDL_KEYDOWN) continue;
      SDL_Keycode key = event.key.keysym.sym;
      if (key == SDLK_q || key == SDLK_ESCAPE) running = false;
      else if (key >= SDLK_1 && key < SDLK_1 + (int)reveal::Style::COUNT) {
        style = (reveal::Style)(key - SDLK_1);
        next = (next + 1) % photos.size();
        send();
      } else if (key == SDLK_n || key == SDLK_SPACE) {
        next = (next + 1) % photos.size();
        send();
      } else if (key == SDLK_s) {
        slow = !slow;
        std::printf("Cámara lenta %s\n", slow ? "activada" : "desactivada");
      }
    }

    uint32_t now = SDL_GetTicks();
    clock += (now - last) * (slow ? 0.25 : 1.0);
    last = now;
    if (playing) {
      reveal::render(style, from, photos[next], options.sender.c_str(), (uint32_t)clock, screen);
      if (clock >= reveal::durationMs(style)) playing = false;
    }

    renderLeds(screen, pixels);
    SDL_UpdateTexture(texture, nullptr, pixels.data(), IMAGE * sizeof(uint32_t));
    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, texture, nullptr, nullptr);
    SDL_RenderPresent(renderer);
    SDL_Delay(16);
  }

  SDL_DestroyTexture(texture);
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return 0;
}
