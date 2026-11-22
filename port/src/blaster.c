/* blaster.c: the Sound Blaster 16 under the sound library's mixer (src/ass/MULTIVOC.C, FX_MAN.C).
 *
 * The library runs as rebuilt; this replaces only its card driver (src/ass/BLASTER.C, which programs
 * the DSP, the DMA controller and the IRQ). What MULTIVOC needs from a card: it hands over a ring of
 * mix buffers (BufferSize bytes in NumDivisions parts) and a function, and the card plays the ring at
 * SampleRate, calling the function each time it finishes a part - on the PC from the DSP's interrupt,
 * here from the audio thread as it reads the ring (audio.c, sb_read), holding the interrupt flag
 * (port/include/interrupt.h) as an interrupt would.
 * The card is an SB16, as in the harness's DOSBox-X (sbtype sb16): 16-bit stereo, the rate as asked
 * (DSP 4.xx sets it in Hz), the CT1745 mixer with the voice volume in 2 dB steps (BLASTER.C writes
 * volume & 0xf8 to registers 0x32/0x33). */
#include <math.h>
#include <stdint.h>
#include <string.h>

#include "blaster.h"                    /* src/ass/BLASTER.H */
#include "machine.h"
#include "sb.h"

BLASTER_CONFIG BLASTER_Config = { 0x220, 6, 7, 1, 5, 0x330, 0 };   /* A220 I7 D1 H5 P330 T6 */

struct sb sb;                           /* read by audio.c */

char *BLASTER_ErrorString(int code)
{
    (void)code;
    return "Sound Blaster error.";
}

int BLASTER_GetEnv(BLASTER_CONFIG *c) { *c = BLASTER_Config; return BLASTER_Ok; }
int BLASTER_SetCardSettings(BLASTER_CONFIG c) { BLASTER_Config = c; return BLASTER_Ok; }
int BLASTER_GetCardSettings(BLASTER_CONFIG *c) { *c = BLASTER_Config; return BLASTER_Ok; }
int BLASTER_Init(void) { sb.voice_volume = 255; sb.gain = 1.0; return BLASTER_Ok; }
void BLASTER_Shutdown(void) { BLASTER_StopPlayback(); }

int BLASTER_GetCardInfo(int *bits, int *channels)
{
    *bits = 16;
    *channels = 2;
    return BLASTER_Ok;
}

int BLASTER_SetMixMode(int mode)
{
    sb.mode = mode & STEREO_16BIT;
    return sb.mode;
}

unsigned BLASTER_GetPlaybackRate(void) { return sb.rate; }

int BLASTER_BeginBufferedPlayback(char *start, int size, int divisions, unsigned rate, int mode,
                                  void (*callback)(void))
{
    sb.playing = 0;
    sb.buf = (uint8_t *)start;
    sb.size = (unsigned)size;
    sb.part = (unsigned)(size / divisions);
    sb.pos = 0;
    sb.rate = rate;
    sb.mode = mode & STEREO_16BIT;
    sb.frame = (sb.mode & SIXTEEN_BIT ? 2u : 1u) * (sb.mode & STEREO ? 2u : 1u);
    sb.callback = callback;
    sb.phase = 0;
    sb.prev[0] = sb.prev[1] = sb.cur[0] = sb.cur[1] = 0;
    sb.playing = 1;
    return BLASTER_Ok;
}

void BLASTER_StopPlayback(void) { sb.playing = 0; }

int BLASTER_CardHasMixer(void) { return 1; }
int BLASTER_GetVoiceVolume(void) { return sb.voice_volume; }

int BLASTER_SetVoiceVolume(int volume)
{
    int level;

    volume = volume < 0 ? 0 : volume > 255 ? 255 : volume;
    sb.voice_volume = volume & 0xf8;
    level = volume >> 3;                /* 0..31; 31 = 0 dB, each step 2 dB down, 0 = off */
    sb.gain = level ? pow(10.0, (level - 31) * 2.0 / 20.0) : 0.0;
    return BLASTER_Ok;
}
