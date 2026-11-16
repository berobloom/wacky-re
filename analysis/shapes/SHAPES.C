/* SHAPES.C: the C shapes tried for fn_1be3c (0x1be3c) and fn_1bf00 (0x1bf00), kept so a later
 * session re-scores them instead of re-inventing them. Build and score:

   mkdir -p .work/shapes && cp analysis/shapes/SHAPES.C .work/shapes/
   tools/wc/wcc.sh .work/toolchains/inst/WC95B .work/shapes SHAPES.C -s -3r -oneatx
   .work/venv/bin/python tools/wc/variants.py .work/shapes/SHAPES.OBJ ww 0x1be3c 0x1bf00 k1 k2 k3 k4
   .work/venv/bin/python tools/wc/variants.py .work/shapes/SHAPES.OBJ ww 0x1bf00 0x1bf7e m1 m2 m3 m4

 *              .work/venv/bin/python tools/wc/variants.py .work/shapes/SHAPES.OBJ ww 0x1c994 0x1cacd t1 t2 t3 t4 t5 t6
 *
 * Best here: k1 196 bytes (the game 196) with 13 instruction diffs, m1 100 bytes (the game 125)
 * with 14, t1 312 (the game 313) with 4. Nothing tried reached 0; the differs: marks in src/ww.c say
 * what is left. <string.h> is required for the t shapes: without it strcat/strlen are real clib
 * calls, not the game's inline expansions, and every t score jumps to ~90 diffs. */
#include <stdlib.h>
#include <string.h>
#pragma pack(1)
struct player {
    short f00, f02, f04, f06, f08, f0a, f0c, f0e;
    short f10, f12, f14;
    short x, y;
    short f1a;
    short f1c, f1e;
    unsigned short place;
    short lap, cell, face;
    short f28[20];
    int pos;
    short f54, f56, f58, f5a, f5c;
    int f5e;
    short f62, f64, f66;
};
#pragma pack()
extern struct player players[];
extern short nplayers, v7ec6c, v7ec70;
extern int v7eca4;
extern short v70720[];
extern int racetime;
extern short myplayer, twoplayer, splitscreen, v88c6a;
void srand(unsigned);
int rand(void);


/* best: 196 bytes (exact size), 13 diffs */
void k1(void)
{
    short i, seat, cd, cand;
    int m;

    m = racetime;
    if (m < 8)
        m = 8;
    srand(m);
    for (i = 0; i <= 7; i++)
        v70720[i] = -1;
    v70720[0] = myplayer;
    seat = 1;
    if (twoplayer != 0 || splitscreen != 0) {
        v70720[1] = v88c6a;
        seat = 2;
    }
    cd = seat;
    cand = -1;
    while (cd <= 7) {
        if (splitscreen == 0) {
            do {
                cand = rand();
            } while (cand < 0 || cand > 7);
        }
        cand++;
        if (cand > 7)
            cand = 0;
        for (i = 0; i <= 7; i++)
            if (cand == v70720[i])
                goto again;
        v70720[cd++] = cand;
    again:
        ;
    }
}

/* break instead of goto: 204 / 15 */
void k2(void)
{
    short i, seat, cd, cand;
    int m;

    m = racetime;
    if (m < 8)
        m = 8;
    srand(m);
    for (i = 0; i <= 7; i++)
        v70720[i] = -1;
    v70720[0] = myplayer;
    seat = 1;
    if (twoplayer != 0 || splitscreen != 0) {
        v70720[1] = v88c6a;
        seat = 2;
    }
    cand = -1;
    cd = seat;
    while (cd <= 7) {
        if (splitscreen == 0) {
            do {
                cand = rand();
            } while (cand < 0 || cand > 7);
        }
        cand++;
        if (cand > 7)
            cand = 0;
        for (i = 0; i <= 7; i++)
            if (cand == v70720[i])
                break;
        if (i > 7)
            v70720[cd++] = cand;
    }
}

/* entry test written out + do/while: 196 / 17 */
void k3(void)
{
    short i, cd, cand;
    int m;

    m = racetime;
    if (m < 8)
        m = 8;
    srand(m);
    for (i = 0; i <= 7; i++)
        v70720[i] = -1;
    v70720[0] = myplayer;
    cd = 1;
    if (twoplayer != 0 || splitscreen != 0) {
        v70720[1] = v88c6a;
        cd = 2;
    }
    cand = -1;
    if (cd <= 7)
        do {
            if (splitscreen == 0) {
                do {
                    cand = rand();
                } while (cand < 0 || cand > 7);
            }
            cand++;
            if (cand > 7)
                cand = 0;
            for (i = 0; i <= 7; i++)
                if (cand == v70720[i])
                    goto again;
            v70720[cd++] = cand;
        again:
            ;
        } while (cd <= 7);
}

/* scan variable separate from the fill counter: 196 / 13 */
void k4(void)
{
    short i, k, n, cand;
    int m;

    m = racetime;
    if (m < 8)
        m = 8;
    srand(m);
    for (i = 0; i <= 7; i++)
        v70720[i] = -1;
    v70720[0] = myplayer;
    n = 1;
    if (twoplayer != 0 || splitscreen != 0) {
        v70720[1] = v88c6a;
        n = 2;
    }
    cand = -1;
    i = n;
    while (i <= 7) {
        if (splitscreen == 0) {
            do {
                cand = rand();
            } while (cand < 0 || cand > 7);
        }
        cand++;
        if (cand > 7)
            cand = 0;
        for (k = 0; k <= 7; k++)
            if (cand == v70720[k])
                goto again;
        v70720[i++] = cand;
    again:
        ;
    }
}

/* best: 100 / 14 */
void m1(void)
{
    short n;
    short cnt;
    short i, val;

    n = nplayers;
    cnt = v7eca4 + 1;
    if (cnt > 0x15)
        cnt = 0x15;
    i = 1;
    do {
        if (cnt > 0) {
            val = v7ec6c;
            cnt--;
        } else {
            val = v7ec70;
        }
        players[i].f64 = 1;
        players[i].face = val;
        i++;
    } while (i <= n);
    nplayers = n;
}

/* stores in both arms, i++ outside: 133 / 17 */
void m2(void)
{
    short n = nplayers;
    short cnt = v7eca4 + 1;
    short i;

    if (cnt > 0x15)
        cnt = 0x15;
    i = 1;
    while (i <= n) {
        if (cnt > 0) {
            cnt--;
            players[i].f64 = 1;
            players[i].face = v7ec6c;
        } else {
            players[i].f64 = 1;
            players[i].face = v7ec70;
        }
        i++;
    }
    nplayers = n;
}

/* continue in one arm: 104 / 16 */
void m3(void)
{
    short n = nplayers;
    short cnt = v7eca4 + 1;
    short i;

    if (cnt > 0x15)
        cnt = 0x15;
    i = 1;
    while (i <= n) {
        players[i].f64 = 1;
        if (cnt-- > 0) {
            players[i].face = v7ec6c;
            i++;
            continue;
        }
        players[i].face = v7ec70;
        i++;
    }
    nplayers = n;
}

/* for(;;) + break: 100 / 18 */
void m4(void)
{
    short n = nplayers;
    short cnt;
    short i, val;

    cnt = v7eca4 + 1;
    if (cnt > 0x15)
        cnt = 0x15;
    for (i = 1; ; i++) {
        if (cnt > 0) {
            val = v7ec6c;
            cnt--;
        } else {
            val = v7ec70;
        }
        players[i].f64 = 1;
        players[i].face = val;
        if (i >= n)
            break;
    }
    nplayers = n;
}

/* ---- fn_1c994 (0x1c994-0x1cacc, the record stamp): shapes tried 2025 iter 9.
 * Score with:  cp analysis/shapes/SHAPES.C .work/shapes/
 *              tools/wc/wcc.sh .work/toolchains/inst/WC95B .work/shapes SHAPES.C -s -3r -oneatx
 *              .work/venv/bin/python tools/wc/variants.py .work/shapes/SHAPES.OBJ ww 0x1c994 0x1cacd t1 t2 t3 t4 t5 t6
 * Best t1 312 bytes (the game 313) with 4 instruction diffs - one register-setup placement, see the
 * differs: mark in src/ww.c. t2/t3/t5/t6 all fall to 7-8 diffs, so the game's loop is entered twice
 * (a do/while or while(1)), not the pre-test `while (strlen(s) < 10) strcat(s, " ")`. */

struct dostime {                        /* filled by 0x43164 (INT 21h/2Ah), stores 0x4316d-0x43179 */
    unsigned char hund;
    unsigned char sec;
    unsigned short minhrs;              /* read as one word at 0x1ca75 */
    unsigned char dst;
};

extern void dos_gettime(struct dostime *t);

#define STAMP_TAIL(name, tail) \
void name(char *s) { \
    char mth[12][4] = { "JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC" }; \
    char num[16]; \
    struct dostime t; \
    char *src; \
    char c; \
    dos_gettime(&t); \
    itoa(t.hund, s, 10); \
    if (s[1] == 0) { s[2] = 0; c = s[0]; s[0] = '0'; s[1] = c; } \
    strcat(s, " "); \
    strcat(s, mth[t.sec - 1]); \
    strcat(s, " "); \
    itoa(t.minhrs, num, 10); \
    src = num; \
    tail \
}

/* the shape now in src/ww.c: 312 / 4 */
STAMP_TAIL(t1, while (1) { strcat(s, src); if (strlen(s) >= 10) return; src = " "; })

/* do/while, assignment before the test: 312 / 7 */
STAMP_TAIL(t2, do { strcat(s, src); src = " "; } while (strlen(s) < 10); )

/* for(;;) + break: 312 / 7 */
STAMP_TAIL(t3, for (;;) { strcat(s, src); src = " "; if (strlen(s) >= 10) break; })

/* the shape a reader would write first, pre-test: 308 / 8 */
void t4(char *s)
{
    char mth[12][4] = { "JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC" };
    char num[16];
    struct dostime t;
    char c;

    dos_gettime(&t);
    itoa(t.hund, s, 10);
    if (s[1] == 0) { s[2] = 0; c = s[0]; s[0] = '0'; s[1] = c; }
    strcat(s, " ");
    strcat(s, mth[t.sec - 1]);
    strcat(s, " ");
    itoa(t.minhrs, num, 10);
    strcat(s, num);
    while (strlen(s) < 10)
        strcat(s, " ");
}

/* month through a variable (extra live value, changes the allocation): 308 / 11 */
void t5(char *s)
{
    char mth[12][4] = { "JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC" };
    char num[16];
    struct dostime t;
    char *mo, *src;
    char c;

    dos_gettime(&t);
    itoa(t.hund, s, 10);
    if (s[1] == 0) { s[2] = 0; c = s[0]; s[0] = '0'; s[1] = c; }
    strcat(s, " ");
    mo = mth[t.sec - 1];
    strcat(s, mo);
    strcat(s, " ");
    itoa(t.minhrs, num, 10);
    src = num;
    while (1) { strcat(s, src); if (strlen(s) >= 10) return; src = " "; }
}

/* itoa's return value as the source pointer: 326 / 12 (the game does not do this, it loads the
 * buffer address with lea before the call, 0x1ca7a) */
STAMP_TAIL(t6, src = itoa(t.minhrs, num, 10); do { strcat(s, src); src = " "; } while (strlen(s) < 10); )
