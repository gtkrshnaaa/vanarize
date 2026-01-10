#ifndef VANARIZE_JIT_EXEC_MEM_H
#define VANARIZE_JIT_EXEC_MEM_H

#include <stddef.h>

// Allocates executable memory (PROT_READ | PROT_WRITE | PROT_EXEC)
void* Jit_AllocExec(size_t size);

// Change protections to RX (Read/Exec) only after writing
void Jit_ProtectExec(void* ptr, size_t size);

// Free executable memory
void Jit_FreeExec(void* ptr, size_t size);

#endif // VANARIZE_JIT_EXEC_MEM_H
