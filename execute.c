/*
 * execute.c — RISC-V RV32I Instruction Executor
 *
 * This is the "heart" of the CPU simulator: the big switch statement
 * that performs each instruction's effect on the register file, memory,
 * and program counter.
 *
 * ═══════════════════════════════════════════════════════════════════
 * DESIGN DECISIONS
 * ═══════════════════════════════════════════════════════════════════
 *
 * 1. OUTER SWITCH ON OPCODE, INNER SWITCH ON FUNCT3 (and funct7).
 *    This mirrors the hardware decoder structure: the opcode selects
 *    the instruction class, funct3 narrows it, and funct7 disambiguates
 *    (e.g., ADD vs SUB).
 *
 * 2. PC UPDATE STRATEGY:
 *    Most instructions advance PC by 4 (one 32-bit instruction).
 *    Branches and jumps REPLACE the PC.  We handle this by:
 *      - Setting a flag `pc_updated` when a branch/jump modifies PC.
 *      - At the end, if !pc_updated, doing pc += 4.
 *    This avoids accidentally adding 4 after a branch.
 *
 * 3. x0 HARDWIRED TO ZERO:
 *    After every instruction, we force regs[0] = 0.  This is simpler
 *    and more robust than checking rd != 0 before every write.
 *
 * 4. FAIL-FAST ON ILLEGAL INSTRUCTIONS:
 *    Unknown opcodes or funct3/funct7 combinations print the PC and
 *    raw instruction word and halt.  Silent misbehavior is the enemy.
 * ═══════════════════════════════════════════════════════════════════
 */

#include "cpu.h"
#include "memory.h"
#include "decode.h"

#include <stdio.h>
#include <stdlib.h>

void execute(CPU *cpu, DecodedInst d) {
    uint32_t *x   = cpu->regs;   /* shorthand for register file */
    int pc_updated = 0;          /* set to 1 if a branch/jump modifies PC */

    switch (d.opcode) {

    /* ══════════════════════════════════════════════════════════════ */
    /*  0x33 — OP (R-type): register-register ALU operations        */
    /*                                                               */
    /*  All R-type instructions: result = rs1 OP rs2, written to rd. */
    /*  funct3 + funct7 together select the specific operation.     */
    /* ══════════════════════════════════════════════════════════════ */
    case 0x33:
        switch (d.funct3) {
        case 0x0:
            if (d.funct7 == 0x00) {
                /* ADD: rd = rs1 + rs2
                 * Simple unsigned addition — overflow wraps (mod 2^32).
                 * Works the same for signed and unsigned because of
                 * two's complement representation. */
                x[d.rd] = x[d.rs1] + x[d.rs2];
            } else if (d.funct7 == 0x20) {
                /* SUB: rd = rs1 - rs2
                 * Subtraction is just addition of the two's complement
                 * negation: rs1 + (~rs2 + 1). */
                x[d.rd] = x[d.rs1] - x[d.rs2];
            } else {
                goto illegal;
            }
            break;

        case 0x1:
            /* SLL: Shift Left Logical — rd = rs1 << (rs2 & 0x1F)
             *
             * Only the lower 5 bits of rs2 are used as the shift amount.
             * Why? A 32-bit value can only shift 0–31 positions. Using
             * 5 bits (max value 31) naturally enforces this. Shifting
             * by 32+ would be undefined behavior in C anyway. */
            x[d.rd] = x[d.rs1] << (x[d.rs2] & 0x1F);
            break;

        case 0x2:
            /* SLT: Set Less Than (signed)
             * rd = 1 if rs1 < rs2 (treating both as signed), else 0.
             *
             * We cast to int32_t so C uses signed comparison.
             * Without the cast, C would do unsigned comparison,
             * and -1 (0xFFFFFFFF) would look LARGER than 0. */
            x[d.rd] = ((int32_t)x[d.rs1] < (int32_t)x[d.rs2]) ? 1 : 0;
            break;

        case 0x3:
            /* SLTU: Set Less Than Unsigned
             * rd = 1 if rs1 < rs2 (unsigned comparison), else 0.
             *
             * No cast needed — uint32_t comparison is already unsigned.
             * Note: SLTU rd, x0, rs2 sets rd=1 if rs2 != 0 (useful idiom). */
            x[d.rd] = (x[d.rs1] < x[d.rs2]) ? 1 : 0;
            break;

        case 0x4:
            /* XOR: rd = rs1 ^ rs2
             * Bitwise exclusive OR. */
            x[d.rd] = x[d.rs1] ^ x[d.rs2];
            break;

        case 0x5:
            if (d.funct7 == 0x00) {
                /* SRL: Shift Right Logical — rd = rs1 >> (rs2 & 0x1F)
                 * Fills vacated high bits with zeros.
                 * Used for unsigned values. */
                x[d.rd] = x[d.rs1] >> (x[d.rs2] & 0x1F);
            } else if (d.funct7 == 0x20) {
                /* SRA: Shift Right Arithmetic — rd = (signed)rs1 >> (rs2 & 0x1F)
                 * Fills vacated high bits with copies of the sign bit.
                 * Used for signed values to preserve the sign during division by powers of 2.
                 *
                 * We cast to int32_t to get arithmetic shift behavior. */
                x[d.rd] = (uint32_t)((int32_t)x[d.rs1] >> (x[d.rs2] & 0x1F));
            } else {
                goto illegal;
            }
            break;

        case 0x6:
            /* OR: rd = rs1 | rs2
             * Bitwise inclusive OR. */
            x[d.rd] = x[d.rs1] | x[d.rs2];
            break;

        case 0x7:
            /* AND: rd = rs1 & rs2
             * Bitwise AND. */
            x[d.rd] = x[d.rs1] & x[d.rs2];
            break;

        default:
            goto illegal;
        }
        break;

    /* ══════════════════════════════════════════════════════════════ */
    /*  0x13 — OP-IMM (I-type): register-immediate ALU operations   */
    /*                                                               */
    /*  Like R-type, but the second operand is an immediate instead  */
    /*  of a register.  There's no SUBI — just use ADDI with a      */
    /*  negative immediate.                                         */
    /* ══════════════════════════════════════════════════════════════ */
    case 0x13:
        switch (d.funct3) {
        case 0x0:
            /* ADDI: rd = rs1 + sign_ext(imm)
             * The workhorse instruction. Also used as:
             *   NOP  = ADDI x0, x0, 0
             *   MV   = ADDI rd, rs1, 0
             *   LI   = ADDI rd, x0, imm */
            x[d.rd] = x[d.rs1] + (uint32_t)d.imm;
            break;

        case 0x1:
            /* SLLI: Shift Left Logical Immediate — rd = rs1 << shamt
             * shamt (shift amount) is in imm[4:0] = bits [24:20].
             * Upper bits of imm must be zero (else illegal). */
            x[d.rd] = x[d.rs1] << (d.imm & 0x1F);
            break;

        case 0x2:
            /* SLTI: Set Less Than Immediate (signed)
             * rd = 1 if (signed)rs1 < sign_ext(imm), else 0. */
            x[d.rd] = ((int32_t)x[d.rs1] < d.imm) ? 1 : 0;
            break;

        case 0x3:
            /* SLTIU: Set Less Than Immediate Unsigned
             * rd = 1 if rs1 < (unsigned)sign_ext(imm), else 0.
             *
             * Note the subtle behavior: the immediate IS sign-extended
             * (to 32 bits), but then the comparison is done unsigned.
             * So SLTIU rd, rs1, -1 checks if rs1 < 0xFFFFFFFF,
             * which is true for all values except 0xFFFFFFFF.
             * Special case: SLTIU rd, rs1, 1 sets rd=1 if rs1 == 0 (SEQZ). */
            x[d.rd] = (x[d.rs1] < (uint32_t)d.imm) ? 1 : 0;
            break;

        case 0x4:
            /* XORI: rd = rs1 ^ sign_ext(imm)
             * Special case: XORI rd, rs1, -1 is bitwise NOT (since -1 = all 1s). */
            x[d.rd] = x[d.rs1] ^ (uint32_t)d.imm;
            break;

        case 0x5: {
            /*
             * SRLI / SRAI: Shift Right Immediate (logical or arithmetic).
             * These share funct3 = 0x5 and are distinguished by bit 30
             * (which is in the funct7 field, specifically imm[10] = inst[30]).
             *
             *   bit 30 = 0 → SRLI (logical, zero-fill)
             *   bit 30 = 1 → SRAI (arithmetic, sign-fill)
             *
             * shamt = imm[4:0] = bits [24:20].
             */
            uint32_t shamt = d.imm & 0x1F;
            if ((d.imm & 0x400) == 0) {
                /* SRLI: Shift Right Logical Immediate */
                x[d.rd] = x[d.rs1] >> shamt;
            } else {
                /* SRAI: Shift Right Arithmetic Immediate */
                x[d.rd] = (uint32_t)((int32_t)x[d.rs1] >> shamt);
            }
            break;
        }

        case 0x6:
            /* ORI: rd = rs1 | sign_ext(imm) */
            x[d.rd] = x[d.rs1] | (uint32_t)d.imm;
            break;

        case 0x7:
            /* ANDI: rd = rs1 & sign_ext(imm) */
            x[d.rd] = x[d.rs1] & (uint32_t)d.imm;
            break;

        default:
            goto illegal;
        }
        break;

    /* ══════════════════════════════════════════════════════════════ */
    /*  0x03 — LOAD (I-type): load from memory into register        */
    /*                                                               */
    /*  Address = rs1 + sign_ext(imm)                               */
    /*  The loaded value is optionally sign- or zero-extended to     */
    /*  32 bits depending on the funct3 field.                      */
    /* ══════════════════════════════════════════════════════════════ */
    case 0x03: {
        uint32_t addr = x[d.rs1] + (uint32_t)d.imm;

        switch (d.funct3) {
        case 0x0:
            /* LB: Load Byte, sign-extended.
             * Read 1 byte, sign-extend from bit 7 to fill 32 bits.
             * Used when the byte represents a signed value (e.g., int8_t).
             *
             * Cast to int8_t to get the sign, then widen to int32_t
             * (which sign-extends), then back to uint32_t for storage. */
            x[d.rd] = (uint32_t)(int32_t)(int8_t)memory_load_byte(addr);
            break;

        case 0x1:
            /* LH: Load Halfword (2 bytes), sign-extended from bit 15.
             * Used for int16_t values. */
            x[d.rd] = (uint32_t)(int32_t)(int16_t)memory_load_halfword(addr);
            break;

        case 0x2:
            /* LW: Load Word (4 bytes). No extension needed — it's already 32 bits. */
            x[d.rd] = memory_load_word(addr);
            break;

        case 0x4:
            /* LBU: Load Byte Unsigned — zero-extended.
             * The byte is placed in bits [7:0] of rd, with bits [31:8] = 0.
             * memory_load_byte() already returns zero-extended uint32_t. */
            x[d.rd] = memory_load_byte(addr);
            break;

        case 0x5:
            /* LHU: Load Halfword Unsigned — zero-extended.
             * The halfword is placed in bits [15:0] of rd, bits [31:16] = 0. */
            x[d.rd] = memory_load_halfword(addr);
            break;

        default:
            goto illegal;
        }
        break;
    }

    /* ══════════════════════════════════════════════════════════════ */
    /*  0x23 — STORE (S-type): store register value to memory       */
    /*                                                               */
    /*  Address = rs1 + sign_ext(imm)                               */
    /*  No destination register — stores write to memory, not regs. */
    /* ══════════════════════════════════════════════════════════════ */
    case 0x23: {
        uint32_t addr = x[d.rs1] + (uint32_t)d.imm;

        switch (d.funct3) {
        case 0x0:
            /* SB: Store Byte — writes the low 8 bits of rs2 to memory. */
            memory_store_byte(addr, x[d.rs2]);
            break;

        case 0x1:
            /* SH: Store Halfword — writes the low 16 bits of rs2. */
            memory_store_halfword(addr, x[d.rs2]);
            break;

        case 0x2:
            /* SW: Store Word — writes all 32 bits of rs2. */
            memory_store_word(addr, x[d.rs2]);
            break;

        default:
            goto illegal;
        }
        break;
    }

    /* ══════════════════════════════════════════════════════════════ */
    /*  0x63 — BRANCH (B-type): conditional branches                */
    /*                                                               */
    /*  Compare rs1 and rs2.  If the condition holds, jump to       */
    /*  PC + sign_ext(imm).  If not, fall through to PC + 4.       */
    /*                                                               */
    /*  The target is relative to the CURRENT PC, not PC+4.        */
    /* ══════════════════════════════════════════════════════════════ */
    case 0x63: {
        int take_branch = 0;

        switch (d.funct3) {
        case 0x0:
            /* BEQ: Branch if Equal — take if rs1 == rs2 */
            take_branch = (x[d.rs1] == x[d.rs2]);
            break;

        case 0x1:
            /* BNE: Branch if Not Equal — take if rs1 != rs2 */
            take_branch = (x[d.rs1] != x[d.rs2]);
            break;

        case 0x4:
            /* BLT: Branch if Less Than (signed)
             * Cast to int32_t for signed comparison. */
            take_branch = ((int32_t)x[d.rs1] < (int32_t)x[d.rs2]);
            break;

        case 0x5:
            /* BGE: Branch if Greater or Equal (signed) */
            take_branch = ((int32_t)x[d.rs1] >= (int32_t)x[d.rs2]);
            break;

        case 0x6:
            /* BLTU: Branch if Less Than Unsigned */
            take_branch = (x[d.rs1] < x[d.rs2]);
            break;

        case 0x7:
            /* BGEU: Branch if Greater or Equal Unsigned */
            take_branch = (x[d.rs1] >= x[d.rs2]);
            break;

        default:
            goto illegal;
        }

        if (take_branch) {
            /*
             * Branch target = current PC + signed offset.
             * The offset is a BYTE offset (not instruction count).
             * Example: imm = 8 means "jump forward 2 instructions" (8/4).
             */
            cpu->pc = cpu->pc + (uint32_t)d.imm;
            pc_updated = 1;
        }
        /* If not taken, pc_updated stays 0, and pc += 4 happens below. */
        break;
    }

    /* ══════════════════════════════════════════════════════════════ */
    /*  0x6F — JAL (J-type): Jump And Link                          */
    /*                                                               */
    /*  rd = PC + 4    (save return address)                        */
    /*  PC = PC + imm  (jump to target)                             */
    /*                                                               */
    /*  Used for function calls.  The return address (the instruction*/
    /*  after the JAL) is saved in rd so the callee can return.     */
    /* ══════════════════════════════════════════════════════════════ */
    case 0x6F:
        x[d.rd] = cpu->pc + 4;  /* save return address */
        cpu->pc = cpu->pc + (uint32_t)d.imm;  /* jump */
        pc_updated = 1;
        break;

    /* ══════════════════════════════════════════════════════════════ */
    /*  0x67 — JALR (I-type): Jump And Link Register                */
    /*                                                               */
    /*  rd  = PC + 4                                                */
    /*  PC  = (rs1 + sign_ext(imm)) & ~1                           */
    /*                                                               */
    /*  The &~1 clears the lowest bit, ensuring the target is       */
    /*  aligned to a 2-byte boundary (required by the spec even     */
    /*  for RV32I which only has 4-byte instructions, because the   */
    /*  C extension uses 2-byte instructions).                      */
    /*                                                               */
    /*  Common use: RET = JALR x0, x1, 0  (jump to ra, discard link)*/
    /* ══════════════════════════════════════════════════════════════ */
    case 0x67:
        {
            uint32_t return_addr = cpu->pc + 4;
            cpu->pc = (x[d.rs1] + (uint32_t)d.imm) & ~1U;
            x[d.rd] = return_addr;
            pc_updated = 1;
        }
        break;

    /* ══════════════════════════════════════════════════════════════ */
    /*  0x37 — LUI (U-type): Load Upper Immediate                   */
    /*                                                               */
    /*  rd = imm << 12  (but our decoder already pre-shifted it)    */
    /*                                                               */
    /*  Loads a 20-bit constant into the upper 20 bits of rd,       */
    /*  zeroing the lower 12 bits.  Used together with ADDI to      */
    /*  build full 32-bit constants:                                */
    /*    LUI  rd, upper20                                          */
    /*    ADDI rd, rd, lower12                                      */
    /* ══════════════════════════════════════════════════════════════ */
    case 0x37:
        x[d.rd] = (uint32_t)d.imm;
        break;

    /* ══════════════════════════════════════════════════════════════ */
    /*  0x17 — AUIPC (U-type): Add Upper Immediate to PC            */
    /*                                                               */
    /*  rd = PC + (imm << 12)                                       */
    /*                                                               */
    /*  Builds a PC-relative address using a 20-bit offset.  Used   */
    /*  for position-independent code: combined with ADDI or JALR   */
    /*  to reach any address within ±2 GiB of the current PC.      */
    /* ══════════════════════════════════════════════════════════════ */
    case 0x17:
        x[d.rd] = cpu->pc + (uint32_t)d.imm;
        break;

    /* ══════════════════════════════════════════════════════════════ */
    /*  0x73 — SYSTEM: ECALL / EBREAK                               */
    /*                                                               */
    /*  ECALL  (imm=0): environment call — triggers a syscall.      */
    /*  EBREAK (imm=1): breakpoint — used by debuggers.             */
    /*                                                               */
    /*  For now, ECALL reads the syscall number from a7 (x17):      */
    /*    a7 = 93  → exit(a0)                                       */
    /*    a7 = 64  → write(a0=fd, a1=buf, a2=count)                */
    /*  EBREAK halts the simulator.                                 */
    /* ══════════════════════════════════════════════════════════════ */
    case 0x73:
        if (d.imm == 0) {
            /* ECALL — handle syscalls */
            uint32_t syscall_num = x[17]; /* a7 */

            switch (syscall_num) {
            case 93: {
                /* exit(status)
                 * a0 = exit status code */
                printf("\n[SIMULATOR] Program exited with code %d\n", (int32_t)x[10]);
                cpu->running = 0;
                break;
            }
            case 64: {
                /* write(fd, buf, count)
                 * a0 = file descriptor (1=stdout, 2=stderr)
                 * a1 = pointer to buffer in simulated memory
                 * a2 = number of bytes to write
                 *
                 * We read bytes from simulated memory and write them
                 * to the host's stdout/stderr. */
                uint32_t fd    = x[10]; /* a0 */
                uint32_t buf   = x[11]; /* a1 */
                uint32_t count = x[12]; /* a2 */

                FILE *out = (fd == 2) ? stderr : stdout;
                for (uint32_t i = 0; i < count; i++) {
                    fputc(memory_load_byte(buf + i), out);
                }
                fflush(out);
                x[10] = count; /* return value: bytes written */
                break;
            }
            default:
                fprintf(stderr, "[SIMULATOR] Unknown syscall %u at PC=0x%08X\n",
                        syscall_num, cpu->pc);
                /* Don't halt — just warn and continue. Some programs
                 * may use syscalls we don't implement yet. */
                x[10] = (uint32_t)-1; /* return -1 (error) in a0 */
                break;
            }
        } else if (d.imm == 1) {
            /* EBREAK — breakpoint */
            printf("[SIMULATOR] EBREAK at PC=0x%08X — halting.\n", cpu->pc);
            cpu->running = 0;
        } else {
            goto illegal;
        }
        break;

    /* ══════════════════════════════════════════════════════════════ */
    /*  FENCE (0x0F): memory ordering                               */
    /*                                                               */
    /*  In a single-core, in-order simulator there's nothing to do. */
    /*  Real hardware uses FENCE to ensure stores from one hart are */
    /*  visible to other harts.  We're single-threaded, so it's a   */
    /*  no-op.                                                      */
    /* ══════════════════════════════════════════════════════════════ */
    case 0x0F:
        /* NOP — no memory ordering needed in a single-core sim */
        break;

    default:
        goto illegal;
    }

    /*
     * ── Enforce x0 = 0 ──────────────────────────────────────────
     *
     * The RISC-V spec says register x0 is hardwired to zero.
     * Any instruction that writes to x0 has no effect.  Rather than
     * checking "if (rd != 0)" before every single write above,
     * we just unconditionally zero x0 after execution.  Simple,
     * correct, and costs one store per instruction.
     */
    x[0] = 0;

    /*
     * ── Advance PC ──────────────────────────────────────────────
     *
     * If a branch or jump already set the PC, don't touch it.
     * Otherwise, move to the next sequential instruction (PC + 4).
     */
    if (!pc_updated) {
        cpu->pc += 4;
    }

    return;

illegal:
    /*
     * ── Illegal instruction handler ─────────────────────────────
     *
     * Print the PC and the raw instruction word so the user can
     * cross-reference with objdump output.  Then halt.
     */
    fprintf(stderr,
            "\n╔══════════════════════════════════════════════╗\n"
            "║ ILLEGAL INSTRUCTION                          ║\n"
            "╠══════════════════════════════════════════════╣\n"
            "║ PC        = 0x%08X                     ║\n"
            "║ Opcode    = 0x%02X                           ║\n"
            "║ funct3    = 0x%X                              ║\n"
            "║ funct7    = 0x%02X                           ║\n"
            "╚══════════════════════════════════════════════╝\n",
            cpu->pc, d.opcode, d.funct3, d.funct7);
    cpu->running = 0;
}

