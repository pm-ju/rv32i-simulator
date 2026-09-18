/*
 * memory.c — RISC-V Simulator Memory Implementation
 *
 * KEY DESIGN DECISION: The memory array is declared 'static' at file scope.
 *
 * Why not a stack-local variable?
 *   The default stack size on most systems is 1–8 MB.  A 16 MB array
 *   declared inside a function would immediately overflow the stack,
 *   causing a segfault before a single instruction executes.
 *
 * Why 'static' instead of malloc()?
 *   A static array in BSS is zero-initialized by the OS loader for free
 *   (the OS maps zero-pages lazily), so memory_init() is technically
 *   redundant on the first call — but we do it explicitly for clarity
 *   and correctness if the simulator is ever reset mid-run.
 *
 *   malloc() + memset() would work equally well, but adds a failure path
 *   (malloc can return NULL) with no benefit for a fixed-size allocation
 *   known at compile time.
 */

#include "memory.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * UART transmit register address.
 * Any store to this address outputs a character to the host terminal
 * instead of writing to the memory array.  See store functions below.
 *
 * 0x10000000 is the UART base address used by QEMU's "virt" machine
 * for RISC-V, so test programs written for QEMU will Just Work™.
 */
#define UART_TX_ADDR 0x10000000

/*
 * The main memory array — 16 MB, lives in BSS (zero-initialized).
 * 'static' keeps it file-scoped; all access goes through the
 * load/store functions, which enforce bounds checking and MMIO.
 */
static uint8_t memory[MEMORY_SIZE];

void memory_init(void) {
    memset(memory, 0, MEMORY_SIZE);
}

/* ─── Helper: bounds check ─────────────────────────────────────────── */

/*
 * Check that [addr, addr+size) lies within the 16 MB memory.
 * On failure: print the offending address, halt the simulator.
 * We don't silently wrap or ignore — that would hide real bugs
 * in the program being simulated.
 */
static void check_bounds(uint32_t addr, uint32_t size, const char *op) {
    if (addr + size > MEMORY_SIZE) {
        /*
         * Special case: accesses to the UART address are handled
         * separately and don't need to be in the memory array.
         */
        if (addr == UART_TX_ADDR) return;

        fprintf(stderr, "MEMORY ERROR: %s at address 0x%08X (size %u) "
                        "is out of bounds (memory size = 0x%08X)\n",
                op, addr, size, MEMORY_SIZE);
        exit(1);
    }
}

/* ─── Load functions ───────────────────────────────────────────────── */

uint32_t memory_load_byte(uint32_t addr) {
    check_bounds(addr, 1, "load_byte");
    return memory[addr];
}

uint32_t memory_load_halfword(uint32_t addr) {
    check_bounds(addr, 2, "load_halfword");

    /*
     * RISC-V is little-endian: the byte at the lower address is the
     * least-significant byte.
     *
     *   Address:   addr     addr+1
     *   Contains:  LSB      MSB
     *
     * So we OR them together with the MSB shifted left by 8.
     */
    return (uint32_t)memory[addr]
         | ((uint32_t)memory[addr + 1] << 8);
}

uint32_t memory_load_word(uint32_t addr) {
    check_bounds(addr, 4, "load_word");

    /*
     * Little-endian 32-bit load:
     *   addr+0 = bits [7:0]    (least significant)
     *   addr+1 = bits [15:8]
     *   addr+2 = bits [23:16]
     *   addr+3 = bits [31:24]  (most significant)
     */
    return (uint32_t)memory[addr]
         | ((uint32_t)memory[addr + 1] << 8)
         | ((uint32_t)memory[addr + 2] << 16)
         | ((uint32_t)memory[addr + 3] << 24);
}

/* ─── Store functions ──────────────────────────────────────────────── */

void memory_store_byte(uint32_t addr, uint32_t value) {
    /*
     * ── MMIO UART intercept ──────────────────────────────────────
     * This is the hardware/software boundary trick:
     *
     * In real hardware, peripheral devices (UART, SPI, GPIO, etc.)
     * are mapped into the CPU's address space.  A store instruction
     * to the UART's transmit-data register doesn't write to RAM —
     * the memory controller routes it to the UART hardware, which
     * shifts the byte out on the serial line.
     *
     * We simulate this by intercepting stores to 0x10000000 and
     * calling putchar() on the host.  From the simulated program's
     * perspective, it's just doing a normal store — it doesn't know
     * (or care) that the "hardware" is actually printf.
     * ─────────────────────────────────────────────────────────────
     */
    if (addr == UART_TX_ADDR) {
        putchar((char)(value & 0xFF));
        fflush(stdout);  /* flush immediately so output appears in real time */
        return;
    }

    check_bounds(addr, 1, "store_byte");
    memory[addr] = (uint8_t)(value & 0xFF);
}

void memory_store_halfword(uint32_t addr, uint32_t value) {
    if (addr == UART_TX_ADDR) {
        putchar((char)(value & 0xFF));
        fflush(stdout);
        return;
    }

    check_bounds(addr, 2, "store_halfword");

    /* Little-endian: low byte first */
    memory[addr]     = (uint8_t)(value & 0xFF);
    memory[addr + 1] = (uint8_t)((value >> 8) & 0xFF);
}

void memory_store_word(uint32_t addr, uint32_t value) {
    if (addr == UART_TX_ADDR) {
        putchar((char)(value & 0xFF));
        fflush(stdout);
        return;
    }

    check_bounds(addr, 4, "store_word");

    /* Little-endian: LSB at lowest address */
    memory[addr]     = (uint8_t)(value & 0xFF);
    memory[addr + 1] = (uint8_t)((value >> 8) & 0xFF);
    memory[addr + 2] = (uint8_t)((value >> 16) & 0xFF);
    memory[addr + 3] = (uint8_t)((value >> 24) & 0xFF);
}

/* ─── Direct access (for ELF loader) ──────────────────────────────── */

uint8_t *memory_get_pointer(uint32_t addr, uint32_t length) {
    if (addr + length > MEMORY_SIZE) {
        return NULL;
    }
    return &memory[addr];
}

