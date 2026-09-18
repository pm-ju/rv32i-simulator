/*
 * elf_loader.h — ELF32 Parser for RISC-V
 *
 * Parses the ELF32 (Executable and Linkable Format) file header and
 * program header table to load executable segments into the simulator's
 * memory.  No external libraries — we define the structs ourselves
 * from the ELF specification.
 *
 * ═══════════════════════════════════════════════════════════════════
 * ELF STRUCTURE OVERVIEW
 * ═══════════════════════════════════════════════════════════════════
 *
 * An ELF file has this layout:
 *
 *   ┌─────────────────────┐  offset 0
 *   │   ELF Header        │  (52 bytes for ELF32)
 *   │   (Elf32_Ehdr)      │  Contains: magic, class, machine type,
 *   │                     │  entry point, program header table offset,
 *   │                     │  section header table offset, etc.
 *   ├─────────────────────┤  offset = e_phoff
 *   │   Program Header    │  (one Elf32_Phdr per segment)
 *   │   Table             │  Each describes a segment to load:
 *   │                     │  type, offset, vaddr, filesz, memsz
 *   ├─────────────────────┤
 *   │   Segments          │  The actual code and data bytes
 *   │   (.text, .data,    │
 *   │    .rodata, .bss)   │
 *   ├─────────────────────┤  offset = e_shoff
 *   │   Section Header    │  (we ignore these — the program headers
 *   │   Table             │   are sufficient for loading)
 *   └─────────────────────┘
 *
 * We only care about PT_LOAD segments in the program header table.
 * ═══════════════════════════════════════════════════════════════════
 */

#ifndef ELF_LOADER_H
#define ELF_LOADER_H

#include <stdint.h>
#include "cpu.h"

/* ── ELF32 Header (52 bytes) ─────────────────────────────────────── */

/*
 * Field sizes match the ELF32 specification exactly:
 *   - Elf32_Addr, Elf32_Off, Elf32_Word = uint32_t (4 bytes)
 *   - Elf32_Half = uint16_t (2 bytes)
 *   - e_ident = 16 bytes (magic + flags)
 */
typedef struct {
    uint8_t  e_ident[16];  /* Magic number and ELF identification */
    uint16_t e_type;       /* Object file type (ET_EXEC=2, ET_DYN=3, ...) */
    uint16_t e_machine;    /* Target architecture (EM_RISCV = 0xF3 = 243) */
    uint32_t e_version;    /* ELF version (should be 1) */
    uint32_t e_entry;      /* Entry point virtual address — where PC starts */
    uint32_t e_phoff;      /* Program header table offset in the file */
    uint32_t e_shoff;      /* Section header table offset (we ignore this) */
    uint32_t e_flags;      /* Processor-specific flags */
    uint16_t e_ehsize;     /* This header's size (should be 52 for ELF32) */
    uint16_t e_phentsize;  /* Size of one program header entry */
    uint16_t e_phnum;      /* Number of program header entries */
    uint16_t e_shentsize;  /* Size of one section header entry */
    uint16_t e_shnum;      /* Number of section header entries */
    uint16_t e_shstrndx;   /* Section name string table index */
} Elf32_Ehdr;

/* ── ELF32 Program Header (32 bytes) ─────────────────────────────── */

/*
 * Each program header describes one segment.
 * We only load segments of type PT_LOAD.
 */
typedef struct {
    uint32_t p_type;    /* Segment type: PT_LOAD=1, PT_NULL=0, PT_DYNAMIC=2, ... */
    uint32_t p_offset;  /* Offset of the segment data in the file */
    uint32_t p_vaddr;   /* Virtual address where this segment should be loaded */
    uint32_t p_paddr;   /* Physical address (usually same as vaddr) */
    uint32_t p_filesz;  /* Number of bytes in the file for this segment */
    uint32_t p_memsz;   /* Number of bytes in memory — may be > filesz for .bss */
    uint32_t p_flags;   /* Permissions: PF_X=1, PF_W=2, PF_R=4 */
    uint32_t p_align;   /* Segment alignment */
} Elf32_Phdr;

/* ELF identification indices in e_ident[] */
#define EI_MAG0    0   /* 0x7F */
#define EI_MAG1    1   /* 'E'  */
#define EI_MAG2    2   /* 'L'  */
#define EI_MAG3    3   /* 'F'  */
#define EI_CLASS   4   /* 1 = 32-bit, 2 = 64-bit */
#define EI_DATA    5   /* 1 = little-endian, 2 = big-endian */

/* ELF constants */
#define ELFCLASS32  1
#define ELFDATA2LSB 1  /* Little-endian */
#define EM_RISCV    0xF3  /* 243 decimal */
#define PT_LOAD     1

/*
 * load_elf — Load an ELF32 RISC-V executable into simulator memory.
 *
 * 1. Reads and validates the ELF header (magic, class, endianness, machine).
 * 2. Walks the program header table.
 * 3. For each PT_LOAD segment, copies p_filesz bytes from the file into
 *    memory[p_vaddr], then zeroes the remaining (p_memsz - p_filesz)
 *    bytes — this is the .bss (uninitialized data) region.
 * 4. Sets cpu->pc = e_entry.
 *
 * Returns 0 on success, -1 on error (with a message printed to stderr).
 */
int load_elf(const char *filename, CPU *cpu);

#endif /* ELF_LOADER_H */

