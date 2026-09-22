/*
 * megachess.h - shared types, theme table and layout for Megachess.
 *
 * Board geometry and the cross-file API live here. The chess rules are
 * MicroChess (src/engine); everything in the sketch root is presentation,
 * input and storage.
 */
#ifndef MEGACHESS_H
#define MEGACHESS_H

#include <Arduino.h>
#include <DIYables_TFT_Touch_Shield.h>

// ---------------------------------------------------------------------------
// Which driver IC is on your shield. DIYables ship this board with one of two:
// RM68140 on early units, HX8357D on later ones after a chip shortage. Both
// use this library (v2.x) and share an API - only the class name differs.
// Run bringup/bringup.ino once; it prints the line to paste here.
// (An ILI9488 is the older, discontinued shield and needs library v1.0.0.)
// ---------------------------------------------------------------------------
//#define MEGACHESS_DRIVER_RM68140
#define MEGACHESS_DRIVER_HX8357D    // confirmed by bringup on this board

#if defined(MEGACHESS_DRIVER_HX8357D)
  typedef DIYables_TFT_HX8357D_Shield PanelBase;
#elif defined(MEGACHESS_DRIVER_RM68140)
  typedef DIYables_TFT_RM68140_Shield PanelBase;
#else
  #error "Define MEGACHESS_DRIVER_RM68140 or MEGACHESS_DRIVER_HX8357D"
#endif

// The DIYables init leaves the controller's ROM-default gamma in place -
// "good enough for this shield", their comment says. It is not: mid-tones
// come out boosted and oranges go yellow. The library keeps the raw command
// path protected, so this thin subclass exposes just enough to load a curve.
class MegaPanel : public PanelBase {
public:
    void writeRegister(uint8_t cmd, const uint8_t* data, uint8_t n) {
        writeCommand(cmd);
        for (uint8_t i = 0; i < n; i++) writeData(pgm_read_byte(data + i));
    }
};
typedef MegaPanel Panel;

extern Panel tft;

// MicroChess. Included last: its printf() macro shadows the real one, so
// nothing below may call printf. Use snprintf/Serial.print instead.
#include "src/engine/MicroChess.h"

extern board_t board;
extern game_t  game;

// ---------------------------------------------------------------------------
// Geometry - rotation 1, so 480 x 320 landscape, board left, panel right.
// ---------------------------------------------------------------------------
#define SCR_W       480
#define SCR_H       320
#define SQ          40                  // one square, and PIECE_PX
#define BOARD_PX    (SQ * 8)            // 320
#define PANEL_X     BOARD_PX
#define PANEL_W     (SCR_W - PANEL_X)   // 160

#define RGB(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))

#include "themes_gen.h"

// The live theme, copied out of PROGMEM once per change so the draw path is
// not doing pgm_read_word on every square.
extern Theme cur;
void theme_apply(uint8_t idx);

// Highlight colours are deliberately theme-independent: they have to stand out
// against every board, and a green that reads on amber also reads on sage.
#define C_SEL       RGB( 96, 200, 110)
#define C_TARGET    RGB( 60, 170,  95)
#define C_LASTMOVE  RGB(216, 190,  72)
#define C_CHECK     RGB(222,  72,  60)
#define C_RIPPLE    RGB(255,  64,  40)   // tap ripple + thinking ripple: feedback, not state
#define C_BAD       RGB(222,  92,  80)

// ---------------------------------------------------------------------------
// App state
// ---------------------------------------------------------------------------
enum AppMode   : uint8_t { MODE_HUMAN_AI = 0, MODE_HOTSEAT = 1, MODE_AI_AI = 2 };
enum AppScreen : uint8_t { SCR_MENU = 0, SCR_GAME = 1, SCR_OVER = 2 };

// Persisted to SD as /MEGACHESS/SETTINGS.DAT.
struct Settings {
    uint32_t magic;
    uint8_t  mode;        // AppMode
    uint8_t  ply;         // engine search depth, 1..4
    uint8_t  humanSide;   // White or Black, only used in MODE_HUMAN_AI
    uint8_t  flipBoard;   // draw with Black at the bottom
    uint8_t  theme;       // index into themes[]
    // Touch calibration, in the library's rotation-0 raw ADC space. Zero in
    // all four means "never calibrated" and the library defaults are used.
    int16_t  calMinX, calMaxX, calMinY, calMaxY;
};
#define SETTINGS_MAGIC 0x4D434833UL     // "MCH3" - bumped for the cal fields

extern Settings settings;
extern AppScreen screen;

// Half-moves of undo kept in RAM. Each slot is a board + game snapshot; see
// the RAM note in README.md before raising it.
#define UNDO_SLOTS 4

// SKILL on the menu: 1-2 are MicroChess plies, from SKILL_FIRST_UMAX up it is
// micro-Max on a clock (see Megachess.ino).
#define SKILL_MAX        7
#define SKILL_FIRST_UMAX 3

// Defined in src/engine/engine.cpp; not declared in MicroChess.h upstream.
extern void reset_turn_flags();

// Declared here rather than in the .ino: the Arduino build hoists generated
// prototypes to the top of the sketch, above any type the sketch declares.
enum MoveResult : uint8_t { MV_OK, MV_ILLEGAL, MV_SELF_CHECK, MV_NONE };

// ---------------------------------------------------------------------------
// ui.cpp
// ---------------------------------------------------------------------------
void ui_begin();
void ui_draw_menu();
void ui_draw_game();                       // full repaint
void ui_draw_square(index_t sq);
void ui_draw_panel();
void ui_set_status(const __FlashStringHelper* msg, uint16_t colour);
void ui_show_targets(index_t from, const uint8_t* mask);
void ui_clear_targets(index_t from, const uint8_t* mask);
void ui_draw_over(const __FlashStringHelper* who, uint16_t colour);
void ui_calibrate();               // 2-point touch calibration, saves to SD
void ui_ripple(index_t sq);        // rings out from a square: tap acknowledged
void ui_confirm_draw(const __FlashStringHelper* title, const __FlashStringHelper* line);
int8_t ui_confirm_hit(int16_t x, int16_t y);   // 1 yes, 0 no, -1 neither
void ui_think_tick();              // one spinner frame; the engine calls this
// Where the thinking ripple plays and what sits there. Captured BEFORE the
// search starts: the board cannot be read mid-search, it is full of trial
// moves.
extern index_t think_sq;
extern Piece   think_piece;
void ui_apply_calibration();
void ui_draw_touch_readout();
extern int16_t tap_x, tap_y;     // last tap, -1 until one happens
void ui_splash(const __FlashStringHelper* line1, const __FlashStringHelper* line2);
void ui_toast(const __FlashStringHelper* msg, uint16_t colour, uint16_t ms);

index_t ui_hit_square(int16_t x, int16_t y);
int8_t  ui_hit_button(int16_t x, int16_t y);

// Panel buttons
#define BTN_UNDO   0
#define BTN_MENU   1
#define BTN_NEW    2
// Menu buttons
#define BTN_MODE_HUMAN_AI 10
#define BTN_MODE_HOTSEAT  11
#define BTN_MODE_AI_AI    12
#define BTN_PLY_DOWN      13
#define BTN_PLY_UP        14
#define BTN_SIDE          15
#define BTN_THEME         16
#define BTN_FLIP          17
#define BTN_START         18
#define BTN_RESUME        19
#define BTN_CALIB         20

// ---------------------------------------------------------------------------
// storage.cpp - every entry point degrades to a no-op with no card fitted.
// ---------------------------------------------------------------------------
bool sd_begin();
bool sd_present();
void sd_load_settings();
void sd_save_settings();
bool sd_has_saved_game();
bool sd_save_game();
bool sd_load_game();
void sd_clear_saved_game();
void sd_pgn_begin();
void sd_pgn_move(index_t from, index_t to, Piece moved, bool capture);
void sd_pgn_finish(uint8_t state);
bool sd_book_move(index_t& from, index_t& to);
void sd_book_reset();
void sd_hist_pop();

// Piece artwork. Falls back to the 1-bit flash masks when no set is loaded.
bool sd_pieces_load(const char* name);     // "AMBER" -> /PIECES/AMBER.SET
bool sd_splash();                          // paint /MEGACHESS/SPLASH.IMG if present
bool sd_pieces_ready();
// Blit one piece onto a square already filled with squareColour.
bool sd_pieces_blit(uint8_t typeIdx, bool white, int16_t x, int16_t y,
                    uint16_t squareColour);

// ---------------------------------------------------------------------------
// Megachess.ino
// ---------------------------------------------------------------------------
extern uint8_t  legal_mask[8];     // 64-bit set of destinations for sel_from
extern index_t  sel_from;
extern index_t  last_from, last_to;
extern bool     ai_thinking;

bool side_is_human(Color side);
void square_name(index_t sq, char* out);   // out[3]: "e4"

#endif // MEGACHESS_H
