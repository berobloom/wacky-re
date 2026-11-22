/* sound.c: the Apogee Sound System's entry points the game calls (src/ass, findings/executables.md).
 *
 * TS_* is the task manager, and the game's clocks run on it (main, src/ww.c: tick_task at 136 Hz
 * into `ticks`, 10 Hz into `racetime`, 1 Hz into `ticks3`, raised to 1000 Hz for the serial link).
 * Here it runs on the hardware thread: each task's service is called `rate` times a second, the
 * way TASK_MAN.C's timer interrupt called it. The struct is TASK_MAN.H's.
 *
 * FX_* and MUSIC_* are the sound library's own code (src/ass): FX_MAN + MULTIVOC over the port's
 * Sound Blaster (blaster.c), and the game's FM song player over the emulated OPL2 (audio.c). */
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

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

    port_cli();                         /* the timer interrupt waits while the flag is clear */
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
    port_sti();
}

/* ---- FX_* is the sound library's own FX_MAN.C over MULTIVOC.C (src/ass), with the port's Sound
 * Blaster driver under it (blaster.c, audio.c). */

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

/* DPMI (src/ass/DPMI.H): nothing pages out here. "DOS memory" (below 1 MB, for the DMA buffer)
 * must still keep ISA DMA's rule: MULTIVOC moves a ring that crosses a 64 KB boundary to the next
 * page with `(ptr & 0xff0000) + 0x10000` (MULTIVOC.C MV_Init), which only works on a 24-bit
 * address - on a heap pointer it gave 0x280000 and a crash. A 64 KB-aligned block never crosses
 * (the ring is 2 * 4 KB), so the move never happens. */
int DPMI_GetDOSMemory(void **ptr, int *descriptor, unsigned length)
{
    *ptr = aligned_alloc(0x10000, (length + 0xffffu) & ~0xffffu);
    if (*ptr)
        memset(*ptr, 0, length);
    *descriptor = (int)(uintptr_t)*ptr;
    return *ptr ? 0 : -1;
}
int DPMI_FreeDOSMemory(int descriptor) { free((void *)(uintptr_t)descriptor); return 0; }

int DPMI_LockMemory(void *a, unsigned n) { (void)a; (void)n; return 0; }
int DPMI_LockMemoryRegion(void *a, void *b) { (void)a; (void)b; return 0; }
int DPMI_UnlockMemory(void *a, unsigned n) { (void)a; (void)n; return 0; }
int DPMI_UnlockMemoryRegion(void *a, void *b) { (void)a; (void)b; return 0; }
