/*
 * cpu.h — RISC-V RV32I CPU State
 *
 * The RV32I base integer ISA defines:
 *   - 32 general-purpose integer registers, x0–x31, each 32 bits wide.
 *   - x0 is hardwired to zero: reads always return 0, writes are discarded.
 *   - A 32-bit program counter (pc) that holds the address of the current
 *     instruction being executed.
 *
 * We also track a "running" flag so the fetch-decode-execute loop knows
 * when to stop (e.g., on ECALL exit or an illegal instruction).
 */

#ifndef CPU_H
#define CPU_H

#include <stdint.h>

typedef struct {
    uint32_t regs[32];  /* x0–x31; x0 is always 0 */
    uint32_t pc;        /* program counter */
    int      running;   /* 1 = running, 0 = halted */
} CPU;

/*
 * cpu_init — Zero all registers, set pc to 0, mark the CPU as running.
 * Call this once before the first fetch cycle.
 */
void cpu_init(CPU *cpu);

/*
 * cpu_dump — Print the entire register file in a human-readable 8×4 grid.
 * Shows both the ABI name (ra, sp, a0, …) and the raw hex value.
 * Useful for debugging after each instruction.
 */
void cpu_dump(const CPU *cpu);

#endif /* CPU_H */

