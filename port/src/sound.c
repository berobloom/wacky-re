/* sound.c: the Apogee Sound System's entry points the game calls (src/ass, findings/executables.md).
 *
 * TS_* is the task manager, and the game's clocks run on it (main, src/ww.c: tick_task at 136 Hz
 * into `ticks`, 10 Hz into `racetime`, 1 Hz into `ticks3`, raised to 1000 Hz for the serial link).
 * Here it runs on the hardware thread: each task's service is called `rate` times a second, the
 * way TASK_MAN.C's timer interrupt called it. The struct is TASK_MAN.H's.
 *
 * FX_* and MUSIC_* accept the configured card and stay silent until the sound port is written, so
 * the game takes the path it takes with a working card (with a refusal it stops in sound_init and
 * waits for a key, src/ww.c). */
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

char *FX_ErrorString(int code) { (void)code; return "Sound is not ported yet."; }
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

/* ---- MUSIC_* (MUSIC.H): no card */
#define MUSIC_Ok 0

char *MUSIC_ErrorString(int code) { (void)code; return "Music is not ported yet."; }
int MUSIC_Init(int card, int address) { (void)card; (void)address; return MUSIC_Ok; }
int MUSIC_Shutdown(void) { return MUSIC_Ok; }
void MUSIC_SetVolume(int v) { (void)v; }
int MUSIC_SongPlaying(void) { return 0; }
int MUSIC_StopSong(void) { return MUSIC_Ok; }
int MUSIC_PlaySong(unsigned char *song, int loop) { (void)song; (void)loop; return MUSIC_Ok; }
void MUSIC_FadeVolume(int tovol, int ms) { (void)tovol; (void)ms; }
void music_pause(void) {}
void music_resume(void) {}
