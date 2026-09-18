/*
 * main.c — RISC-V RV32I Simulator Entry Point
 *
 * This file ties everything together:
 *   1. Parses command-line arguments (ELF file path, optional flags).
 *   2. Initializes the CPU and memory.
 *   3. Loads the program (from ELF file or built-in test program).
 *   4. Runs the fetch → decode → execute loop until the CPU halts.
 *
 * ═══════════════════════════════════════════════════════════════════
 * THE FETCH-DECODE-EXECUTE CYCLE
 * ═══════════════════════════════════════════════════════════════════
 *
 * Every CPU in existence follows this same fundamental loop:
 *
 *   1. FETCH:   Read the instruction word from memory at the address
 *               held in the Program Counter (PC).
 *
 *   2. DECODE:  Break the instruction word into its constituent fields
 *               (opcode, registers, immediate, function codes) so we
 *               know WHAT operation to perform and on WHICH operands.
 *
 *   3. EXECUTE: Perform the operation — arithmetic, memory access,
 *               branch, etc. — and update the register file, memory,
 *               and/or PC accordingly.
 *
 *   4. Repeat until halted (ECALL exit, illegal instruction, etc.).
 *
 * This is a SEQUENTIAL, IN-ORDER, SINGLE-ISSUE simulator: we process
 * one instruction at a time, in program order.  Real hardware pipelines
 * and superscalar cores overlap these stages, but the programmer-visible
 * behavior is identical.
 * ═══════════════════════════════════════════════════════════════════
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

#include "cpu.h"
#include "memory.h"
#include "decode.h"
#include "elf_loader.h"

/* Forward declaration — defined in execute.c */
void execute(CPU *cpu, DecodedInst d);

/*
 * Built-in test program: a handful of hardcoded instructions to verify
 * the fetch-decode-execute loop works without needing an ELF file.
 *
 * These hex values are the raw 32-bit instruction encodings.  You can
 * verify them with: riscv32-unknown-elf-objdump -d
 */
static uint32_t test_program[] = {
    0x00500093,  /*  ADDI  x1,  x0,  5       → x1 = 5           */
    0x00A00113,  /*  ADDI  x2,  x0,  10      → x2 = 10          */
    0x002081B3,  /*  ADD   x3,  x1,  x2      → x3 = 15          */
    0x40208233,  /*  SUB   x4,  x1,  x2      → x4 = -5 (0xFFFFFFFB) */
    0x0020F2B3,  /*  AND   x5,  x1,  x2      → x5 = 5 & 10 = 0  */
    0x0020E333,  /*  OR    x6,  x1,  x2      → x6 = 5 | 10 = 15 */
    0x0020C3B3,  /*  XOR   x7,  x1,  x2      → x7 = 5 ^ 10 = 15 */
    /*
     * Halt: ECALL with a7=93 (exit), a0=0 (success).
     * We set up the registers for the syscall, then trigger ECALL.
     */
    0x05D00893,  /*  ADDI  x17, x0,  93      → a7 = 93 (exit)   */
    0x00000513,  /*  ADDI  x10, x0,  0       → a0 = 0 (status)  */
    0x00000073,  /*  ECALL                    → exit(0)          */
};

static void print_usage(const char *progname) {
    printf("RISC-V RV32I Simulator\n\n");
    printf("Usage:\n");
    printf("  %s <elf_file>       Load and run a RISC-V ELF executable\n", progname);
    printf("  %s --test           Run the built-in test program\n", progname);
    printf("  %s --help           Show this help message\n", progname);
}

int main(int argc, char *argv[]) {
    CPU cpu;
    int debug_mode = 0;  /* If set, dump registers after each instruction */

    cpu_init(&cpu);
    memory_init();

    /* ── Parse command-line arguments ─────────────────────────── */

    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    /* Check for flags */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--debug") == 0 || strcmp(argv[i], "-d") == 0) {
            debug_mode = 1;
        }
    }

    if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
        print_usage(argv[0]);
        return 0;
    }

    if (strcmp(argv[1], "--test") == 0) {
        /*
         * Built-in test mode: load the hardcoded test program into
         * memory at address 0 and run it.
         */
        printf("=== Running built-in test program ===\n\n");

        /* Copy test program into simulator memory at address 0 */
        for (size_t i = 0; i < sizeof(test_program) / sizeof(test_program[0]); i++) {
            memory_store_word((uint32_t)(i * 4), test_program[i]);
        }
        cpu.pc = 0;
    } else {
        /*
         * ELF mode: load a RISC-V ELF executable.
         */
        if (load_elf(argv[1], &cpu) != 0) {
            fprintf(stderr, "Failed to load ELF file '%s'\n", argv[1]);
            return 1;
        }
    }

    /* ── The Fetch-Decode-Execute Loop ────────────────────────── */

    /*
     * This is the main simulation loop.  Each iteration processes
     * exactly one instruction, mirroring one clock cycle of a
     * simple single-cycle CPU.
     *
     * We cap at 100 million instructions as a safety valve against
     * infinite loops.  Real programs should ECALL exit or EBREAK
     * long before this.
     */
    uint64_t instruction_count = 0;
    const uint64_t MAX_INSTRUCTIONS = 100000000ULL; /* 100 million */

    clock_t start_time = clock();
    while (cpu.running && instruction_count < MAX_INSTRUCTIONS) {
        /* ── FETCH ────────────────────────────────────────────── */
        /*
         * Read the 32-bit instruction word from memory at the
         * address held in the PC.
         *
         * In real hardware, this goes to the instruction cache (L1i),
         * then to main memory if there's a cache miss.  We just
         * index our flat array.
         */
        uint32_t instruction = memory_load_word(cpu.pc);

        if (debug_mode) {
            printf("[PC=0x%08X] Fetched: 0x%08X\n", cpu.pc, instruction);
        }

        /* ── DECODE ───────────────────────────────────────────── */
        /*
         * Extract the instruction fields: opcode, rd, rs1, rs2,
         * funct3, funct7, and the sign-extended immediate.
         */
        DecodedInst decoded = decode(instruction);

        /* ── EXECUTE ──────────────────────────────────────────── */
        /*
         * Perform the instruction's operation: ALU computation,
         * memory load/store, branch evaluation, etc.
         * This also advances the PC (either +4 or to a branch target).
         */
        execute(&cpu, decoded);

        instruction_count++;

        if (debug_mode) {
            cpu_dump(&cpu);
            printf("\n");
        }
    }
    clock_t end_time = clock();
    double seconds = (double)(end_time - start_time) / CLOCKS_PER_SEC;

    if (instruction_count >= MAX_INSTRUCTIONS) {
        printf("\n[SIMULATOR] Reached instruction limit (%llu). Halting.\n",
               (unsigned long long)MAX_INSTRUCTIONS);
    }

    /* ── Final state ──────────────────────────────────────────── */

    printf("\n=== Simulation complete ===\n");
    printf("Instructions executed: %llu\n\n", (unsigned long long)instruction_count);
    printf("Instructions executed: %llu\n", (unsigned long long)instruction_count);
    if (seconds > 0.0) {
        printf("Execution time: %.3f seconds\n", seconds);
        printf("Performance: %.2f MIPS\n", (instruction_count / 1000000.0) / seconds);
    }
    printf("\nPC = 0x%08X\n", cpu.pc);
    cpu_dump(&cpu);

    return 0;
}

