#include "Jit/CodeGen.h"
#include "Jit/CodeGen.h"
#include "Jit/AssemblerX64.h"
#include "Jit/ExecutableMemory.h"
#include "Core/Native.h"
#include <stdio.h>
#include <stdlib.h>

int main() {
    printf("Testing Malloc JIT Call...\n");
    
    void* mem = Jit_AllocExec(4096);
    Assembler as;
    Asm_Init(&as, (uint8_t*)mem, 4096);
    
    // Prologue
    Asm_Push(&as, RBP);
    Asm_Mov_Reg_Reg(&as, RBP, RSP);
    
    // Call malloc(1024)
    Asm_Mov_Imm64(&as, RDI, 1024);
    
    // We need to ensure stack alignment?
    // Entered with Call (misaligned 8). Pushed RBP (aligned 16).
    // So aligned.
    
    void* funcPtr = (void*)malloc;
    Asm_Mov_Reg_Ptr(&as, RAX, funcPtr);
    Asm_Call_Reg(&as, RAX);
    
    // Epilogue
    Asm_Mov_Reg_Reg(&as, RSP, RBP);
    Asm_Pop(&as, RBP);
    Asm_Ret(&as);
    
    JitFunction func = (JitFunction)mem;
    printf("Executing...\n");
    func();
    printf("Done.\n");
    return 0;
}
