# RISC-V (RV32I) Instruction Set Simulator

A from-scratch, dependency-free RISC-V Instruction Set Simulator written in pure C11. 

This project was built to deeply explore and understand CPU architecture, instruction decoding, memory mapping, and bare-metal execution. It simulates a 32-bit RISC-V environment capable of loading real `.elf` binaries, executing bare-metal assembly, and running standard C programs using `newlib`.

## Features
* **Full RV32I Support:** Implements the complete unprivileged base integer instruction set (R, I, S, B, U, and J formats).
* **Custom ELF32 Loader:** Parses and loads ELF binaries into simulated memory without relying on external libraries.
* **Memory Subsystem:** A flat 16 MB simulated memory space with strict out-of-bounds checking.
* **MMIO UART:** Memory-mapped I/O intercept at `0x10000000` to route simulated standard output directly to the host terminal.
* **ECALL / Syscalls:** Implements the RISC-V Linux ABI for `write` (64) and `exit` (93), allowing standard C library functions like `printf` to function seamlessly.
* **Test Harness:** Includes an extensive built-in suite verifying bitwise logic, sign-extensions, and shift behaviors.

## Project Structure

```text
.
├── main.c                  # Entry point, CLI parsing, and fetch-decode-execute loop
├── cpu.h / cpu.c           # CPU state (32 registers, PC) and initialization
├── memory.h / memory.c     # 16 MB memory array, load/store logic, and UART MMIO
├── decode.h / decode.c     # Bitwise extraction of opcodes, registers, and immediates
├── execute.c               # Instruction execution (ALU operations, branching, syscalls)
├── elf_loader.h / .c       # Custom ELF32 parser and memory segment loader
├── Makefile                # Build system for the simulator
└── tests/
    ├── test_instructions.c # Hardcoded hex instruction test suite
    ├── hello.s             # Bare-metal assembly test
    └── hello.c             # C test utilizing printf and syscalls
```

## Getting Started

### Prerequisites
* **Host Compiler:** `gcc` or `clang` (to build the simulator).
* **Cross-Compiler:** `riscv64-unknown-elf-gcc` (optional, to compile your own RISC-V C/Assembly programs).

### Building the Simulator
Use the provided Makefile to build the `rvsim` executable:
```bash
make
```

### Running Built-In Tests
The simulator includes a hardcoded test harness that validates the execution of 47 distinct instructions and edge cases (sign-extension, branching, etc.):
```bash
./rvsim --test
```
You can also compile and run the standalone test runner:
```bash
make test
./tests/test_runner
```

### Running Custom ELF Binaries
You can run any RV32I statically compiled ELF file.

1. **Compile a C program** (ensure it fits the 16MB memory layout):
   ```bash
   riscv64-unknown-elf-gcc -march=rv32i -mabi=ilp32 -O2 -Wl,-Ttext=0x00010000 -Wl,-Tdata=0x00020000 -Wl,-Tbss=0x00030000 -o program.elf program.c
   ```
2. **Execute via the simulator**:
   ```bash
   ./rvsim program.elf
   ```

## Performance & Benchmarks
The simulator is built for educational clarity but is surprisingly performant for a pure interpreter loop (no JIT compilation or threading).
* **High Instruction Throughput:** Sustains roughly **~35-40 MIPS (Millions of Instructions Per Second)**. Running a naive prime calculation benchmark executed 16,000,000+ RV32I instructions in ~0.4 seconds.
* **Lightweight Codebase:** The entire project sits comfortably under **2,000 Lines of Code (LoC)**, making it simple to study or extend.
* **Tiny Footprint:** The compiled `rvsim.exe` binary is extremely small, typically under **150 KB**.

## Architecture & Design Decisions

* **Hardwired `x0`:** The RISC-V spec requires register `x0` to always be 0. This is enforced unconditionally at the end of every instruction cycle (`cpu->regs[0] = 0`), which is simpler and less error-prone than guarding every register write.
* **Sign Extension Strategy:** Immediates in RISC-V are scattered across instruction bits and are frequently signed. Our decoder reconstructs the immediate, placing the sign bit in the highest active position, and relies on C's arithmetic right-shift (`(int32_t)val >> N`) to cleanly propagate the sign bit down to the 32-bit boundary.
* **Stack Pointer (`sp`):** Initialized safely 16 bytes below the top of the 16 MB memory space to accommodate standard C environments where the stack grows downward.
* **Memory Allocation:** The 16 MB memory is allocated as a `static` array in the BSS segment to avoid stack overflow issues typical of large local array allocations.

## License
MIT License
