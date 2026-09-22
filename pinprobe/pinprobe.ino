/*
 * Find where the wire goes.
 *
 * Drives DRIVE_PIN low then high and scans EVERY other pin to see which one
 * follows. Whatever DRIVE_PIN is actually connected to, this finds it - no
 * guessing which hole the other end landed in.
 *
 * Pins 0 and 1 are skipped (they are the USB serial port).
 *
 * Serial monitor: 115200. Send any character to re-scan.
 */

#define DRIVE_PIN 52
#define SD_CS_PIN 10        // held high so the card releases its MISO line

#define FIRST_PIN 10
#define LAST_PIN  13

static void scan() {
    Serial.println();
    Serial.print(F("=== driving pin "));
    Serial.print(DRIVE_PIN);
    Serial.println(F(", scanning 10..13 ==="));

    // Every scanned pin sits at INPUT_PULLUP, so pin 10 reads high and the
    // card stays deselected without special handling.

    uint8_t found = 0;

    for (uint8_t p = FIRST_PIN; p <= LAST_PIN; p++) {
        if (p == DRIVE_PIN) continue;

        pinMode(p, INPUT_PULLUP);
        pinMode(DRIVE_PIN, OUTPUT);

        digitalWrite(DRIVE_PIN, LOW);
        delayMicroseconds(600);
        const bool low = (digitalRead(p) == LOW);

        digitalWrite(DRIVE_PIN, HIGH);
        delayMicroseconds(600);
        const bool high = (digitalRead(p) == HIGH);

        pinMode(DRIVE_PIN, INPUT_PULLUP);

        if (low && high) {
            Serial.print(F("  FOLLOWS -> pin "));
            if (p >= 54) {
                Serial.print(F("A"));
                Serial.print(p - 54);
                Serial.print(F(" (digital "));
                Serial.print(p);
                Serial.print(F(")"));
            } else {
                Serial.print(p);
            }
            Serial.println();
            found++;
        }
    }

    Serial.println();
    if (found == 0) {
        Serial.println(F("  nothing follows - that wire is not connected"));
        Serial.println(F("  to any pin on this board."));
    } else {
        Serial.print(F("  "));
        Serial.print(found);
        Serial.println(F(" pin(s) connected."));
    }
    Serial.println(F("Send any character to re-scan."));
}

void setup() {
    Serial.begin(115200);
    while (!Serial && millis() < 3000) { }
    scan();
}

void loop() {
    if (Serial.available()) {
        while (Serial.available()) Serial.read();
        scan();
    }
}
