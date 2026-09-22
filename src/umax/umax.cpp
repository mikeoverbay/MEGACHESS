/***************************************************************************/
/*                               micro-Max,                                */
/* A chess program smaller than 2KB (of non-blank source), by H.G. Muller  */
/***************************************************************************/
/* version 4.8 (1953 characters) features:                                 */
/* - recursive negamax search                                              */
/* - all-capture MVV/LVA quiescence search                                 */
/* - (internal) iterative deepening                                        */
/* - best-move-first 'sorting'                                             */
/* - a hash table storing score and best move                              */
/* - futility pruning                                                      */
/* - king safety through magnetic, frozen king in middle-game              */
/* - R=2 null-move pruning                                                 */
/* - keep hash and repetition-draw detection                               */
/* - better defense against passers through gradual promotion              */
/* - extend check evasions in inner nodes                                  */
/* - reduction of all non-Pawn, non-capture moves except hash move (LMR)   */
/* - full FIDE rules (expt under-promotion) and move-legality checking     */
/*                                                                         */
/* Source: http://home.hccnet.nl/h.g.muller/umax4_8.c                      */
/*                                                                         */
/* Megachess: fitted to an 8-bit AVR with 16-bit ints. D() is the upstream */
/* text line for line; every departure is marked MEGA. In short:           */
/*  - the hash table is U entries, its keys 32-bit, the key table in flash */
/*  - the root deepens against a millisecond budget, and a hard stop makes */
/*    every node fail high so the tree unwinds with the best move so far   */
/*  - the repetition lock of game positions moved out of D() into          */
/*    umax_lock_current(), which recycles a short ring of slots            */
/*  - console I/O replaced by the umax_* calls declared in umax.h          */
/***************************************************************************/
#include "umax.h"
#include "umax_keys.h"

#ifdef __AVR__
#define UMAX_KEY(i) ((long) pgm_read_dword(&UMAX_KEYS[i]))
#else
#define UMAX_KEY(i) (UMAX_KEYS[i])
#endif

#define W while
#define K(A,B) UMAX_KEY((A)+((B)&8)+S*((B)&7))         /* MEGA: T[] is in flash */
#define J(A) K(y+A,b[y])-K(x+A,u)-K(H+A,t)

#define U 128                                          /* MEGA: hash entries, a power of two; 9 bytes each */
static struct _ {long K;int V;char X,Y,D;} A[U];       /* hash table                */

enum {M=136,S=128,I=8000};                             /* MEGA: were int variables  */
static int Q,O,K,R,k=16;                               /* MEGA: no console buffers  */
static long J,Z;                                       /* MEGA: 32-bit keys         */
static unsigned long N;

static signed char L,                                  /* MEGA: signed on purpose   */
w[]={0,2,2,7,-1,8,12,23},                      /* relative piece values    */
o[]={-16,-15,-17,0,1,16,0,1,16,15,17,0,14,18,31,33,0, /* step-vector lists */
     7,-1,11,6,8,3,6,                          /* 1st dir. in o[] per piece*/
     6,3,5,7,4,5,3,6},                         /* initial piece setup      */
b[129];                                        /* board: half of 16x8+dummy*/

/* MEGA: search control ---------------------------------------------------- */
bool (*umax_hook)() = 0;
static uint32_t umaxStart;
static uint16_t umaxBudget;
static uint8_t  umaxMaxDepth, umaxAbort, umaxDone, umaxDepth;
static int      umaxScore;

#ifdef __AVR__
static uint16_t umaxSpMin = 0xFFFF;
#define UMAX_STACK_PROBE() do { uint16_t sp_ = SP; if (sp_ < umaxSpMin) umaxSpMin = sp_; } while (0)
#else
#define UMAX_STACK_PROBE() ((void) 0)
#endif

static int umax_more(int d) {                  /* deepen once more?        */
    if (umaxAbort || d > umaxMaxDepth + 1 || d >= 98) return 0;
    return (millis() - umaxStart) * 3 < umaxBudget;   /* the next pass costs several times the last */
}

static void umax_poll() {                      /* every 64 nodes           */
    if (millis() - umaxStart > (uint32_t) umaxBudget + umaxBudget / 2) umaxAbort = 1;
    else if (umax_hook && umax_hook()) umaxAbort = 1;
}

static int D(int q,int l,int e,int E,int z,int n);

static int D(int q,int l,int e,int E,int z,int n)  /* recursive minimax search, k=moving side, n=depth*/
{                       /* (q,l)=window, e=current eval. score, E=e.p. sqr.*/
                        /* e=score, z=prev.dest; J,Z=hashkeys; return score*/
 int j,r,m,v,d,h,i,F,G,V,P,C,s;
 long f=J,g=Z;                                 /* MEGA: keys are long      */
 signed char t,p,u,x,y,X,Y,H,B;
 struct _*a=A+((J+(long)k*E)&(U-1));           /* lookup pos. in hash table*/
 if(umaxAbort)return l;                        /* MEGA: hard stop, unwind  */
 UMAX_STACK_PROBE();                           /* MEGA                     */

 q--;                                          /* adj. window: delay bonus */
 k^=24;                                        /* change sides             */
 d=a->D;m=a->V;X=a->X;Y=a->Y;                  /* resume at stored depth   */
 if(a->K-Z|z|                                  /* miss: other pos. or empty*/
  !(m<=q|X&8&&m>=l|X&S))                       /*   or window incompatible */
  d=Y=0;                                       /* start iter. from scratch */
 X&=~M;                                        /* start at best-move hint  */

 W(d++<n||d<3||                                /* iterative deepening loop */
   z&K==I&&(umax_more(d)||                     /* MEGA: root: time budget  */
   (K=X,L=Y&~M,d=3)))                          /* time's up: go do best    */
 {x=B=X;                                       /* start scan at prev. best */
  h=Y&S;                                       /* request try noncastl. 1st*/
  P=d<3?I:D(-l,1-l,-e,S,0,d-3);                /* Search null move         */
  m=-P<l|R>35?d>2?-I:e:-P;                     /* Prune or stand-pat       */
  if(!(++N&63))umax_poll();                    /* MEGA: node count -> hook */
  do{u=b[x];                                   /* scan board looking for   */
   if(u&k)                                     /*  own piece (inefficient!)*/
   {r=p=u&7;                                   /* p = piece type (set r>0) */
    j=o[p+16];                                 /* first step vector f.piece*/
    W(r=p>2&r<0?-r:-o[++j])                    /* loop over directions o[] */
    {A:                                        /* resume normal after best */
     y=x;F=G=S;                                /* (x,y)=move, (F,G)=castl.R*/
     do{                                       /* y traverses ray, or:     */
      H=y=h?Y^h:y+r;                           /* sneak in prev. best move */
      if(y&M)break;                            /* board edge hit           */
      m=E-S&b[E]&&y-E<2&E-y<2?I:m;             /* bad castling             */
      if(p<3&y==E)H^=16;                       /* shift capt.sqr. H if e.p.*/
      t=b[H];if(t&k|p<3&!(y-x&7)-!t)break;     /* capt. own, bad pawn mode */
      i=37*w[t&7]+(t&192);                     /* value of capt. piece t   */
      m=i<0?I:m;                               /* K capture                */
      if(m>=l&d>1)goto C;                      /* abort on fail high       */

      v=d-1?e:i-p;                             /* MVV/LVA scoring          */
      if(d-!t>1)                               /* remaining depth          */
      {v=p<6?b[x+8]-b[y+8]:0;                  /* center positional pts.   */
       b[G]=b[H]=b[x]=0;b[y]=u|32;             /* do move, set non-virgin  */
       if(!(G&M))b[F]=k+6,v+=50;               /* castling: put R & score  */
       v-=p-4|R>29?0:20;                       /* penalize mid-game K move */
       if(p<3)                                 /* pawns:                   */
       {v-=9*((x-2&M||b[x-2]-u)+               /* structure, undefended    */
              (x+2&M||b[x+2]-u)-1              /*        squares plus bias */
             +(b[x^16]==k+36))                 /* kling to non-virgin King */
             -(R>>2);                          /* end-game Pawn-push bonus */
        V=y+r+1&S?647-p:2*(u&y+16&32);         /* promotion or 6/7th bonus */
        b[y]+=V;i+=V;                          /* change piece, add score  */
       }
       v+=e+i;V=m>q?m:q;                       /* new eval and alpha       */
       J+=J(0);Z+=J(8)+G-S;                    /* update hash key          */
       C=d-1-(d>5&p>2&!t&!h);
       C=R>29|d<3|P-I?C:d;                     /* extend 1 ply if in check */
       do
        s=C>2|v>V?-D(-l,-V,-v,                 /* recursive eval. of reply */
                              F,0,C):v;        /* or fail low if futile    */
       W(s>q&++C<d);v=s;
       if(z&&K-I&&v+I&&x==K&y==L)              /* move pending & in root:  */
       {Q=-e-i;O=F;                            /*   exit if legal & found  */
        umaxDone=1;                            /* MEGA: lock done outside  */
        R+=i>>7;return l;                      /* captured non-P material  */
       }
       J=f;Z=g;                                /* restore hash key         */
       b[G]=k+6;b[F]=b[y]=0;b[x]=u;b[H]=t;     /* undo move,G can be dummy */
      }
      if(v>m)                                  /* new best, update max,best*/
       m=v,X=x,Y=y|S&F;                        /* mark double move with S  */
      if(h){h=0;goto A;}                       /* redo after doing old best*/
      if(x+r-y|u&32|                           /* not 1st step,moved before*/
         p>2&(p-4|j-7||                        /* no P & no lateral K move,*/
         b[G=x+3^r>>1&7]-k-6                   /* no virgin R in corner G, */
         ||b[G^1]|b[G^2])                      /* no 2 empty sq. next to R */
        )t+=p<5;                               /* fake capt. for nonsliding*/
      else F=y;                                /* enable e.p.              */
     }W(!t);                                   /* if not capt. continue ray*/
  }}}W((x=x+9&~M)-B);                          /* next sqr. of board, wrap */
C:if(m>I-M|m<M-I)d=98;                         /* mate holds to any depth  */
  m=m+I|P==I?m:0;                              /* best loses K: (stale)mate*/
  if(a->D<99&&!umaxAbort)                      /* protect game history     */
   a->K=Z,a->V=m,a->D=d,                       /* always store in hash tab */
   a->X=X|8*(m>q)|S*(m<l),a->Y=Y;              /* move, type (bound/exact),*/
  if(z&&!umaxAbort){umaxDepth=d-1;umaxScore=m;}/* MEGA: was the Kibitz line*/
 }                                             /*    encoded in X S,8 bits */
 k^=24;                                        /* change sides back        */
 return m+=m<e;                                /* delayed-loss bonus       */
}

/* MEGA: game positions as draw locks ---------------------------------------
 * Upstream marks the position it is leaving as a draw (D=99, V=0) so that a
 * repetition in the tree scores 0, and never overwrites such a slot. With a
 * 16-million-entry table that costs nothing; with 256 it would fill the
 * table by move 60. This keeps the last UMAX_LOCKS positions and unlocks the
 * oldest. A position met a second time becomes a win for whoever is to move,
 * i.e. a loss for whoever would enter it a third time: that is the rule the
 * MicroChess layer enforces. */
#define UMAX_LOCKS 24
static struct { long key; uint16_t idx; } locks[UMAX_LOCKS];
static uint8_t lockHead, lockCount;

static void umax_lock_current() {
    const uint16_t idx = (uint16_t) ((J + (long) k * O) & (U - 1));
    struct _* a = A + idx;
    if (a->D == 99 && a->K == Z) { a->V = I - 1; return; }
    if (lockCount == UMAX_LOCKS) {
        struct _* old = A + locks[lockHead].idx;
        if (old->D == 99 && old->K == locks[lockHead].key) old->D = 0;
    } else {
        lockCount++;
    }
    locks[lockHead].idx = idx;
    locks[lockHead].key = Z;
    lockHead = (uint8_t) ((lockHead + 1) % UMAX_LOCKS);
    a->K = Z; a->V = 0; a->D = 99; a->X = (char) (8 | S); a->Y = 0;
}

/* MEGA: the API ------------------------------------------------------------ */
void umax_clear_board() {
    for (uint8_t x = 0; x < 128; x++) {
        if (x & 0x88) {                        /* centre-points table, in the unused half */
            const int f = (x & 7) - 4, r = 2 * (x >> 4) - 7;
            b[x] = (signed char) ((4 * f * f + r * r) / 4);
        } else {
            b[x] = 0;
        }
    }
    b[128] = 0;
}

void umax_put(uint8_t sq, uint8_t code) { b[sq] = (signed char) code; }

uint8_t umax_piece_at(uint8_t sq) { return (uint8_t) b[sq]; }

void umax_set_position(bool whiteToMove, uint8_t ep) {
    k = whiteToMove ? 16 : 8;                  /* D() flips before it scans */
    O = ep;
    J = Z = 0;
    int material = 0, present = 0;
    for (uint8_t x = 0; x < 128; x++) {
        if (x & 0x88) continue;
        const signed char u = b[x];
        if (!u) continue;
        J += K(x, u); Z += K(x + 8, u);
        const uint8_t p = u & 7;
        if (p == 4) continue;                  /* kings cancel out         */
        const int val = 37 * w[p] + (u & 192);
        present += val >> 7;
        material += (u & 8) ? val : -val;
    }
    Q = whiteToMove ? material : -material;
    R = 40 - present;                          /* non-pawn material gone   */
    if (R < 0) R = 0;
    umaxDone = 0;
}

void umax_new_game() {
    memset(A, 0, sizeof A);
    lockHead = lockCount = 0;
    umax_clear_board();
    for (uint8_t f = 0; f < 8; f++) {
        b[f]       = (signed char) (o[f + 24] | UMAX_BLACK);
        b[f + 112] = (signed char) (o[f + 24] | UMAX_WHITE);
        b[f + 16]  = UMAX_BPAWN | UMAX_BLACK;
        b[f + 96]  = UMAX_WPAWN | UMAX_WHITE;
    }
    umax_set_position(true, UMAX_NO_EP);
}

bool umax_think(uint16_t budgetMs, uint8_t maxDepth, UmaxResult& r) {
    umax_lock_current();                       /* the position we leave    */
    umaxStart = millis(); umaxBudget = budgetMs; umaxMaxDepth = maxDepth;
    umaxAbort = umaxDone = umaxDepth = 0; umaxScore = 0; N = 0;
    K = I;
    D(-I, I, Q, O, 1, 3);
    r.depth = umaxDepth; r.score = umaxScore; r.nodes = N;
    r.ms = (uint16_t) (millis() - umaxStart);
    r.from = r.to = 0;
    if (!umaxDone) {
        if (umaxDepth == 0 || K == I) return false;  /* nothing finished, or no move at all */
        umaxAbort = 0;                         /* stopped before the final pass: play K,L now */
        D(-I, I, Q, O, 1, 3);
        if (!umaxDone) return false;
    }
    r.from = (uint8_t) K; r.to = (uint8_t) L;
    umax_lock_current();                       /* the position we hand over*/
    return true;
}

int umax_stack_low() {
#ifdef __AVR__
    extern char* __brkval;
    extern char  __heap_start;
    const uint16_t heapTop = __brkval ? (uint16_t) __brkval : (uint16_t) &__heap_start;
    return (int) (umaxSpMin - heapTop);
#else
    return 0;
#endif
}
