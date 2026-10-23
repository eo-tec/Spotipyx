# Simulador del panel en el Mac: compila el mismo código de animaciones que el ESP32
# (lib/photo_reveal) y pinta cada LED como un punto en una ventana SDL.
#   make         compila build/reveal-sim
#   make run     lo abre (teclas: 1-9 estilo, N foto nueva, S cámara lenta, Q salir)
#   make gifs    graba un GIF por estilo en build/<estilo>.gif
#   make test    comprueba que cada estilo acaba exactamente en la foto
# Requisitos: brew install sdl2 ffmpeg (ffmpeg solo para los GIF).

CXX ?= clang++
CORE := ../lib/photo_reveal/src
# La fuente Picopixel se reutiliza de las librerías de PlatformIO del firmware.
GFX := ../.pio/libdeps/esp32dev/Adafruit GFX Library
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -I$(CORE) -Istubs -I"$(GFX)" $(shell sdl2-config --cflags)
LDLIBS := $(shell sdl2-config --libs)
SOURCES := sim_main.cpp $(wildcard $(CORE)/*.cpp)
STYLES := classic polaroid stardust letter drop postcard film print newpic

build/reveal-sim: $(SOURCES) $(wildcard $(CORE)/*.h) | build
	$(CXX) $(CXXFLAGS) $(SOURCES) -o $@ $(LDLIBS)

build:
	mkdir -p build

build/tests: tests.cpp $(wildcard $(CORE)/*.cpp) $(wildcard $(CORE)/*.h) | build
	$(CXX) $(CXXFLAGS) tests.cpp $(wildcard $(CORE)/*.cpp) -o $@

test: build/tests
	./build/tests

run: build/reveal-sim
	./build/reveal-sim

gifs: build/reveal-sim
	@for s in $(STYLES); do \
	  rm -rf build/frames-$$s && mkdir -p build/frames-$$s && \
	  ./build/reveal-sim --style $$s --record build/frames-$$s && \
	  ffmpeg -loglevel error -y -framerate 25 -i build/frames-$$s/frame-%04d.ppm \
	    -vf "scale=320:-1:flags=lanczos,split[a][b];[a]palettegen[p];[b][p]paletteuse" build/$$s.gif && \
	  echo "build/$$s.gif"; \
	done

clean:
	rm -rf build

.PHONY: run gifs test clean
