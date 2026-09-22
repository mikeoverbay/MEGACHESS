/*
 * umaxbench - how fast and how deep does micro-Max search on this Mega?
 *
 * Self-play from the start position at two budgets, printing depth, nodes,
 * time and the low-water mark of the stack. src/umax here is a copy of the
 * project's src/umax made by tools/umax_bench.sh - edit the original.
 */
#include "src/umax/umax.h"

static int free_ram() {
    extern char* __brkval;
    extern char  __heap_start;
    char top;
    return (int) (&top - (__brkval ? __brkval : &__heap_start));
}

static void sq_name(uint8_t s, char* out) {
    out[0] = 'a' + (s & 7); out[1] = '8' - (s >> 4); out[2] = 0;
}

static void print_board() {
    static const char glyph[] = ".?+nkbrq?*?NKBRQ";
    for (uint8_t row = 0; row < 8; row++) {
        for (uint8_t f = 0; f < 8; f++) {
            Serial.print(glyph[umax_piece_at(UMAX_SQ(row, f)) & 15]);
            Serial.print(' ');
        }
        Serial.println();
    }
}

static void run(uint16_t budget, uint8_t plies, uint8_t maxDepth) {
    Serial.print(F("--- budget ")); Serial.print(budget); Serial.print(F(" ms, max depth "));
    Serial.print(maxDepth); Serial.print(F(", ")); Serial.print(plies); Serial.println(F(" plies"));
    Serial.println(F("ply move  depth score  nodes    ms  nodes/s stack"));
    umax_new_game();
    for (uint8_t ply = 1; ply <= plies; ply++) {
        UmaxResult r;
        if (!umax_think(budget, maxDepth, r)) { Serial.println(F("no move")); break; }
        char a[3], c[3];
        sq_name(r.from, a); sq_name(r.to, c);
        char line[64];
        snprintf(line, sizeof line, "%3u %s-%s %5u %5d %6lu %5u %8lu %5d",
                 (unsigned) ply, a, c, (unsigned) r.depth, r.score, r.nodes,
                 (unsigned) r.ms, r.ms ? (r.nodes * 1000UL) / r.ms : 0UL, umax_stack_low());
        Serial.println(line);
    }
    print_board();
}

void setup() {
    Serial.begin(115200);
    delay(300);
    Serial.println(F("umaxbench"));
    Serial.print(F("free RAM before: ")); Serial.println(free_ram());
    run(2500, 10, 20);
    run(10000, 4, 20);
    Serial.print(F("free RAM after: ")); Serial.println(free_ram());
    Serial.println(F("done"));
}

void loop() {}
