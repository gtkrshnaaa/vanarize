#include "Core/Memory.h"
#include <sys/mman.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define HEAP_SIZE (1024 * 1024 * 256) // 256 MB for the Nursery (Initial Proof of Concept)

static char* heapStart = NULL;
static char* heapPtr = NULL;
static char* heapEnd = NULL;

void VM_InitMemory(void) {
    // Allocate a large contiguous block using mmap
    heapStart = mmap(NULL, HEAP_SIZE, 
                     PROT_READ | PROT_WRITE, 
                     MAP_PRIVATE | MAP_ANONYMOUS, 
                     -1, 0);
                     
    if (heapStart == MAP_FAILED) {
        perror("[Vanarize Core] Fatal: Failed to map memory");
        exit(1);
    }
    
    heapPtr = heapStart;
    heapEnd = heapStart + HEAP_SIZE;
    
    // printf("[Vanarize Core] Memory Initialized: %p - %p (%d MB)\n", heapStart, heapEnd, HEAP_SIZE / (1024*1024));
}

void* MemAlloc(size_t size) {
    if (heapPtr + size > heapEnd) {
        // Trigger GC would happen here
        fprintf(stderr, "[Vanarize Core] OOM: Nursery Exhausted.\n");
        exit(1);
    }
    
    void* result = heapPtr;
    heapPtr += size;
    return result;
}

void VM_FreeMemory(void) {
    if (heapStart != NULL) {
        munmap(heapStart, HEAP_SIZE);
        heapStart = NULL;
        heapPtr = NULL;
    }
}
