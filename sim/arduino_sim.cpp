// Megachess simulator - simulated clock and Serial.
#include <Arduino.h>

static uint32_t simMs = 0;

uint32_t millis() { return simMs; }
uint32_t micros() { return simMs * 1000UL; }
void     delay(uint32_t ms) { simMs += ms; }
void     delayMicroseconds(uint32_t us) { simMs += (us + 999) / 1000; }
void     sim_advance(uint32_t ms) { simMs += ms; }

SerialSim Serial;

// The engine's own freeMemory() reads AVR heap symbols; build.py swaps it for
// this. Anything comfortably above options.low_mem_limit (810) keeps the
// search from pruning itself.
int freeMemory() { return 4000; }
