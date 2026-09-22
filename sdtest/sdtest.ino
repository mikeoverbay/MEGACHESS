/*
 * Megachess - SD card test
 *
 * Nothing but the card. No display, so a display problem cannot muddy the
 * result, and the shield can even be off the board if you wire the module
 * direct.
 *
 * On a Mega the shield's microSD slot is NOT wired to the Mega's SPI pins -
 * DIYables say so themselves. Three jumpers fix it:
 *
 *     Mega 50 (MISO) -> shield D12
 *     Mega 51 (MOSI) -> shield D11
 *     Mega 52 (SCK)  -> shield D13
 *     CS stays on D10 - no wire needed.
 *
 * Serial monitor: 115200 baud. Re-runs the whole check whenever you send a
 * character, so you can reseat a wire and retest without re-uploading.
 */
#include <SPI.h>
#include <SD.h>

#define SD_CS 10

static void listDir(const char* path, uint8_t indent) {
    File dir = SD.open(path);
    if (!dir) {
        Serial.print(F("    (cannot open "));
        Serial.print(path);
        Serial.println(F(")"));
        return;
    }
    if (!dir.isDirectory()) { dir.close(); return; }

    while (true) {
        File e = dir.openNextFile();
        if (!e) break;
        for (uint8_t i = 0; i < indent; i++) Serial.print(' ');
        Serial.print(F("    "));
        Serial.print(e.name());
        if (e.isDirectory()) {
            Serial.println(F("/"));
        } else {
            Serial.print(F("   "));
            Serial.print(e.size());
            Serial.println(F(" bytes"));
        }
        e.close();
    }
    dir.close();
}

// The game needs these to be readable, so check them specifically.
static void checkSet(const char* path) {
    Serial.print(F("  "));
    Serial.print(path);
    Serial.print(F(" : "));
    File f = SD.open(path, FILE_READ);
    if (!f) { Serial.println(F("MISSING")); return; }

    uint8_t hdr[32];
    if (f.read(hdr, 32) != 32) {
        Serial.println(F("too short"));
        f.close();
        return;
    }
    if (memcmp(hdr, "MCPS", 4) != 0) {
        Serial.println(F("bad magic - not a Megachess piece set"));
        f.close();
        return;
    }
    const uint16_t ver = (uint16_t) hdr[4] | ((uint16_t) hdr[5] << 8);
    const uint16_t px  = (uint16_t) hdr[6] | ((uint16_t) hdr[7] << 8);
    const uint32_t want = 32UL + 12UL * px * px * 3UL;

    Serial.print(F("ok  v"));
    Serial.print(ver);
    Serial.print(F("  "));
    Serial.print(px);
    Serial.print('x');
    Serial.print(px);
    Serial.print(F("  name='"));
    Serial.print((const char*) (hdr + 8));
    Serial.print(F("'  size "));
    Serial.print(f.size());
    Serial.println(f.size() == want ? F(" (correct)") : F(" (WRONG - rebuild it)"));

    // Time a piece read: this is what the board redraw pays per piece.
    const uint32_t t0 = micros();
    uint8_t row[120];
    f.seek(32);
    for (uint8_t i = 0; i < 40; i++) f.read(row, sizeof(row));
    const uint32_t dt = micros() - t0;
    Serial.print(F("        one piece reads in "));
    Serial.print(dt / 1000.0, 1);
    Serial.println(F(" ms"));
    f.close();
}

static void runTest() {
    Serial.println();
    Serial.println(F("=== Megachess SD test ==="));
    Serial.print(F("CS pin "));
    Serial.println(SD_CS);

    pinMode(SD_CS, OUTPUT);

    Serial.print(F("SD.begin() ... "));
    if (!SD.begin(SD_CS)) {
        Serial.println(F("FAILED"));
        Serial.println();
        Serial.println(F("Check, in this order:"));
        Serial.println(F("  1. Is a card actually in the slot, pushed until it clicks?"));
        Serial.println(F("  2. The three jumpers - this is the usual cause on a Mega:"));
        Serial.println(F("       Mega 50 -> shield D12   (MISO)"));
        Serial.println(F("       Mega 51 -> shield D11   (MOSI)"));
        Serial.println(F("       Mega 52 -> shield D13   (SCK)"));
        Serial.println(F("     Short wires. Long flying leads often will not clock."));
        Serial.println(F("  3. Card must be FAT32 or FAT16, MBR - not GPT, not exFAT."));
        Serial.println(F("  4. Some shields need the card seated before power-on."));
        Serial.println();
        Serial.println(F("Send any character to test again."));
        return;
    }
    Serial.println(F("OK"));

    Serial.println();
    Serial.println(F("--- root ---"));
    listDir("/", 0);

    Serial.println(F("--- /PIECES ---"));
    listDir("/PIECES", 0);

    Serial.println(F("--- /MEGACHESS ---"));
    listDir("/MEGACHESS", 0);

    Serial.println();
    Serial.println(F("--- piece sets the game will look for ---"));
    checkSet("/PIECES/AMBER.SET");
    checkSet("/PIECES/CLASSIC.SET");
    checkSet("/PIECES/POCKET.SET");
    checkSet("/PIECES/SLATE.SET");

    Serial.println();
    Serial.print(F("--- opening book --- "));
    {
        File f = SD.open("/MCHESS/BOOK.TXT", FILE_READ);
        if (!f) {
            Serial.println(F("MISSING"));
        } else {
            uint16_t lines = 0;
            bool inLine = false;
            while (f.available()) {
                const char c = f.read();
                if (c == '\n' || c == '\r') inLine = false;
                else if (!inLine) { inLine = true; if (c != '#') lines++; }
            }
            Serial.print(f.size());
            Serial.print(F(" bytes, "));
            Serial.print(lines);
            Serial.println(F(" opening lines"));
            f.close();
        }
    }

    // Writing is what saving, settings and the PGN log all need.
    Serial.print(F("--- write test --- "));
    SD.remove("/MCHESS/SDTEST.TXT");
    File w = SD.open("/MCHESS/SDTEST.TXT", FILE_WRITE);
    if (!w) {
        Serial.println(F("cannot create a file - card may be write protected"));
    } else {
        w.println(F("megachess write test"));
        w.close();
        File r = SD.open("/MCHESS/SDTEST.TXT", FILE_READ);
        if (r && r.size() > 0) {
            Serial.println(F("write + read back OK"));
            r.close();
        } else {
            Serial.println(F("wrote it but could not read it back"));
        }
        SD.remove("/MCHESS/SDTEST.TXT");
    }

    Serial.println();
    Serial.println(F("All good - the game will use the card."));
    Serial.println(F("Send any character to test again."));
}

void setup() {
    Serial.begin(115200);
    while (!Serial && millis() < 3000) { }
    runTest();
}

void loop() {
    if (Serial.available()) {
        while (Serial.available()) Serial.read();
        runTest();
    }
}
