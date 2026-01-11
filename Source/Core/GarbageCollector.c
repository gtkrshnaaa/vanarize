#include "Core/GarbageCollector.h"
#include "Core/VanarizeObject.h"
#include "Core/VanarizeValue.h"
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>

// Global list of all allocated objects (intrusive linked list)
static Obj* objectList = NULL;

// Root set (stack variables, globals)
#define MAX_ROOTS 256
static Value* roots[MAX_ROOTS];
static int rootCount = 0;

// Stack scanning
static void* stackBottom = NULL;

void GC_Init(void* stackBase) {
    stackBottom = stackBase;
    objectList = NULL;
    rootCount = 0;
}

void GC_RegisterRoot(Value* root) {
    if (rootCount >= MAX_ROOTS) {
        fprintf(stderr, "[GC] Error: Root set overflow\n");
        exit(1);
    }
    roots[rootCount++] = root;
}

void GC_UnregisterRoot(Value* root) {
    for (int i = 0; i < rootCount; i++) {
        if (roots[i] == root) {
            roots[i] = roots[--rootCount];
            return;
        }
    }
}

static uintptr_t minAddr = UINTPTR_MAX;
static uintptr_t maxAddr = 0;

void GC_RegisterObject(Obj* obj) {
    uintptr_t addr = (uintptr_t)obj;
    if (addr < minAddr) minAddr = addr;
    if (addr > maxAddr) maxAddr = addr;
    
    obj->next = objectList;
    obj->isMarked = false;
    objectList = obj;
}

static void markValue(Value value) {
    // STRICT TAGGING CHECK
    // Only mark if it is a Boxed Object (QNAN + ...).
    // Raw Doubles (not QNAN) and Raw Integers are ignored.
    if (!IsObj(value)) return;
    
    Obj* obj = ValueToObj(value);
    
    // Conservative GC Check (Address Validity)
    uintptr_t addr = (uintptr_t)obj;
    if (addr < minAddr || addr > maxAddr) return;
    
    // Alignment check (malloc usually 8-byte aligned)
    if (addr % 8 != 0) return;
    
    // printf("GC: Marking %p. Range: %lx-%lx\n", obj, minAddr, maxAddr);
    // fflush(stdout);
    
    if (obj == NULL || obj->isMarked) return;
    
    obj->isMarked = true;
    
    // Mark referenced objects based on type
    if (obj->type == OBJ_STRING) {
        // Strings have no references
    } else if (obj->type == OBJ_FUNCTION) {
        // Functions reference nothing (for now)
    } else if (obj->type == OBJ_STRUCT) {
        ObjStruct* s = (ObjStruct*)obj;
        // Scan Bitmap to find pointers
        // ... (Logic)
        uint64_t map = s->pointerBitmap;
        int offset = 0;
        
        while (map > 0) {
            if (map & 1) {
                Value* ptrVal = (Value*)(s->data + offset);
                markValue(*ptrVal);
            }
            map >>= 1;
            offset += 8;
        }
    }
}

static void markStack(void) {
    void* stackTop = &stackTop; // Address of local variable is current stack top
    
    // Iterate from top (low address) to bottom (high address)
    // Assume 64-bit alignment
    uint64_t* start = (uint64_t*)stackTop;
    uint64_t* end = (uint64_t*)stackBottom;
    
    // printf("GC: Scanning Stack %p to %p\n", start, end);
    // fflush(stdout);
    
    // Sanity check order
    if (start > end) {
        // Stack grows up?? (Unlikely on x64 Linux) or we are in a weird context
        uint64_t* temp = start;
        start = end;
        end = temp;
    }
    
    for (uint64_t* slot = start; slot < end; slot++) {
        Value val = (Value)*slot;
        markValue(val);
    }
}

static void markRoots(void) {
    markStack();
    for (int i = 0; i < rootCount; i++) {
        markValue(*roots[i]);
    }
}

// Free-list support
typedef struct FreeBlock {
    size_t size;
    struct FreeBlock* next;
} FreeBlock;

extern FreeBlock* freeList;

static void sweep(void) {
    Obj** obj = &objectList;
    while (*obj) {
        if (!(*obj)->isMarked) {
            // Unreachable object, free it
            Obj* unreached = *obj;
            *obj = unreached->next;
            
            // Return to free list
            // Get block start (before size header)
            char* blockStart = (char*)unreached - sizeof(size_t);
            size_t blockSize = *(size_t*)blockStart;
            
            FreeBlock* freed = (FreeBlock*)blockStart;
            freed->size = blockSize;
            freed->next = freeList;
            freeList = freed;
        } else {
            // Reachable, unmark for next cycle
            (*obj)->isMarked = false;
            obj = &(*obj)->next;
        }
    }
    // No need for GC_ResetHeap with free-list
}

void GC_Collect(void) {
    if (rootCount == 0 && stackBottom == NULL) return; // Optimization/Safety
    
    // printf("GC: Starting Collection. Roots: %d\n", rootCount);
    // fflush(stdout);
    
    markRoots();
    sweep();
    
    // printf("GC: Finished Collection.\n");
    // fflush(stdout);
}
