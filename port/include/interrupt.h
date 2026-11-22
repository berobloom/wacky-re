/* ASS's INTERRUP.H for the port: the interrupt flag. On the PC `cli` keeps the timer and the sound
 * card's interrupt out while the library changes its voice list or song pointer. Here those
 * "interrupts" are the hardware thread's timer tasks (sound.c ts_tick) and the audio thread's
 * Sound Blaster interrupt (audio.c), and they take the same recursive lock (machine.c), so
 * DisableInterrupts / RestoreInterrupts keep them out as cli / popf did. Lock order: this flag, then
 * the task list, then the OPL. */
#ifndef PORT_INTERRUPT_H
#define PORT_INTERRUPT_H
void port_cli(void);
void port_sti(void);
static inline unsigned long DisableInterrupts(void) { port_cli(); return 0; }
static inline void RestoreInterrupts(unsigned long flags) { (void)flags; port_sti(); }
#endif
