# native-sim: animaciones de foto nueva en el Mac

Compila **el mismo código** que el firmware (`lib/photo_reveal`) y pinta el panel de
64x64 como LEDs en una ventana SDL. Sirve para diseñar y revisar la animación que sale
cuando a un frame le llega una foto, sin flashear nada.

```bash
brew install sdl2 ffmpeg   # una vez
make run                   # ventana interactiva
make gifs                  # build/<estilo>.gif de cada estilo
make test                  # cada estilo acaba exactamente en la foto
```

Teclas: `1`-`9` eligen estilo (classic, polaroid, stardust, letter, drop, postcard, film,
print, newpic) y lo lanzan,
`N` o espacio manda otra foto, `S` cámara lenta, `Q` salir.

Opciones: `--style polaroid`, `--sender Ana`, `--photos a.ppm,b.ppm`.
Para probar con una foto propia:

```bash
ffmpeg -i foto.jpg -vf "scale=64:64:force_original_aspect_ratio=increase:flags=area,crop=64:64" photos/mia.ppm
./build/reveal-sim --photos photos/mia.ppm,photos/faro.ppm
```

El estilo que usa el frame está en `NEW_PHOTO_STYLE` (`src/photos.cpp`).
Requiere haber compilado el firmware una vez (`pio run`), porque la fuente Picopixel se
lee de `.pio/libdeps`.
