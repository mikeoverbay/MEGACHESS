/*
 * storage.cpp - the SD card.
 *
 * Everything here is optional. If no card is fitted, or the slot is not
 * jumpered to the Mega's SPI pins (see README - DIYables do not wire it on
 * Mega form factor), every entry point returns false or does nothing and the
 * game runs on built-in artwork with no saving.
 *
 * Card layout:
 *   /PIECES/<THEME>.SET        piece artwork, see build_assets.py for the format
 *   /MCHESS/SETTINGS.DAT    persisted Settings struct
 *   /MCHESS/SAVE.DAT        the in-progress game: raw board + game structs
 *   /MCHESS/GAMES.PGN       every finished game, appended
 *   /MCHESS/BOOK.TXT        opening lines, one per line, long algebraic
 *   /IMAGES/SPLASH.IMG       boot picture, made by tools/splash.py
 */
#include <SD.h>
#include "megachess.h"

#define SD_CS      10
// 8.3 names only: the SD library cannot open a folder called MEGACHESS,
// for reading or writing, and says nothing about why. Keep every name here
// to eight characters, a dot, three.
#define DIR_MC     "/MCHESS"
#define DIR_IMG    "/IMAGES"                // yours: pictures
#define F_SPLASH   DIR_IMG "/SPLASH.IMG"
#define F_SETTINGS DIR_MC "/SETTINGS.DAT"
#define F_SAVE     DIR_MC "/SAVE.DAT"
#define F_PGN      DIR_MC "/GAMES.PGN"
#define F_BOOK     DIR_MC "/BOOK.TXT"

static bool sdOK = false;

// Move history, kept for the opening book and the PGN writer. The engine's own
// game.history[] only holds MAX_REPS*2-1 entries, enough to spot a repetition
// but not to match a book line.
#define HIST_MAX 40
static uint16_t moveHist[HIST_MAX];
static uint8_t  histCount = 0;
static bool     bookExhausted = false;

bool sd_present() { return sdOK; }

bool sd_begin() {
    pinMode(SD_CS, OUTPUT);
    sdOK = SD.begin(SD_CS);
    if (sdOK && !SD.exists(DIR_MC)) SD.mkdir(DIR_MC);
    return sdOK;
}

// ---------------------------------------------------------------------------
// piece artwork
// ---------------------------------------------------------------------------
static File     setFile;
static bool     setReady = false;
// Rows read from the card per SD call. One piece is 40 rows of 120 bytes;
// reading them one row at a time meant 40 File::read() calls per piece, each
// paying the SD library's position/cluster overhead. Four at a time cuts that
// to 10 for 360 bytes of RAM.
#define BLIT_ROWS 4
static uint8_t  rowBuf[SQ * 3 * BLIT_ROWS];
static uint16_t lineBuf[SQ];

bool sd_pieces_ready() { return setReady; }

bool sd_pieces_load(const char* name) {
    setReady = false;
    if (setFile) setFile.close();
    if (!sdOK) return false;

    char path[32];
    snprintf(path, sizeof(path), "/PIECES/%s.SET", name);
    setFile = SD.open(path, FILE_READ);
    if (!setFile) return false;

    uint8_t hdr[32];
    if (setFile.read(hdr, 32) != 32 || memcmp(hdr, "MCPS", 4) != 0) {
        setFile.close();
        return false;
    }
    const uint16_t px = (uint16_t) hdr[6] | ((uint16_t) hdr[7] << 8);
    if (px != SQ) {          // artwork built for a different square size
        setFile.close();
        return false;
    }
    setReady = true;
    return true;
}

// Blend src over dst. a is 0..255.
static inline uint16_t blend565(uint16_t s, uint16_t d, uint8_t a) {
    const uint8_t w  = (a == 255) ? 32 : (a >> 3);
    const uint8_t iw = 32 - w;
    const uint16_t r = ((((s >> 11) & 0x1F) * w) + (((d >> 11) & 0x1F) * iw)) >> 5;
    const uint16_t g = ((((s >> 5) & 0x3F) * w) + (((d >> 5) & 0x3F) * iw)) >> 5;
    const uint16_t b = (((s & 0x1F) * w) + ((d & 0x1F) * iw)) >> 5;
    return (r << 11) | (g << 5) | b;
}

bool sd_pieces_blit(uint8_t typeIdx, bool white, int16_t x, int16_t y,
                    uint16_t squareColour) {
    if (!setReady || typeIdx > 5) return false;

    // File order is WP WN WB WR WQ WK then the same for Black, and typeIdx is
    // (MicroChess type - 1), which is already pawn..king.
    const uint8_t idx = (white ? 0 : 6) + typeIdx;
    const uint32_t base = 32UL + (uint32_t) idx * SQ * SQ * 3UL;

    // Rows are contiguous, so seek once and read straight through.
    if (!setFile.seek(base)) return false;

    // ONE address window for the whole 40x40 block. pushColors() only streams
    // pixel data - it never touches the window - and the controller
    // auto-increments across the rect, so setting it per row was 40 redundant
    // window setups per piece.
    tft.setAddrWindow(x, y, x + SQ - 1, y + SQ - 1);

    for (uint8_t r = 0; r < SQ; r += BLIT_ROWS) {
        if (setFile.read(rowBuf, sizeof(rowBuf)) != (int) sizeof(rowBuf)) {
            setReady = false;          // card pulled mid-draw; fall back
            return false;
        }
        const uint8_t* p = rowBuf;
        for (uint8_t sub = 0; sub < BLIT_ROWS; sub++) {
            for (uint8_t c = 0; c < SQ; c++, p += 3) {
                const uint8_t a = p[2];
                if (a == 0) {
                    lineBuf[c] = squareColour;
                } else {
                    const uint16_t src = (uint16_t) p[0] | ((uint16_t) p[1] << 8);
                    lineBuf[c] = (a == 255) ? src : blend565(src, squareColour, a);
                }
            }
            tft.pushColors(lineBuf, SQ);
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// boot splash
// ---------------------------------------------------------------------------
// /IMAGES/SPLASH.IMG - a 480x320 RGB565 image, raw or run-length encoded,
// written by tools/splash.py. Painted at boot if present; if absent the text
// splash stays up until the menu. Reuses the piece blit buffers.
bool sd_splash() {
    if (!sdOK) return false;
    File f = SD.open(F_SPLASH, FILE_READ);
    if (!f) return false;

    uint8_t hdr[16];
    if (f.read(hdr, 16) != 16 || memcmp(hdr, "MCSP", 4) != 0) { f.close(); return false; }
    const uint8_t  mode = hdr[4];
    const uint16_t w = (uint16_t) hdr[5] | ((uint16_t) hdr[6] << 8);
    const uint16_t h = (uint16_t) hdr[7] | ((uint16_t) hdr[8] << 8);
    if (w != SCR_W || h != SCR_H) { f.close(); return false; }

    uint32_t left = (uint32_t) w * h;
    tft.setAddrWindow(0, 0, SCR_W - 1, SCR_H - 1);

    if (mode == 0) {
        // raw: rowBuf is 480 bytes, 240 pixels a read
        uint16_t* px = (uint16_t*) rowBuf;
        const uint16_t per = sizeof(rowBuf) / 2;
        while (left) {
            const uint16_t n = (left < per) ? (uint16_t) left : per;
            if (f.read(rowBuf, n * 2) != (int) (n * 2)) break;
            tft.pushColors(px, n);
            left -= n;
        }
    } else {
        // rle: read a block of [run][colour] pairs into rowBuf, expand each
        // through lineBuf (40 px) so a long run is a few pushes, not a
        // per-pixel loop
        const uint16_t pairs = sizeof(rowBuf) / 3;
        while (left) {
            const int got = f.read(rowBuf, pairs * 3);
            if (got < 3) break;
            for (uint16_t i = 0; i + 2 < (uint16_t) got && left; i += 3) {
                uint16_t run = rowBuf[i];
                const uint16_t c = (uint16_t) rowBuf[i + 1] | ((uint16_t) rowBuf[i + 2] << 8);
                if (run > left) run = (uint16_t) left;
                left -= run;
                const uint16_t fillN = (run < SQ) ? run : SQ;
                for (uint16_t k = 0; k < fillN; k++) lineBuf[k] = c;
                while (run) {
                    const uint16_t n = (run < SQ) ? run : SQ;
                    tft.pushColors(lineBuf, n);
                    run -= n;
                }
            }
        }
    }
    f.close();
    return true;
}

// ---------------------------------------------------------------------------
// settings
// ---------------------------------------------------------------------------
void sd_load_settings() {
    if (!sdOK) return;
    File f = SD.open(F_SETTINGS, FILE_READ);
    if (!f) return;
    Settings tmp;
    const int n = f.read((uint8_t*) &tmp, sizeof(tmp));
    f.close();
    if (n == (int) sizeof(tmp) && tmp.magic == SETTINGS_MAGIC) {
        settings = tmp;
        if (settings.ply < 1) settings.ply = 1;
        if (settings.ply > 4) settings.ply = 4;
        if (settings.theme >= THEME_COUNT) settings.theme = 0;
        if (settings.mode > MODE_AI_AI) settings.mode = MODE_HUMAN_AI;
    }
}

void sd_save_settings() {
    if (!sdOK) return;
    settings.magic = SETTINGS_MAGIC;
    SD.remove(F_SETTINGS);
    File f = SD.open(F_SETTINGS, FILE_WRITE);
    if (!f) return;
    f.write((const uint8_t*) &settings, sizeof(settings));
    f.close();
}

// ---------------------------------------------------------------------------
// save / resume
// ---------------------------------------------------------------------------
struct SaveHdr {
    uint32_t magic;
    uint16_t boardSz, gameSz;
    uint8_t  mode, ply, humanSide, flip;
    uint8_t  histCount;
};
#define SAVE_MAGIC 0x4D435356UL   // "MCSV"

bool sd_has_saved_game() { return sdOK && SD.exists(F_SAVE); }

void sd_clear_saved_game() { if (sdOK) SD.remove(F_SAVE); }

bool sd_save_game() {
    if (!sdOK) return false;
    SaveHdr h;
    h.magic     = SAVE_MAGIC;
    h.boardSz   = sizeof(board_t);
    h.gameSz    = sizeof(game_t);
    h.mode      = settings.mode;
    h.ply       = settings.ply;
    h.humanSide = settings.humanSide;
    h.flip      = settings.flipBoard;
    h.histCount = histCount;

    SD.remove(F_SAVE);
    File f = SD.open(F_SAVE, FILE_WRITE);
    if (!f) { Serial.println(F("save: SAVE.DAT would not open")); return false; }
    f.write((const uint8_t*) &h, sizeof(h));
    f.write((const uint8_t*) &board, sizeof(board_t));
    f.write((const uint8_t*) &game, sizeof(game_t));
    f.write((const uint8_t*) moveHist, histCount * sizeof(uint16_t));
    f.close();
    return true;
}

bool sd_load_game() {
    if (!sdOK) { Serial.println(F("resume: no card")); return false; }
    File f = SD.open(F_SAVE, FILE_READ);
    if (!f) { Serial.println(F("resume: SAVE.DAT would not open")); return false; }

    SaveHdr h;
    const int got = f.read((uint8_t*) &h, sizeof(h));
    bool ok = (got == (int) sizeof(h))
              && h.magic == SAVE_MAGIC
              && h.boardSz == sizeof(board_t)      // guards a struct change
              && h.gameSz == sizeof(game_t)        // after a recompile
              && h.histCount <= HIST_MAX;
    if (!ok) {                                     // say which check failed
        Serial.print(F("resume: header ")); Serial.print(got); Serial.print('/'); Serial.print((int) sizeof(h));
        Serial.print(F(" magic ")); Serial.print(h.magic == SAVE_MAGIC ? F("ok") : F("BAD"));
        Serial.print(F(" board ")); Serial.print(h.boardSz); Serial.print('/'); Serial.print((int) sizeof(board_t));
        Serial.print(F(" game ")); Serial.print(h.gameSz); Serial.print('/'); Serial.print((int) sizeof(game_t));
        Serial.print(F(" hist ")); Serial.println(h.histCount);
    }
    if (ok) ok = (f.read((uint8_t*) &board, sizeof(board_t)) == (int) sizeof(board_t));
    if (ok) ok = (f.read((uint8_t*) &game, sizeof(game_t)) == (int) sizeof(game_t));
    if (!ok) Serial.println(F("resume: read failed"));
    if (ok) {
        histCount = h.histCount;
        if (histCount)
            f.read((uint8_t*) moveHist, histCount * sizeof(uint16_t));
        settings.mode      = h.mode;
        settings.ply       = h.ply;
        settings.humanSide = h.humanSide;
        settings.flipBoard = h.flip;
    }
    f.close();
    return ok;
}

// ---------------------------------------------------------------------------
// PGN log
// ---------------------------------------------------------------------------
// Moves are written in long algebraic (e2e4, b1c3, e7e8q). That is not strict
// SAN, but it needs no disambiguation pass and lichess, SCID and ChessBase all
// import it. See README.
static void lan(index_t from, index_t to, Piece moved, bool capture, char* out) {
    uint8_t i = 0;
    const uint8_t t = getType(moved);
    if (t != Pawn && t != Empty) out[i++] = "  NBRQK"[t];
    out[i++] = 'a' + (from % 8);
    out[i++] = '8' - (from / 8);
    if (capture) out[i++] = 'x';
    out[i++] = 'a' + (to % 8);
    out[i++] = '8' - (to / 8);
    // A pawn reaching the last rank is auto-queened by the engine.
    if (t == Pawn && (to / 8 == 0 || to / 8 == 7)) out[i++] = 'q';
    out[i] = '\0';
}

void sd_pgn_begin() {
    histCount = 0;
    bookExhausted = false;
    if (!sdOK) return;
    File f = SD.open(F_PGN, FILE_WRITE);
    if (!f) return;
    f.println();
    f.println(F("[Event \"Megachess\"]"));
    f.println(F("[Site \"Mega 2560\"]"));
    f.print(F("[White \""));
    f.print((settings.mode == MODE_HOTSEAT ||
             (settings.mode == MODE_HUMAN_AI && settings.humanSide == White))
            ? F("Human") : F("MicroChess"));
    f.println(F("\"]"));
    f.print(F("[Black \""));
    f.print((settings.mode == MODE_HOTSEAT ||
             (settings.mode == MODE_HUMAN_AI && settings.humanSide == Black))
            ? F("Human") : F("MicroChess"));
    f.println(F("\"]"));
    f.print(F("[Depth \""));
    f.print(settings.ply);
    f.println(F("\"]"));
    f.println();
    f.close();
}

void sd_pgn_move(index_t from, index_t to, Piece moved, bool capture) {
    if (histCount < HIST_MAX)
        moveHist[histCount++] = (uint16_t) from | ((uint16_t) to << 6);

    if (!sdOK) return;
    File f = SD.open(F_PGN, FILE_WRITE);
    if (!f) return;
    char m[10];
    lan(from, to, moved, capture, m);
    // histCount is now the ply count; White's moves are the odd ones.
    if (histCount & 1) {
        f.print((histCount + 1) / 2);
        f.print('.');
    }
    f.print(m);
    f.print(((histCount & 1) || (histCount % 16 == 0)) ? ' ' : '\n');
    f.close();
}

void sd_pgn_finish(uint8_t state) {
    if (!sdOK) return;
    File f = SD.open(F_PGN, FILE_WRITE);
    if (!f) return;
    f.println();
    switch (state) {
        case WHITE_CHECKMATE:  f.println(F("1-0    {White mates}"));        break;
        case BLACK_CHECKMATE:  f.println(F("0-1    {Black mates}"));        break;
        case WHITE_3_MOVE_REP: f.println(F("0-1    {repetition}"));         break;
        case BLACK_3_MOVE_REP: f.println(F("1-0    {repetition}"));         break;
        case STALEMATE:        f.println(F("1/2-1/2 {stalemate}"));         break;
        case MOVE_LIMIT:       f.println(F("1/2-1/2 {move limit}"));        break;
        default:               f.println(F("*"));                           break;
    }
    f.close();
}

// ---------------------------------------------------------------------------
// opening book
// ---------------------------------------------------------------------------
// BOOK.TXT holds one line per opening, moves in plain long algebraic separated
// by spaces, e.g.  "e2e4 e7e5 g1f3 b8c6 f1b5"  - a '#' starts a comment.
// We look for lines whose first histCount moves match the game so far and take
// the next move, picking at random among the candidates.
void sd_book_reset() {
    histCount = 0;
    bookExhausted = false;
}

// Undo rolls the board back, so the move history the book matches against has
// to come back with it - otherwise the book is comparing its lines to moves
// that are no longer on the board.
void sd_hist_pop() {
    if (histCount) histCount--;
    bookExhausted = false;
}

static bool parse_move(const char* s, index_t& from, index_t& to) {
    if (s[0] < 'a' || s[0] > 'h' || s[1] < '1' || s[1] > '8') return false;
    if (s[2] < 'a' || s[2] > 'h' || s[3] < '1' || s[3] > '8') return false;
    from = (index_t) ((s[0] - 'a') + ('8' - s[1]) * 8);
    to   = (index_t) ((s[2] - 'a') + ('8' - s[3]) * 8);
    return true;
}

bool sd_book_move(index_t& from, index_t& to) {
    if (!sdOK || bookExhausted || histCount >= HIST_MAX) return false;

    File f = SD.open(F_BOOK, FILE_READ);
    if (!f) { bookExhausted = true; return false; }

    uint8_t matches = 0;
    char line[96];

    while (f.available()) {
        uint8_t n = 0;
        while (f.available() && n < sizeof(line) - 1) {
            const char c = f.read();
            if (c == '\n' || c == '\r') { if (n) break; else continue; }
            line[n++] = c;
        }
        line[n] = '\0';
        if (!n || line[0] == '#') continue;

        // Walk the line, checking it against the game so far.
        uint8_t ply = 0;
        bool    ok = true;
        char*   p = line;
        index_t bf, bt;
        while (*p) {
            while (*p == ' ') p++;
            if (!*p) break;
            if (!parse_move(p, bf, bt)) { ok = false; break; }

            if (ply < histCount) {
                const uint16_t want = (uint16_t) bf | ((uint16_t) bt << 6);
                if (moveHist[ply] != want) { ok = false; break; }
            } else {
                // First move past the prefix - a candidate. Reservoir sample
                // so every matching line gets an equal chance without keeping
                // a list.
                matches++;
                if (random(matches) == 0) { from = bf; to = bt; }
                break;
            }
            ply++;
            while (*p && *p != ' ') p++;
        }
        (void) ok;
    }
    f.close();

    if (matches == 0) { bookExhausted = true; return false; }
    return true;
}
