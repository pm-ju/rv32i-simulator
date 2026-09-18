# RISC-V RV32I Simulator — Makefile
#
# Targets:
#   make          — build the simulator (rvsim.exe on Windows, rvsim on Unix)
#   make test     — build and run the instruction test harness
#   make clean    — remove all build artifacts
#
# Compiler: gcc or clang, C11 standard, all warnings enabled.

CC      = gcc
CFLAGS  = -Wall -Wextra -std=c11 -g -O2
TARGET  = rvsim

# Source files for the main simulator
SRCS    = main.c cpu.c memory.c decode.c execute.c elf_loader.c
OBJS    = $(SRCS:.c=.o)

# Test harness
TEST_SRCS = tests/test_instructions.c cpu.c memory.c decode.c execute.c
TEST_TARGET = tests/test_runner

# ── Default target ─────────────────────────────────────────────
all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $^

# ── Test target ────────────────────────────────────────────────
test: $(TEST_TARGET)
	./$(TEST_TARGET)

$(TEST_TARGET): $(TEST_SRCS)
	@mkdir -p tests
	$(CC) $(CFLAGS) -o $@ $^

# ── Clean ──────────────────────────────────────────────────────
clean:
	rm -f $(TARGET) $(TARGET).exe $(TEST_TARGET) $(TEST_TARGET).exe *.o

.PHONY: all test clean

