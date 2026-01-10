#include "Core/Memory.h"
#include "Core/GarbageCollector.h"
#include <sys/mman.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define HEAP_SIZE (1024 * 1024 * 256) // 256 MB for the Nursery (Initial Proof of Concept)

// Export for GC
char* heapStart = NULL;
char* heapPtr = NULL;
static char* heapEnd = NULL;

void GC_ResetHeap(void) {
    // Reset bump pointer (WARNING: This invalidates all pointers!)
    // A proper GC would copy live objects
    heapPtr = heapStart;
}

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
    // Align to 8 bytes
    size = (size + 7) & ~7;
    
    if (heapPtr + size > heapEnd) {
        // Trigger GC
        fprintf(stderr, "[Vanarize Core] Nursery full, triggering GC...\n");
        GC_Collect();
        
        // Check again after GC
        if (heapPtr + size > heapEnd) {
            fprintf(stderr, "[Vanarize Core] OOM: Nursery Exhausted after GC.\n");
            exit(1);
        }
    }
    
    void* result = heapPtr;
    heapPtr += size;
    
    // Register with GC if it's an object
    // Caller must cast to Obj* and initialize type/next
    
    return result;
}

void VM_FreeMemory(void) {
    if (heapStart != NULL) {
        munmap(heapStart, HEAP_SIZE);
        heapStart = NULL;
        heapPtr = NULL;
    }
}
