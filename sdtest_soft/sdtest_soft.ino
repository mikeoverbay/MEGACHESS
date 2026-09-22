/*
 * Megachess - SD card test, hardware OR software SPI
 *
 * WHY THIS EXISTS
 *
 * DIYables wire the shield's microSD to D10-D13. On an Uno those are the SPI
 * pins and it just works. On a Mega they are ordinary digital pins - the
 * Mega's SPI hardware is on D50-D52 - so the stock SD library cannot reach the
 * card. DIYables' answer is three jumper wires (CS on D10 needs none).
 *
 * But the card IS connected to the Mega, on pins 10-13. Only the SPI
 * peripheral is in the wrong place. SdFat can bit-bang SPI on any pins, so it
 * can drive the card through the connection the shield already made, with no
 * wiring at all. This sketch tests whichever mode it was built for and says
 * which one that was.
 *
 *
 * SOFTWARE SPI MODE needs SPI_DRIVER_SELECT=2 to reach SdFat's own .cpp files,
 * which a #define in a sketch cannot do. The Arduino IDE has no way to pass
 * it, so an IDE build lands in HARDWARE mode and will fail without jumpers.
 * To build the software-SPI version:
 *
 *   arduino-cli compile -b arduino:avr:mega --upload -p COM8 \
 *     --build-property "compiler.cpp.extra_flags=-DSPI_DRIVER_SELECT=2" \
 *     sdtest_soft
 *
 * Do NOT set SPI_DRIVER_SELECT in libraries/SdFat/src/SdFatConfig.h to make
 * the IDE do it: that is global, and the vs1053_for_SdFat library used by the
 * MP3 sketches in this sketchbook would stop compiling.
 *
 *
 * Serial monitor: 115200. Send any character to re-run.
 */
#include <SdFat.h>

#define SD_CS   10
#define SD_MOSI 11               // shield D11, per DIYables' pin map
#define SD_MISO 12               // shield D12
#define SD_SCK  13               // shield D13

#if SPI_DRIVER_SELECT == 2
  SoftSpiDriver<SD_MISO, SD_MOSI, SD_SCK> softSpi;
  #define SD_CONFIG   SdSpiConfig(SD_CS, DEDICATED_SPI, SD_SCK_MHZ(0), &softSpi)
  #define MODE_NAME   "SOFTWARE SPI on pins 11/12/13 - no jumpers"
  #define MODE_IS_SOFT 1
#else
  #define SD_CONFIG   SdSpiConfig(SD_CS, SHARED_SPI, SD_SCK_MHZ(4))
  #define MODE_NAME   "HARDWARE SPI on pins 50/51/52 - needs the 3 jumpers"
  #define MODE_IS_SOFT 0
#endif

SdFat32 sd;

static void listDir(const char* path) {
    File32 dir;
    if (!dir.open(path, O_RDONLY)) {
        Serial.print(F("    (no "));
        Serial.print(path);
        Serial.println(F(")"));
        return;
    }
    File32 e;
    char name[32];
    while (e.openNext(&dir, O_RDONLY)) {
        e.getName(name, sizeof(name));
        Serial.print(F("    "));
        Serial.print(name);
        if (e.isDir()) {
            Serial.println('/');
        } else {
            Serial.print(F("   "));
            Serial.print(e.fileSize());
            Serial.println(F(" bytes"));
        }
        e.close();
    }
    dir.close();
}

static void checkSet(const char* path) {
    Serial.print(F("  "));
    Serial.print(path);
    Serial.print(F(" : "));

    File32 f;
    if (!f.open(path, O_RDONLY)) { Serial.println(F("MISSING")); return; }

    uint8_t hdr[32];
    if (f.read(hdr, 32) != 32 || memcmp(hdr, "MCPS", 4) != 0) {
        Serial.println(F("bad header"));
        f.close();
        return;
    }
    const uint16_t px = (uint16_t) hdr[6] | ((uint16_t) hdr[7] << 8);
    const uint32_t want = 32UL + 12UL * px * px * 3UL;

    Serial.print(px);
    Serial.print('x');
    Serial.print(px);
    Serial.print(F(" '"));
    Serial.print((const char*) (hdr + 8));
    Serial.print(F("' "));
    Serial.print(f.fileSize());
    Serial.print(f.fileSize() == want ? F(" bytes ok") : F(" bytes WRONG"));

    // 40 rows of 120 bytes - exactly what redrawing one square reads.
    uint8_t row[120];
    f.seekSet(32);
    const uint32_t t0 = millis();
    for (uint8_t i = 0; i < 40; i++) f.read(row, sizeof(row));
    Serial.print(F("   one piece: "));
    Serial.print(millis() - t0);
    Serial.println(F(" ms"));
    f.close();
}

static void runTest() {
    Serial.println();
    Serial.println(F("=== Megachess SD test ==="));
    Serial.print(F("mode: "));
    Serial.println(F(MODE_NAME));
    Serial.print(F("CS "));     Serial.print(SD_CS);
#if MODE_IS_SOFT
    Serial.print(F("  MOSI ")); Serial.print(SD_MOSI);
    Serial.print(F("  MISO ")); Serial.print(SD_MISO);
    Serial.print(F("  SCK "));  Serial.print(SD_SCK);
#endif
    Serial.println();

    Serial.print(F("begin() ... "));
    if (!sd.begin(SD_CONFIG)) {
        Serial.println(F("FAILED"));
        Serial.println();
#if MODE_IS_SOFT
        Serial.println(F("Bit-banging pins 11/12/13 did not reach the card."));
        Serial.println(F("So the shield does not usefully route its SD lines"));
        Serial.println(F("there on this revision, or the card is not seated."));
        Serial.println(F("The three jumpers are then the only option:"));
        Serial.println(F("   Mega 50 -> shield D12   (MISO)"));
        Serial.println(F("   Mega 51 -> shield D11   (MOSI)"));
        Serial.println(F("   Mega 52 -> shield D13   (SCK)"));
        Serial.println(F("Pin 12 sits under the shield, so that means a"));
        Serial.println(F("soldered lead or stacking headers."));
#else
        Serial.println(F("This build uses the Mega's SPI hardware on 50/51/52."));
        Serial.println(F("Without the jumpers fitted that cannot work, and"));
        Serial.println(F("this result says nothing about software SPI."));
        Serial.println(F("See the header comment for the build command."));
#endif
        Serial.println();
        Serial.println(F("Send any character to retry."));
        return;
    }
    Serial.println(F("OK"));
#if MODE_IS_SOFT
    Serial.println(F(">>> the card works with NO jumpers <<<"));
#endif

    Serial.print(F("card: "));
    Serial.print(sd.card()->sectorCount() / 2048UL);
    Serial.print(F(" MB, FAT"));
    Serial.println(sd.fatType());

    Serial.println(F("--- / ---"));
    listDir("/");
    Serial.println(F("--- /PIECES ---"));
    listDir("/PIECES");
    Serial.println(F("--- /MEGACHESS ---"));
    listDir("/MEGACHESS");

    Serial.println();
    Serial.println(F("--- piece sets ---"));
    checkSet("/PIECES/AMBER.SET");
    checkSet("/PIECES/CLASSIC.SET");

    Serial.print(F("--- write test --- "));
    sd.remove("/MEGACHESS/SDTEST.TXT");
    File32 w;
    if (!w.open("/MEGACHESS/SDTEST.TXT", O_WRONLY | O_CREAT | O_TRUNC)) {
        Serial.println(F("cannot create a file"));
    } else {
        w.println(F("megachess write test"));
        w.close();
        File32 r;
        if (r.open("/MEGACHESS/SDTEST.TXT", O_RDONLY) && r.fileSize() > 0) {
            Serial.println(F("write + read back OK"));
            r.close();
        } else {
            Serial.println(F("wrote but could not read back"));
        }
        sd.remove("/MEGACHESS/SDTEST.TXT");
    }

    Serial.println();
    Serial.println(F("Send any character to re-run."));
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
