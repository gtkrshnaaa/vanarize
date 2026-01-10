#include "Jit/ExecutableMemory.h"
#include <sys/mman.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

void* Jit_AllocExec(size_t size) {
    // Determine page size
    // long pageSize = sysconf(_SC_PAGESIZE);
    
    // Allocate memory with RWX permissions (simplification for JIT: write then exec)
    // Ideally W^X security is better, but for this stage RWX is easiest.
    void* ptr = mmap(NULL, size, 
                     PROT_READ | PROT_WRITE | PROT_EXEC, 
                     MAP_PRIVATE | MAP_ANONYMOUS, 
                     -1, 0);
                     
    if (ptr == MAP_FAILED) {
        perror("[Vanarize JIT] Fatal: Failed to allocate executable memory");
        exit(1);
    }
    
    return ptr;
}

void Jit_ProtectExec(void* ptr, size_t size) {
    // Optionally remove WRITE permission to enforce W^X
    // mprotect(ptr, size, PROT_READ | PROT_EXEC);
    (void)ptr; (void)size;
}

void Jit_FreeExec(void* ptr, size_t size) {
    if (ptr != NULL) {
        munmap(ptr, size);
    }
}
