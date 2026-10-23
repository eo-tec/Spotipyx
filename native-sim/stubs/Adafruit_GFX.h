// En el Mac no hay Adafruit GFX completa: Fonts/Picopixel.h solo necesita las
// estructuras de la fuente (gfxfont.h) y que PROGMEM no signifique nada.
#pragma once
#include <stdint.h>
#define PROGMEM
#include <gfxfont.h>
