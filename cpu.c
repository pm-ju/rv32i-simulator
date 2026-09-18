/*
 * cpu.c — RISC-V RV32I CPU State Implementation
 */

#include "cpu.h"
#include <stdio.h>
#include <string.h>

/*
 * ABI register names per the RISC-V calling convention.
 * These are the names you'd see in assembly output (ra, sp, a0, etc.).
 * We store them here so cpu_dump() output is immediately readable
 * without cross-referencing a register number table.
 */
static const char *reg_names[32] = {
    "zero", "ra",  "sp",  "gp",  "tp",  "t0",  "t1",  "t2",
    "s0",   "s1",  "a0",  "a1",  "a2",  "a3",  "a4",  "a5",
    "a6",   "a7",  "s2",  "s3",  "s4",  "s5",  "s6",  "s7",
    "s8",   "s9",  "s10", "s11", "t3",  "t4",  "t5",  "t6"
};

void cpu_init(CPU *cpu) {
    /*
     * memset is fine here — all-zero-bits is the correct initial value
     * for both unsigned integers (0) and the pc (start at address 0).
     */
    memset(cpu->regs, 0, sizeof(cpu->regs));
    cpu->pc = 0;
    cpu->running = 1;

    /* Initialize stack pointer (sp / x2) to the top of memory minus 16 bytes padding */
    cpu->regs[2] = 0x00FFFFF0;
}

void cpu_dump(const CPU *cpu) {
    printf("PC = 0x%08X\n", cpu->pc);
    printf("─────────────────────────────────────────────────────────\n");

    /*
     * Print registers in a 4-column grid: x0–x7, x8–x15, x16–x23, x24–x31.
     * Each cell shows: xNN(abi) = 0xHHHHHHHH
     * This layout matches how register files are commonly drawn in
     * architecture textbooks.
     */
    for (int i = 0; i < 32; i++) {
        printf("x%-2d(%-4s)=0x%08X", i, reg_names[i], cpu->regs[i]);

        if ((i % 4) == 3) {
            printf("\n");
        } else {
            printf("  ");
        }
    }
    printf("─────────────────────────────────────────────────────────\n");
}

