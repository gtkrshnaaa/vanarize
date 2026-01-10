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

void GC_Init(void) {
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

void GC_RegisterObject(Obj* obj) {
    obj->next = objectList;
    obj->isMarked = false;
    objectList = obj;
}

static void markValue(Value value) {
    if (!IsObj(value)) return;
    Obj* obj = ValueToObj(value);
    if (obj == NULL || obj->isMarked) return;
    
    obj->isMarked = true;
    
    // Mark referenced objects based on type
    if (obj->type == OBJ_STRING) {
        // Strings have no references
    } else if (obj->type == OBJ_FUNCTION) {
        // Functions reference nothing (for now)
    } else if (obj->type == OBJ_STRUCT) {
        ObjStruct* s = (ObjStruct*)obj;
        for (int i = 0; i < s->fieldCount; i++) {
            markValue(s->fields[i]);
        }
    }
}

static void markRoots(void) {
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
}

void GC_Collect(void) {
    markRoots();
    sweep();
    // No need for GC_ResetHeap with free-list
}
