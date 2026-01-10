#include <stdio.h>
#include <assert.h>
#include <stdint.h>
#include "Jit/ExecutableMemory.h"
#include "Jit/AssemblerX64.h"

// Signature of our generated function: () -> uint64_t
typedef uint64_t (*JitFunc)(void);

int main() {
    printf("Testing JIT Backend...\n");
    
    size_t memSize = 4096;
    void* execMem = Jit_AllocExec(memSize);
    
    Assembler as;
    Asm_Init(&as, (uint8_t*)execMem, memSize);
    
    // GENERATE CODE: return 12345
    // MOV RAX, 12345
    // RET
    
    printf("Emitting: MOV RAX, 12345\n");
    Asm_Mov_Imm64(&as, RAX, 12345);
    
    printf("Emitting: RET\n");
    Asm_Ret(&as);
    
    printf("Code generated. Executing...\n");
    
    // Cast memory to function pointer
    JitFunc func = (JitFunc)execMem;
    
    // EXECUTE
    uint64_t result = func();
    
    printf("Result: %lu\n", result);
    
    assert(result == 12345);
    
    // TEST 2: Addition
    // Reset
    Asm_Init(&as, (uint8_t*)execMem, memSize);
    
    // CODE:
    // MOV RAX, 10
    // MOV RCX, 20
    // ADD RAX, RCX
    // RET
    
    Asm_Mov_Imm64(&as, RAX, 10);
    Asm_Mov_Imm64(&as, RCX, 20);
    Asm_Add_Reg_Reg(&as, RAX, RCX);
    Asm_Ret(&as);
    
    uint64_t sum = func();
    printf("Sum Result: %lu (Expected 30)\n", sum);
    assert(sum == 30);
    
    Jit_FreeExec(execMem, memSize);
    
    printf("JIT Backend OK.\n");
    return 0;
}
