/* cpu.c: the per-thread register files and stacks of the translated asm module (cpu.h). */
#include "cpu.h"

__thread struct cpu cpu_tls;
__thread uint8_t cpu_stack[0x10000];
