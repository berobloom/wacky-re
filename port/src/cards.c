/* cards.c: what the sound library's FX_MAN and MULTIVOC link against besides the Sound Blaster -
 * the other cards' drivers, which the port does not have (it is an SB16, blaster.c), and three
 * Watcom built-ins.
 * The other cards are never reached: FX_SetupCard and MV_Init switch on the configured card
 * (WACKY.CFG; card 0 is the Sound Blaster). If a player's config names one, its init fails with the
 * library's own card error, as on a PC without that card. */
#include <stdint.h>

static char none[] = "This sound card is not emulated by the port (use the Sound Blaster).";

#define FAIL(name) int name(void) { return -1; }
#define NOP(name) void name(void) {}
#define ERRSTR(name) char *name(int code) { (void)code; return none; }

/* Pro AudioSpectrum (PAS16.C) */
FAIL(PAS_Init) NOP(PAS_Shutdown) ERRSTR(PAS_ErrorString) FAIL(PAS_GetCardInfo)
FAIL(PAS_SetMixMode) FAIL(PAS_BeginBufferedPlayback) NOP(PAS_StopPlayback)
FAIL(PAS_GetPlaybackRate) FAIL(PAS_GetPCMVolume) FAIL(PAS_SetPCMVolume)
/* Sound Source (SNDSRC.C) */
FAIL(SS_Init) NOP(SS_Shutdown) ERRSTR(SS_ErrorString) FAIL(SS_SetMixMode)
FAIL(SS_BeginBufferedPlayback) NOP(SS_StopPlayback) NOP(SS_SetPort)
/* Gravis UltraSound (GUSWAVE.C) */
FAIL(GUSWAVE_Init) NOP(GUSWAVE_Shutdown) ERRSTR(GUSWAVE_ErrorString) FAIL(GUSWAVE_PlayVOC)
FAIL(GUSWAVE_VoicePlaying) FAIL(GUSWAVE_Kill) FAIL(GUSWAVE_KillAllVoices) FAIL(GUSWAVE_SetPitch)
FAIL(GUSWAVE_SetPan3D) NOP(GUSWAVE_SetVolume) FAIL(GUSWAVE_GetVolume) NOP(GUSWAVE_SetCallBack)
/* PC speaker (PCFX.C) */
FAIL(PCFX_Init) FAIL(PCFX_Shutdown) ERRSTR(PCFX_ErrorString) FAIL(PCFX_Play) FAIL(PCFX_Stop)
FAIL(PCFX_SoundPlaying) NOP(PCFX_SetCallBack) FAIL(PCFX_SetTotalVolume) FAIL(PCFX_GetTotalVolume)

/* Watcom's <stdlib.h> min/max (macros there, functions in the library's calls here) */
int min(int a, int b) { return a < b ? a : b; }
int max(int a, int b) { return a > b ? a : b; }

/* _MULTIVC.H ClearBuffer_DW (#pragma aux: rep stosd): `length` dwords of `data` */
void ClearBuffer_DW(void *ptr, unsigned data, int length)
{
    uint32_t *p = ptr;

    while (length-- > 0)
        *p++ = data;
}
