/* audio.c: the OPL2 at 0x388/0x389 that the game's FM song player writes (src/ass/FMCHIP.C,
 * ADLIB_PORT), emulated by Nuked OPL3 (port/third_party/nukedopl), and the sound output.
 *
 * FMCHIP writes the register index to 0x388 and the data to 0x389 (FM_SendOutput); it reads 0x388
 * only to wait, never for the status or the timers, so neither is emulated: 0x388 reads 0.
 * The writes come from the game thread (MUSIC_* calls) and the hardware thread (the song task);
 * the samples are made on SDL's audio thread, or, with PORT_WAV=file, by the hardware thread in step
 * with the clock (audio_pump) and written to that file - the headless way to hear a run.
 * A write takes effect at the next block of samples (SDL: 512 frames, 10 ms); OPL3's own write
 * buffer spaces the writes as the chip would. */
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL2/SDL.h>

#include "machine.h"
#include "opl3.h"

#define OPL_RATE 49716                  /* the chip's own: 14.318 MHz / 288 */

static opl3_chip opl;
static pthread_mutex_t amu = PTHREAD_MUTEX_INITIALIZER;
static unsigned reg_index;
static int ready;

static FILE *wav;
static uint64_t wav_frames;
static unsigned rate = OPL_RATE;

void audio_out(unsigned port, unsigned v)
{
    if (!ready)
        return;
    pthread_mutex_lock(&amu);
    if (port == 0x388)
        reg_index = v & 0xff;
    else if (port == 0x389)
        OPL3_WriteRegBuffered(&opl, (uint16_t)reg_index, (uint8_t)v);
    pthread_mutex_unlock(&amu);
}

unsigned audio_in(unsigned port)
{
    (void)port;
    return 0;                           /* status: no timer expired, no IRQ */
}

static void render(int16_t *s, unsigned frames)
{
    pthread_mutex_lock(&amu);
    OPL3_GenerateStream(&opl, s, frames);
    pthread_mutex_unlock(&amu);
}

static void sdl_callback(void *u, Uint8 *stream, int len)
{
    (void)u;
    render((int16_t *)stream, (unsigned)len / 4);
}

static void wav_header(uint32_t frames)
{
    uint32_t data = frames * 4, v32;
    uint16_t v16;

    fseek(wav, 0, SEEK_SET);
    fwrite("RIFF", 1, 4, wav);
    v32 = 36 + data; fwrite(&v32, 4, 1, wav);
    fwrite("WAVEfmt ", 1, 8, wav);
    v32 = 16; fwrite(&v32, 4, 1, wav);
    v16 = 1; fwrite(&v16, 2, 1, wav);           /* PCM */
    v16 = 2; fwrite(&v16, 2, 1, wav);           /* stereo */
    v32 = rate; fwrite(&v32, 4, 1, wav);
    v32 = rate * 4; fwrite(&v32, 4, 1, wav);
    v16 = 4; fwrite(&v16, 2, 1, wav);
    v16 = 16; fwrite(&v16, 2, 1, wav);
    fwrite("data", 1, 4, wav);
    fwrite(&data, 4, 1, wav);
    fseek(wav, 0, SEEK_END);
}

/* PORT_WAV: the samples due by `us` (the port's clock), called by the hardware thread */
void audio_pump(uint64_t us)
{
    int16_t buf[2 * 1024];
    uint64_t due;

    if (!wav)
        return;
    due = us * rate / 1000000u;
    while (wav_frames < due) {
        unsigned n = due - wav_frames > 1024 ? 1024 : (unsigned)(due - wav_frames);
        render(buf, n);
        fwrite(buf, 4, n, wav);
        wav_frames += n;
    }
}

void audio_close(void)
{
    if (!wav)
        return;
    wav_header((uint32_t)wav_frames);
    fclose(wav);
    wav = NULL;
}

/* PORT_WAV=file records instead of playing; PORT_NOSOUND=1 plays nothing */
void audio_start(int headless)
{
    const char *w = getenv("PORT_WAV");

    if (w) {
        wav = fopen(w, "wb");
        if (!wav)
            port_fatal("PORT_WAV: cannot write %s", w);
        wav_header(0);
    } else if (!headless && !getenv("PORT_NOSOUND")) {
        SDL_AudioSpec want, have;
        SDL_AudioDeviceID dev;

        if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
            fprintf(stderr, "port: no audio (%s)\n", SDL_GetError());
            return;
        }
        memset(&want, 0, sizeof want);
        want.freq = 44100;
        want.format = AUDIO_S16SYS;
        want.channels = 2;
        want.samples = 512;
        want.callback = sdl_callback;
        dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
        if (!dev) {
            fprintf(stderr, "port: no audio (%s)\n", SDL_GetError());
            return;
        }
        rate = (unsigned)have.freq;
        OPL3_Reset(&opl, rate);         /* Nuked resamples from 49716 Hz to the device's rate */
        ready = 1;
        SDL_PauseAudioDevice(dev, 0);
        return;
    } else
        return;
    OPL3_Reset(&opl, rate);
    ready = 1;
}
