#
# hello.s — Bare-metal RISC-V test program for Phase 5
#
# This writes "Hello!\n" to the UART at 0x10000000, one byte at a time,
# then exits via ECALL.
#
# No libc, no stack, no .data — pure register manipulation.
#
# Compile:
#   riscv32-unknown-elf-gcc -march=rv32i -mabi=ilp32 -nostdlib \
#       -nostartfiles -ffreestanding -Ttext=0x00010000 \
#       -o tests/hello.elf tests/hello.s
#

    .global _start
    .text

_start:
    # Load UART transmit register address into t0
    # UART_TX_ADDR = 0x10000000
    # LUI loads the upper 20 bits: 0x10000 << 12 = 0x10000000
    lui     t0, 0x10000         # t0 = 0x10000000

    # Write 'H' (0x48)
    li      t1, 0x48            # t1 = 'H'
    sb      t1, 0(t0)          # store byte to UART

    # Write 'e' (0x65)
    li      t1, 0x65
    sb      t1, 0(t0)

    # Write 'l' (0x6C)
    li      t1, 0x6C
    sb      t1, 0(t0)

    # Write 'l' (0x6C)
    li      t1, 0x6C
    sb      t1, 0(t0)

    # Write 'o' (0x6F)
    li      t1, 0x6F
    sb      t1, 0(t0)

    # Write '!' (0x21)
    li      t1, 0x21
    sb      t1, 0(t0)

    # Write newline (0x0A)
    li      t1, 0x0A
    sb      t1, 0(t0)

    # Exit cleanly via ECALL
    # a7 = 93 (exit syscall number)
    # a0 = 0  (exit code)
    li      a7, 93
    li      a0, 0
    ecall

    # Should never reach here, but just in case:
_loop:
    j       _loop

