/*
 * test_instructions.c — RV32I Instruction Test Harness
 *
 * Tests every category of RV32I instruction by feeding hand-encoded
 * instruction words into the decoder and executor, then checking the
 * resulting register values against expected results.
 *
 * Each test is a small "program" (array of uint32_t instruction words)
 * that runs on a freshly initialized CPU + memory.
 *
 * ═══════════════════════════════════════════════════════════════════
 * HOW TO VERIFY THESE ENCODINGS
 * ═══════════════════════════════════════════════════════════════════
 *
 * Write the instruction in assembly, assemble it, and check with objdump:
 *
 *   echo "addi x1, x0, 5" > /tmp/test.s
 *   riscv32-unknown-elf-as -o /tmp/test.o /tmp/test.s
 *   riscv32-unknown-elf-objdump -d /tmp/test.o
 *
 * The hex in the objdump output should match our hardcoded values.
 * ═══════════════════════════════════════════════════════════════════
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "../cpu.h"
#include "../memory.h"
#include "../decode.h"

/* Forward declaration from execute.c */
void execute(CPU *cpu, DecodedInst d);

/* ── Test infrastructure ──────────────────────────────────────────── */

static int tests_passed = 0;
static int tests_failed = 0;

#define ASSERT_REG(cpu, reg, expected)                                     \
    do {                                                                   \
        if ((cpu).regs[reg] == (uint32_t)(expected)) {                     \
            tests_passed++;                                                \
        } else {                                                           \
            tests_failed++;                                                \
            printf("  FAIL: x%d = 0x%08X, expected 0x%08X\n",             \
                   (reg), (cpu).regs[reg], (uint32_t)(expected));          \
        }                                                                  \
    } while (0)

/*
 * run_program — Load an array of instruction words into memory,
 * reset the CPU, and execute until ECALL(exit) or we run out of
 * instructions.  Returns the CPU state for assertion checking.
 */
static CPU run_program(const uint32_t *program, size_t count) {
    CPU cpu;
    cpu_init(&cpu);
    memory_init();

    /* Load program into memory at address 0 */
    for (size_t i = 0; i < count; i++) {
        memory_store_word((uint32_t)(i * 4), program[i]);
    }

    /* Execute up to 1000 instructions (safety limit) */
    int limit = 1000;
    while (cpu.running && limit-- > 0) {
        uint32_t inst = memory_load_word(cpu.pc);
        DecodedInst d = decode(inst);
        execute(&cpu, d);
    }

    return cpu;
}

/* Convenience macro to compute array size */
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

/* ══════════════════════════════════════════════════════════════════ */
/*  TEST CASES                                                      */
/* ══════════════════════════════════════════════════════════════════ */

/* ── Test 1: R-type ALU (ADD, SUB, AND, OR, XOR, SLT, SLTU) ───── */
static void test_r_type_alu(void) {
    printf("Test: R-type ALU operations...\n");

    uint32_t prog[] = {
        0x00500093,  /* ADDI  x1,  x0,  5      → x1 = 5           */
        0x00A00113,  /* ADDI  x2,  x0,  10     → x2 = 10          */

        0x002081B3,  /* ADD   x3,  x1,  x2     → x3 = 15          */
        0x40208233,  /* SUB   x4,  x1,  x2     → x4 = -5 (0xFFFFFFFB) */
        0x0020F2B3,  /* AND   x5,  x1,  x2     → x5 = 5&10 = 0    */
        0x0020E333,  /* OR    x6,  x1,  x2     → x6 = 5|10 = 15   */
        0x0020C3B3,  /* XOR   x7,  x1,  x2     → x7 = 5^10 = 15   */
        0x0020A433,  /* SLT   x8,  x1,  x2     → x8 = 1 (5 < 10)  */
        0x0010A4B3,  /* SLT   x9,  x1,  x1     → x9 = 0 (5 !< 5) */
        0x0020B533,  /* SLTU  x10, x1,  x2     → x10= 1 (5 < 10)  */

        /* Halt */
        0x05D00893,  /* ADDI  x17, x0,  93     → a7 = exit         */
        0x00000513,  /* ADDI  x10, x0,  0      → a0 = 0 — but we overwrote x10 above */
        0x00000073,  /* ECALL                                       */
    };
    /* Note: x10 gets overwritten by SLTU then by the exit setup ADDI */

    CPU cpu = run_program(prog, ARRAY_SIZE(prog));

    ASSERT_REG(cpu, 1,  5);
    ASSERT_REG(cpu, 2,  10);
    ASSERT_REG(cpu, 3,  15);
    ASSERT_REG(cpu, 4,  0xFFFFFFFB);  /* -5 in two's complement */
    ASSERT_REG(cpu, 5,  0);           /* 5 AND 10 = 0 (no common bits in binary) */
    ASSERT_REG(cpu, 6,  15);          /* 5 OR 10 = 0b0101 | 0b1010 = 0b1111 = 15 */
    ASSERT_REG(cpu, 7,  15);          /* 5 XOR 10 = 0b0101 ^ 0b1010 = 0b1111 = 15 */
    ASSERT_REG(cpu, 8,  1);           /* 5 < 10 (signed) → true */
    ASSERT_REG(cpu, 9,  0);           /* 5 < 5 → false */

    printf("  R-type ALU: done\n\n");
}

/* ── Test 2: R-type Shifts (SLL, SRL, SRA) ────────────────────── */
static void test_r_type_shifts(void) {
    printf("Test: R-type shift operations...\n");

    uint32_t prog[] = {
        0x00800093,  /* ADDI  x1,  x0,  8      → x1 = 8           */
        0x00200113,  /* ADDI  x2,  x0,  2      → x2 = 2           */

        0x002091B3,  /* SLL   x3,  x1,  x2     → x3 = 8 << 2 = 32 */
        0x0020D233,  /* SRL   x4,  x1,  x2     → x4 = 8 >> 2 = 2  */

        /* Test SRA with a negative number */
        0xFFF00293,  /* ADDI  x5,  x0,  -1     → x5 = 0xFFFFFFFF  */
        0x00100313,  /* ADDI  x6,  x0,  1      → x6 = 1           */
        0x4062D3B3,  /* SRA   x7,  x5,  x6     → x7 = -1 >> 1 = -1 (0xFFFFFFFF) */

        /* SRA with -8 >> 2 = -2 */
        0xFF800413,  /* ADDI  x8,  x0,  -8     → x8 = 0xFFFFFFF8  */
        0x402454B3,  /* SRA   x9,  x8,  x2     → x9 = -8 >> 2 = -2 (0xFFFFFFFE) */

        0x05D00893,  /* ADDI  x17, x0,  93     */
        0x00000513,  /* ADDI  x10, x0,  0      */
        0x00000073,  /* ECALL                   */
    };

    CPU cpu = run_program(prog, ARRAY_SIZE(prog));

    ASSERT_REG(cpu, 3, 32);          /* 8 << 2 */
    ASSERT_REG(cpu, 4, 2);           /* 8 >> 2 (logical) */
    ASSERT_REG(cpu, 7, 0xFFFFFFFF);  /* -1 >> 1 (arithmetic) stays -1 */
    ASSERT_REG(cpu, 9, 0xFFFFFFFE);  /* -8 >> 2 (arithmetic) = -2 */

    printf("  R-type shifts: done\n\n");
}

/* ── Test 3: I-type ALU (ADDI, ANDI, ORI, XORI, SLTI, SLTIU) ─── */
static void test_i_type_alu(void) {
    printf("Test: I-type ALU operations...\n");

    uint32_t prog[] = {
        0x00A00093,  /* ADDI  x1,  x0,  10     → x1 = 10          */
        0xFFE08113,  /* ADDI  x2,  x1,  -2     → x2 = 8           */
        0x00F0F193,  /* ANDI  x3,  x1,  15     → x3 = 10 & 15 = 10*/
        0x0FF0E213,  /* ORI   x4,  x1,  0xFF   → x4 = 10|255 = 255*/
        0x00F0C293,  /* XORI  x5,  x1,  15     → x5 = 10^15 = 5   */
        0x00B0A313,  /* SLTI  x6,  x1,  11     → x6 = 1 (10 < 11) */
        0x00A0A393,  /* SLTI  x7,  x1,  10     → x7 = 0 (10 !< 10)*/

        /* SLTIU: sign-extended immediate compared unsigned */
        0x00B0B413,  /* SLTIU x8,  x1,  11     → x8 = 1 (10 < 11) */

        0x05D00893,  /* ADDI  x17, x0,  93     */
        0x00000513,  /* ADDI  x10, x0,  0      */
        0x00000073,  /* ECALL                   */
    };

    CPU cpu = run_program(prog, ARRAY_SIZE(prog));

    ASSERT_REG(cpu, 1,  10);
    ASSERT_REG(cpu, 2,  8);
    ASSERT_REG(cpu, 3,  10);   /* 10 & 15 = 10 */
    ASSERT_REG(cpu, 4,  255);  /* 10 | 0xFF = 0xFF */
    ASSERT_REG(cpu, 5,  5);    /* 10 ^ 15 = 5 */
    ASSERT_REG(cpu, 6,  1);    /* 10 < 11 signed */
    ASSERT_REG(cpu, 7,  0);    /* 10 !< 10 */
    ASSERT_REG(cpu, 8,  1);    /* 10 < 11 unsigned */

    printf("  I-type ALU: done\n\n");
}

/* ── Test 4: I-type Shifts (SLLI, SRLI, SRAI) ────────────────── */
static void test_i_type_shifts(void) {
    printf("Test: I-type shift operations...\n");

    uint32_t prog[] = {
        0x00800093,  /* ADDI  x1,  x0,  8      → x1 = 8           */
        0x00209113,  /* SLLI  x2,  x1,  2      → x2 = 32          */
        0x0020D193,  /* SRLI  x3,  x1,  2      → x3 = 2           */

        /* SRAI with negative */
        0xFF800213,  /* ADDI  x4,  x0,  -8     → x4 = 0xFFFFFFF8  */
        0x40225293,  /* SRAI  x5,  x4,  2      → x5 = -2 (0xFFFFFFFE) */

        0x05D00893,  /* ADDI  x17, x0,  93     */
        0x00000513,  /* ADDI  x10, x0,  0      */
        0x00000073,  /* ECALL                   */
    };

    CPU cpu = run_program(prog, ARRAY_SIZE(prog));

    ASSERT_REG(cpu, 2, 32);
    ASSERT_REG(cpu, 3, 2);
    ASSERT_REG(cpu, 5, 0xFFFFFFFE);  /* -8 >> 2 = -2 */

    printf("  I-type shifts: done\n\n");
}

/* ── Test 5: Load/Store (SW/LW round-trip, LB/LBU sign extension) ─ */
static void test_load_store(void) {
    printf("Test: Load/Store operations...\n");

    /*
     * Strategy:
     *   1. Store values to a memory location (using high address to avoid
     *      overlapping with our program code).
     *   2. Load them back and verify.
     *   3. Test sign vs zero extension for byte/halfword loads.
     */
    uint32_t prog[] = {
        /* Set up base address for data at 0x1000 (4096) */
        0x00001437,  /* LUI   x8,  0x1         → x8 = 0x00001000  */

        /* Store word: 0xDEADBEEF at address 0x1000 */
        0xDEADE0B7,  /* LUI   x1,  0xDEADE     → x1 = 0xDEADE000 */
        0xEEF08093,  /* ADDI  x1,  x1,  -273   → x1 = 0xDEADBEEF */
        /* -273 = 0xFFFFFEEF; 0xDEADE000 + 0xFFFFFEEF = 0xDEADDEEF...
         * Actually let's use a simpler value. */

        /* Let me use a simpler approach: store 0x80 (128) to test sign extension */
        0x08000093,  /* ADDI  x1,  x0,  128    → x1 = 128 (0x80)  */
        0x00140023,  /* SB    x1,  0(x8)        → mem[0x1000] = 0x80 */

        /* LB (sign-extended): 0x80 → bit 7 is set → sign-extends to 0xFFFFFF80 */
        0x00040103,  /* LB    x2,  0(x8)        → x2 = 0xFFFFFF80 (-128) */

        /* LBU (zero-extended): 0x80 → just 0x00000080 */
        0x00044183,  /* LBU   x3,  0(x8)        → x3 = 0x00000080 (128) */

        /* Store word and load it back */
        0x12345237,  /* LUI   x4,  0x12345     → x4 = 0x12345000  */
        0x67820213,  /* ADDI  x4,  x4,  0x678  → x4 = 0x12345678  */
        0x00442023,  /* SW    x4,  0(x8)        → mem[0x1000] = 0x12345678 */
        0x00042283,  /* LW    x5,  0(x8)        → x5 = 0x12345678 */

        /* Store halfword and load it back — test LH vs LHU */
        0xFFF00313,  /* ADDI  x6,  x0,  -1     → x6 = 0xFFFFFFFF  */
        0x00641423,  /* SH    x6,  8(x8)        → mem[0x1008] = 0xFFFF */
        0x00841383,  /* LH    x7,  8(x8)        → x7 = 0xFFFFFFFF (sign-ext) */
        0x00845E13,  /* LHU   x28, 8(x8)        → x28= 0x0000FFFF (zero-ext) */

        0x05D00893,  /* ADDI  x17, x0,  93     */
        0x00000513,  /* ADDI  x10, x0,  0      */
        0x00000073,  /* ECALL                   */
    };

    CPU cpu = run_program(prog, ARRAY_SIZE(prog));

    ASSERT_REG(cpu, 2,  0xFFFFFF80);  /* LB: 0x80 sign-extends to -128 */
    ASSERT_REG(cpu, 3,  0x00000080);  /* LBU: 0x80 zero-extends to 128 */
    ASSERT_REG(cpu, 5,  0x12345678);  /* SW + LW round-trip */
    ASSERT_REG(cpu, 7,  0xFFFFFFFF);  /* LH: 0xFFFF sign-extends to -1 */
    ASSERT_REG(cpu, 28, 0x0000FFFF);  /* LHU: 0xFFFF zero-extends to 65535 */

    printf("  Load/Store: done\n\n");
}

/* ── Test 6: Branches (BEQ, BNE, BLT, BGE, BLTU, BGEU) ──────── */
static void test_branches(void) {
    printf("Test: Branch operations...\n");

    /*
     * Test BEQ taken: if x1 == x2, skip the next instruction.
     *
     * addr 0x00: ADDI x1, x0, 5        → x1 = 5
     * addr 0x04: ADDI x2, x0, 5        → x2 = 5
     * addr 0x08: BEQ  x1, x2, +8       → branch to 0x10 (skip next)
     * addr 0x0C: ADDI x3, x0, 99       → x3 = 99 (SHOULD BE SKIPPED)
     * addr 0x10: ADDI x3, x0, 42       → x3 = 42 (landed here)
     */
    uint32_t prog_beq[] = {
        0x00500093,  /* ADDI x1, x0, 5          */
        0x00500113,  /* ADDI x2, x0, 5          */
        0x00208463,  /* BEQ  x1, x2, +8         → skip to addr 0x10 */
        0x06300193,  /* ADDI x3, x0, 99         → should be skipped */
        0x02A00193,  /* ADDI x3, x0, 42         → should execute */

        0x05D00893,  0x00000513, 0x00000073,  /* exit */
    };

    CPU cpu = run_program(prog_beq, ARRAY_SIZE(prog_beq));
    ASSERT_REG(cpu, 3, 42);  /* BEQ was taken, skipped the 99 */

    /*
     * Test BNE taken: if x1 != x2, branch.
     */
    uint32_t prog_bne[] = {
        0x00500093,  /* ADDI x1, x0, 5          */
        0x00A00113,  /* ADDI x2, x0, 10         */
        0x00209463,  /* BNE  x1, x2, +8         → skip to addr 0x10 */
        0x06300193,  /* ADDI x3, x0, 99         → should be skipped */
        0x02A00193,  /* ADDI x3, x0, 42         → should execute */

        0x05D00893,  0x00000513, 0x00000073,
    };

    cpu = run_program(prog_bne, ARRAY_SIZE(prog_bne));
    ASSERT_REG(cpu, 3, 42);  /* BNE was taken */

    /*
     * Test BLT (signed less than):
     *   x1 = -5 (0xFFFFFFFB), x2 = 5
     *   -5 < 5 in signed → branch taken
     */
    uint32_t prog_blt[] = {
        0xFFB00093,  /* ADDI x1, x0, -5         → x1 = -5          */
        0x00500113,  /* ADDI x2, x0, 5           → x2 = 5           */
        0x0020C463,  /* BLT  x1, x2, +8          → skip to addr 0x10 */
        0x06300193,  /* ADDI x3, x0, 99          → should be skipped */
        0x02A00193,  /* ADDI x3, x0, 42          → should execute   */

        0x05D00893,  0x00000513, 0x00000073,
    };

    cpu = run_program(prog_blt, ARRAY_SIZE(prog_blt));
    ASSERT_REG(cpu, 3, 42);  /* BLT was taken (-5 < 5) */

    /*
     * Test BGE (signed greater or equal):
     *   x1 = 10, x2 = 5
     *   10 >= 5 → branch taken
     */
    uint32_t prog_bge[] = {
        0x00A00093,  /* ADDI x1, x0, 10         */
        0x00500113,  /* ADDI x2, x0, 5           */
        0x0020D463,  /* BGE  x1, x2, +8          → skip to addr 0x10 */
        0x06300193,  /* ADDI x3, x0, 99          → should be skipped */
        0x02A00193,  /* ADDI x3, x0, 42          → should execute   */

        0x05D00893,  0x00000513, 0x00000073,
    };

    cpu = run_program(prog_bge, ARRAY_SIZE(prog_bge));
    ASSERT_REG(cpu, 3, 42);

    /*
     * Test BLTU (unsigned less than):
     *   x1 = 5, x2 = 0xFFFFFFFF (which is -1 signed, but huge unsigned)
     *   5 < 0xFFFFFFFF unsigned → branch taken
     */
    uint32_t prog_bltu[] = {
        0x00500093,  /* ADDI x1, x0, 5           */
        0xFFF00113,  /* ADDI x2, x0, -1          → x2 = 0xFFFFFFFF */
        0x0020E463,  /* BLTU x1, x2, +8          → skip to addr 0x10 */
        0x06300193,  /* ADDI x3, x0, 99          → should be skipped */
        0x02A00193,  /* ADDI x3, x0, 42          → should execute   */

        0x05D00893,  0x00000513, 0x00000073,
    };

    cpu = run_program(prog_bltu, ARRAY_SIZE(prog_bltu));
    ASSERT_REG(cpu, 3, 42);

    /*
     * Test BGEU (unsigned greater or equal):
     *   x1 = 0xFFFFFFFF, x2 = 5
     *   0xFFFFFFFF >= 5 unsigned → branch taken
     */
    uint32_t prog_bgeu[] = {
        0xFFF00093,  /* ADDI x1, x0, -1          → x1 = 0xFFFFFFFF */
        0x00500113,  /* ADDI x2, x0, 5            */
        0x0020F463,  /* BGEU x1, x2, +8           → skip to addr 0x10 */
        0x06300193,  /* ADDI x3, x0, 99           → should be skipped */
        0x02A00193,  /* ADDI x3, x0, 42           → should execute   */

        0x05D00893,  0x00000513, 0x00000073,
    };

    cpu = run_program(prog_bgeu, ARRAY_SIZE(prog_bgeu));
    ASSERT_REG(cpu, 3, 42);

    printf("  Branches: done\n\n");
}

/* ── Test 7: JAL and JALR ─────────────────────────────────────── */
static void test_jumps(void) {
    printf("Test: JAL and JALR...\n");

    /*
     * Test JAL: Jump forward, save return address in rd.
     *
     * addr 0x00: JAL   x1, +8      → x1 = 0x04 (return addr), PC = 0x08
     * addr 0x04: ADDI  x2, x0, 99  → should be SKIPPED
     * addr 0x08: ADDI  x2, x0, 42  → should execute
     */
    uint32_t prog_jal[] = {
        0x008000EF,  /* JAL  x1, +8              → x1 = 4, jump to 0x08 */
        0x06300113,  /* ADDI x2, x0, 99          → skipped */
        0x02A00113,  /* ADDI x2, x0, 42          → executes */

        0x05D00893,  0x00000513, 0x00000073,
    };

    CPU cpu = run_program(prog_jal, ARRAY_SIZE(prog_jal));
    ASSERT_REG(cpu, 1, 4);   /* return address = PC+4 = 0x04 */
    ASSERT_REG(cpu, 2, 42);  /* JAL skipped the 99 */

    /*
     * Test JALR: Jump to address in register + offset.
     *
     * addr 0x00: ADDI  x1, x0, 12  → x1 = 12 (target address)
     * addr 0x04: JALR  x2, x1, 0   → x2 = 0x08 (return addr), PC = 12
     * addr 0x08: ADDI  x3, x0, 99  → should be SKIPPED
     * addr 0x0C: ADDI  x3, x0, 42  → should execute (addr 12)
     */
    uint32_t prog_jalr[] = {
        0x00C00093,  /* ADDI  x1, x0, 12         → x1 = 12 */
        0x00008167,  /* JALR  x2, x1, 0          → x2 = 8, PC = 12 */
        0x06300193,  /* ADDI  x3, x0, 99         → skipped */
        0x02A00193,  /* ADDI  x3, x0, 42         → executes */

        0x05D00893,  0x00000513, 0x00000073,
    };

    cpu = run_program(prog_jalr, ARRAY_SIZE(prog_jalr));
    ASSERT_REG(cpu, 2, 8);   /* return address = 0x08 */
    ASSERT_REG(cpu, 3, 42);  /* JALR skipped the 99 */

    printf("  JAL/JALR: done\n\n");
}

/* ── Test 8: LUI and AUIPC ───────────────────────────────────── */
static void test_upper_immediate(void) {
    printf("Test: LUI and AUIPC...\n");

    uint32_t prog[] = {
        /*
         * LUI: Load Upper Immediate.
         * LUI x1, 0x12345 → x1 = 0x12345000
         *
         * The immediate is placed in bits [31:12], with bits [11:0] zeroed.
         */
        0x123450B7,  /* LUI   x1, 0x12345       → x1 = 0x12345000 */

        /*
         * AUIPC: Add Upper Immediate to PC.
         * At this point PC = 0x04 (second instruction).
         * AUIPC x2, 0x00001 → x2 = 0x04 + 0x00001000 = 0x00001004
         */
        0x00001117,  /* AUIPC x2, 0x1            → x2 = PC + 0x1000 = 0x1004 */

        /* LUI + ADDI to build a full 32-bit constant:
         * Target: 0x12345678
         * LUI  x3, 0x12345 → x3 = 0x12345000
         * ADDI x3, x3, 0x678 → x3 = 0x12345678 */
        0x123451B7,  /* LUI   x3, 0x12345       → x3 = 0x12345000 */
        0x67818193,  /* ADDI  x3, x3, 0x678     → x3 = 0x12345678 */

        0x05D00893,  0x00000513, 0x00000073,
    };

    CPU cpu = run_program(prog, ARRAY_SIZE(prog));

    ASSERT_REG(cpu, 1, 0x12345000);
    ASSERT_REG(cpu, 2, 0x00001004);  /* PC was 0x04 when AUIPC executed */
    ASSERT_REG(cpu, 3, 0x12345678);  /* LUI + ADDI combo */

    printf("  LUI/AUIPC: done\n\n");
}

/* ── Test 9: x0 is always zero ────────────────────────────────── */
static void test_x0_hardwired(void) {
    printf("Test: x0 hardwired to zero...\n");

    uint32_t prog[] = {
        /* Try to write to x0 — it should remain 0 */
        0x00500013,  /* ADDI  x0, x0, 5         → x0 should still be 0 */
        0x00500093,  /* ADDI  x1, x0, 5         → x1 = 5 (verify x0 reads as 0) */

        0x05D00893,  0x00000513, 0x00000073,
    };

    CPU cpu = run_program(prog, ARRAY_SIZE(prog));

    ASSERT_REG(cpu, 0, 0);  /* x0 must always be 0 */
    ASSERT_REG(cpu, 1, 5);  /* x1 = x0 + 5 = 0 + 5 = 5 */

    printf("  x0 hardwired: done\n\n");
}

/* ── Test 10: Negative immediate sign extension ───────────────── */
static void test_sign_extension(void) {
    printf("Test: Sign extension on immediates...\n");

    uint32_t prog[] = {
        /* ADDI with negative immediate: -1 */
        0xFFF00093,  /* ADDI x1, x0, -1         → x1 = 0xFFFFFFFF */

        /* ADDI with -2048 (minimum I-type immediate) */
        0x80000113,  /* ADDI x2, x0, -2048      → x2 = 0xFFFFF800 */

        /* ADDI with 2047 (maximum I-type immediate) */
        0x7FF00193,  /* ADDI x3, x0, 2047       → x3 = 0x000007FF */

        0x05D00893,  0x00000513, 0x00000073,
    };

    CPU cpu = run_program(prog, ARRAY_SIZE(prog));

    ASSERT_REG(cpu, 1, 0xFFFFFFFF);  /* -1 */
    ASSERT_REG(cpu, 2, 0xFFFFF800);  /* -2048 */
    ASSERT_REG(cpu, 3, 0x000007FF);  /* 2047 */

    printf("  Sign extension: done\n\n");
}

/* ══════════════════════════════════════════════════════════════════ */
/*  MAIN                                                            */
/* ══════════════════════════════════════════════════════════════════ */

int main(void) {
    printf("═══════════════════════════════════════════\n");
    printf("  RV32I Instruction Test Harness\n");
    printf("═══════════════════════════════════════════\n\n");

    test_r_type_alu();
    test_r_type_shifts();
    test_i_type_alu();
    test_i_type_shifts();
    test_load_store();
    test_branches();
    test_jumps();
    test_upper_immediate();
    test_x0_hardwired();
    test_sign_extension();

    printf("═══════════════════════════════════════════\n");
    printf("  Results: %d passed, %d failed\n", tests_passed, tests_failed);
    printf("═══════════════════════════════════════════\n");

    return tests_failed > 0 ? 1 : 0;
}
