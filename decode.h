/*
 * decode.h — RISC-V RV32I Instruction Decoder
 *
 * A raw 32-bit RISC-V instruction word packs multiple fields into
 * fixed bit positions.  The decoder's job is to extract those fields
 * into a clean struct that the executor can work with.
 *
 * RV32I uses six instruction formats:
 *
 *   R-type:  Register-register ALU ops (ADD, SUB, AND, OR, …)
 *            [31:25 funct7][24:20 rs2][19:15 rs1][14:12 funct3][11:7 rd][6:0 opcode]
 *
 *   I-type:  Register-immediate ALU ops, loads, JALR
 *            [31:20 imm[11:0]][19:15 rs1][14:12 funct3][11:7 rd][6:0 opcode]
 *
 *   S-type:  Stores (SB, SH, SW)
 *            [31:25 imm[11:5]][24:20 rs2][19:15 rs1][14:12 funct3][11:7 imm[4:0]][6:0 opcode]
 *
 *   B-type:  Conditional branches (BEQ, BNE, BLT, …)
 *            [31 imm[12]][30:25 imm[10:5]][24:20 rs2][19:15 rs1][14:12 funct3]
 *            [11:8 imm[4:1]][7 imm[11]][6:0 opcode]
 *
 *   U-type:  Upper-immediate (LUI, AUIPC)
 *            [31:12 imm[31:12]][11:7 rd][6:0 opcode]
 *
 *   J-type:  Unconditional jumps (JAL)
 *            [31 imm[20]][30:21 imm[10:1]][20 imm[11]][19:12 imm[19:12]][11:7 rd][6:0 opcode]
 *
 * The immediate field is always sign-extended from its most-significant bit
 * (which is always instruction bit 31) to fill a full int32_t.
 */

#ifndef DECODE_H
#define DECODE_H

#include <stdint.h>

/*
 * DecodedInst — All fields extracted from one instruction word.
 *
 * Not every field is meaningful for every format:
 *   - R-type uses rd, rs1, rs2, funct3, funct7 (no imm)
 *   - I-type uses rd, rs1, funct3, imm (no rs2, funct7 is part of imm)
 *   - S-type uses rs1, rs2, funct3, imm (no rd)
 *   - B-type uses rs1, rs2, funct3, imm (no rd)
 *   - U-type uses rd, imm (no rs1, rs2, funct3, funct7)
 *   - J-type uses rd, imm (no rs1, rs2, funct3, funct7)
 *
 * We fill all fields anyway; the executor simply ignores the irrelevant ones.
 */
typedef struct {
    uint32_t opcode;  /* bits [6:0]   — selects the instruction format/class */
    uint32_t rd;      /* bits [11:7]  — destination register */
    uint32_t funct3;  /* bits [14:12] — sub-operation within an opcode group */
    uint32_t rs1;     /* bits [19:15] — source register 1 */
    uint32_t rs2;     /* bits [24:20] — source register 2 (R/S/B types) */
    uint32_t funct7;  /* bits [31:25] — further sub-operation (R-type only) */
    int32_t  imm;     /* sign-extended immediate (format-dependent) */
} DecodedInst;

/*
 * decode — Extract all fields from a raw 32-bit instruction word.
 *
 * The opcode (bits [6:0]) determines which format the instruction uses,
 * which in turn determines how the immediate is assembled.
 */
DecodedInst decode(uint32_t instruction);

#endif /* DECODE_H */

