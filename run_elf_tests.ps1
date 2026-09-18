$env:PATH = "C:\msys64\ucrt64\bin;C:\msys64\usr\bin;$env:PATH"
$GCC = "C:\msys64\ucrt64\bin\riscv64-unknown-elf-gcc.exe"

# Compile bare-metal assembly
& $GCC -march=rv32i -mabi=ilp32 -nostdlib -nostartfiles -ffreestanding -Ttext=0x00010000 -o tests/hello.elf tests/hello.s

# Run it
.\rvsim.exe tests/hello.elf

# Compile C code with libc (newlib)
& $GCC -march=rv32i -mabi=ilp32 -O2 "-Wl,-Ttext=0x00010000" "-Wl,-Tdata=0x00020000" "-Wl,-Tbss=0x00030000" -o tests/hello_c.elf tests/hello.c

# Run it
.\rvsim.exe tests/hello_c.elf
