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

static void sweep(void) {
    Obj** obj = &objectList;
    while (*obj) {
        if (!(*obj)->isMarked) {
            // Unreachable object, remove from list (don't free yet)
            Obj* unreached = *obj;
            *obj = unreached->next;
        } else {
            // Reachable, unmark for next cycle
            (*obj)->isMarked = false;
            obj = &(*obj)->next;
        }
    }
}

// Compact live objects (simplified: just reset heap pointer)
// In a real GC, we would copy live objects to beginning of nursery
extern char* heapStart;
extern char* heapPtr;
void GC_ResetHeap(void);

void GC_Collect(void) {
    markRoots();
    sweep();
    
    // For now, reset heap (simple copying collector would be better)
    // This works because we're using a bump-pointer allocator
    // and objects are never moved after allocation
    GC_ResetHeap();
}
