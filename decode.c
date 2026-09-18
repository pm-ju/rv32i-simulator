/*
 * decode.c — RISC-V RV32I Instruction Decoder Implementation
 *
 * This file extracts bit-fields from a raw 32-bit instruction word
 * for each of the six RV32I instruction formats.
 *
 * ═══════════════════════════════════════════════════════════════════
 * SIGN-EXTENSION STRATEGY
 * ═══════════════════════════════════════════════════════════════════
 *
 * Many immediates are shorter than 32 bits and represent SIGNED values
 * (e.g., branch offsets can be negative).  We need to "sign-extend"
 * them: copy the most-significant bit (the sign bit) into all the
 * higher bits.
 *
 * The technique used here:
 *   (int32_t)instruction >> N
 *
 * This works because:
 *   1. Casting to int32_t makes the value signed.
 *   2. Right-shifting a signed value uses ARITHMETIC shift on gcc/clang,
 *      which fills the vacated high bits with copies of the sign bit.
 *   3. The sign bit of every RV32I immediate is always instruction bit 31,
 *      which becomes the sign bit of the int32_t.
 *
 * Why not use unsigned shift (logical shift)?
 *   Logical shift (>>) on an unsigned value fills the high bits with 0.
 *   So an immediate like -4 (0xFFFFFFFC) would become a large positive
 *   number instead.  For branch offsets and address calculations, this
 *   would produce wildly wrong results.
 *
 * C11 caveat: arithmetic right shift of signed values is
 * "implementation-defined" (C11 §6.5.7 ¶5), meaning the standard
 * doesn't require it.  However, EVERY mainstream compiler (gcc, clang,
 * MSVC, ICC) implements it as arithmetic shift.  We rely on this here
 * because the alternative (manual sign-extension with bit masking) is
 * less readable and error-prone, and we're targeting gcc/clang.
 * ═══════════════════════════════════════════════════════════════════
 */

#include "decode.h"

/*
 * Helper macros for extracting bit fields.
 *
 * BITS(inst, hi, lo) extracts bits [hi:lo] from inst, inclusive.
 *   1. Right-shift by 'lo' to move the desired field to bit 0.
 *   2. Mask with (2^(hi-lo+1) - 1) to keep only the field bits.
 *
 * Example: BITS(0xDEADBEEF, 14, 12) extracts bits [14:12].
 *   0xDEADBEEF >> 12 = 0x000DEADB
 *   width = 14 - 12 + 1 = 3
 *   mask  = (1 << 3) - 1 = 0x7
 *   result = 0x000DEADB & 0x7 = 0x3
 */
#define BITS(inst, hi, lo)  (((inst) >> (lo)) & ((1U << ((hi) - (lo) + 1)) - 1))

DecodedInst decode(uint32_t inst) {
    DecodedInst d;

    /*
     * ── Fields common to ALL formats ──────────────────────────────
     *
     * These three fields always live in the same bit positions
     * regardless of the instruction format:
     *   opcode = bits [6:0]
     *   rd     = bits [11:7]
     *   funct3 = bits [14:12]
     */
    d.opcode = BITS(inst, 6, 0);
    d.rd     = BITS(inst, 11, 7);
    d.funct3 = BITS(inst, 14, 12);
    d.rs1    = BITS(inst, 19, 15);
    d.rs2    = BITS(inst, 24, 20);
    d.funct7 = BITS(inst, 31, 25);

    /*
     * ── Immediate extraction (format-dependent) ──────────────────
     *
     * The opcode tells us which format the instruction uses, which
     * determines how the immediate bits are arranged.  We decode the
     * immediate here rather than in separate functions because:
     *   1. It keeps all bit-manipulation in one place.
     *   2. The executor doesn't need to know about instruction formats.
     */
    switch (d.opcode) {

        /* ────────────────────────────────────────────────────────── */
        /*  R-TYPE:  opcode = 0x33 (OP)                              */
        /*                                                            */
        /*  31      25 24  20 19  15 14  12 11   7 6    0            */
        /*  [funct7  ] [rs2 ] [rs1 ] [f3  ] [rd  ] [opcode]         */
        /*                                                            */
        /*  No immediate — all operands are in registers.            */
        /* ────────────────────────────────────────────────────────── */
        case 0x33:
            d.imm = 0;
            break;

        /* ────────────────────────────────────────────────────────── */
        /*  I-TYPE:  opcode = 0x13 (OP-IMM), 0x03 (LOAD),           */
        /*           0x67 (JALR), 0x73 (SYSTEM)                      */
        /*                                                            */
        /*  31          20 19  15 14  12 11   7 6    0               */
        /*  [imm[11:0]   ] [rs1 ] [f3  ] [rd  ] [opcode]            */
        /*                                                            */
        /*  imm = sign-extend(inst[31:20])                           */
        /*                                                            */
        /*  The immediate is a 12-bit signed value in bits [31:20].  */
        /*  Since bit 31 is the sign bit of both the immediate and   */
        /*  the int32_t, we can sign-extend by simply doing an       */
        /*  arithmetic right shift by 20:                            */
        /*                                                            */
        /*    (int32_t)inst >> 20                                    */
        /*                                                            */
        /*  This shifts bit 31 down to bit 11, and the arithmetic    */
        /*  shift fills bits [31:12] with copies of the original     */
        /*  bit 31 — which is exactly sign extension.                */
        /* ────────────────────────────────────────────────────────── */
        case 0x13:  /* OP-IMM (ADDI, ANDI, ORI, XORI, SLLI, SRLI, SRAI, SLTI, SLTIU) */
        case 0x03:  /* LOAD   (LB, LH, LW, LBU, LHU) */
        case 0x67:  /* JALR */
        case 0x73:  /* SYSTEM (ECALL, EBREAK) */
            d.imm = (int32_t)inst >> 20;
            break;

        /* ────────────────────────────────────────────────────────── */
        /*  S-TYPE:  opcode = 0x23 (STORE)                           */
        /*                                                            */
        /*  31      25 24  20 19  15 14  12 11   7 6    0            */
        /*  [imm11:5 ] [rs2 ] [rs1 ] [f3  ] [imm4:0] [opcode]      */
        /*                                                            */
        /*  The immediate is split across two non-contiguous fields: */
        /*    imm[11:5] = bits [31:25]  (the upper 7 bits)           */
        /*    imm[4:0]  = bits [11:7]   (the lower 5 bits)           */
        /*                                                            */
        /*  We reassemble them:                                      */
        /*    upper = (int32_t)inst >> 20   — this gives imm[11:5]   */
        /*            already sign-extended, but in bits [11:5].     */
        /*            Actually, (int32_t)inst >> 20 puts inst[31:20] */
        /*            into the result, so we mask off the bottom 5   */
        /*            bits (which are rs2, not part of the imm).     */
        /*                                                            */
        /*  Cleaner approach: build it from pieces.                  */
        /*    imm = (inst[31:25] << 5) | inst[11:7]                 */
        /*    Then sign-extend from bit 11.                          */
        /* ────────────────────────────────────────────────────────── */
        case 0x23: {
            /*
             * Reassemble the 12-bit immediate from its two pieces:
             *   imm[11:5] from instruction bits [31:25]
             *   imm[4:0]  from instruction bits [11:7]
             */
            uint32_t imm_11_5 = BITS(inst, 31, 25);  /* 7 bits */
            uint32_t imm_4_0  = BITS(inst, 11, 7);   /* 5 bits */
            uint32_t raw = (imm_11_5 << 5) | imm_4_0; /* 12-bit unsigned */

            /*
             * Sign-extend from bit 11:
             *   If bit 11 is set (negative), fill upper 20 bits with 1s.
             *   0xFFFFF000 is the mask for the upper 20 bits.
             */
            d.imm = (raw & 0x800)
                  ? (int32_t)(raw | 0xFFFFF000)
                  : (int32_t)raw;
            break;
        }

        /* ────────────────────────────────────────────────────────── */
        /*  B-TYPE:  opcode = 0x63 (BRANCH)                          */
        /*                                                            */
        /*  31   30    25 24  20 19  15 14 12 11  8  7   6    0      */
        /*  [12] [10:5  ] [rs2 ] [rs1 ] [f3 ] [4:1] [11] [opcode]   */
        /*                                                            */
        /*  The immediate encodes a SIGNED BYTE OFFSET, but with     */
        /*  bit 0 always implicitly 0 (instructions are 2-byte or    */
        /*  4-byte aligned in RISC-V).  So the encoded immediate is: */
        /*    {imm[12], imm[10:5], imm[4:1], imm[11]}               */
        /*  scrambled across the instruction word.                   */
        /*                                                            */
        /*  We reassemble:                                           */
        /*    bit 12   = inst[31]                                    */
        /*    bit 11   = inst[7]                                     */
        /*    bits 10:5 = inst[30:25]                                */
        /*    bits 4:1  = inst[11:8]                                 */
        /*    bit 0    = 0 (always, not encoded)                     */
        /*                                                            */
        /*  The result is a 13-bit signed value (range ±4 KiB).      */
        /* ────────────────────────────────────────────────────────── */
        case 0x63: {
            uint32_t imm_12   = BITS(inst, 31, 31);  /* 1 bit  */
            uint32_t imm_11   = BITS(inst, 7, 7);    /* 1 bit  */
            uint32_t imm_10_5 = BITS(inst, 30, 25);  /* 6 bits */
            uint32_t imm_4_1  = BITS(inst, 11, 8);   /* 4 bits */

            uint32_t raw = (imm_12 << 12)
                         | (imm_11 << 11)
                         | (imm_10_5 << 5)
                         | (imm_4_1 << 1);
            /* bit 0 is implicitly 0 */

            /* Sign-extend from bit 12 (the 13th bit) */
            d.imm = (raw & 0x1000)
                  ? (int32_t)(raw | 0xFFFFE000)
                  : (int32_t)raw;
            break;
        }

        /* ────────────────────────────────────────────────────────── */
        /*  U-TYPE:  opcode = 0x37 (LUI), 0x17 (AUIPC)              */
        /*                                                            */
        /*  31                   12 11   7 6    0                     */
        /*  [imm[31:12]           ] [rd  ] [opcode]                  */
        /*                                                            */
        /*  The immediate occupies the upper 20 bits.  It's stored   */
        /*  "pre-shifted" — the value is already in bits [31:12] of  */
        /*  the instruction, and bits [11:0] are implicitly zero.    */
        /*                                                            */
        /*  No sign-extension is needed: the 32-bit immediate has    */
        /*  the sign bit in bit 31, which is already the MSB of a    */
        /*  uint32_t/int32_t.                                        */
        /* ────────────────────────────────────────────────────────── */
        case 0x37:  /* LUI */
        case 0x17:  /* AUIPC */
            /*
             * Mask off the lower 12 bits (which hold rd and opcode).
             * The result is the immediate with bits [11:0] zeroed.
             */
            d.imm = (int32_t)(inst & 0xFFFFF000);
            break;

        /* ────────────────────────────────────────────────────────── */
        /*  J-TYPE:  opcode = 0x6F (JAL)                             */
        /*                                                            */
        /*  31   30       21 20  19        12 11   7 6    0          */
        /*  [20] [10:1     ] [11] [19:12    ] [rd  ] [opcode]       */
        /*                                                            */
        /*  Like B-type, the bits are scrambled to keep the sign bit */
        /*  (bit 20 of the immediate) at instruction bit 31, and to  */
        /*  share as much wiring as possible with other formats.     */
        /*                                                            */
        /*  The immediate is a 21-bit signed value (range ±1 MiB),   */
        /*  with bit 0 implicitly 0.                                 */
        /* ────────────────────────────────────────────────────────── */
        case 0x6F: {
            uint32_t imm_20    = BITS(inst, 31, 31);  /* 1 bit  */
            uint32_t imm_10_1  = BITS(inst, 30, 21);  /* 10 bits */
            uint32_t imm_11    = BITS(inst, 20, 20);  /* 1 bit  */
            uint32_t imm_19_12 = BITS(inst, 19, 12);  /* 8 bits */

            uint32_t raw = (imm_20 << 20)
                         | (imm_19_12 << 12)
                         | (imm_11 << 11)
                         | (imm_10_1 << 1);
            /* bit 0 is implicitly 0 */

            /* Sign-extend from bit 20 (the 21st bit) */
            d.imm = (raw & 0x100000)
                  ? (int32_t)(raw | 0xFFE00000)
                  : (int32_t)raw;
            break;
        }

        /* ────────────────────────────────────────────────────────── */
        /*  Unknown opcode — set imm to 0; the executor will handle  */
        /*  the error with a detailed message.                       */
        /* ────────────────────────────────────────────────────────── */
        default:
            d.imm = 0;
            break;
    }

    return d;
}

