/*
 * ui.cpp - everything that touches the screen.
 *
 * Rotation 1: 480 x 320 landscape. The left 320x320 is the board at 40px per
 * square, exactly the size of the piece artwork, so a piece is a straight blit
 * with no scaling. The right 160px is the status panel.
 *
 * Pieces come from the SD card when a .SET is loaded - full colour with an
 * alpha channel, blended against the square underneath. With no card the
 * 1-bit masks in pieces_bmp.h stand in.
 *
 * Nothing here knows the rules. It reads board/game and draws them.
 */
#include "megachess.h"
#include "pieces_bmp.h"

// AVR pointers are 16 bits, so a PROGMEM table of pointers reads with
// pgm_read_word. This sketch is AVR-only (Mega 2560).
#define PGM_PTR(arr, i) ((const uint8_t*) pgm_read_word(&(arr)[i]))

Theme cur;

void theme_apply(uint8_t idx) {
    if (idx >= THEME_COUNT) idx = 0;
    memcpy_P(&cur, &themes[idx], sizeof(Theme));
}

// --- panel geometry --------------------------------------------------------
// No title or version in play: the menu already says MEGACHESS, and the
// 70px they took is what makes everything below readable at size 2.
#define P_CARD_Y     6
#define P_CARD_H   88                  // three size-2 lines
#define P_TAKEN_Y  100                  // "TAKEN" caption + material balance
#define P_TRAY_A   120                  // pieces White has taken
#define P_TRAY_B   146                  // pieces Black has taken
#define P_RULE_Y   174
#define BTN_X      (PANEL_X + 10)
#define BTN_W      140
#define BTN_H      34
#define UNDO_Y     180
#define MENU_Y     220
#define NEW_Y      260                  // ends 298, clear of the footer
#define P_FOOT_Y   300
#define TRAY_PITCH  23                  // 22px glyphs, six a row

// ---------------------------------------------------------------------------
// text helpers - the built-in GFX font advances 6*size px per character
// ---------------------------------------------------------------------------
static void text_at(int16_t x, int16_t y, uint8_t size, uint16_t col, const char* s) {
    tft.setTextSize(size);
    tft.setTextColor(col);
    tft.setCursor(x, y);
    tft.print(s);
}

static void text_at_P(int16_t x, int16_t y, uint8_t size, uint16_t col,
                      const __FlashStringHelper* s) {
    tft.setTextSize(size);
    tft.setTextColor(col);
    tft.setCursor(x, y);
    tft.print(s);
}

static void text_mid(int16_t x, int16_t w, int16_t y, uint8_t size,
                     uint16_t col, const char* s) {
    text_at(x + (w - (int16_t) strlen(s) * 6 * size) / 2, y, size, col, s);
}

static void text_mid_P(int16_t x, int16_t w, int16_t y, uint8_t size,
                       uint16_t col, const __FlashStringHelper* s) {
    text_at_P(x + (w - (int16_t) strlen_P((const char*) s) * 6 * size) / 2,
              y, size, col, s);
}

static void button(int16_t x, int16_t y, int16_t w, int16_t h,
                   const __FlashStringHelper* label, bool primary, bool enabled) {
    const uint16_t bg = primary ? cur.accent : (enabled ? cur.card : cur.panelBg);
    const uint16_t fg = primary ? cur.panelBg : (enabled ? cur.text : cur.textDim);
    tft.fillRect(x, y, w, h, bg);
    tft.drawRect(x, y, w, h, primary ? cur.accent : cur.cardHi);
    text_mid_P(x, w, y + (h - 16) / 2, 2, fg, label);
}

static void button_s(int16_t x, int16_t y, int16_t w, int16_t h,
                     const char* label, bool primary, bool enabled) {
    const uint16_t bg = primary ? cur.accent : (enabled ? cur.card : cur.panelBg);
    const uint16_t fg = primary ? cur.panelBg : (enabled ? cur.text : cur.textDim);
    tft.fillRect(x, y, w, h, bg);
    tft.drawRect(x, y, w, h, primary ? cur.accent : cur.cardHi);
    text_mid(x, w, y + (h - 16) / 2, 2, fg, label);
}

// ---------------------------------------------------------------------------
// board
// ---------------------------------------------------------------------------
static void sq_to_xy(index_t sq, int16_t& x, int16_t& y) {
    int8_t c = sq % 8, r = sq / 8;
    if (settings.flipBoard) { c = 7 - c; r = 7 - r; }
    x = (int16_t) c * SQ;
    y = (int16_t) r * SQ;
}

void square_name(index_t sq, char* out) {
    out[0] = 'a' + (sq % 8);
    out[1] = '8' - (sq / 8);     // engine row 0 is rank 8
    out[2] = '\0';
}

// Mix two RGB565s. w is 0..16, the weight of a.
static uint16_t mix565(uint16_t a, uint16_t b, uint8_t w) {
    const uint8_t iw = 16 - w;
    const uint16_t r = ((((a >> 11) & 0x1F) * w) + (((b >> 11) & 0x1F) * iw)) >> 4;
    const uint16_t g = ((((a >> 5) & 0x3F) * w) + (((b >> 5) & 0x3F) * iw)) >> 4;
    const uint16_t bl = (((a & 0x1F) * w) + ((b & 0x1F) * iw)) >> 4;
    return (r << 11) | (g << 5) | bl;
}

static void draw_piece(Piece p, int16_t x, int16_t y, uint16_t squareColour) {
    const uint8_t t = getType(p);                 // Pawn=1 .. King=6
    const bool    w = (getSide(p) == White);

    if (sd_pieces_blit(t - 1, w, x, y, squareColour)) return;

    // No card: three flat 1-bit layers. Black takes a dark rim and a light
    // detail pass, so it stays a dark silhouette on a light square.
    tft.drawBitmap(x, y, PGM_PTR(piece_fill, t - 1), PIECE_PX, PIECE_PX,
                   w ? cur.wFill : cur.bFill);
    tft.drawBitmap(x, y, PGM_PTR(piece_rim, t - 1), PIECE_PX, PIECE_PX,
                   w ? cur.wRim : cur.bRim);
    tft.drawBitmap(x, y, PGM_PTR(piece_det, t - 1), PIECE_PX, PIECE_PX,
                   w ? cur.wDet : cur.bDet);
}

void ui_draw_square(index_t sq) {
    if (sq < 0 || sq > 63) return;

    int16_t x, y;
    sq_to_xy(sq, x, y);

    const int8_t c = sq % 8, r = sq / 8;
    const bool light = (((c + r) & 1) == 0);
    const uint16_t bg = light ? cur.lightSq : cur.darkSq;
    tft.fillRect(x, y, SQ, SQ, bg);

    // The piece goes down FIRST. The card blit streams all 1600 pixels of the
    // square - transparent ones as the square colour - so anything drawn
    // before it is wiped. Borders and coordinates were invisible on every
    // occupied square until this was reordered.
    const Piece p = board.get(sq);
    const bool occupied = !isEmpty(p);
    if (occupied) draw_piece(p, x, y, bg);

    // Coordinates: the opposite square colour, pulled back toward this one so
    // they stay a hint rather than a label.
    const int8_t sc = settings.flipBoard ? 7 - c : c;
    const int8_t sr = settings.flipBoard ? 7 - r : r;
    const uint16_t tint = mix565(light ? cur.darkSq : cur.lightSq, bg, 11);
    if (sr == 7) {
        char f[2] = { (char) ('a' + c), '\0' };
        text_at(x + SQ - 7, y + SQ - 9, 1, tint, f);
    }
    if (sc == 0) {
        char n[2] = { (char) ('8' - r), '\0' };
        text_at(x + 3, y + 3, 1, tint, n);
    }

    if (sq == last_from || sq == last_to) {
        tft.drawRect(x,     y,     SQ,     SQ,     C_LASTMOVE);
        tft.drawRect(x + 1, y + 1, SQ - 2, SQ - 2, C_LASTMOVE);
    }

    if (occupied && getType(p) == King) {
        const bool inChk = (getSide(p) == White) ? game.white_king_in_check
                                                 : game.black_king_in_check;
        if (inChk) {
            for (uint8_t k = 0; k < 3; k++)
                tft.drawRect(x + k, y + k, SQ - 2 * k, SQ - 2 * k, C_CHECK);
        }
    }

    if (sq == sel_from) {
        for (uint8_t k = 0; k < 3; k++)
            tft.drawRect(x + k, y + k, SQ - 2 * k, SQ - 2 * k, C_SEL);
    }
}

static void draw_target(index_t sq) {
    int16_t x, y;
    sq_to_xy(sq, x, y);
    const int16_t cx = x + SQ / 2, cy = y + SQ / 2;
    if (isEmpty(board.get(sq))) {
        tft.fillCircle(cx, cy, 6, C_TARGET);
    } else {
        for (uint8_t k = 0; k < 3; k++)
            tft.drawCircle(cx, cy, SQ / 2 - 3 - k, C_TARGET);
    }
}

// Rings appearing outward from a square, ~160ms. Nothing is erased: the
// square gets redrawn by whatever happens next (the move lands, or the
// rejection toast), so the rings simply vanish with the decision.
void ui_ripple(index_t sq) {
    int16_t x, y;
    sq_to_xy(sq, x, y);
    const int16_t cx = x + SQ / 2, cy = y + SQ / 2;
    // radius 20 would put the outer ring's last pixel in the next square
    for (uint8_t r = 3; r <= 18; r += 5) {
        tft.drawCircle(cx, cy, r,     C_RIPPLE);
        tft.drawCircle(cx, cy, r + 1, C_RIPPLE);
        delay(40);
    }
}

index_t think_sq    = -1;
Piece   think_piece = Empty;

// One frame of the thinking ripple, on the piece you just moved. Called from
// inside the engine's search through its own live_update hook
// (set_led_strip), which the engine rate-limits to ~10ms, so this only has
// to throttle itself to something the eye can follow.
//
// Rings are not erased individually - erasing on the board would mean
// restoring the piece art under each ring, and the board cannot be read
// mid-search. Instead four rings ripple outward over ~440ms, then the square
// is repainted once from the piece captured before the search, and the cycle
// repeats. One SD blit per cycle is under 10% of think time.
void ui_think_tick() {
    static uint8_t  phase = 0;
    static uint32_t last  = 0;
    if (!ai_thinking || think_sq < 0) { phase = 0; return; }
    if (millis() - last < 110) return;
    last = millis();

    int16_t x, y;
    sq_to_xy(think_sq, x, y);
    const int16_t cx = x + SQ / 2, cy = y + SQ / 2;

    if (phase == 0) {
        const int8_t c = think_sq % 8, r = think_sq / 8;
        const uint16_t bg = (((c + r) & 1) == 0) ? cur.lightSq : cur.darkSq;
        tft.fillRect(x, y, SQ, SQ, bg);
        tft.drawRect(x,     y,     SQ,     SQ,     C_LASTMOVE);   // it IS last_to
        tft.drawRect(x + 1, y + 1, SQ - 2, SQ - 2, C_LASTMOVE);
        if (!isEmpty(think_piece)) draw_piece(think_piece, x, y, bg);
    }
    const uint8_t rad = 3 + phase * 5;                         // 3, 8, 13, 18
    tft.drawCircle(cx, cy, rad,     C_RIPPLE);
    tft.drawCircle(cx, cy, rad + 1, C_RIPPLE);
    phase = (phase + 1) & 3;
}

void ui_show_targets(index_t from, const uint8_t* mask) {
    ui_draw_square(from);
    for (index_t i = 0; i < 64; i++)
        if (getbit(mask, i)) draw_target(i);
}

void ui_clear_targets(index_t from, const uint8_t* mask) {
    for (index_t i = 0; i < 64; i++)
        if (getbit(mask, i)) ui_draw_square(i);
    ui_draw_square(from);
}

// ---------------------------------------------------------------------------
// panel
// ---------------------------------------------------------------------------
static char     status_text[16] = "";
static uint16_t status_colour   = 0;

static void draw_status_card() {
    tft.fillRect(PANEL_X + 6, P_CARD_Y, PANEL_W - 12, P_CARD_H, cur.card);
    tft.drawRect(PANEL_X + 6, P_CARD_Y, PANEL_W - 12, P_CARD_H, cur.cardHi);

    // 1: whose move - size 3. Always the side, even while the engine thinks;
    // THINKING is the status line under it.
    text_mid_P(PANEL_X, PANEL_W, P_CARD_Y + 8, 3, cur.accent,
               game.turn == White ? F("WHITE") : F("BLACK"));

    // 2: what - an explicit status wins, else say what to do
    if (status_text[0]) {
        text_mid(PANEL_X, PANEL_W, P_CARD_Y + 40, 2,
                 status_colour ? status_colour : cur.text, status_text);
    } else if (!ai_thinking) {
        const __FlashStringHelper* msg;
        if (!side_is_human((Color) game.turn)) msg = F("ENGINE");
        else if (sel_from < 0)                 msg = F("TAP A PIECE");
        else                                   msg = F("TAP A DOT");
        text_mid_P(PANEL_X, PANEL_W, P_CARD_Y + 40, 2, cur.text, msg);
    }

    // 3: the last move in real notation - "1. e2-e4", "1... g7-g5" - in full
    // text colour, not dim: it was the line nobody could read.
    char line[16];
    if (last_from >= 0) {
        char a[3], b[3];
        square_name(last_from, a);
        square_name(last_to, b);
        const unsigned n = (game.move_num + 1) / 2;
        snprintf(line, sizeof(line), (game.move_num & 1) ? "%u. %s-%s" : "%u... %s-%s", n, a, b);
    } else {
        snprintf(line, sizeof(line), "move 1");
    }
    text_mid(PANEL_X, PANEL_W, P_CARD_Y + 64, 2, cur.text, line);
}

void ui_set_status(const __FlashStringHelper* msg, uint16_t colour) {
    if (msg) {
        strncpy_P(status_text, (const char*) msg, sizeof(status_text) - 1);
        status_text[sizeof(status_text) - 1] = '\0';
    } else {
        status_text[0] = '\0';
    }
    status_colour = colour;
    if (screen == SCR_GAME) draw_status_card();
}

// A line in the menu's footer strip: why a button did nothing.
void ui_menu_note(const __FlashStringHelper* msg, uint16_t colour) {
    tft.fillRect(0, 258, SCR_W, 24, cur.panelBg);
    text_mid_P(0, SCR_W, 262, 2, colour, msg);
}

void ui_toast(const __FlashStringHelper* msg, uint16_t colour, uint16_t ms) {
    ui_set_status(msg, colour);
    delay(ms);
    ui_set_status(NULL, 0);
}

static const uint8_t pieceValue[7] = { 0, 1, 3, 3, 5, 9, 0 };

static void draw_trays() {
    tft.fillRect(PANEL_X, P_TAKEN_Y, PANEL_W, P_RULE_Y - P_TAKEN_Y, cur.panelBg);

    // Material from White's side: what White took minus what Black took.
    int bal = 0;
    for (uint8_t i = 0; i < game.white_taken_count; i++) bal += pieceValue[getType(game.taken_by_white[i].piece)];
    for (uint8_t i = 0; i < game.black_taken_count; i++) bal -= pieceValue[getType(game.taken_by_black[i].piece)];

    text_at_P(PANEL_X + 10, P_TAKEN_Y, 2, cur.textDim, F("TAKEN"));
    if (bal) {
        char b[6];
        snprintf(b, sizeof(b), "%+d", bal);
        text_at(PANEL_X + PANEL_W - 10 - 12 * (int16_t) strlen(b), P_TAKEN_Y, 2, cur.accent, b);
    }

    // Row A: pieces White has taken (Black's), in the black side's rim colour
    // so they read as the dark side on the dark panel. Row B: White's pieces
    // Black has taken, in the white body colour. Six a row; the balance
    // figure carries anything past that.
    for (uint8_t row = 0; row < 2; row++) {
        const bool whiteCaptor = (row == 0);
        const uint8_t n = whiteCaptor ? game.white_taken_count : game.black_taken_count;
        for (uint8_t i = 0; i < n && i < 6; i++) {
            const Piece p = whiteCaptor ? game.taken_by_white[i].piece
                                        : game.taken_by_black[i].piece;
            const uint8_t t = getType(p);
            if (t == Empty || t > King) continue;
            tft.drawBitmap(PANEL_X + 10 + i * TRAY_PITCH, row ? P_TRAY_B : P_TRAY_A,
                           PGM_PTR(piece_tray, t - 1), TRAY_PX, TRAY_PX,
                           whiteCaptor ? cur.bRim : cur.wFill);
        }
    }
    tft.drawFastHLine(PANEL_X + 10, P_RULE_Y, PANEL_W - 20, cur.cardHi);
}

void ui_draw_panel() {
    tft.fillRect(PANEL_X, 0, PANEL_W, SCR_H, cur.panelBg);
    draw_status_card();
    draw_trays();
    button(BTN_X, UNDO_Y, BTN_W, BTN_H, F("UNDO"), false, true);
    button(BTN_X, MENU_Y, BTN_W, BTN_H, F("MENU"), false, true);
    button(BTN_X, NEW_Y,  BTN_W, BTN_H, F("NEW"),  true,  true);
    ui_draw_touch_readout();
}

// Footer: where the artwork comes from and the engine depth.
// Footer: artwork source and engine depth. Its own strip below NEW - the old
// one was painted over the bottom of that button.
void ui_draw_touch_readout() {
    char f[16];
    snprintf(f, sizeof(f), "%s  skill %u", settings.ply >= SKILL_FIRST_UMAX ? "uMAX" : "MC",
             (unsigned) settings.ply);
    tft.fillRect(PANEL_X, P_FOOT_Y, PANEL_W, SCR_H - P_FOOT_Y, cur.panelBg);
    text_mid(PANEL_X, PANEL_W, P_FOOT_Y, 2, cur.textDim, f);
}

void ui_draw_game() {
    for (index_t i = 0; i < 64; i++) ui_draw_square(i);
    tft.drawRect(0, 0, BOARD_PX, BOARD_PX, cur.frame);
    ui_draw_panel();
}

// ---------------------------------------------------------------------------
// menu
// ---------------------------------------------------------------------------
#define M_MODE_Y   68
#define M_MODE_H   42
#define M_MODE_W  146
#define M_ROW2_Y  140
#define M_ROW2_H   42
#define M_ACT_Y   194
#define M_ACT_H    44

// The menu in parts, so a tap repaints only what it changed. Each part paints
// over its own footprint completely, so it can be redrawn in place.
static void menu_modes() {
    button(20,  M_MODE_Y, M_MODE_W, M_MODE_H, F("VS ENGINE"),
           settings.mode == MODE_HUMAN_AI, true);
    button(176, M_MODE_Y, M_MODE_W, M_MODE_H, F("2 PLAYER"),
           settings.mode == MODE_HOTSEAT, true);
    button(332, M_MODE_Y, M_MODE_W, M_MODE_H, F("DEMO"),
           settings.mode == MODE_AI_AI, true);
}

static void menu_skill() {
    const bool needDepth = (settings.mode != MODE_HOTSEAT);
    tft.fillRect(20, M_ROW2_Y - 20, 176, 16, cur.panelBg);   // the label changes width
    text_at_P(20, M_ROW2_Y - 20, 2, cur.textDim,
              settings.ply >= SKILL_FIRST_UMAX ? F("SKILL: uMAX") : F("SKILL: MCHESS"));
    button(20, M_ROW2_Y, 42, M_ROW2_H, F("-"), false, needDepth);
    tft.fillRect(68, M_ROW2_Y, 64, M_ROW2_H, cur.card);
    tft.drawRect(68, M_ROW2_Y, 64, M_ROW2_H, cur.cardHi);
    {
        char d[4];
        snprintf(d, sizeof(d), "%u", (unsigned) settings.ply);
        text_mid(68, 64, M_ROW2_Y + 9, 3, needDepth ? cur.text : cur.textDim, d);
    }
    button(138, M_ROW2_Y, 42, M_ROW2_H, F("+"), false, needDepth);
}

static void menu_side() {
    button(196, M_ROW2_Y, 92, M_ROW2_H,
           settings.humanSide == White ? F("WHITE") : F("BLACK"),
           false, settings.mode == MODE_HUMAN_AI);
}

static void menu_theme() {
    button_s(296, M_ROW2_Y, 90, M_ROW2_H, cur.name, false, true);
}

static void menu_board() {
    button(394, M_ROW2_Y, 84, M_ROW2_H,
           settings.flipBoard ? F("FLIP") : F("NORM"), false, true);
}

static void menu_actions() {
    const bool canResume = sd_has_saved_game();
    button(20,  M_ACT_Y, 170, M_ACT_H, F("RESUME"),   false, canResume);
    button(200, M_ACT_Y, 190, M_ACT_H, F("NEW GAME"), true,  true);
    button(400, M_ACT_Y,  78, M_ACT_H, F("CAL"),      false, true);

    if (!sd_present()) {
        text_mid_P(0, SCR_W, 262, 2, cur.textDim, F("no SD: built-in pieces"));
    } else if (canResume) {
        text_mid_P(0, SCR_W, 262, 2, C_SEL, F("saved game: tap RESUME"));
    } else {
        text_mid_P(0, SCR_W, 262, 2, cur.textDim, F("SD ready: saves + PGN on"));
    }
}

void ui_draw_menu() {
    tft.fillScreen(cur.panelBg);
    text_mid_P(0, SCR_W, 6, 4, cur.accent, F("MEGACHESS"));
    text_at_P(20,  M_MODE_Y - 20, 2, cur.textDim, F("GAME MODE"));
    menu_modes();
    menu_skill();
    text_at_P(196, M_ROW2_Y - 20, 2, cur.textDim, F("PLAY AS"));
    menu_side();
    text_at_P(296, M_ROW2_Y - 20, 2, cur.textDim, F("THEME"));
    menu_theme();
    text_at_P(394, M_ROW2_Y - 20, 2, cur.textDim, F("BOARD"));
    menu_board();
    menu_actions();
}

// After a tap on the menu: repaint only what that button changed. THEME is
// the exception - every colour on the screen moves with it.
void ui_menu_update(int8_t btn) {
    switch (btn) {
        case BTN_MODE_HUMAN_AI:
        case BTN_MODE_HOTSEAT:
        case BTN_MODE_AI_AI:  menu_modes(); menu_skill(); menu_side(); break;  // skill and side follow the mode
        case BTN_PLY_DOWN:
        case BTN_PLY_UP:      menu_skill(); break;
        case BTN_SIDE:        menu_side(); menu_board(); break;   // playing Black turns the board
        case BTN_FLIP:        menu_board(); break;
        default:              ui_draw_menu(); break;
    }
}

// Game over. The board is left exactly as it ended, for study - nothing is
// drawn over it. The verdict takes the top of the panel where the title was;
// the reason is already in the status card below it.
// Game over. The board is left exactly as it ended, for study. The verdict
// takes the status card: who, why (already in status_text), what next.
// Game over. The board is left exactly as it ended, for study. The verdict
// takes the status card: who, why (already in status_text), what next.
void ui_draw_over(const __FlashStringHelper* who, uint16_t colour) {
    tft.fillRect(PANEL_X + 6, P_CARD_Y, PANEL_W - 12, P_CARD_H, cur.card);
    tft.drawRect(PANEL_X + 6, P_CARD_Y, PANEL_W - 12, P_CARD_H, cur.cardHi);
    text_mid_P(PANEL_X, PANEL_W, P_CARD_Y + 10, 2, colour,   who);
    text_mid  (PANEL_X, PANEL_W, P_CARD_Y + 38, 2, cur.text, status_text);
    text_mid_P(PANEL_X, PANEL_W, P_CARD_Y + 64, 2, cur.textDim, F("NEW or MENU"));
}


// ---------------------------------------------------------------------------
// hit testing
// ---------------------------------------------------------------------------
index_t ui_hit_square(int16_t x, int16_t y) {
    if (x < 0 || x >= BOARD_PX || y < 0 || y >= BOARD_PX) return -1;
    int8_t c = x / SQ, r = y / SQ;
    if (settings.flipBoard) { c = 7 - c; r = 7 - r; }
    return (index_t) (c + r * 8);
}

static bool inRect(int16_t x, int16_t y, int16_t rx, int16_t ry,
                   int16_t rw, int16_t rh) {
    return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

int8_t ui_hit_button(int16_t x, int16_t y) {
    if (screen == SCR_MENU) {
        if (inRect(x, y, 20,  M_MODE_Y, M_MODE_W, M_MODE_H)) return BTN_MODE_HUMAN_AI;
        if (inRect(x, y, 176, M_MODE_Y, M_MODE_W, M_MODE_H)) return BTN_MODE_HOTSEAT;
        if (inRect(x, y, 332, M_MODE_Y, M_MODE_W, M_MODE_H)) return BTN_MODE_AI_AI;
        if (inRect(x, y, 20,  M_ROW2_Y, 42,  M_ROW2_H))      return BTN_PLY_DOWN;
        if (inRect(x, y, 138, M_ROW2_Y, 42,  M_ROW2_H))      return BTN_PLY_UP;
        if (inRect(x, y, 196, M_ROW2_Y, 92,  M_ROW2_H))      return BTN_SIDE;
        if (inRect(x, y, 296, M_ROW2_Y, 90,  M_ROW2_H))      return BTN_THEME;
        if (inRect(x, y, 394, M_ROW2_Y, 84,  M_ROW2_H))      return BTN_FLIP;
        if (inRect(x, y, 20,  M_ACT_Y,  170, M_ACT_H))       return BTN_RESUME;
        if (inRect(x, y, 200, M_ACT_Y,  190, M_ACT_H))       return BTN_START;
        if (inRect(x, y, 400, M_ACT_Y,   78, M_ACT_H))       return BTN_CALIB;
        return -1;
    }

    if (inRect(x, y, BTN_X, UNDO_Y, BTN_W, BTN_H)) return BTN_UNDO;
    if (inRect(x, y, BTN_X, MENU_Y, BTN_W, BTN_H)) return BTN_MENU;
    if (inRect(x, y, BTN_X, NEW_Y,  BTN_W, BTN_H)) return BTN_NEW;
    return -1;
}

// Push whatever calibration we have into the library. All-zero means the
// panel has never been calibrated, so keep the library's own defaults.
void ui_apply_calibration() {
    if (settings.calMinX || settings.calMaxX ||
        settings.calMinY || settings.calMaxY) {
        tft.setTouchCalibration(settings.calMinX, settings.calMaxX,
                                settings.calMinY, settings.calMaxY);
    } else {
        tft.setTouchCalibration(136, 907, 942, 139);
    }
}

// Wait for a firm, settled press and return its averaged raw reading.
static bool read_target(int& rawX, int& rawY) {
    int x, y, z;
    // wait for release first, so one press cannot answer two targets
    uint8_t clear = 0;
    const uint32_t deadline = millis() + 30000UL;
    while (clear < 20) {
        if (millis() > deadline) return false;      // never trap the user
        tft.readTouchRaw(x, y, z);
        clear = (z <= 10) ? clear + 1 : 0;
        delay(5);
    }
    // wait for a press
    uint8_t held = 0;
    while (held < 8) {
        if (millis() > deadline) return false;
        tft.readTouchRaw(x, y, z);
        held = (z > 20) ? held + 1 : 0;
        delay(5);
    }
    // average a burst
    long sx = 0, sy = 0;
    uint8_t n = 0;
    for (uint8_t i = 0; i < 24; i++) {
        tft.readTouchRaw(x, y, z);
        if (z > 20) { sx += x; sy += y; n++; }
        delay(4);
    }
    if (n < 8) return false;
    rawX = (int) (sx / n);
    rawY = (int) (sy / n);
    return true;
}

static void draw_target_cross(int16_t cx, int16_t cy, uint16_t col) {
    tft.drawCircle(cx, cy, 14, col);
    tft.drawCircle(cx, cy, 13, col);
    tft.drawFastHLine(cx - 20, cy, 41, col);
    tft.drawFastVLine(cx, cy - 20, 41, col);
    tft.fillCircle(cx, cy, 3, col);
}

// Two-point calibration. getTouch() maps raw->screen linearly, so two known
// screen points are enough to solve for the four constants it uses.
void ui_calibrate() {
    const int16_t TL_X = 40,  TL_Y = 40;
    const int16_t BR_X = SCR_W - 41, BR_Y = SCR_H - 41;

    int r1x, r1y, r2x, r2y;

    tft.fillScreen(cur.panelBg);
    text_mid_P(0, SCR_W, 100, 3, cur.accent, F("CALIBRATE"));
    text_mid_P(0, SCR_W, 150, 2, cur.text,    F("press each cross exactly"));
    text_mid_P(0, SCR_W, 176, 2, cur.textDim, F("hard stylus or fingernail"));
    text_mid_P(0, SCR_W, 210, 2, cur.textDim, F("wait 30s to cancel"));
    delay(1200);

    tft.fillScreen(cur.panelBg);
    draw_target_cross(TL_X, TL_Y, C_SEL);
    text_mid_P(0, SCR_W, 150, 2, cur.text, F("press the top-left cross"));
    if (!read_target(r1x, r1y)) return;

    tft.fillScreen(cur.panelBg);
    draw_target_cross(BR_X, BR_Y, C_SEL);
    text_mid_P(0, SCR_W, 150, 2, cur.text, F("press the bottom-right cross"));
    if (!read_target(r2x, r2y)) return;

    // Rotation 3, from the library's getTouch():
    //   screenX = map(rawY, maxY, minY, 0, W-1)
    //   screenY = map(rawX, maxX, minX, 0, H-1)
    // so rawY is linear in screenX and rawX is linear in screenY. Solve each
    // for its value at screen 0 and at screen max.
    const int16_t W = SCR_W - 1, H = SCR_H - 1;

    const float sy = (float) (r2y - r1y) / (float) (BR_X - TL_X);
    const float maxY = r1y - sy * TL_X;
    const float minY = maxY + sy * W;

    const float sx = (float) (r2x - r1x) / (float) (BR_Y - TL_Y);
    const float maxX = r1x - sx * TL_Y;
    const float minX = maxX + sx * H;

    settings.calMinX = (int16_t) (minX + 0.5f);
    settings.calMaxX = (int16_t) (maxX + 0.5f);
    settings.calMinY = (int16_t) (minY + 0.5f);
    settings.calMaxY = (int16_t) (maxY + 0.5f);
    ui_apply_calibration();
    sd_save_settings();

    Serial.print(F("calibration: minX="));  Serial.print(settings.calMinX);
    Serial.print(F(" maxX="));              Serial.print(settings.calMaxX);
    Serial.print(F(" minY="));              Serial.print(settings.calMinY);
    Serial.print(F(" maxY="));              Serial.println(settings.calMaxY);

    tft.fillScreen(cur.panelBg);
    text_mid_P(0, SCR_W, 20, 3, C_SEL, F("SAVED"));
    text_mid_P(0, SCR_W, 56, 2, cur.textDim, F("drag: dot should follow you"));

    // A real button to leave by. Relying on a corner tap was daft: if the
    // calibration is still off, a corner is the least likely place to
    // register. This also times out on its own, so the screen can never
    // trap you.
    const int16_t dx = SCR_W / 2 - 70, dy = SCR_H - 60;
    button(dx, dy, 140, 44, F("DONE"), true, true);

    const uint32_t started = millis();
    uint32_t lastTouch = started;

    while (true) {
        int tx, ty;
        if (tft.getTouch(tx, ty)) {
            lastTouch = millis();
            if (tx >= dx && tx < dx + 140 && ty >= dy && ty < dy + 44) {
                delay(300);
                return;
            }
            // do not scribble over the button or the text
            if (ty > 70 && ty < dy - 6) tft.fillCircle(tx, ty, 3, C_SEL);
        }
        // leave on its own if untouched for a while, or after a hard cap
        if (millis() - lastTouch > 12000UL) return;
        if (millis() - started   > 90000UL) return;
    }
}

// Modal yes/no over the board. Draw and hit-test only - the sketch owns the
// input loop. It sits over the board so the panel's own buttons stay out of
// it; the caller repaints the board afterwards, being the only thing that
// knows what was underneath.
#define CF_X      30
#define CF_Y      95
#define CF_W     (BOARD_PX - 60)
#define CF_H     130
#define CF_BY    (CF_Y + 78)
#define CF_BW    100
#define CF_BH    40
#define CF_YES_X (CF_X + 20)
#define CF_NO_X  (CF_X + CF_W - 20 - CF_BW)

void ui_confirm_draw(const __FlashStringHelper* title, const __FlashStringHelper* line) {
    tft.fillRect(CF_X, CF_Y, CF_W, CF_H, cur.card);
    tft.drawRect(CF_X,     CF_Y,     CF_W,     CF_H,     cur.accent);
    tft.drawRect(CF_X + 1, CF_Y + 1, CF_W - 2, CF_H - 2, cur.accent);
    text_mid_P(CF_X, CF_W, CF_Y + 12, 3, cur.text,    title);
    text_mid_P(CF_X, CF_W, CF_Y + 46, 2, cur.textDim, line);
    button(CF_YES_X, CF_BY, CF_BW, CF_BH, F("YES"), false, true);
    button(CF_NO_X,  CF_BY, CF_BW, CF_BH, F("NO"),  true,  true);   // the safe answer is the primary
}

int8_t ui_confirm_hit(int16_t x, int16_t y) {
    if (inRect(x, y, CF_YES_X, CF_BY, CF_BW, CF_BH)) return 1;
    if (inRect(x, y, CF_NO_X,  CF_BY, CF_BW, CF_BH)) return 0;
    return -1;
}

// Adafruit's HX8357D gamma table (Adafruit_HX8357 initd[], SETGAMMA 0xE0),
// verified against their source. Comment the define out to A/B against the
// controller's ROM default.
// #define MEGACHESS_GAMMA_ADAFRUIT   // off: made things worse on this glass. ROM curve it is.
static const uint8_t hx8357d_gamma[34] PROGMEM = {
    0x02, 0x0A, 0x11, 0x1d, 0x23, 0x35, 0x41, 0x4b, 0x4b, 0x42, 0x3A, 0x27, 0x1B, 0x08, 0x09, 0x03,
    0x02, 0x0A, 0x11, 0x1d, 0x23, 0x35, 0x41, 0x4b, 0x4b, 0x42, 0x3A, 0x27, 0x1B, 0x08, 0x09, 0x03,
    0x00, 0x01 };

void ui_begin() {
    tft.begin();
#if defined(MEGACHESS_GAMMA_ADAFRUIT) && defined(MEGACHESS_DRIVER_HX8357D)
    tft.writeRegister(0xE0, hx8357d_gamma, sizeof(hx8357d_gamma));
#endif
    // Rotation 1 and 3 are both 480x320 landscape, 180 degrees apart.
    tft.setRotation(3);
    ui_apply_calibration();
    // Display OFF (MIPI DCS 0x28): the panel shows nothing while the first
    // screen - the logo, or the menu without a card - is written into its
    // memory. ui_display_on() then shows it whole: no clear, no flash.
    tft.writeRegister(0x28, NULL, 0);
}

void ui_display_on() {
    tft.writeRegister(0x29, NULL, 0);      // Display ON
}

void ui_display_off() {
    tft.writeRegister(0x28, NULL, 0);      // Display OFF: draw the next screen unseen
}
