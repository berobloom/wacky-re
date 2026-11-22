/* sb.h: the port's Sound Blaster state, shared by blaster.c (the driver MULTIVOC calls) and
 * audio.c (which plays the DMA ring and raises the card's interrupt). */
#ifndef PORT_SB_H
#define PORT_SB_H
#include <stdint.h>

struct sb {
    volatile int playing;
    uint8_t *buf;                       /* MULTIVOC's ring of mix buffers */
    unsigned size, part, pos;           /* ring bytes, bytes a part, read position */
    unsigned rate, mode, frame;         /* Hz, MULTIVOC mix mode, bytes a sample frame */
    void (*callback)(void);             /* MV_ServiceVoc: called after each part, as the IRQ did */
    double phase;                       /* resampling: position between prev and cur, 0..1 */
    int prev[2], cur[2];
    int voice_volume;                   /* the mixer's voice volume as BLASTER.C writes it */
    double gain;
};

extern struct sb sb;
#endif
