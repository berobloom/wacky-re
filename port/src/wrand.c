/* wrand.c: Watcom clib's rand/srand as WW.EXE has them (0x43088 rand, 0x430ac srand; the game's own
 * bytes, port/tools/checkrand.py runs both side by side).
 *
 * The game's choices (AI, pickups, the seat shuffle, demo seeds) follow this sequence, so the port must
 * draw the same numbers. The seed is where the game keeps it: the dword at 0x64008 in the data object
 * (initially 1; 0x43082 returns its address), so a dump of the port's data holds the game's seed.
 * checkrand.py builds this file with WRAND_SEED pointing at a variable of its own. */
#include <stdint.h>

#ifndef WRAND_SEED
#define WRAND_SEED ((volatile uint32_t *)0x64008u)
#endif

/* 0x43088: next = next * 0x41c64e6d + 0x3039; return next >> 16 & 0x7fff */
int rand(void)
{
    uint32_t n = *WRAND_SEED * 0x41c64e6du + 0x3039u;

    *WRAND_SEED = n;
    return (int)(n >> 16 & 0x7fff);
}

/* 0x430ac */
void srand(unsigned seed) { *WRAND_SEED = seed; }
