/*
 * umax.h - micro-Max 4.8 (H.G. Muller) as a move chooser for Megachess.
 *
 * The engine keeps its own 0x88 board. The sketch loads a position into it,
 * asks for a move, and plays that move through MicroChess, which stays the
 * authority on the rules. Nothing here knows about MicroChess or the panel.
 */
#ifndef UMAX_H
#define UMAX_H
#include <Arduino.h>

// Piece codes: type | colour | flags.
#define UMAX_WPAWN   1
#define UMAX_BPAWN   2
#define UMAX_KNIGHT  3
#define UMAX_KING    4
#define UMAX_BISHOP  5
#define UMAX_ROOK    6
#define UMAX_QUEEN   7
#define UMAX_WHITE   8
#define UMAX_BLACK  16
#define UMAX_MOVED  32     // clear on a king or rook: may still castle; clear on a pawn: may double-step
#define UMAX_RANK6  64     // pawn on its 6th rank, worth more
#define UMAX_RANK7 128     // pawn on its 7th rank, worth more still
#define UMAX_NO_EP 128     // no en-passant square

// Squares are 0x88: 16 * row + file, row 0 = rank 8 (Black's back rank).
#define UMAX_SQ(row, file) ((uint8_t) (((row) << 4) | (file)))

struct UmaxResult {
    uint8_t  from, to;     // 0x88 squares
    uint8_t  depth;        // plies completed
    int      score;        // for the side that moved; a pawn is 74
    uint32_t nodes;
    uint16_t ms;
};

void    umax_new_game();                                  // start position, empty hash
void    umax_clear_board();                               // no pieces; follow with umax_put()
void    umax_put(uint8_t sq, uint8_t code);
void    umax_set_position(bool whiteToMove, uint8_t ep);  // once the pieces are placed
uint8_t umax_piece_at(uint8_t sq);

// Search the current position. Deepens while elapsed < budgetMs / 3 and
// depth <= maxDepth; a hard stop at 1.5 x budgetMs unwinds the search and
// keeps the best move so far. On true the move has been played on the
// engine's own board.
bool umax_think(uint16_t budgetMs, uint8_t maxDepth, UmaxResult& r);

// Called every 64 nodes. Return true to stop the search early.
extern bool (*umax_hook)();

int umax_stack_low();     // AVR: least bytes between stack and heap seen while searching

#endif
