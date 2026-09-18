#include <stdio.h>

int _write(int file, char *ptr, int len) {
    register long a0 __asm__("a0") = file;
    register long a1 __asm__("a1") = (long)ptr;
    register long a2 __asm__("a2") = len;
    register long a7 __asm__("a7") = 64;
    __asm__ volatile ("ecall" : "+r"(a0) : "r"(a1), "r"(a2), "r"(a7) : "memory");
    return a0;
}
void _exit(int status) {
    register long a0 __asm__("a0") = status;
    register long a7 __asm__("a7") = 93;
    __asm__ volatile ("ecall" : : "r"(a0), "r"(a7) : "memory");
    while (1) {}
}
int _close(int file) { return -1; }
int _fstat(int file, void *st) { return 0; }
int _isatty(int file) { return 1; }
int _lseek(int file, int ptr, int dir) { return 0; }
int _read(int file, char *ptr, int len) { return 0; }

char heap[4096];
char *heap_ptr = heap;
void *_sbrk(int incr) {
    char *prev = heap_ptr;
    heap_ptr += incr;
    return prev;
}

int main(void) {
    printf("Starting CPU-heavy benchmark...\n");
    int prime_count = 0;
    // Calculate primes up to 10,000 to burn CPU cycles but finish quickly
    for (int i = 2; i < 10000; i++) {
        int is_prime = 1;
        for (int j = 2; j * j <= i; j++) {
            if (i % j == 0) {
                is_prime = 0;
                break;
            }
        }
        if (is_prime) {
            prime_count++;
        }
    }
    printf("Benchmark complete! Found %d primes.\n", prime_count);
    return 0;
}
