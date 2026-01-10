#include "Core/Memory.h"
#include "Core/GarbageCollector.h"
#include <sys/mman.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define HEAP_SIZE (1024 * 1024 * 256) // 256 MB

// Free-List Node
typedef struct FreeBlock {
    size_t size;
    struct FreeBlock* next;
} FreeBlock;

static char* heapStart = NULL;
static char* heapEnd = NULL;
FreeBlock* freeList = NULL; // Export for GC

void GC_ResetHeap(void) {
    // After sweep, rebuild free list
    // For now, simple approach: reset to single large block
    freeList = (FreeBlock*)heapStart;
    freeList->size = HEAP_SIZE - sizeof(FreeBlock);
    freeList->next = NULL;
}

void VM_InitMemory(void) {
    heapStart = mmap(NULL, HEAP_SIZE, 
                     PROT_READ | PROT_WRITE, 
                     MAP_PRIVATE | MAP_ANONYMOUS, 
                     -1, 0);
                     
    if (heapStart == MAP_FAILED) {
        perror("[Vanarize Core] Fatal: Failed to map memory");
        exit(1);
    }
    
    heapEnd = heapStart + HEAP_SIZE;
    
    // Initialize free list with one big block
    freeList = (FreeBlock*)heapStart;
    freeList->size = HEAP_SIZE - sizeof(FreeBlock);
    freeList->next = NULL;
}

void* MemAlloc(size_t size) {
    // Align to 8 bytes
    size = (size + 7) & ~7;
    // Add space for size header
    size_t totalSize = size + sizeof(size_t);
    
    // Find first fit
    FreeBlock** block = &freeList;
    while (*block) {
        if ((*block)->size >= totalSize) {
            FreeBlock* found = *block;
            
            // Split block if remainder is large enough
            if (found->size > totalSize + sizeof(FreeBlock) + 16) {
                FreeBlock* remainder = (FreeBlock*)((char*)found + totalSize);
                remainder->size = found->size - totalSize;
                remainder->next = found->next;
                *block = remainder;
            } else {
                // Use entire block
                *block = found->next;
            }
            
            // Store size and return pointer after header
            *(size_t*)found = totalSize;
            return (char*)found + sizeof(size_t);
        }
        block = &(*block)->next;
    }
    
    // No free block found, trigger GC
    fprintf(stderr, "[Vanarize Core] Heap full, triggering GC...\n");
    GC_Collect();
    
    // Try again after GC
    block = &freeList;
    while (*block) {
        if ((*block)->size >= totalSize) {
            FreeBlock* found = *block;
            *block = found->next;
            *(size_t*)found = totalSize;
            return (char*)found + sizeof(size_t);
        }
        block = &(*block)->next;
    }
    
    fprintf(stderr, "[Vanarize Core] OOM: No free blocks after GC.\n");
    exit(1);
}

void VM_FreeMemory(void) {
    if (heapStart != NULL) {
        munmap(heapStart, HEAP_SIZE);
        heapStart = NULL;
    }
}
