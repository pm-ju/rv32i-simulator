/*
 * memory.h — RISC-V Simulator Memory Subsystem
 *
 * Models a flat 16 MB address space (0x00000000 – 0x00FFFFFF).
 * RISC-V is little-endian: the least-significant byte of a word
 * is stored at the lowest address.
 *
 * Why 16 MB?  Large enough to hold realistic bare-metal programs
 * and their data, small enough to fit comfortably in a process's
 * BSS segment on any modern machine.
 */

#ifndef MEMORY_H
#define MEMORY_H

#include <stdint.h>

/* 16 MB = 16 * 1024 * 1024 = 16,777,216 bytes */
#define MEMORY_SIZE (16 * 1024 * 1024)

/*
 * memory_init — Zero the entire 16 MB memory.
 * Must be called once before any load/store.
 */
void memory_init(void);

/*
 * memory_load_byte — Read a single byte from the given address.
 * Returns the byte zero-extended to uint32_t.
 */
uint32_t memory_load_byte(uint32_t addr);

/*
 * memory_load_halfword — Read 2 bytes (little-endian) from addr.
 * addr must be 2-byte aligned; returns the halfword zero-extended to uint32_t.
 */
uint32_t memory_load_halfword(uint32_t addr);

/*
 * memory_load_word — Read 4 bytes (little-endian) from addr.
 * addr must be 4-byte aligned; returns the full 32-bit word.
 */
uint32_t memory_load_word(uint32_t addr);

/*
 * memory_store_byte — Write the low 8 bits of value to addr.
 */
void memory_store_byte(uint32_t addr, uint32_t value);

/*
 * memory_store_halfword — Write the low 16 bits of value to addr (little-endian).
 * addr must be 2-byte aligned.
 */
void memory_store_halfword(uint32_t addr, uint32_t value);

/*
 * memory_store_word — Write all 32 bits of value to addr (little-endian).
 * addr must be 4-byte aligned.
 */
void memory_store_word(uint32_t addr, uint32_t value);

/*
 * memory_get_pointer — Return a raw pointer into the memory array.
 * Used by the ELF loader to bulk-copy segments directly.
 * Returns NULL if addr + length would exceed MEMORY_SIZE.
 */
uint8_t *memory_get_pointer(uint32_t addr, uint32_t length);

#endif /* MEMORY_H */

