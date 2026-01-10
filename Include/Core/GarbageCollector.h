#ifndef VANARIZE_CORE_GC_H
#define VANARIZE_CORE_GC_H

#include "Core/VanarizeObject.h"
#include "Core/VanarizeValue.h"

// Initialize GC subsystem
void GC_Init(void);

// Trigger a full Mark-and-Sweep garbage collection cycle
void GC_Collect(void);

// Register an object in the global object list (called by allocator)
void GC_RegisterObject(Obj* obj);

// Register a root location (e.g., stack variable, global)
void GC_RegisterRoot(Value* root);

// Remove a root location
void GC_UnregisterRoot(Value* root);

#endif // VANARIZE_CORE_GC_H
