/* ASS's INTERRUP.H for the port: the interrupt flag guards a song pointer against the timer task.
 * Here the task runs on the hardware thread under the task manager's mutex (port/src/sound.c), and
 * the guarded sections are single stores, so these are no-ops. */
#ifndef PORT_INTERRUPT_H
#define PORT_INTERRUPT_H
static inline unsigned long DisableInterrupts(void) { return 0; }
static inline void RestoreInterrupts(unsigned long flags) { (void)flags; }
#endif
