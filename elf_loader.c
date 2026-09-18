/*
 * elf_loader.c — ELF32 Parser and Loader Implementation
 *
 * This file reads an ELF32 executable from disk and loads its
 * PT_LOAD segments into the simulator's memory array.
 *
 * We parse the binary format ourselves (no libelf) to understand
 * exactly what's happening.  The process mirrors what a real OS
 * kernel or bootloader does when loading a program.
 */

#include "elf_loader.h"
#include "memory.h"

#include <stdio.h>
#include <string.h>

int load_elf(const char *filename, CPU *cpu) {
    FILE *f = fopen(filename, "rb");
    if (!f) {
        fprintf(stderr, "ELF LOADER: Cannot open file '%s'\n", filename);
        return -1;
    }

    /* ── Step 1: Read the ELF header (52 bytes for ELF32) ──────── */

    Elf32_Ehdr ehdr;
    if (fread(&ehdr, sizeof(Elf32_Ehdr), 1, f) != 1) {
        fprintf(stderr, "ELF LOADER: Failed to read ELF header\n");
        fclose(f);
        return -1;
    }

    /* ── Step 2: Validate the ELF header ───────────────────────── */

    /*
     * Check the magic number: the first 4 bytes must be 0x7F 'E' 'L' 'F'.
     * This is how you distinguish an ELF file from any other binary.
     * Every tool (readelf, objdump, the kernel) checks this first.
     */
    if (ehdr.e_ident[EI_MAG0] != 0x7F ||
        ehdr.e_ident[EI_MAG1] != 'E'  ||
        ehdr.e_ident[EI_MAG2] != 'L'  ||
        ehdr.e_ident[EI_MAG3] != 'F') {
        fprintf(stderr, "ELF LOADER: Not an ELF file (bad magic: %02X %02X %02X %02X)\n",
                ehdr.e_ident[0], ehdr.e_ident[1], ehdr.e_ident[2], ehdr.e_ident[3]);
        fclose(f);
        return -1;
    }

    /*
     * Check class = ELFCLASS32 (not 64-bit).
     * A 64-bit ELF has different struct sizes and we can't parse it
     * with our Elf32_Ehdr/Elf32_Phdr.
     */
    if (ehdr.e_ident[EI_CLASS] != ELFCLASS32) {
        fprintf(stderr, "ELF LOADER: Not a 32-bit ELF (class=%d, expected %d)\n",
                ehdr.e_ident[EI_CLASS], ELFCLASS32);
        fclose(f);
        return -1;
    }

    /*
     * Check data encoding = little-endian.
     * RISC-V base ISA is little-endian.  A big-endian ELF would have
     * all the multi-byte fields (addresses, sizes) byte-swapped.
     */
    if (ehdr.e_ident[EI_DATA] != ELFDATA2LSB) {
        fprintf(stderr, "ELF LOADER: Not little-endian (data=%d, expected %d)\n",
                ehdr.e_ident[EI_DATA], ELFDATA2LSB);
        fclose(f);
        return -1;
    }

    /*
     * Check machine type = EM_RISCV (0xF3 = 243).
     * This ensures we're not accidentally loading an ARM or x86 binary.
     */
    if (ehdr.e_machine != EM_RISCV) {
        fprintf(stderr, "ELF LOADER: Not a RISC-V ELF (machine=0x%04X, expected 0x%04X)\n",
                ehdr.e_machine, EM_RISCV);
        fclose(f);
        return -1;
    }

    printf("ELF LOADER: Valid RV32 ELF file\n");
    printf("  Entry point:  0x%08X\n", ehdr.e_entry);
    printf("  PH offset:    %u bytes into file\n", ehdr.e_phoff);
    printf("  PH entries:   %u (each %u bytes)\n", ehdr.e_phnum, ehdr.e_phentsize);

    /* ── Step 3: Walk the Program Header Table ─────────────────── */

    /*
     * The program header table is an array of Elf32_Phdr structs.
     * It starts at file offset e_phoff and has e_phnum entries.
     *
     * Each entry describes one "segment" — a contiguous chunk of the
     * file that should be loaded into memory at a specific address.
     */
    for (int i = 0; i < ehdr.e_phnum; i++) {
        Elf32_Phdr phdr;

        /*
         * Seek to the i-th program header.
         * e_phoff = offset of the table
         * i * e_phentsize = offset within the table
         */
        fseek(f, ehdr.e_phoff + i * ehdr.e_phentsize, SEEK_SET);

        if (fread(&phdr, sizeof(Elf32_Phdr), 1, f) != 1) {
            fprintf(stderr, "ELF LOADER: Failed to read program header %d\n", i);
            fclose(f);
            return -1;
        }

        /* We only care about PT_LOAD segments.  Other types (PT_NOTE,
         * PT_DYNAMIC, etc.) are metadata that we don't need. */
        if (phdr.p_type != PT_LOAD) {
            continue;
        }

        printf("  Loading segment %d:\n", i);
        printf("    Virtual addr: 0x%08X\n", phdr.p_vaddr);
        printf("    File size:    %u bytes (code + initialized data)\n", phdr.p_filesz);
        printf("    Memory size:  %u bytes (includes .bss if memsz > filesz)\n", phdr.p_memsz);
        printf("    File offset:  %u\n", phdr.p_offset);

        /* ── Step 4: Load the segment into simulator memory ───── */

        /*
         * Get a pointer into our memory array at the segment's
         * virtual address.  In a real OS, this would involve page
         * table setup and virtual-to-physical mapping.  In our
         * flat-memory simulator, vaddr IS the array index.
         */
        uint8_t *dest = memory_get_pointer(phdr.p_vaddr, phdr.p_memsz);
        if (!dest) {
            fprintf(stderr, "ELF LOADER: Segment at 0x%08X (size %u) exceeds memory\n",
                    phdr.p_vaddr, phdr.p_memsz);
            fclose(f);
            return -1;
        }

        /*
         * Copy p_filesz bytes from the file into memory.
         * This covers .text (code) and .data (initialized global variables).
         */
        fseek(f, phdr.p_offset, SEEK_SET);
        if (fread(dest, 1, phdr.p_filesz, f) != phdr.p_filesz) {
            fprintf(stderr, "ELF LOADER: Failed to read segment data\n");
            fclose(f);
            return -1;
        }

        /*
         * Zero the remaining bytes (p_memsz - p_filesz).
         *
         * This is the .bss section: global variables declared but not
         * initialized in the C source (e.g., "int count;").  The ELF
         * file doesn't store zero bytes for these — it just says
         * "this segment needs more memory than what's in the file."
         * The loader is responsible for zeroing that extra space.
         *
         * Example:
         *   .data (initialized):   int x = 42;    → in the file
         *   .bss (uninitialized):   int y;         → NOT in the file, just zeroed
         */
        if (phdr.p_memsz > phdr.p_filesz) {
            memset(dest + phdr.p_filesz, 0, phdr.p_memsz - phdr.p_filesz);
            printf("    .bss zeroed:  %u bytes\n", phdr.p_memsz - phdr.p_filesz);
        }
    }

    /* ── Step 5: Set the entry point ───────────────────────────── */

    /*
     * e_entry is the virtual address of the first instruction to execute.
     * For a bare-metal program, this is typically _start.
     * For a program linked with libc, this is the C runtime entry point
     * (_start → __libc_start_main → main).
     */
    cpu->pc = ehdr.e_entry;
    printf("ELF LOADER: Done. PC set to entry point 0x%08X\n\n", ehdr.e_entry);

    fclose(f);
    return 0;
}

