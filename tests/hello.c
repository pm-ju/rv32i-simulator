#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>

/* 
 * Provide libgloss syscall stubs mapping to our simulator's ECALL ABI:
 * a7 = syscall number, a0, a1, a2 = arguments.
 */

/* syscall 64 = write */
int _write(int file, char *ptr, int len) {
    register long a0 __asm__("a0") = file;
    register long a1 __asm__("a1") = (long)ptr;
    register long a2 __asm__("a2") = len;
    register long a7 __asm__("a7") = 64;

    __asm__ volatile ("ecall"
                      : "+r"(a0)
                      : "r"(a1), "r"(a2), "r"(a7)
                      : "memory");
    return a0;
}

/* syscall 93 = exit */
void _exit(int status) {
    register long a0 __asm__("a0") = status;
    register long a7 __asm__("a7") = 93;

    __asm__ volatile ("ecall"
                      : 
                      : "r"(a0), "r"(a7)
                      : "memory");
    while (1) {} /* never returns */
}

/* Dummy stubs for other syscalls newlib might call */
int _close(int file) { return -1; }
int _fstat(int file, struct stat *st) { 
    st->st_mode = S_IFCHR; /* fake terminal */
    return 0; 
}
int _isatty(int file) { return 1; }
int _lseek(int file, int ptr, int dir) { return 0; }
int _read(int file, char *ptr, int len) { return 0; }

/* Simple memory allocator for newlib's printf if it uses malloc */
char heap[4096];
char *heap_ptr = heap;
void *_sbrk(int incr) {
    char *prev = heap_ptr;
    heap_ptr += incr;
    return prev;
}

int main(void) {
    printf("Hello from RISC-V C code!\n");
    printf("Simulated CPU running correctly.\n");
    return 0;
}
