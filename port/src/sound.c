/* sound.c: the Apogee Sound System's entry points the game calls (src/ass, findings/executables.md).
 *
 * TS_* is the task manager, and the game's clocks run on it (main, src/ww.c: tick_task at 136 Hz
 * into `ticks`, 10 Hz into `racetime`, 1 Hz into `ticks3`, raised to 1000 Hz for the serial link).
 * Here it runs on the hardware thread: each task's service is called `rate` times a second, the
 * way TASK_MAN.C's timer interrupt called it. The struct is TASK_MAN.H's.
 *
 * FX_* accepts the configured card and plays nothing, so the game takes the path it takes with a
 * working card (with a refusal it stops in sound_init and waits for a key, src/ww.c). MUSIC_* runs
 * the game's own FM song player, silent until the OPL2 is emulated (below). */
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>

#include "machine.h"

typedef struct task {
    struct task *next;
    struct task *prev;
    void (*TaskService)(struct task *);
    void *data;
    long rate;
    volatile long count;
    int priority;
    int active;
} task;

#define MAXTASKS 16
static task *tasks[MAXTASKS];
static uint32_t due[MAXTASKS];          /* the next call, in us */
static pthread_mutex_t tmu = PTHREAD_MUTEX_INITIALIZER;

task *TS_ScheduleTask(void (*f)(task *), int rate, int priority, void *data)
{
    task *t = calloc(1, sizeof *t);
    int i;

    t->TaskService = f;
    t->rate = rate > 0 ? rate : 1;
    t->priority = priority;
    t->data = data;
    pthread_mutex_lock(&tmu);
    for (i = 0; i < MAXTASKS && tasks[i]; i++)
        ;
    if (i == MAXTASKS)
        port_fatal("TS_ScheduleTask: more than %d tasks", MAXTASKS);
    tasks[i] = t;
    due[i] = 0;
    pthread_mutex_unlock(&tmu);
    return t;
}

void TS_Dispatch(void)
{
    int i;

    pthread_mutex_lock(&tmu);
    for (i = 0; i < MAXTASKS; i++)
        if (tasks[i])
            tasks[i]->active = 1;
    pthread_mutex_unlock(&tmu);
}

void TS_SetTaskRate(task *t, int rate)
{
    t->rate = rate > 0 ? rate : 1;
}

void TS_Terminate(task *t)
{
    int i;

    pthread_mutex_lock(&tmu);
    for (i = 0; i < MAXTASKS; i++)
        if (tasks[i] == t)
            tasks[i] = NULL;
    pthread_mutex_unlock(&tmu);
    free(t);
}

void TS_Shutdown(void)
{
    int i;

    for (i = 0; i < MAXTASKS; i++)
        tasks[i] = NULL;
}

/* the hardware thread's timer: every task whose time has come, as often as it is behind (capped,
 * so a stopped debugger does not replay a minute of ticks) */
void ts_tick(uint32_t now)
{
    int i, n;

    pthread_mutex_lock(&tmu);
    for (i = 0; i < MAXTASKS; i++) {
        task *t = tasks[i];
        uint32_t step;

        if (!t || !t->active)
            continue;
        step = 1000000u / (uint32_t)t->rate;
        if (!due[i])
            due[i] = now + step;
        for (n = 0; (int32_t)(now - due[i]) >= 0 && n < 50; n++) {
            due[i] += step;
            t->TaskService(t);
        }
        if (n == 50)
            due[i] = now + step;
    }
    pthread_mutex_unlock(&tmu);
}

/* ---- FX_* (FX_MAN.H): no card */
#define FX_Ok 0

typedef struct { int MaxVoices, MaxSampleBits, MaxChannels; } fx_device;   /* FX_MAN.H */

char *FX_ErrorString(int code) { (void)code; return "Sound effects are not ported yet."; }
int FX_SetupCard(int card, fx_device *dev)
{
    (void)card;
    dev->MaxVoices = 8;
    dev->MaxSampleBits = 16;
    dev->MaxChannels = 2;
    return FX_Ok;
}
int FX_Init(int card, int voices, int channels, int bits, unsigned rate)
{
    (void)card; (void)voices; (void)channels; (void)bits; (void)rate;
    return FX_Ok;
}
int FX_Shutdown(void) { return FX_Ok; }
void FX_SetVolume(int v) { (void)v; }
int FX_SetPitch(int handle, int pitch) { (void)handle; (void)pitch; return FX_Ok; }
int FX_PlayVOC(char *p, int pitch, int vol, int l, int r, int prio, unsigned long cb)
{
    (void)p; (void)pitch; (void)vol; (void)l; (void)r; (void)prio; (void)cb;
    return 0;
}
int FX_SoundActive(int handle) { (void)handle; return 0; }
int FX_StopSound(int handle) { (void)handle; return FX_Ok; }
int FX_StopAllSounds(void) { return FX_Ok; }

/* ---- MUSIC_* (MUSIC.H). Wacky Wheels' MUSIC module sends the FM cards (0-2, the .KLM songs) to its
 * own FM song player (findings/executables.md "Sound library"), which the port runs as rebuilt:
 * src/ass/FMSONG.C on this task manager, FMCHIP.C writing the OPL2 at 0x388 (no OPL emulated yet,
 * so silent - but the song task runs at the song's own rate, so MUSIC_SongPlaying is true for as
 * long as the song lasts, and the intro's waits on it last as on the PC). Cards 3-5 (MIDI) are
 * accepted and silent. */
#define MUSIC_Ok 0

int FMSONG_Init(int card);
void FMSONG_Shutdown(void);
void FMSONG_StopSong(void);
void FMSONG_PlaySong(unsigned char *song, int loopflag);
int FMSONG_SongPlaying(void);
void FMSONG_Continue(void);
void FMSONG_Pause(void);
void FMSONG_SetVolume(int volume);

static int fm;                          /* MUSIC_Init chose the FM player */

char *MUSIC_ErrorString(int code) { (void)code; return "Music error."; }
int MUSIC_Init(int card, int address)
{
    (void)address;
    fm = card >= 0 && card <= 2;
    return fm ? FMSONG_Init(card) : MUSIC_Ok;
}
int MUSIC_Shutdown(void) { if (fm) FMSONG_Shutdown(); return MUSIC_Ok; }
void MUSIC_SetVolume(int v) { if (fm) FMSONG_SetVolume(v); }
int MUSIC_SongPlaying(void) { return fm ? FMSONG_SongPlaying() : 0; }
int MUSIC_StopSong(void) { if (fm) FMSONG_StopSong(); return MUSIC_Ok; }
int MUSIC_PlaySong(unsigned char *song, int loop) { if (fm) FMSONG_PlaySong(song, loop); return MUSIC_Ok; }
void MUSIC_FadeVolume(int tovol, int ms) { (void)ms; if (fm) FMSONG_SetVolume(tovol); }
void music_pause(void) { if (fm) FMSONG_Pause(); }
void music_resume(void) { if (fm) FMSONG_Continue(); }

/* DPMI memory locking (src/ass/DPMI.H): nothing pages out here */
int DPMI_LockMemory(void *a, unsigned n) { (void)a; (void)n; return 0; }
int DPMI_LockMemoryRegion(void *a, void *b) { (void)a; (void)b; return 0; }
int DPMI_UnlockMemory(void *a, unsigned n) { (void)a; (void)n; return 0; }
int DPMI_UnlockMemoryRegion(void *a, void *b) { (void)a; (void)b; return 0; }
