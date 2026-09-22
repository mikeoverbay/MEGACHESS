/*
 * Megachess - SD jumper continuity test
 *
 * Checks the three wires electrically, before any SD code is involved. It
 * builds a full connectivity map of the six pins, so it catches the three
 * distinct failures that all look identical from SD.begin():
 *
 *   - a wire not making contact        (pin reads open)
 *   - a wire in the wrong hole         (pin connects to the wrong partner)
 *   - MISO and MOSI swapped            (50-11 and 51-12 instead of 50-12, 51-11)
 *
 * Expected wiring:
 *
 *     Mega 50 (MISO) ---- shield 12
 *     Mega 51 (MOSI) ---- shield 11
 *     Mega 52 (SCK)  ---- shield 13
 *
 * How it works: drive one pin LOW as an output, leave the other five as
 * INPUT_PULLUP, and see which ones follow it down. A pin that reads LOW is
 * connected; one that stays HIGH is not. Repeat for all six.
 *
 * The SD card is held deselected (CS high) throughout so it releases its MISO
 * line and cannot fight the test.
 *
 * Serial monitor: 115200. Send any character to re-run.
 */

#define SD_CS 10

const uint8_t PINS[6]     = { 50, 51, 52, 11, 12, 13 };
const char* const NAMES[6] = { "50", "51", "52", "11", "12", "13" };

// index pairs we expect to be joined: 50-12, 51-11, 52-13
const uint8_t EXPECT[3][2] = { { 0, 4 }, { 1, 3 }, { 2, 5 } };

static bool connected[6][6];

static void scan() {
    for (uint8_t d = 0; d < 6; d++) {
        // everything floating-high except the one we drive
        for (uint8_t i = 0; i < 6; i++) pinMode(PINS[i], INPUT_PULLUP);
        delayMicroseconds(200);

        pinMode(PINS[d], OUTPUT);
        digitalWrite(PINS[d], LOW);
        delayMicroseconds(500);          // let the pullups settle

        for (uint8_t s = 0; s < 6; s++) {
            connected[d][s] = (s == d) ? true : (digitalRead(PINS[s]) == LOW);
        }

        pinMode(PINS[d], INPUT_PULLUP);
    }
}

static void runTest() {
    Serial.println();
    Serial.println(F("=== SD jumper continuity test ==="));

    // Deselect the card so it stops driving MISO and cannot skew the reads.
    pinMode(SD_CS, OUTPUT);
    digitalWrite(SD_CS, HIGH);
    delay(5);

    scan();

    // --- the map -----------------------------------------------------------
    Serial.println();
    Serial.println(F("connectivity map ('*' = joined)"));
    Serial.println(F("      50  51  52  11  12  13"));
    for (uint8_t d = 0; d < 6; d++) {
        Serial.print(F("  "));
        Serial.print(NAMES[d]);
        Serial.print(F("  "));
        for (uint8_t s = 0; s < 6; s++) {
            Serial.print(d == s ? F("  . ") : (connected[d][s] ? F("  * ") : F("  - ")));
        }
        Serial.println();
    }

    // --- verdict per expected wire -----------------------------------------
    Serial.println();
    uint8_t good = 0;
    for (uint8_t w = 0; w < 3; w++) {
        const uint8_t a = EXPECT[w][0], b = EXPECT[w][1];
        Serial.print(F("  Mega "));
        Serial.print(NAMES[a]);
        Serial.print(F(" -- shield "));
        Serial.print(NAMES[b]);
        Serial.print(F("  ("));
        Serial.print(w == 0 ? F("MISO") : (w == 1 ? F("MOSI") : F("SCK")));
        Serial.print(F(") : "));

        if (connected[a][b] && connected[b][a]) {
            Serial.println(F("OK"));
            good++;
        } else if (connected[a][b] || connected[b][a]) {
            Serial.println(F("INTERMITTENT - reads one way only, reseat it"));
        } else {
            Serial.println(F("NOT CONNECTED"));
        }
    }

    // --- anything joined that should not be --------------------------------
    bool stray = false;
    for (uint8_t d = 0; d < 6; d++) {
        for (uint8_t s = d + 1; s < 6; s++) {
            if (!connected[d][s]) continue;
            bool wanted = false;
            for (uint8_t w = 0; w < 3; w++) {
                if ((EXPECT[w][0] == d && EXPECT[w][1] == s) ||
                    (EXPECT[w][0] == s && EXPECT[w][1] == d)) wanted = true;
            }
            if (!wanted) {
                if (!stray) { Serial.println(); stray = true; }
                Serial.print(F("  UNEXPECTED join: "));
                Serial.print(NAMES[d]);
                Serial.print(F(" -- "));
                Serial.println(NAMES[s]);
            }
        }
    }

    Serial.println();
    if (good == 3 && !stray) {
        Serial.println(F("All three wires correct. Run sdtest next."));
    } else {
        Serial.println(F("Wiring is not right yet."));
        if (connected[0][3] || connected[1][4]) {
            Serial.println(F("Looks like MISO and MOSI are SWAPPED."));
            Serial.println(F("50 goes to 12, and 51 goes to 11 - they cross."));
        }
        Serial.println(F("Note: pin 13 carries the on-board LED, so if 52-13"));
        Serial.println(F("alone looks odd, check it with a meter before"));
        Serial.println(F("rewiring anything."));
    }

    Serial.println();
    Serial.println(F("Send any character to re-run."));
}

// ---------------------------------------------------------------------------
// Interactive probe
//
// All six pins sit at INPUT_PULLUP, so they read HIGH until something pulls
// one down. Touch a GND wire to any of them and every pin that goes LOW is
// printed. That is the useful part: short Mega 50 and if the jumper is good,
// shield 12 goes low WITH it. One poke proves the wire and identifies the pin.
// ---------------------------------------------------------------------------
static uint8_t readMask() {
    uint8_t m = 0;
    for (uint8_t i = 0; i < 6; i++)
        if (digitalRead(PINS[i]) == LOW) m |= (1 << i);
    return m;
}

static void probeTick() {
    static uint8_t  lastStable = 0;
    static uint8_t  candidate  = 0;
    static uint32_t since      = 0;

    const uint8_t m = readMask();
    if (m != candidate) { candidate = m; since = millis(); return; }
    if (millis() - since < 30) return;          // debounce the contact
    if (m == lastStable) return;
    lastStable = m;

    if (m == 0) { Serial.println(F("  (released)")); return; }

    Serial.print(F("  LOW:"));
    for (uint8_t i = 0; i < 6; i++) {
        if (m & (1 << i)) { Serial.print(' '); Serial.print(NAMES[i]); }
    }

    // If exactly one expected pair went low together, that wire is proven.
    for (uint8_t w = 0; w < 3; w++) {
        const uint8_t bits = (1 << EXPECT[w][0]) | (1 << EXPECT[w][1]);
        if (m == bits) {
            Serial.print(F("   <-- jumper "));
            Serial.print(NAMES[EXPECT[w][0]]);
            Serial.print('-');
            Serial.print(NAMES[EXPECT[w][1]]);
            Serial.print(F(" GOOD ("));
            Serial.print(w == 0 ? F("MISO") : (w == 1 ? F("MOSI") : F("SCK")));
            Serial.print(')');
        }
    }
    // Only one end responded - the wire is not carrying.
    for (uint8_t w = 0; w < 3; w++) {
        if (m == (uint8_t) (1 << EXPECT[w][0]) || m == (uint8_t) (1 << EXPECT[w][1])) {
            Serial.print(F("   <-- ONLY THIS END - jumper "));
            Serial.print(NAMES[EXPECT[w][0]]);
            Serial.print('-');
            Serial.print(NAMES[EXPECT[w][1]]);
            Serial.print(F(" not carrying"));
        }
    }
    Serial.println();
}

void setup() {
    Serial.begin(115200);
    while (!Serial && millis() < 3000) { }
    runTest();
    Serial.println(F("--- probe mode ---"));
    Serial.println(F("Touch a GND wire to a pin. Every pin that goes low is"));
    Serial.println(F("listed - short Mega 50 and shield 12 should follow it."));
    Serial.println(F("GND is on the power header, or any pin marked GND."));
    Serial.println();
    for (uint8_t i = 0; i < 6; i++) pinMode(PINS[i], INPUT_PULLUP);
}

void loop() {
    if (Serial.available()) {
        while (Serial.available()) Serial.read();
        runTest();
        Serial.println(F("--- probe mode ---"));
        for (uint8_t i = 0; i < 6; i++) pinMode(PINS[i], INPUT_PULLUP);
        return;
    }
    probeTick();
}
