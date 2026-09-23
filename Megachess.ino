/*
 * Megachess - chess on an Arduino Mega 2560 with the DIYables 3.5" 480x320
 * touch shield.
 *
 *   rules + search   MicroChess by Trent M. Wyatt, in src/engine (MIT)
 *   display + touch  DIYables_TFT_Touch_Shield
 *   artwork, UI, storage, engine glue   this sketch
 *
 * Run bringup/bringup.ino FIRST. It tells you which driver IC your shield has
 * (paste the line it prints into megachess.h), whether touch lands where you
 * press, and whether the SD slot is reachable. See README.md.
 *
 * The engine's own take_turn() blocks on Serial for a human move, so this
 * sketch does not use it; apply_move() and ai_move() below drive the same
 * engine pieces non-interactively. Everything else in src/engine is upstream
 * apart from two documented safety patches - see README.
 */
#include <SD.h>
#include "megachess.h"
#include "src/umax/umax.h"

Panel    tft;
// board and game are defined by the engine itself, in src/engine/engine.cpp -
// megachess.h only declares them extern.

// Calibration zeros mean "never calibrated" - the library defaults apply
// until the CAL button on the menu is used.
Settings settings = { SETTINGS_MAGIC, MODE_HUMAN_AI, 4 /* micro-Max, 2.5 s */, White, 0,
                      1 /* AMBER */, 0, 0, 0, 0 };
AppScreen screen = SCR_MENU;

// ---------------------------------------------------------------------------
// SKILL. Levels 1 and 2 are MicroChess searching that many plies. From
// SKILL_FIRST_UMAX up the move comes from micro-Max on a clock, and MicroChess
// only keeps the rules (and steps in if the two ever disagree).
// ---------------------------------------------------------------------------
static const uint16_t UMAX_BUDGET_MS[SKILL_MAX - SKILL_FIRST_UMAX + 1] PROGMEM =
    { 1000, 2500, 5000, 10000, 20000 };

static bool skill_uses_umax() { return settings.ply >= SKILL_FIRST_UMAX; }

static uint16_t skill_budget_ms() {
    uint8_t i = settings.ply - SKILL_FIRST_UMAX;
    if (i > SKILL_MAX - SKILL_FIRST_UMAX) i = SKILL_MAX - SKILL_FIRST_UMAX;
    return pgm_read_word(&UMAX_BUDGET_MS[i]);
}

uint8_t legal_mask[8];
index_t sel_from  = -1;
index_t last_from = -1, last_to = -1;
bool    ai_thinking = false;
int16_t tap_x = -1, tap_y = -1;   // last tap, for the on-screen readout

// ---------------------------------------------------------------------------
// undo. Each slot is a full board + game snapshot; the newest also serves as
// the scratch copy for the "does this move leave my own king in check" trial,
// so a rejected move costs nothing extra.
// ---------------------------------------------------------------------------
struct Snapshot {
    board_t b;
    game_t  g;
    index_t lf, lt;
};
static Snapshot undoRing[UNDO_SLOTS];
static uint8_t  undoCount = 0, undoHead = 0;

static void push_undo() {
    undoRing[undoHead].b  = board;
    undoRing[undoHead].g  = game;
    undoRing[undoHead].lf = last_from;
    undoRing[undoHead].lt = last_to;
    undoHead = (undoHead + 1) % UNDO_SLOTS;
    if (undoCount < UNDO_SLOTS) undoCount++;
}

static bool pop_undo() {
    if (!undoCount) return false;
    undoHead = (undoHead + UNDO_SLOTS - 1) % UNDO_SLOTS;
    board     = undoRing[undoHead].b;
    game      = undoRing[undoHead].g;
    last_from = undoRing[undoHead].lf;
    last_to   = undoRing[undoHead].lt;
    undoCount--;
    return true;
}

// ---------------------------------------------------------------------------
bool side_is_human(Color side) {
    switch (settings.mode) {
        case MODE_HOTSEAT: return true;
        case MODE_AI_AI:   return false;
        default:           return side == settings.humanSide;
    }
}

static void apply_options() {
    game.options.print_level        = None;      // the panel is the output
    // With micro-Max choosing, MicroChess searches only as a fallback: keep it quick.
    const uint8_t ply = skill_uses_umax() ? 2 : settings.ply;
    game.options.maxply             = ply;
    game.options.minply             = 1;
    game.options.max_max_ply        = ply + 2;
    game.options.max_quiescent_ply  = ply + 2;
    game.options.time_limit         = 2500;      // ms - also caps how
                                             // long touch is ignored
    game.options.live_update        = True;      // drives the thinking spinner
    game.options.openbook           = False;     // the SD book replaces it
    game.options.random             = True;
    game.options.shuffle_pieces     = True;
    game.options.integrate          = True;
    game.options.alpha_beta_pruning = True;
    game.options.continuous         = False;
    game.options.profiling          = False;
    game.options.mistakes           = 0;
    game.options.randskip           = 0;
    game.options.white_human        = side_is_human(White);
    game.options.black_human        = side_is_human(Black);
}

static void new_game() {
    board.init();
    game.init();
    apply_options();
    game.sort_pieces(game.turn);
    game.shuffle_pieces(SHUFFLE);

    sel_from  = -1;
    last_from = last_to = -1;
    undoCount = undoHead = 0;
    memset(legal_mask, 0, sizeof(legal_mask));

    sd_book_reset();
    sd_pgn_begin();
    umax_new_game();
}

// ---------------------------------------------------------------------------
// legal destinations for one square
//
// choose_best_moves() enumerates every move for every piece and hands each to
// a callback. Passing our own instead of consider_move() gives the move list
// without any of the search: nothing calls make_move(), so the board is not
// touched and it returns in milliseconds.
//
// These are the engine's own pseudo-legal moves, so a destination that would
// leave your king in check is still shown. That case is caught when the move
// is actually played - see apply_move().
// ---------------------------------------------------------------------------
static index_t collect_from;

static void collect_cb(piece_gen_t& gen) {
    if (gen.move.from == collect_from && gen.move.to >= 0 && gen.move.to < 64)
        setbit(legal_mask, gen.move.to);
}

static game_t mask_saved;      // static, not on the stack: game_t is big

static void build_legal_mask(index_t from) {
    memset(legal_mask, 0, sizeof(legal_mask));
    collect_from = from;

    // choose_best_moves() is not a read-only query. At ply 0 it declares
    // STALEMATE or CHECKMATE from the move counts it gathered, and it also
    // writes king locations, timeout flags and move counters. Letting any of
    // that survive a scan whose only job is to list one piece's destinations
    // ended the game the moment you picked a piece up - after which no move
    // could ever validate, because consider_move() returns immediately once
    // state != PLAYING.
    mask_saved = game;

    // take_turn() starts the move clock before calling this; without it
    // timeout() measures against a stale timestamp and aborts the walk early,
    // so most pieces came back with no legal moves at all.
    game.stats.start_move_stats();
    game.stats.move_stats.depth = 0;
    game.alpha = MIN_VALUE;
    game.beta  = MAX_VALUE;
    game.supply_valid  = False;
    game.user_supplied = False;
    game.book_supplied = False;

    move_t wmove = { -1, -1, MIN_VALUE };
    move_t bmove = { -1, -1, MAX_VALUE };
    choose_best_moves(wmove, bmove, collect_cb);

    game = mask_saved;
}

// ---------------------------------------------------------------------------
// playing a move
// ---------------------------------------------------------------------------
// Mirrors the commit half of the engine's take_turn().
static void commit(move_t& mv, move_t& wmove, move_t& bmove, bool whitesTurn) {
    game.last_move = mv;
    const index_t before = game.piece_count;

    piece_gen_t gen(mv, wmove, bmove, consider_move, False);
    gen.move = mv;
    gen.init(board, game);
    make_move(gen);
    check_kings();

    if ((PLAYING == game.state) && add_to_history(gen.move))
        game.state = whitesTurn ? WHITE_3_MOVE_REP : BLACK_3_MOVE_REP;

    // The engine scores "moved and left my own king attacked" as an immediate
    // loss. For the engine's own moves that stands; for a human's it is caught
    // by the caller and the move is taken back instead.
    if (whitesTurn && game.white_king_in_check)  game.state = BLACK_CHECKMATE;
    if (!whitesTurn && game.black_king_in_check) game.state = WHITE_CHECKMATE;

    game.turn = !game.turn;
    game.move_num++;

    if (before != game.piece_count) {
        for (index_t i = 0; i < game.piece_count; i++) {
            if (game.pieces[i].x == -1) {
                game.pieces[i] = game.pieces[--game.piece_count];
                break;
            }
        }
    }
}

// Does the side to move have any legal move? MicroChess only ever lists
// pseudo-legal moves and only calls mate for the engine's own side, so this
// tries each candidate for real and takes it straight back. Used to call
// mate or stalemate on a human, and to keep the engine's fallback legal.
// About 30 trial moves at a few ms each; the mask is cleared afterwards.
static bool find_legal_move(index_t& from, index_t& to) {
    const bool whitesTurn = (game.turn == White);
    move_t wmove = { -1, -1, MIN_VALUE };
    move_t bmove = { -1, -1, MAX_VALUE };
    // A trial is one make_move, no search behind it.
    const uint8_t mp = game.options.maxply, mq = game.options.max_quiescent_ply,
                  mm = game.options.max_max_ply;
    game.options.maxply = 0; game.options.max_quiescent_ply = 0; game.options.max_max_ply = 0;
    bool found = false;
    for (index_t sq = 0; sq < 64 && !found; sq++) {
        const Piece p = board.get(sq);
        if (isEmpty(p) || getSide(p) != game.turn) continue;
        build_legal_mask(sq);
        for (index_t t = 0; t < 64; t++) {
            if (!(getbit(legal_mask, t))) continue;
            push_undo();
            game.stats.start_move_stats();
            game.stats.move_stats.depth = 0;
            reset_turn_flags();
            game.alpha = wmove.value;
            game.beta  = bmove.value;
            game.supplied      = move_t(sq, t, 0L);
            game.user_supplied = True;
            game.supply_valid  = True;
            move_t mv = game.supplied;
            commit(mv, wmove, bmove, whitesTurn);
            const bool selfCheck = whitesTurn ? game.white_king_in_check
                                              : game.black_king_in_check;
            pop_undo();
            if (!selfCheck) { from = sq; to = t; found = true; break; }
        }
    }
    game.options.maxply = mp; game.options.max_quiescent_ply = mq; game.options.max_max_ply = mm;
    memset(legal_mask, 0, sizeof(legal_mask));
    return found;
}

// After a move has been played: if the side now to move has no legal move
// the game is over. This is the only way a human is ever mated here.
static void settle_side_to_move() {
    if (game.state != PLAYING) return;
    index_t f, t;
    if (!find_legal_move(f, t)) call_no_moves();
}

// No legal move: mate if in check, else stalemate. Sets game.state.
static void call_no_moves() {
    const bool whitesTurn = (game.turn == White);
    const bool inCheck = whitesTurn ? game.white_king_in_check : game.black_king_in_check;
    if (inCheck) game.state = whitesTurn ? BLACK_CHECKMATE : WHITE_CHECKMATE;
    else         game.state = STALEMATE;
}

static MoveResult apply_move(index_t from, index_t to) {
    const bool  whitesTurn = (game.turn == White);
    const Piece moved      = board.get(from);
    const bool  capture    = !isEmpty(board.get(to));

    // Validate against the engine's own move list for THIS piece - the very
    // list the destination dots were drawn from - with no search in the path.
    //
    // Validating through choose_best_moves(consider_move) meant a full
    // alpha-beta search of every piece that happened to sit before this one
    // in the shuffled pieces[] list, and consider_move() has three early
    // returns ahead of its supplied-move check (check_mem, state, and
    // supply_valid) that could fire during that unrelated search and reject
    // a perfectly legal move. The dots and the verdict now come from the
    // same scan, so they cannot disagree.
    build_legal_mask(from);
    // getbit() is a bare `a & b` with no outer parentheses, so it must be
    // wrapped before `!` - otherwise `!a & b` parses as `(!a) & b`.
    const bool legal = (getbit(legal_mask, to)) != 0;
    if (!legal) return MV_ILLEGAL;

    push_undo();

    move_t wmove = { -1, -1, MIN_VALUE };
    move_t bmove = { -1, -1, MAX_VALUE };

    game.stats.start_move_stats();
    game.stats.move_stats.depth = 0;
    reset_turn_flags();                     // clears the supplied-move flags
    game.alpha = wmove.value;
    game.beta  = bmove.value;

    game.supplied      = move_t(from, to, 0L);
    game.user_supplied = True;
    game.supply_valid  = True;              // validated above

    move_t mv = game.supplied;
    commit(mv, wmove, bmove, whitesTurn);
    game.stats.stop_move_stats();

    const bool selfCheck = whitesTurn ? game.white_king_in_check
                                      : game.black_king_in_check;
    if (selfCheck) { pop_undo(); return MV_SELF_CHECK; }

    last_from = mv.from;
    last_to   = mv.to;
    sd_pgn_move(mv.from, mv.to, moved, capture);
    return MV_OK;
}

// ---------------------------------------------------------------------------
// micro-Max as the move chooser (SKILL >= SKILL_FIRST_UMAX)
//
// The position is copied onto micro-Max's own board, it searches on a clock,
// and the move it names is played through MicroChess exactly like a human's:
// validated against MicroChess's own move list for that piece first. If the
// two ever disagree, MicroChess's search decides instead.
// ---------------------------------------------------------------------------
static bool umax_tick() {
    ui_think_tick();
#ifdef SIM
    return false;                 // the simulator queues taps ahead of time
#else
    int tx, ty;
    return touch_read(tx, ty);  // a finger on the panel ends the think early
#endif
}

static bool umax_pick(index_t& from, index_t& to) {
    umax_clear_board();
    for (index_t i = 0; i < 64; i++) {
        const Piece p = board.get(i);
        if (isEmpty(p)) continue;
        const uint8_t row = i >> 3, col = i & 7;
        const bool white = (getSide(p) == White);
        uint8_t code;
        switch (getType(p)) {
            case Pawn:
                code = white ? UMAX_WPAWN : UMAX_BPAWN;
                // Off its start rank it has moved; on its 6th/7th it is worth more.
                if (row != (white ? 6 : 1)) code |= UMAX_MOVED;
                if (row == (white ? 2 : 5)) code |= UMAX_RANK6;
                if (row == (white ? 1 : 6)) code |= UMAX_RANK7;
                break;
            case Knight: code = UMAX_KNIGHT | UMAX_MOVED; break;
            case Bishop: code = UMAX_BISHOP | UMAX_MOVED; break;
            case Queen:  code = UMAX_QUEEN  | UMAX_MOVED; break;
            case Rook:   code = UMAX_ROOK; if (hasMoved(p)) code |= UMAX_MOVED; break;  // castling rights
            default:     code = UMAX_KING; if (hasMoved(p)) code |= UMAX_MOVED; break;
        }
        code |= white ? UMAX_WHITE : UMAX_BLACK;
        umax_put(UMAX_SQ(row, col), code);
    }

    // En passant is on when the last move was a pawn's double step.
    uint8_t ep = UMAX_NO_EP;
    if (last_from >= 0 && last_to >= 0) {
        const Piece  lp = board.get(last_to);
        const int8_t dr = (int8_t) (last_to >> 3) - (int8_t) (last_from >> 3);
        if (!isEmpty(lp) && getType(lp) == Pawn && (dr == 2 || dr == -2))
            ep = UMAX_SQ((last_from >> 3) + dr / 2, last_to & 7);
    }
    umax_set_position(game.turn == White, ep);

#ifdef SIM
    const uint8_t maxDepth = 5;   // simulated time stands still during a search
#else
    const uint8_t maxDepth = 20;
#endif
    UmaxResult r;
    if (!umax_think(skill_budget_ms(), maxDepth, r)) return false;
    Serial.print(F("uMAX d")); Serial.print(r.depth);
    Serial.print(F(" score ")); Serial.print(r.score);
    Serial.print(' '); Serial.print(r.nodes); Serial.print(F(" nodes "));
    Serial.print(r.ms); Serial.print(F(" ms, stack low "));
    Serial.println(umax_stack_low());
    from = (index_t) ((r.from >> 4) * 8 + (r.from & 7));
    to   = (index_t) ((r.to   >> 4) * 8 + (r.to   & 7));
    return true;
}

static MoveResult ai_move() {
    const bool whitesTurn = (game.turn == White);

    push_undo();

    move_t wmove = { -1, -1, MIN_VALUE };
    move_t bmove = { -1, -1, MAX_VALUE };

    game.stats.start_move_stats();
    game.stats.move_stats.depth = 0;
    reset_turn_flags();
    game.alpha = wmove.value;
    game.beta  = bmove.value;

    // An opening-book move is offered to the generator the same way a human's
    // is. If it turns out not to be legal in this position, supply_valid stays
    // false and the full search result is used instead.
    index_t bf, bt;
    if (sd_book_move(bf, bt)) {
        game.supplied      = move_t(bf, bt, 0L);
        game.book_supplied = True;
        game.supply_valid  = False;
    } else if (skill_uses_umax()) {
        index_t uf, ut;
        const bool picked = umax_pick(uf, ut);
        if (!picked) Serial.println(F("uMAX: no move, MicroChess decides"));
        if (picked) {
            build_legal_mask(uf);
            const bool legal = (getbit(legal_mask, ut)) != 0;
            memset(legal_mask, 0, sizeof(legal_mask));
            if (!legal) {
                char a[3], c[3];
                square_name(uf, a); square_name(ut, c);
                Serial.print(F("uMAX: ")); Serial.print(a); Serial.print('-'); Serial.print(c);
                Serial.println(F(" not in MicroChess list, its search decides"));
            }
            if (legal) {
                const Piece moved   = board.get(uf);
                const bool  capture = !isEmpty(board.get(ut));
                game.supplied      = move_t(uf, ut, 0L);
                game.user_supplied = True;
                game.supply_valid  = True;
                move_t mv = game.supplied;
                commit(mv, wmove, bmove, whitesTurn);
                game.stats.stop_move_stats();
                const bool selfCheck = whitesTurn ? game.white_king_in_check
                                                  : game.black_king_in_check;
                if (!selfCheck) {
                    last_from = mv.from;
                    last_to   = mv.to;
                    sd_pgn_move(mv.from, mv.to, moved, capture);
                    return MV_OK;
                }
                // MicroChess says that leaves the king attacked. Take it back
                // and let its own search decide.
                Serial.println(F("uMAX: move leaves king attacked, MicroChess decides"));
                pop_undo();
                push_undo();
                game.stats.start_move_stats();
                game.stats.move_stats.depth = 0;
                reset_turn_flags();
                game.alpha = wmove.value;
                game.beta  = bmove.value;
            }
        }
    }

    if (game.options.shuffle_pieces) {
        game.sort_pieces(game.turn);
        game.shuffle_pieces(SHUFFLE);
    }

    choose_best_moves(wmove, bmove, consider_move);
    game.stats.stop_move_stats();

    move_t mv = game.supply_valid ? game.supplied : (whitesTurn ? wmove : bmove);
    if (mv.from >= 0 && mv.to >= 0) {
        const Piece moved   = board.get(mv.from);
        const bool  capture = !isEmpty(board.get(mv.to));
        commit(mv, wmove, bmove, whitesTurn);
        const bool selfCheck = whitesTurn ? game.white_king_in_check
                                          : game.black_king_in_check;
        if (!selfCheck) {
            last_from = mv.from;
            last_to   = mv.to;
            sd_pgn_move(mv.from, mv.to, moved, capture);
            return MV_OK;
        }
        pop_undo();                          // its move left its own king attacked
        push_undo();
    }

    // The search came up empty, or with a move that hangs its king. Any legal
    // move beats that, and none at all is mate or stalemate - called properly,
    // not by the engine losing its own king.
    Serial.println(F("engine: search gave no legal move, trying each"));
    index_t f, t;
    if (!find_legal_move(f, t)) { pop_undo(); call_no_moves(); return MV_NONE; }

    game.stats.start_move_stats();
    game.stats.move_stats.depth = 0;
    reset_turn_flags();
    game.alpha = wmove.value;
    game.beta  = bmove.value;
    game.supplied      = move_t(f, t, 0L);
    game.user_supplied = True;
    game.supply_valid  = True;
    move_t any = game.supplied;
    const Piece moved   = board.get(any.from);
    const bool  capture = !isEmpty(board.get(any.to));
    commit(any, wmove, bmove, whitesTurn);
    last_from = any.from;
    last_to   = any.to;
    sd_pgn_move(any.from, any.to, moved, capture);
    return MV_OK;
}

// ---------------------------------------------------------------------------
// redraw after a move
//
// Castling moves a rook and en passant clears a pawn somewhere neither square
// touches, so those need the whole board. Everything else only disturbs a
// handful of squares, which matters because a full repaint with SD artwork is
// about a second.
// ---------------------------------------------------------------------------
static void redraw_after_move(index_t oldFrom, index_t oldTo) {
    if (game.last_was_castle || game.last_was_en_passant ||
        game.last_was_pawn_promotion) {
        ui_draw_game();
        return;
    }
    for (index_t i = 0; i < 64; i++)
        if (getbit(legal_mask, i)) ui_draw_square(i);
    memset(legal_mask, 0, sizeof(legal_mask));

    ui_draw_square(oldFrom);
    ui_draw_square(oldTo);
    ui_draw_square(last_from);
    ui_draw_square(last_to);
    ui_draw_square(game.wking);
    ui_draw_square(game.bking);
    tft.drawRect(0, 0, BOARD_PX, BOARD_PX, cur.frame);
    ui_draw_panel();
}

static bool check_game_over() {
    if (game.state == PLAYING) return false;

    screen = SCR_OVER;

    // The engine scores a threefold repetition as a LOSS for the side that
    // caused it (not the draw standard chess gives). The verdict here follows
    // the engine, as the PGN log already does, so the two never disagree.
    const __FlashStringHelper* who;
    const __FlashStringHelper* why;
    uint16_t colour = C_SEL;
    switch (game.state) {
        case WHITE_CHECKMATE:  who = F("WHITE WINS"); why = F("CHECKMATE");   break;
        case BLACK_CHECKMATE:  who = F("BLACK WINS"); why = F("CHECKMATE");   break;
        case WHITE_3_MOVE_REP: who = F("BLACK WINS"); why = F("REPETITION");  break;
        case BLACK_3_MOVE_REP: who = F("WHITE WINS"); why = F("REPETITION");  break;
        case STALEMATE:        who = F("DRAW");       why = F("STALEMATE");   colour = cur.text; break;
        case MOVE_LIMIT:       who = F("DRAW");       why = F("MOVE LIMIT");  colour = cur.text; break;
        default:               who = F("GAME OVER");  why = F("");            colour = cur.text; break;
    }

    sd_pgn_finish(game.state);
    sd_clear_saved_game();

    ui_set_status(why, cur.text);   // status card carries the reason
    ui_draw_panel();
    ui_draw_over(who, colour);      // verdict replaces the title block
    return true;
}

// ---------------------------------------------------------------------------
// touch
// ---------------------------------------------------------------------------
// Every poll of the panel comes through here, so a lid press at the edge can
// never hold a wait or cut a search short either.
bool touch_read(int& x, int& y) {
    if (!tft.getTouch(x, y)) return false;
    return x >= TOUCH_EDGE && x < SCR_W - TOUCH_EDGE &&
           y >= TOUCH_EDGE && y < SCR_H - TOUCH_EDGE;
}

static bool get_tap(int16_t& x, int16_t& y) {
    static uint32_t last = 0;
    int tx, ty;
    if (!touch_read(tx, ty)) return false;
    const uint32_t now = millis();
    if (now - last < 240) return false;      // debounce, and one tap per press

    // Resistive panels jitter, and the very first reading after contact is
    // the worst one: pressure is still ramping and the point is still
    // settling. Keep sampling for a short window and use the average.
    long sx = tx, sy = ty;
    uint8_t n = 1;
    const uint32_t until = now + 45;
    while ((int32_t) (millis() - until) < 0) {
        if (touch_read(tx, ty)) { sx += tx; sy += ty; n++; }
        delay(2);
    }
    last = millis();
    x = tap_x = (int16_t) (sx / n);
    y = tap_y = (int16_t) (sy / n);
    return true;
}

// Block until the finger lifts, so the press that opened a dialog cannot be
// read again as its answer.
static void wait_release() {
    int x, y;
    uint8_t clear = 0;
    while (clear < 6) { clear = touch_read(x, y) ? 0 : clear + 1; delay(5); }
}

// Modal yes/no. Anything outside the two buttons, or 30 s of silence, is NO.
static bool confirm(const __FlashStringHelper* title, const __FlashStringHelper* line) {
    ui_confirm_draw(title, line);
    wait_release();
    const uint32_t until = millis() + 30000UL;
    while ((int32_t) (millis() - until) < 0) {
        int16_t x, y;
        if (!get_tap(x, y)) continue;
        const int8_t r = ui_confirm_hit(x, y);
        wait_release();
        return r == 1;
    }
    return false;
}

static void start_game(bool resumed) {
    if (!resumed) new_game(); else { apply_options(); umax_new_game(); }
    screen = SCR_GAME;
    tft.fillScreen(cur.panelBg);             // the menu goes before the board builds up
    ui_set_status(NULL, 0);
    ui_draw_game();
}

static void handle_menu(int8_t btn) {
    switch (btn) {
        case BTN_MODE_HUMAN_AI: settings.mode = MODE_HUMAN_AI; break;
        case BTN_MODE_HOTSEAT:  settings.mode = MODE_HOTSEAT;  break;
        case BTN_MODE_AI_AI:    settings.mode = MODE_AI_AI;    break;
        case BTN_PLY_DOWN:      if (settings.ply > 1) settings.ply--; break;
        case BTN_PLY_UP:        if (settings.ply < SKILL_MAX) settings.ply++; break;
        case BTN_SIDE:
            settings.humanSide = (settings.humanSide == White) ? Black : White;
            // Playing Black reads far better with the board turned round.
            settings.flipBoard = (settings.humanSide == Black) ? 1 : 0;
            break;
        case BTN_THEME:
            settings.theme = (settings.theme + 1) % THEME_COUNT;
            theme_apply(settings.theme);
            sd_pieces_load(cur.name);
            break;
        case BTN_FLIP:          settings.flipBoard = !settings.flipBoard; break;

        case BTN_CALIB:
            ui_calibrate();
            ui_draw_menu();
            return;

        case BTN_RESUME:
            if (!sd_has_saved_game()) {
                ui_menu_note(F("no saved game on the card"), C_BAD);
                return;
            }
            if (sd_load_game()) {
                theme_apply(settings.theme);
                sd_save_settings();
                start_game(true);
                return;
            }
            ui_menu_note(F("saved game unreadable: see Serial"), C_BAD);
            return;

        case BTN_START:
            sd_save_settings();
            start_game(false);
            return;

        default: return;
    }
    sd_save_settings();
    ui_menu_update(btn);                     // just the part that changed; THEME repaints all
}

static void handle_game_touch(int16_t x, int16_t y) {
    const int8_t btn = ui_hit_button(x, y);
    if (btn == BTN_MENU) {
        // Leaving a game in progress gets the same second look as wiping one.
        // With a card it is saved after every move and RESUME brings it back;
        // without one the menu's NEW GAME is the only way on.
        if (game.state == PLAYING && game.move_num > 0 &&
            !confirm(F("LEAVE GAME?"), sd_present() ? F("RESUME gets it back")
                                                    : F("current game is lost"))) {
            ui_draw_game();                      // paint over the box
            return;
        }
        screen = SCR_MENU;
        ui_draw_menu();
        return;
    }
    if (btn == BTN_NEW) {
        // A game with moves on the board is worth a second look before it goes.
        if (game.state == PLAYING && game.move_num > 0 &&
            !confirm(F("NEW GAME?"), F("current game is lost"))) {
            ui_draw_game();                      // paint over the box
            return;
        }
        new_game();
        ui_draw_game();
        return;
    }
    if (btn == BTN_UNDO) {
        // Step back past the engine's reply as well, so an undo hands the
        // board back to the player who asked for it.
        uint8_t steps = (settings.mode == MODE_HUMAN_AI) ? 2 : 1;
        bool did = false;
        while (steps-- && pop_undo()) { sd_hist_pop(); did = true; }
        if (did) {
            sel_from = -1;
            memset(legal_mask, 0, sizeof(legal_mask));
            ui_draw_game();
            sd_save_game();
        } else {
            ui_toast(F("NO UNDO"), cur.textDim, 700);
        }
        return;
    }

    const index_t sq = ui_hit_square(x, y);
    if (sq < 0) return;
    if (!side_is_human((Color) game.turn)) return;

    const Piece p = board.get(sq);
    const bool  ownPiece = !isEmpty(p) && (getSide(p) == game.turn);

    if (sel_from < 0) {
        if (!ownPiece) return;
        sel_from = sq;
        build_legal_mask(sq);
        ui_show_targets(sq, legal_mask);
        ui_set_status(NULL, 0);
        return;
    }

    if (sq == sel_from) {                    // tap again to put it back down
        const index_t was = sel_from;
        sel_from = -1;
        ui_clear_targets(was, legal_mask);
        memset(legal_mask, 0, sizeof(legal_mask));
        ui_set_status(NULL, 0);
        return;
    }

    if (ownPiece) {                          // switch to another of your pieces
        const index_t was = sel_from;
        sel_from = -1;
        ui_clear_targets(was, legal_mask);
        sel_from = sq;
        build_legal_mask(sq);
        ui_show_targets(sq, legal_mask);
        ui_set_status(NULL, 0);
        return;
    }

    const index_t from = sel_from;
    const index_t oldFrom = last_from, oldTo = last_to;
    sel_from = -1;

    const MoveResult r = apply_move(from, sq);
    if (r == MV_OK) {
        redraw_after_move(oldFrom, oldTo);
        sd_save_game();
        settle_side_to_move();               // mate or stalemate on the other side
        if (!check_game_over() && game.white_king_in_check)
            ui_set_status(F("CHECK"), C_CHECK);
        else if (game.state == PLAYING && game.black_king_in_check)
            ui_set_status(F("CHECK"), C_CHECK);
        return;
    }

    sel_from = from;                         // keep it in hand and explain
    ui_draw_square(sq);
    ui_show_targets(from, legal_mask);
    ui_toast(r == MV_SELF_CHECK ? F("KING IN CHECK") : F("NOT LEGAL"),
             C_BAD, 900);
}

// ---------------------------------------------------------------------------
void setup() {
    Serial.begin(115200);

    // A15 is not on the shield's footprint, so it floats - good enough to
    // seed the engine's randomness and the opening book's choice of line.
    randomSeed(analogRead(A15) ^ micros());
    umax_hook = umax_tick;

    theme_apply(settings.theme);
    ui_begin();                      // panel stays dark until the first screen is ready

    // The logo first, before anything else touches the card: it is written
    // into the panel's memory while the display is off and shown whole.
    sd_begin();
    const bool logo = sd_splash();
    const uint32_t shown = millis();
    if (logo) ui_display_on();

    sd_load_settings();
    ui_apply_calibration();          // settings may carry a saved calibration
    theme_apply(settings.theme);
    if (sd_present()) sd_pieces_load(cur.name);

    // The logo stays up 3 s from the moment it appeared, or until a tap.
    if (logo) {
        int tx, ty;
        while ((int32_t) (millis() - (shown + 3000)) < 0) {
            if (touch_read(tx, ty)) break;
        }
    }

    Serial.println(F("Megachess"));
    Serial.print(F("  SD      : "));
    Serial.println(sd_present() ? F("ready") : F("not present"));
    Serial.print(F("  pieces  : "));
    Serial.println(sd_pieces_ready() ? F("from card") : F("built-in"));
    Serial.print(F("  free RAM: "));
    Serial.println(freeMemory());

    // Is anything pressing the panel with nobody touching it? A lid that
    // overlaps the active area reads as a finger that never lifts. The
    // library counts z > 10 as a press.
    {
        int rx, ry, rz, zmax = 0, xat = 0, yat = 0;
        for (uint8_t i = 0; i < 10; i++) {
            tft.readTouchRaw(rx, ry, rz);
            if (rz > zmax) { zmax = rz; xat = rx; yat = ry; }
            delay(20);
        }
        Serial.print(F("  touch   : "));
        if (zmax > 10) {
            Serial.print(F("PRESSED, z ")); Serial.print(zmax);
            Serial.print(F(" at raw ")); Serial.print(xat); Serial.print(','); Serial.println(yat);
        } else {
            Serial.print(F("idle, z ")); Serial.println(zmax);
        }
    }

    board.init();
    game.init();
    apply_options();

    // After the logo the menu paints straight over it. Switching the display
    // off in between was tried: this glass shows WHITE with the driver off,
    // so it can blank into a picture but not bridge two of them.
    screen = SCR_MENU;
    ui_draw_menu();
    if (!logo) ui_display_on();      // no card: the menu is the first thing seen
}

void loop() {
    // Touch FIRST, always. The engine-turn branch below returns every
    // iteration while it is the engine's move, and in DEMO mode that is every
    // iteration forever - so checking touch after it meant no button worked
    // and the only way out of a demo was the reset button.
    {
        int16_t tx, ty;
        if (get_tap(tx, ty)) {
            switch (screen) {
                case SCR_MENU: handle_menu(ui_hit_button(tx, ty)); break;
                case SCR_GAME: handle_game_touch(tx, ty);          break;
                case SCR_OVER:
                    if (ui_hit_button(tx, ty) == BTN_NEW) {
                        new_game();
                        screen = SCR_GAME;
                        ui_set_status(NULL, 0);
                        ui_draw_game();
                    } else {
                        screen = SCR_MENU;
                        ui_draw_menu();
                    }
                    break;
            }
            return;
        }
    }

    // The engine's turn.
    if (screen == SCR_GAME && game.state == PLAYING &&
        !side_is_human((Color) game.turn)) {

        ai_thinking = true;
        ui_set_status(F("THINKING"), cur.accent);

        const index_t oldFrom = last_from, oldTo = last_to;
        const MoveResult r = ai_move();
        ai_thinking = false;

        if (r == MV_NONE) {
            if (game.state == PLAYING) call_no_moves();   // mate or stalemate, whichever it is
        } else {
            // Show the move: three flips where the piece was, then three where
            // it went. The screen still shows the old position here, and the
            // piece that moved is now on last_to.
            const Piece moved = board.get(last_to);
            ui_blink_square(last_from, moved);
            redraw_after_move(oldFrom, oldTo);
            ui_blink_square(last_to, moved);
            ui_draw_square(last_to);                 // its border back
            sd_save_game();
            settle_side_to_move();       // the other side may now have no move at all
        }

        if (!check_game_over()) {
            ui_set_status(NULL, 0);
            if (game.white_king_in_check || game.black_king_in_check)
                ui_set_status(F("CHECK"), C_CHECK);
        }

        // Give touch a real window between engine moves. The loop polls once
        // per iteration, and an iteration here is a whole search - so in DEMO,
        // where every move is the engine's, a tap almost never coincided and
        // the only way out was the reset button.
        {
            const uint32_t until = millis() + 700;
            while ((int32_t) (millis() - until) < 0) {
                int16_t tx, ty;
                if (get_tap(tx, ty)) {
                    handle_game_touch(tx, ty);
                    break;
                }
            }
        }
        return;
    }

}
