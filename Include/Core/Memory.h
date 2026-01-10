#ifndef VANARIZE_CORE_MEMORY_H
#define VANARIZE_CORE_MEMORY_H

#include <stddef.h>

// Initializes the heap (Nursery + Old Gen)
void VM_InitMemory(void);

// Allocates strict contiguous memory from the Nursery
void* MemAlloc(size_t size);

// Frees all memory (for shutdown)
void VM_FreeMemory(void);

#endif // VANARIZE_CORE_MEMORY_H
