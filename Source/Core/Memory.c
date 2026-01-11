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

// Bump Pointer "Wilderness"
static char* bumpPointer = NULL;

static char* heapStart = NULL;
static char* heapEnd = NULL;
FreeBlock* freeList = NULL; // Export for GC

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
    bumpPointer = heapStart;
    
    // Initialize free list (empty initially)
    freeList = NULL;
}

void* MemAlloc(size_t size) {
    // 1. O(1) Bump Pointer Fast Path
    // Align to 8 bytes
    size = (size + 7) & ~7;
    size_t totalSize = size + sizeof(size_t);
    
    if (bumpPointer + totalSize <= heapEnd) {
        FreeBlock* block = (FreeBlock*)bumpPointer;
        bumpPointer += totalSize;
        
        // Tag size
        *(size_t*)block = totalSize;
        return (char*)block + sizeof(size_t);
    }

    // 2. Slow Path: Try Free List
    FreeBlock** block = &freeList;
    while (*block) {
        if ((*block)->size >= totalSize) {
            FreeBlock* found = *block;
            
            // Split block logic
            if (found->size > totalSize + sizeof(FreeBlock) + 16) {
                FreeBlock* remainder = (FreeBlock*)((char*)found + totalSize);
                remainder->size = found->size - totalSize;
                remainder->next = found->next;
                *block = remainder;
            } else {
                *block = found->next;
            }
            
            *(size_t*)found = totalSize;
            return (char*)found + sizeof(size_t);
        }
        block = &(*block)->next;
    }
    
    // 3. Fallback: GC
    // fprintf(stderr, "[Vanarize Core] Heap full (bump & free list exhausted), triggering GC...\n");
    GC_Collect();
    
    // 4. Retry Free List (GC puts reclaimed objects here)
    block = &freeList;
    while (*block) {
        if ((*block)->size >= totalSize) {
            FreeBlock* found = *block;
            if (found->size > totalSize + sizeof(FreeBlock) + 16) {
                FreeBlock* remainder = (FreeBlock*)((char*)found + totalSize);
                remainder->size = found->size - totalSize;
                remainder->next = found->next;
                *block = remainder;
            } else {
                *block = found->next;
            }
            *(size_t*)found = totalSize;
            return (char*)found + sizeof(size_t);
        }
        block = &(*block)->next;
    }
    
    // 5. Retry Bump Pointer (Unlikely unless Compacting GC implemented later)
    if (bumpPointer + totalSize <= heapEnd) {
         FreeBlock* ptr = (FreeBlock*)bumpPointer;
         bumpPointer += totalSize;
         *(size_t*)ptr = totalSize;
         return (char*)ptr + sizeof(size_t);
    }

    fprintf(stderr, "[Vanarize Core] OOM: Heap exhausted even after GC.\n");
    exit(1);
    return NULL;
}

void VM_FreeMemory(void) {
    if (heapStart != NULL) {
        munmap(heapStart, HEAP_SIZE);
        heapStart = NULL;
    }
}
