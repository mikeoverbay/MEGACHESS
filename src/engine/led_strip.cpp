/**
 * led_strip.cpp - Megachess replacement for the upstream MicroChess file.
 *
 * Upstream drives a WS2811 strip from LED_STRIP_PIN via FastLED. On this
 * board that pin is 6, which is D6 of the TFT shield's 8-bit parallel data
 * bus - FastLED.show() would bit-bang the display bus and corrupt whatever
 * is on screen. The strip is not wired on this build either way, so both
 * entry points are stubbed out. That also drops FastLED's 192 bytes of
 * CRGB leds[BOARD_SIZE] and its flash cost.
 *
 * engine.cpp still calls set_led_strip() from choose_best_moves() when
 * options.live_update is set, so the symbol has to exist.
 */
#include <Arduino.h>
#include <stdint.h>
#include "MicroChess.h"

void init_led_strip() { }

// The engine calls this from inside choose_best_moves() while it searches
// (rate-limited to ~10ms, only when options.live_update is set). Upstream
// lit an LED for the piece under evaluation; here it advances the on-screen
// thinking spinner. Declared in megachess.h, defined in ui.cpp.
extern void ui_think_tick();
void set_led_strip(index_t const /* flash = -1 */) { ui_think_tick(); }
